#include "pch.h"
#include "ss_embed.h"

#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <memory>
#include <cstring>

// SwiftScript internal headers
#include "ss_vm.hpp"
#include "ss_chunk.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_compiler.hpp"
#include "ss_type_checker.hpp"
#include "ss_native_registry.hpp"
#include "ss_native_convert.hpp"

using namespace swive;

/* ============================================================================
 * Version
 * ============================================================================ */

static constexpr int SS_VERSION_MAJOR = 1;
static constexpr int SS_VERSION_MINOR = 0;
static constexpr int SS_VERSION_PATCH = 0;
static const char* SS_VERSION_STRING = "1.0.0";

/* ============================================================================
 * Internal Context Structure
 * ============================================================================ */

struct SSContext_ {
    // VM instance
    std::unique_ptr<VM> vm;

    // Cached state for compiler
    std::string base_directory;
    std::vector<std::string> import_paths;

    // Callbacks
    SSPrintFunc print_callback{ nullptr };
    void* print_user_data{ nullptr };

    SSErrorFunc error_callback{ nullptr };
    void* error_user_data{ nullptr };

    // User data
    void* user_data{ nullptr };

    // Error state
    std::string last_error;
    int last_error_line{ 0 };

    // Registered native functions (name -> callback)
    std::unordered_map<std::string, SSNativeFunc> registered_functions;

    // Native object lifetime management
    SSReleaseNotifyFunc release_callback{ nullptr };
    void* release_user_data{ nullptr };

    // Track engine-owned NativeObject wrappers by native_ptr
    // Used for ss_invalidate_native() to find and null-out wrappers
    std::unordered_multimap<void*, NativeObject*> engine_owned_objects;

    void track_engine_object(void* native_ptr, NativeObject* wrapper) {
        engine_owned_objects.emplace(native_ptr, wrapper);
    }

    void untrack_engine_object(void* native_ptr, NativeObject* wrapper) {
        auto range = engine_owned_objects.equal_range(native_ptr);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second == wrapper) {
                engine_owned_objects.erase(it);
                return;
            }
        }
    }

    void set_error(SSResult code, const std::string& msg, int line = 0) {
        last_error = msg;
        last_error_line = line;
        if (error_callback) {
            error_callback(this, code, msg.c_str(), line, error_user_data);
        }
    }

    void clear_error() {
        last_error.clear();
        last_error_line = 0;
    }
};

/* ============================================================================
 * Internal Script Structure
 * ============================================================================ */

struct SSScript_ {
    Assembly assembly;
};

/* ============================================================================
 * Value Conversion Helpers
 * ============================================================================ */

static Value ssvalue_to_internal(SSContext ctx, SSValue val) {
    switch (val.type) {
        case SS_TYPE_NULL:
            return Value::null();
        case SS_TYPE_BOOL:
            return Value::from_bool(val.data.bool_val != 0);
        case SS_TYPE_INT:
            return Value::from_int(val.data.int_val);
        case SS_TYPE_FLOAT:
            return Value::from_float(val.data.float_val);
        case SS_TYPE_STRING: {
            if (!val.data.string_val) return Value::null();
            auto* str_obj = ctx->vm->allocate_object<StringObject>(
                std::string(val.data.string_val)
            );
            return Value::from_object(str_obj);
        }
        case SS_TYPE_OBJECT:
            // Native object pointer pass-through
            if (val.data.object_ptr) {
                return Value::from_object(static_cast<Object*>(val.data.object_ptr));
            }
            return Value::null();
        default:
            return Value::null();
    }
}

// Thread-local string buffer for returning string values
static thread_local std::string tls_string_buffer;

static SSValue internal_to_ssvalue(const Value& val) {
    SSValue result;
    result.data.int_val = 0; // Zero initialize

    if (val.is_null()) {
        result.type = SS_TYPE_NULL;
    }
    else if (val.is_bool()) {
        result.type = SS_TYPE_BOOL;
        result.data.bool_val = val.as_bool() ? 1 : 0;
    }
    else if (val.is_int()) {
        result.type = SS_TYPE_INT;
        result.data.int_val = val.as_int();
    }
    else if (val.is_float()) {
        result.type = SS_TYPE_FLOAT;
        result.data.float_val = val.as_float();
    }
    else if (val.is_object()) {
        Object* obj = val.as_object();
        if (obj && obj->type == ObjectType::String) {
            result.type = SS_TYPE_STRING;
            tls_string_buffer = static_cast<StringObject*>(obj)->data;
            result.data.string_val = tls_string_buffer.c_str();
        }
        else {
            result.type = SS_TYPE_OBJECT;
            result.data.object_ptr = obj;
        }
    }
    else {
        result.type = SS_TYPE_NULL;
    }

    return result;
}

/* ============================================================================
 * Native Function Bridge
 * ============================================================================ */

/**
 * Bridges SSNativeFunc callbacks into the NativeRegistry system.
 * When a registered function is called from a script, the VM looks it up
 * in NativeRegistry, which calls this bridge, which calls the user's SSNativeFunc.
 */
static void register_bridge_function(SSContext ctx, const std::string& name, SSNativeFunc func) {
    auto& registry = NativeRegistry::instance();

    // Capture context and callback
    registry.register_function(name,
        [ctx, func, name](VM& vm, std::span<Value> args) -> Value {
            // Convert args to SSValue array
            std::vector<SSValue> ss_args(args.size());
            for (size_t i = 0; i < args.size(); ++i) {
                ss_args[i] = internal_to_ssvalue(args[i]);
            }

            SSValue result_val;
            result_val.type = SS_TYPE_NULL;
            result_val.data.int_val = 0;

            SSResult res = func(ctx,
                                ss_args.data(),
                                static_cast<int>(ss_args.size()),
                                &result_val);

            if (res != SS_OK) {
                throw std::runtime_error("Native function '" + name + "' returned error code " + std::to_string(res));
            }

            return ssvalue_to_internal(ctx, result_val);
        }
    );
}

/* ============================================================================
 * Context Lifecycle Implementation
 * ============================================================================ */

SS_API SSContext ss_create_context(void) {
    return ss_create_context_ex(0, 0);
}

SS_API SSContext ss_create_context_ex(size_t max_stack_size, int enable_debug) {
    try {
        auto* ctx = new SSContext_();
        VMConfig config;
        if (max_stack_size > 0) {
            config.max_stack_size = max_stack_size;
        }
        config.enable_debug = (enable_debug != 0);
        ctx->vm = std::make_unique<VM>(config);
        return ctx;
    }
    catch (...) {
        return nullptr;
    }
}

SS_API void ss_destroy_context(SSContext context) {
    if (!context) return;

    // Unregister all bridge functions from the global registry
    auto& registry = NativeRegistry::instance();
    for (auto& [name, _] : context->registered_functions) {
        registry.unregister_function(name);
    }

    delete context;
}

/* ============================================================================
 * Script Compilation & Loading Implementation
 * ============================================================================ */

SS_API SSResult ss_compile(SSContext context,
                           const char* source,
                           const char* source_name,
                           SSScript* out_script) {
    if (!context || !source || !out_script) return SS_ERROR_INVALID_ARG;
    context->clear_error();

    try {
        Lexer lexer(source);
        auto tokens = lexer.tokenize_all();

        Parser parser(std::move(tokens));
        auto program = parser.parse();

        Compiler compiler;
        if (!context->base_directory.empty()) {
            compiler.set_base_directory(context->base_directory);
        }

        Assembly chunk = compiler.compile(program);

        auto* script = new SSScript_();
        script->assembly = std::move(chunk);
        *out_script = script;
        return SS_OK;
    }
    catch (const CompilerError& e) {
        context->set_error(SS_ERROR_COMPILE, e.what(), e.line());
        return SS_ERROR_COMPILE;
    }
    catch (const std::exception& e) {
        context->set_error(SS_ERROR_COMPILE, e.what());
        return SS_ERROR_COMPILE;
    }
}

SS_API SSResult ss_compile_checked(SSContext context,
                                   const char* source,
                                   const char* source_name,
                                   SSScript* out_script) {
    if (!context || !source || !out_script) return SS_ERROR_INVALID_ARG;
    context->clear_error();

    try {
        Lexer lexer(source);
        auto tokens = lexer.tokenize_all();

        Parser parser(std::move(tokens));
        auto program = parser.parse();

        // Type checking pass
        TypeChecker checker;
        checker.check_no_throw(program);
        const auto& errors = checker.errors();
        if (!errors.empty()) {
            std::string error_msg;
            for (const auto& err : errors) {
                if (!error_msg.empty()) error_msg += "\n";
                error_msg += err.message();
            }
            context->set_error(SS_ERROR_TYPE_CHECK, error_msg,
                               errors.front().line());
            return SS_ERROR_TYPE_CHECK;
        }

        // Compile
        Compiler compiler;
        if (!context->base_directory.empty()) {
            compiler.set_base_directory(context->base_directory);
        }

        Assembly chunk = compiler.compile(program);

        auto* script = new SSScript_();
        script->assembly = std::move(chunk);
        *out_script = script;
        return SS_OK;
    }
    catch (const CompilerError& e) {
        context->set_error(SS_ERROR_COMPILE, e.what(), e.line());
        return SS_ERROR_COMPILE;
    }
    catch (const std::exception& e) {
        context->set_error(SS_ERROR_COMPILE, e.what());
        return SS_ERROR_COMPILE;
    }
}

SS_API SSResult ss_load_bytecode(SSContext context,
                                 const void* data,
                                 size_t data_size,
                                 SSScript* out_script) {
    if (!context || !data || data_size == 0 || !out_script) return SS_ERROR_INVALID_ARG;
    context->clear_error();

    try {
        std::string buf(static_cast<const char*>(data), data_size);
        std::istringstream stream(buf, std::ios::binary);

        auto* script = new SSScript_();
        script->assembly = Assembly::deserialize(stream);
        *out_script = script;
        return SS_OK;
    }
    catch (const std::exception& e) {
        context->set_error(SS_ERROR_IO, e.what());
        return SS_ERROR_IO;
    }
}

SS_API SSResult ss_load_bytecode_file(SSContext context,
                                      const char* file_path,
                                      SSScript* out_script) {
    if (!context || !file_path || !out_script) return SS_ERROR_INVALID_ARG;
    context->clear_error();

    try {
        std::ifstream in(file_path, std::ios::binary);
        if (!in.is_open()) {
            context->set_error(SS_ERROR_IO,
                               std::string("Cannot open file: ") + file_path);
            return SS_ERROR_IO;
        }

        auto* script = new SSScript_();
        script->assembly = Assembly::deserialize(in);
        *out_script = script;
        return SS_OK;
    }
    catch (const std::exception& e) {
        context->set_error(SS_ERROR_IO, e.what());
        return SS_ERROR_IO;
    }
}

SS_API SSResult ss_compile_to_bytecode(SSContext context,
                                       const char* source,
                                       const char* source_name,
                                       void** out_data,
                                       size_t* out_data_size) {
    if (!context || !source || !out_data || !out_data_size) return SS_ERROR_INVALID_ARG;
    context->clear_error();

    try {
        Lexer lexer(source);
        auto tokens = lexer.tokenize_all();

        Parser parser(std::move(tokens));
        auto program = parser.parse();

        Compiler compiler;
        if (!context->base_directory.empty()) {
            compiler.set_base_directory(context->base_directory);
        }

        Assembly chunk = compiler.compile(program);

        // Serialize to memory buffer
        std::ostringstream oss(std::ios::binary);
        chunk.serialize(oss);
        std::string data = oss.str();

        // Allocate output buffer
        void* buf = std::malloc(data.size());
        if (!buf) {
            context->set_error(SS_ERROR_OUT_OF_MEMORY, "Failed to allocate bytecode buffer");
            return SS_ERROR_OUT_OF_MEMORY;
        }
        std::memcpy(buf, data.data(), data.size());

        *out_data = buf;
        *out_data_size = data.size();
        return SS_OK;
    }
    catch (const CompilerError& e) {
        context->set_error(SS_ERROR_COMPILE, e.what(), e.line());
        return SS_ERROR_COMPILE;
    }
    catch (const std::exception& e) {
        context->set_error(SS_ERROR_COMPILE, e.what());
        return SS_ERROR_COMPILE;
    }
}

SS_API void ss_destroy_script(SSScript script) {
    delete script;
}

/* ============================================================================
 * Script Execution Implementation
 * ============================================================================ */

SS_API SSResult ss_execute(SSContext context,
                           SSScript script,
                           SSValue* out_result) {
    if (!context || !script) return SS_ERROR_INVALID_ARG;
    context->clear_error();

    try {
        Value result = context->vm->execute(script->assembly);

        if (out_result) {
            *out_result = internal_to_ssvalue(result);
        }
        return SS_OK;
    }
    catch (const std::exception& e) {
        context->set_error(SS_ERROR_RUNTIME, e.what());
        return SS_ERROR_RUNTIME;
    }
}

SS_API SSResult ss_run(SSContext context,
                       const char* source,
                       SSValue* out_result) {
    if (!context || !source) return SS_ERROR_INVALID_ARG;

    SSScript script = nullptr;
    SSResult res = ss_compile(context, source, nullptr, &script);
    if (res != SS_OK) return res;

    res = ss_execute(context, script, out_result);
    ss_destroy_script(script);
    return res;
}

SS_API SSResult ss_call_function(SSContext context,
                                 const char* func_name,
                                 const SSValue* args,
                                 int arg_count,
                                 SSValue* out_result) {
    if (!context || !func_name) return SS_ERROR_INVALID_ARG;
    context->clear_error();

    try {
        // Look up the function in globals
        if (!context->vm->has_global(func_name)) {
            context->set_error(SS_ERROR_NOT_FOUND,
                               std::string("Function not found: ") + func_name);
            return SS_ERROR_NOT_FOUND;
        }

        Value func_val = context->vm->get_global(func_name);

        // Build source code that calls the function with arguments
        // This is a simple approach; a more optimized approach would
        // directly push args onto the stack and invoke.
        std::string call_source = func_name;
        call_source += "(";
        for (int i = 0; i < arg_count; ++i) {
            if (i > 0) call_source += ", ";
            // Embed literal values
            switch (args[i].type) {
                case SS_TYPE_NULL:   call_source += "nil"; break;
                case SS_TYPE_BOOL:   call_source += args[i].data.bool_val ? "true" : "false"; break;
                case SS_TYPE_INT:    call_source += std::to_string(args[i].data.int_val); break;
                case SS_TYPE_FLOAT:  call_source += std::to_string(args[i].data.float_val); break;
                case SS_TYPE_STRING:
                    call_source += "\"";
                    if (args[i].data.string_val) {
                        call_source += args[i].data.string_val;
                    }
                    call_source += "\"";
                    break;
                default:
                    call_source += "nil";
                    break;
            }
        }
        call_source += ")";

        return ss_run(context, call_source.c_str(), out_result);
    }
    catch (const std::exception& e) {
        context->set_error(SS_ERROR_RUNTIME, e.what());
        return SS_ERROR_RUNTIME;
    }
}

/* ============================================================================
 * Native Function Registration Implementation
 * ============================================================================ */

SS_API SSResult ss_register_function(SSContext context,
                                     const char* script_name,
                                     SSNativeFunc func) {
    if (!context || !script_name || !func) return SS_ERROR_INVALID_ARG;

    try {
        std::string name(script_name);
        context->registered_functions[name] = func;
        register_bridge_function(context, name, func);
        return SS_OK;
    }
    catch (const std::exception& e) {
        context->set_error(SS_ERROR_RUNTIME, e.what());
        return SS_ERROR_RUNTIME;
    }
}

SS_API SSResult ss_unregister_function(SSContext context,
                                       const char* script_name) {
    if (!context || !script_name) return SS_ERROR_INVALID_ARG;

    std::string name(script_name);
    auto it = context->registered_functions.find(name);
    if (it == context->registered_functions.end()) {
        return SS_ERROR_NOT_FOUND;
    }

    NativeRegistry::instance().unregister_function(name);
    context->registered_functions.erase(it);
    return SS_OK;
}

/* ============================================================================
 * Global Variables Implementation
 * ============================================================================ */

SS_API SSResult ss_set_global(SSContext context,
                              const char* name,
                              SSValue value) {
    if (!context || !name) return SS_ERROR_INVALID_ARG;

    try {
        Value internal_val = ssvalue_to_internal(context, value);
        context->vm->set_global(name, internal_val);
        return SS_OK;
    }
    catch (const std::exception& e) {
        context->set_error(SS_ERROR_RUNTIME, e.what());
        return SS_ERROR_RUNTIME;
    }
}

SS_API SSResult ss_get_global(SSContext context,
                              const char* name,
                              SSValue* out_value) {
    if (!context || !name || !out_value) return SS_ERROR_INVALID_ARG;

    try {
        if (!context->vm->has_global(name)) {
            return SS_ERROR_NOT_FOUND;
        }
        Value val = context->vm->get_global(name);
        *out_value = internal_to_ssvalue(val);
        return SS_OK;
    }
    catch (const std::exception& e) {
        context->set_error(SS_ERROR_RUNTIME, e.what());
        return SS_ERROR_RUNTIME;
    }
}

/* ============================================================================
 * Callbacks Implementation
 * ============================================================================ */

SS_API void ss_set_print_callback(SSContext context,
                                  SSPrintFunc func,
                                  void* user_data) {
    if (!context) return;
    context->print_callback = func;
    context->print_user_data = user_data;
}

SS_API void ss_set_error_callback(SSContext context,
                                  SSErrorFunc func,
                                  void* user_data) {
    if (!context) return;
    context->error_callback = func;
    context->error_user_data = user_data;
}

/* ============================================================================
 * Error Information Implementation
 * ============================================================================ */

SS_API const char* ss_get_last_error(SSContext context) {
    if (!context) return "";
    return context->last_error.c_str();
}

SS_API int ss_get_last_error_line(SSContext context) {
    if (!context) return 0;
    return context->last_error_line;
}

/* ============================================================================
 * Module System Implementation
 * ============================================================================ */

SS_API void ss_set_base_directory(SSContext context, const char* dir) {
    if (!context || !dir) return;
    context->base_directory = dir;
}

SS_API void ss_add_import_path(SSContext context, const char* path) {
    if (!context || !path) return;
    context->import_paths.push_back(path);
}

/* ============================================================================
 * Memory Management Implementation
 * ============================================================================ */

SS_API void ss_free_buffer(void* buffer) {
    std::free(buffer);
}

SS_API void ss_get_memory_stats(SSContext context,
                                size_t* out_total_alloc,
                                size_t* out_total_freed,
                                size_t* out_current_objects) {
    if (!context) return;

    const auto& stats = context->vm->get_stats();
    if (out_total_alloc)      *out_total_alloc = stats.total_allocated;
    if (out_total_freed)      *out_total_freed = stats.total_freed;
    if (out_current_objects)   *out_current_objects = stats.current_objects;
}

/* ============================================================================
 * User Data Implementation
 * ============================================================================ */

SS_API void ss_set_user_data(SSContext context, void* user_data) {
    if (!context) return;
    context->user_data = user_data;
}

SS_API void* ss_get_user_data(SSContext context) {
    if (!context) return nullptr;
    return context->user_data;
}

/* ============================================================================
 * Native Object Lifetime Management Implementation
 * ============================================================================ */

SS_API SSResult ss_wrap_native(SSContext context,
                               void* native_ptr,
                               const char* type_name,
                               SSOwnership ownership,
                               SSValue* out_value) {
    if (!context || !native_ptr || !type_name || !out_value) return SS_ERROR_INVALID_ARG;

    try {
        // Look up type info from registry (may be nullptr if not registered)
        NativeTypeInfo* type_info = NativeRegistry::instance().find_type(type_name);

        auto* native_obj = context->vm->allocate_object<NativeObject>(
            native_ptr, std::string(type_name), type_info
        );

        // Set ownership mode
        if (ownership == SS_OWNERSHIP_ENGINE) {
            native_obj->prevent_release = true;
            context->track_engine_object(native_ptr, native_obj);

            // Wire up release notification
            // Uses a C function pointer (not lambda with captures) to match
            // NativeReleaseNotifyFunc signature. Context is passed via the
            // release_notify_context field.
            native_obj->release_notify = [](void* ctx_ptr, void* ptr,
                                            const char* tname, void* ud) {
                auto* ctx = static_cast<SSContext_*>(ctx_ptr);
                // Notify the engine
                if (ctx->release_callback) {
                    ctx->release_callback(ctx, ptr, tname, ctx->release_user_data);
                }
                // Remove from tracking (erase all entries for this ptr)
                ctx->engine_owned_objects.erase(ptr);
            };
            native_obj->release_notify_context = context;
            native_obj->release_notify_user_data = context->release_user_data;
        }

        out_value->type = SS_TYPE_OBJECT;
        out_value->data.object_ptr = static_cast<Object*>(native_obj);
        return SS_OK;
    }
    catch (const std::exception& e) {
        context->set_error(SS_ERROR_RUNTIME, e.what());
        return SS_ERROR_RUNTIME;
    }
}

SS_API SSResult ss_set_ownership(SSContext context,
                                 SSValue value,
                                 SSOwnership ownership) {
    if (!context) return SS_ERROR_INVALID_ARG;
    if (value.type != SS_TYPE_OBJECT || !value.data.object_ptr) return SS_ERROR_INVALID_ARG;

    Object* obj = static_cast<Object*>(value.data.object_ptr);
    if (obj->type != ObjectType::Native) return SS_ERROR_INVALID_ARG;

    NativeObject* native_obj = static_cast<NativeObject*>(obj);
    bool was_engine = native_obj->prevent_release;
    bool now_engine = (ownership == SS_OWNERSHIP_ENGINE);

    native_obj->prevent_release = now_engine;

    // Update tracking
    if (!was_engine && now_engine) {
        // VM → Engine: start tracking
        context->track_engine_object(native_obj->native_ptr, native_obj);
    }
    else if (was_engine && !now_engine) {
        // Engine → VM: stop tracking
        context->untrack_engine_object(native_obj->native_ptr, native_obj);
    }

    return SS_OK;
}

SS_API SSResult ss_get_ownership(SSContext context,
                                 SSValue value,
                                 SSOwnership* out_ownership) {
    if (!context || !out_ownership) return SS_ERROR_INVALID_ARG;
    if (value.type != SS_TYPE_OBJECT || !value.data.object_ptr) return SS_ERROR_INVALID_ARG;

    Object* obj = static_cast<Object*>(value.data.object_ptr);
    if (obj->type != ObjectType::Native) return SS_ERROR_INVALID_ARG;

    NativeObject* native_obj = static_cast<NativeObject*>(obj);
    *out_ownership = native_obj->prevent_release ? SS_OWNERSHIP_ENGINE : SS_OWNERSHIP_VM;
    return SS_OK;
}

SS_API void ss_set_release_callback(SSContext context,
                                    SSReleaseNotifyFunc func,
                                    void* user_data) {
    if (!context) return;
    context->release_callback = func;
    context->release_user_data = user_data;

    // Apply to all existing engine-owned objects
    for (auto& [ptr, wrapper] : context->engine_owned_objects) {
        if (wrapper && func) {
            wrapper->release_notify = [](void* ctx_ptr, void* nptr,
                                          const char* tname, void* ud) {
                auto* ctx = static_cast<SSContext_*>(ctx_ptr);
                if (ctx->release_callback) {
                    ctx->release_callback(ctx, nptr, tname, ctx->release_user_data);
                }
                ctx->engine_owned_objects.erase(nptr);
            };
            wrapper->release_notify_context = context;
            wrapper->release_notify_user_data = user_data;
        }
        else if (wrapper && !func) {
            // Clearing callback
            wrapper->release_notify = nullptr;
            wrapper->release_notify_context = nullptr;
            wrapper->release_notify_user_data = nullptr;
        }
    }
}

SS_API SSResult ss_invalidate_native(SSContext context, void* native_ptr) {
    if (!context || !native_ptr) return SS_ERROR_INVALID_ARG;

    // Find all NativeObject wrappers pointing to this native_ptr
    auto range = context->engine_owned_objects.equal_range(native_ptr);
    for (auto it = range.first; it != range.second; ++it) {
        NativeObject* wrapper = it->second;
        if (wrapper && wrapper->native_ptr == native_ptr) {
            // Null out the native pointer - script will see null on access
            wrapper->native_ptr = nullptr;
        }
    }

    // Remove all tracking entries for this pointer
    context->engine_owned_objects.erase(native_ptr);
    return SS_OK;
}

SS_API void* ss_get_native_ptr(SSContext context, SSValue value) {
    if (!context) return nullptr;
    if (value.type != SS_TYPE_OBJECT || !value.data.object_ptr) return nullptr;

    Object* obj = static_cast<Object*>(value.data.object_ptr);
    if (obj->type != ObjectType::Native) return nullptr;

    NativeObject* native_obj = static_cast<NativeObject*>(obj);
    return native_obj->native_ptr;  // nullptr if invalidated
}

/* ============================================================================
 * Version Implementation
 * ============================================================================ */

SS_API const char* ss_version(void) {
    return SS_VERSION_STRING;
}

SS_API void ss_version_numbers(int* major, int* minor, int* patch) {
    if (major) *major = SS_VERSION_MAJOR;
    if (minor) *minor = SS_VERSION_MINOR;
    if (patch) *patch = SS_VERSION_PATCH;
}
