// SPDX-License-Identifier: MIT
// Copyright (c) 2025 29thnight

/**
 * @file ss_embed.h
 * @brief SwiftScript Embedding API.
 *
 * C API for embedding the SwiftScript VM into game engines and other host applications.
 *
 * Supported engines:
 *   - Unreal Engine (C++ direct / DLL)
 *   - Unity (C# P/Invoke)
 *   - Godot (GDExtension / C++)
 *   - Custom engines (any language with C FFI)
 *
 * Usage:
 *   1. ss_create_context()    - Create a scripting context
 *   2. ss_register_function() - Register host functions callable from scripts
 *   3. ss_compile() or ss_load_bytecode() - Load a script
 *   4. ss_execute()           - Run the script
 *   5. ss_destroy_context()   - Clean up
 */

#ifndef SS_EMBED_H
#define SS_EMBED_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * Platform / Export Macros
 * ============================================================================ */

#ifdef __cplusplus
#define SS_EXTERN_C extern "C"
#else
#define SS_EXTERN_C
#endif

#if defined(_WIN32) || defined(_WIN64)
    #ifdef SS_BUILD_DLL
        #define SS_API SS_EXTERN_C __declspec(dllexport)
    #elif defined(SS_IMPORT_DLL)
        #define SS_API SS_EXTERN_C __declspec(dllimport)
    #else
        #define SS_API SS_EXTERN_C
    #endif
#elif defined(__GNUC__) || defined(__clang__)
    #define SS_API SS_EXTERN_C __attribute__((visibility("default")))
#else
    #define SS_API SS_EXTERN_C
#endif

/* ============================================================================
 * Opaque Handle Types
 * ============================================================================ */

/** Opaque handle to a SwiftScript context (VM + compiler state) */
typedef struct SSContext_* SSContext;

/** Opaque handle to a compiled script (Assembly bytecode) */
typedef struct SSScript_* SSScript;

/* ============================================================================
 * Value Types
 * ============================================================================ */

/** SwiftScript value type tags */
typedef enum SSValueType {
    SS_TYPE_NULL     = 0,
    SS_TYPE_BOOL     = 1,
    SS_TYPE_INT      = 2,
    SS_TYPE_FLOAT    = 3,
    SS_TYPE_STRING   = 4,
    SS_TYPE_OBJECT   = 5
} SSValueType;

/** SwiftScript value - POD type for FFI safety */
typedef struct SSValue {
    SSValueType type;
    union {
        int         bool_val;
        int64_t     int_val;
        double      float_val;
        const char* string_val;   /* Valid only during callback scope */
        void*       object_ptr;   /* Opaque native object pointer */
    } data;
} SSValue;

/* Value construction helpers */
#define SS_NULL()              ((SSValue){ SS_TYPE_NULL,   {.int_val = 0}           })
#define SS_BOOL(b)             ((SSValue){ SS_TYPE_BOOL,   {.bool_val = (b)}        })
#define SS_INT(i)              ((SSValue){ SS_TYPE_INT,    {.int_val = (i)}         })
#define SS_FLOAT(f)            ((SSValue){ SS_TYPE_FLOAT,  {.float_val = (f)}       })
#define SS_STRING(s)           ((SSValue){ SS_TYPE_STRING, {.string_val = (s)}      })
#define SS_OBJECT(p)           ((SSValue){ SS_TYPE_OBJECT, {.object_ptr = (p)}      })

/* ============================================================================
 * Error Codes
 * ============================================================================ */

typedef enum SSResult {
    SS_OK                  = 0,
    SS_ERROR_COMPILE       = 1,   /* Compilation failed */
    SS_ERROR_RUNTIME       = 2,   /* Runtime VM error */
    SS_ERROR_INVALID_ARG   = 3,   /* Invalid argument passed */
    SS_ERROR_NOT_FOUND     = 4,   /* Function/variable not found */
    SS_ERROR_OUT_OF_MEMORY = 5,   /* Memory allocation failed */
    SS_ERROR_IO            = 6,   /* File I/O error */
    SS_ERROR_TYPE_CHECK    = 7    /* Type checking failed */
} SSResult;

/* ============================================================================
 * Callback Types
 * ============================================================================ */

/**
 * Native function callback type.
 *
 * @param context   The calling context
 * @param args      Array of arguments passed from script
 * @param arg_count Number of arguments
 * @param out_result Pointer to store the return value
 * @return SS_OK on success, error code on failure
 */
typedef SSResult (*SSNativeFunc)(SSContext context,
                                 const SSValue* args,
                                 int arg_count,
                                 SSValue* out_result);

/**
 * Print/log callback type for redirecting script output.
 * Game engines typically want to redirect print() to their own console.
 *
 * @param context   The calling context
 * @param message   The message string
 * @param user_data User-provided data pointer
 */
typedef void (*SSPrintFunc)(SSContext context,
                            const char* message,
                            void* user_data);

/**
 * Error callback type for handling VM errors.
 *
 * @param context   The calling context
 * @param error_code The error code
 * @param message   Human-readable error description
 * @param line      Source line number (0 if unavailable)
 * @param user_data User-provided data pointer
 */
typedef void (*SSErrorFunc)(SSContext context,
                            SSResult error_code,
                            const char* message,
                            int line,
                            void* user_data);

/* ============================================================================
 * Context Lifecycle
 * ============================================================================ */

/**
 * Create a new SwiftScript context.
 * Each context contains an independent VM instance with its own global state.
 * Thread-safe: Each context should be used from a single thread only.
 *
 * @return New context handle, or NULL on failure
 */
SS_API SSContext ss_create_context(void);

/**
 * Create a context with custom configuration.
 *
 * @param max_stack_size  Maximum stack size (0 = default 65536)
 * @param enable_debug    Enable debug mode (0 = off, 1 = on)
 * @return New context handle, or NULL on failure
 */
SS_API SSContext ss_create_context_ex(size_t max_stack_size, int enable_debug);

/**
 * Destroy a context and free all associated resources.
 * All scripts compiled within this context become invalid.
 *
 * @param context Context to destroy
 */
SS_API void ss_destroy_context(SSContext context);

/* ============================================================================
 * Script Compilation & Loading
 * ============================================================================ */

/**
 * Compile SwiftScript source code into a script handle.
 *
 * @param context    The context to compile within
 * @param source     Source code string (UTF-8, null-terminated)
 * @param source_name Optional name for error messages (e.g., "main.ss"), can be NULL
 * @param out_script Receives the compiled script handle
 * @return SS_OK on success, SS_ERROR_COMPILE on failure
 */
SS_API SSResult ss_compile(SSContext context,
                           const char* source,
                           const char* source_name,
                           SSScript* out_script);

/**
 * Compile with type checking enabled.
 * Performs static analysis before compilation for earlier error detection.
 *
 * @param context    The context
 * @param source     Source code string
 * @param source_name Optional name for error messages
 * @param out_script Receives the compiled script handle
 * @return SS_OK, SS_ERROR_TYPE_CHECK, or SS_ERROR_COMPILE
 */
SS_API SSResult ss_compile_checked(SSContext context,
                                   const char* source,
                                   const char* source_name,
                                   SSScript* out_script);

/**
 * Load pre-compiled bytecode from memory.
 * Useful for shipping pre-compiled .ssasm files.
 *
 * @param context    The context
 * @param data       Bytecode data buffer
 * @param data_size  Size of bytecode data in bytes
 * @param out_script Receives the script handle
 * @return SS_OK on success
 */
SS_API SSResult ss_load_bytecode(SSContext context,
                                 const void* data,
                                 size_t data_size,
                                 SSScript* out_script);

/**
 * Load pre-compiled bytecode from a file.
 *
 * @param context    The context
 * @param file_path  Path to .ssasm file
 * @param out_script Receives the script handle
 * @return SS_OK on success, SS_ERROR_IO on file error
 */
SS_API SSResult ss_load_bytecode_file(SSContext context,
                                      const char* file_path,
                                      SSScript* out_script);

/**
 * Compile source code and serialize to bytecode buffer.
 * Caller must free the buffer with ss_free_buffer().
 *
 * @param context       The context
 * @param source        Source code
 * @param source_name   Optional source name
 * @param out_data      Receives pointer to bytecode buffer
 * @param out_data_size Receives size of bytecode buffer
 * @return SS_OK on success
 */
SS_API SSResult ss_compile_to_bytecode(SSContext context,
                                       const char* source,
                                       const char* source_name,
                                       void** out_data,
                                       size_t* out_data_size);

/**
 * Destroy a compiled script.
 *
 * @param script Script to destroy
 */
SS_API void ss_destroy_script(SSScript script);

/* ============================================================================
 * Script Execution
 * ============================================================================ */

/**
 * Execute a compiled script.
 *
 * @param context    The context
 * @param script     Compiled script to execute
 * @param out_result Optional: receives the return value (can be NULL)
 * @return SS_OK on success, SS_ERROR_RUNTIME on failure
 */
SS_API SSResult ss_execute(SSContext context,
                           SSScript script,
                           SSValue* out_result);

/**
 * Compile and execute source code in one step.
 * Convenience function combining ss_compile() + ss_execute().
 *
 * @param context    The context
 * @param source     Source code string
 * @param out_result Optional: receives the return value
 * @return SS_OK on success
 */
SS_API SSResult ss_run(SSContext context,
                       const char* source,
                       SSValue* out_result);

/**
 * Call a global function defined in a previously executed script.
 *
 * @param context      The context
 * @param func_name    Name of the global function
 * @param args         Array of arguments
 * @param arg_count    Number of arguments
 * @param out_result   Receives the return value
 * @return SS_OK on success, SS_ERROR_NOT_FOUND if function not found
 */
SS_API SSResult ss_call_function(SSContext context,
                                 const char* func_name,
                                 const SSValue* args,
                                 int arg_count,
                                 SSValue* out_result);

/* ============================================================================
 * Native Function Registration
 * ============================================================================ */

/**
 * Register a native C function callable from SwiftScript.
 *
 * @param context     The context
 * @param script_name The function name visible in scripts
 * @param func        Native function callback
 * @return SS_OK on success
 *
 * Example (C):
 *   SSResult my_add(SSContext ctx, const SSValue* args, int argc, SSValue* result) {
 *       if (argc != 2) return SS_ERROR_INVALID_ARG;
 *       result->type = SS_TYPE_INT;
 *       result->data.int_val = args[0].data.int_val + args[1].data.int_val;
 *       return SS_OK;
 *   }
 *   ss_register_function(ctx, "native_add", my_add);
 *
 * Script usage:
 *   let result = native_add(3, 4)  // returns 7
 */
SS_API SSResult ss_register_function(SSContext context,
                                     const char* script_name,
                                     SSNativeFunc func);

/**
 * Unregister a previously registered native function.
 *
 * @param context     The context
 * @param script_name Function name to unregister
 * @return SS_OK on success, SS_ERROR_NOT_FOUND if not registered
 */
SS_API SSResult ss_unregister_function(SSContext context,
                                       const char* script_name);

/* ============================================================================
 * Global Variables
 * ============================================================================ */

/**
 * Set a global variable accessible from scripts.
 *
 * @param context The context
 * @param name    Variable name
 * @param value   Value to set
 * @return SS_OK on success
 */
SS_API SSResult ss_set_global(SSContext context,
                              const char* name,
                              SSValue value);

/**
 * Get a global variable value.
 *
 * @param context   The context
 * @param name      Variable name
 * @param out_value Receives the value
 * @return SS_OK on success, SS_ERROR_NOT_FOUND if not defined
 */
SS_API SSResult ss_get_global(SSContext context,
                              const char* name,
                              SSValue* out_value);

/* ============================================================================
 * Callbacks
 * ============================================================================ */

/**
 * Set a callback for print() output redirection.
 * By default, print() goes to stdout. Game engines should redirect
 * to their own console/log system.
 *
 * @param context   The context
 * @param func      Print callback function (NULL to reset to default)
 * @param user_data Arbitrary pointer passed to the callback
 */
SS_API void ss_set_print_callback(SSContext context,
                                  SSPrintFunc func,
                                  void* user_data);

/**
 * Set a callback for error handling.
 *
 * @param context   The context
 * @param func      Error callback function (NULL to reset to default)
 * @param user_data Arbitrary pointer passed to the callback
 */
SS_API void ss_set_error_callback(SSContext context,
                                  SSErrorFunc func,
                                  void* user_data);

/* ============================================================================
 * Error Information
 * ============================================================================ */

/**
 * Get the last error message for the context.
 * The returned string is valid until the next API call on this context.
 *
 * @param context The context
 * @return Error message string, or empty string if no error
 */
SS_API const char* ss_get_last_error(SSContext context);

/**
 * Get the last error line number.
 *
 * @param context The context
 * @return Line number, or 0 if unavailable
 */
SS_API int ss_get_last_error_line(SSContext context);

/* ============================================================================
 * Module System
 * ============================================================================ */

/**
 * Set the base directory for resolving import statements.
 *
 * @param context The context
 * @param dir     Base directory path
 */
SS_API void ss_set_base_directory(SSContext context, const char* dir);

/**
 * Add an import search path.
 * Multiple paths can be added; they are searched in order.
 *
 * @param context The context
 * @param path    Directory path to search for modules
 */
SS_API void ss_add_import_path(SSContext context, const char* path);

/* ============================================================================
 * Memory Management
 * ============================================================================ */

/**
 * Free a buffer allocated by the embedding API.
 * Use this to free buffers returned by ss_compile_to_bytecode(), etc.
 *
 * @param buffer Pointer to buffer to free
 */
SS_API void ss_free_buffer(void* buffer);

/**
 * Get VM memory statistics.
 *
 * @param context           The context
 * @param out_total_alloc   Receives total bytes allocated (can be NULL)
 * @param out_total_freed   Receives total bytes freed (can be NULL)
 * @param out_current_objects Receives current live object count (can be NULL)
 */
SS_API void ss_get_memory_stats(SSContext context,
                                size_t* out_total_alloc,
                                size_t* out_total_freed,
                                size_t* out_current_objects);

/* ============================================================================
 * User Data
 * ============================================================================ */

/**
 * Store arbitrary user data in a context.
 * Useful for associating game engine objects with a scripting context.
 *
 * @param context   The context
 * @param user_data Pointer to store
 */
SS_API void ss_set_user_data(SSContext context, void* user_data);

/**
 * Retrieve previously stored user data.
 *
 * @param context The context
 * @return User data pointer, or NULL if not set
 */
SS_API void* ss_get_user_data(SSContext context);

/* ============================================================================
 * Native Object Lifetime Management
 * ============================================================================ */

/**
 * Ownership model for native objects.
 *
 * By default, native objects are owned by the VM: when the script's reference
 * count drops to 0, the VM calls the registered destructor and frees the
 * underlying C++ pointer.
 *
 * Game engines typically need to own their objects (e.g., Transforms, GameObjects)
 * and only expose them to scripts as borrowed references. This API lets you
 * control who owns the native pointer.
 */

/** Native object ownership mode */
typedef enum SSOwnership {
    SS_OWNERSHIP_VM     = 0,  /* VM owns native_ptr: destructor called on RC=0 (default) */
    SS_OWNERSHIP_ENGINE = 1   /* Engine owns native_ptr: VM never frees it */
} SSOwnership;

/**
 * Callback invoked when the VM releases its last reference to an
 * engine-owned native object. The engine can use this to decrement
 * its own reference count or perform other bookkeeping.
 *
 * NOTE: The native_ptr is NOT freed by the VM. This callback is purely
 * a notification that the script no longer references this object.
 *
 * @param context    The context
 * @param native_ptr The native pointer that was being wrapped
 * @param type_name  The registered type name
 * @param user_data  User-provided data from ss_set_release_callback
 */
typedef void (*SSReleaseNotifyFunc)(SSContext context,
                                    void* native_ptr,
                                    const char* type_name,
                                    void* user_data);

/**
 * Wrap an engine-owned native pointer as a script-accessible object.
 * The VM will NOT free the native pointer when the script releases it.
 *
 * This is the primary way to expose game engine objects (Transform,
 * GameObject, Rigidbody, etc.) to scripts without transferring ownership.
 *
 * @param context    The context
 * @param native_ptr Pointer to the engine-owned C++ object
 * @param type_name  Type name (used for method/property resolution)
 * @param ownership  Ownership mode (SS_OWNERSHIP_VM or SS_OWNERSHIP_ENGINE)
 * @param out_value  Receives an SSValue wrapping the native object
 * @return SS_OK on success
 *
 * Example:
 *   // Engine owns the Transform - VM just borrows it
 *   Transform* tr = gameObject->GetTransform();
 *   SSValue val;
 *   ss_wrap_native(ctx, tr, "Transform", SS_OWNERSHIP_ENGINE, &val);
 *   ss_set_global(ctx, "transform", val);
 *
 *   // Now script can do:  transform.position = Vector3(1, 2, 3)
 *   // When script is done, Transform is NOT deleted by VM
 */
SS_API SSResult ss_wrap_native(SSContext context,
                               void* native_ptr,
                               const char* type_name,
                               SSOwnership ownership,
                               SSValue* out_value);

/**
 * Set the ownership mode for an existing native object.
 * Can be used to transfer ownership after creation.
 *
 * @param context   The context
 * @param value     SSValue containing a native object (SS_TYPE_OBJECT)
 * @param ownership New ownership mode
 * @return SS_OK on success, SS_ERROR_INVALID_ARG if not a native object
 */
SS_API SSResult ss_set_ownership(SSContext context,
                                 SSValue value,
                                 SSOwnership ownership);

/**
 * Get the ownership mode of a native object.
 *
 * @param context       The context
 * @param value         SSValue containing a native object
 * @param out_ownership Receives the ownership mode
 * @return SS_OK on success, SS_ERROR_INVALID_ARG if not a native object
 */
SS_API SSResult ss_get_ownership(SSContext context,
                                 SSValue value,
                                 SSOwnership* out_ownership);

/**
 * Set a callback that is invoked when the VM drops its last reference
 * to an engine-owned native object.
 *
 * This enables the engine to track when scripts are no longer using
 * a particular object (e.g., to decrement its own refcount or unpin it).
 *
 * @param context   The context
 * @param func      Release notification callback (NULL to disable)
 * @param user_data Arbitrary pointer passed to the callback
 *
 * Example (Unreal Engine):
 *   void OnScriptRelease(SSContext ctx, void* ptr, const char* type, void* ud) {
 *       UObject* obj = static_cast<UObject*>(ptr);
 *       obj->RemoveFromRoot();  // Allow GC to collect it
 *   }
 *   ss_set_release_callback(ctx, OnScriptRelease, nullptr);
 */
SS_API void ss_set_release_callback(SSContext context,
                                    SSReleaseNotifyFunc func,
                                    void* user_data);

/**
 * Manually invalidate a native object from the engine side.
 * Call this when the engine destroys an object that scripts may still reference.
 * After invalidation, any script access to this object will return null.
 *
 * This prevents dangling pointer crashes when engine objects are destroyed
 * before scripts release them (e.g., when a GameObject is Destroy()'d in Unity).
 *
 * @param context    The context
 * @param native_ptr The native pointer to invalidate
 * @return SS_OK on success
 *
 * Example (Unity):
 *   void OnDestroy() {
 *       ss_invalidate_native(ctx, this);  // Script sees null instead of crash
 *   }
 */
SS_API SSResult ss_invalidate_native(SSContext context, void* native_ptr);

/**
 * Extract the raw native pointer from an SSValue.
 * Returns NULL if the value is not a native object or has been invalidated.
 *
 * @param context The context
 * @param value   SSValue containing a native object
 * @return Native pointer, or NULL
 */
SS_API void* ss_get_native_ptr(SSContext context, SSValue value);

/* ============================================================================
 * Version Information
 * ============================================================================ */

/** Get SwiftScript version string (e.g., "1.0.0") */
SS_API const char* ss_version(void);

/** Get SwiftScript version components */
SS_API void ss_version_numbers(int* major, int* minor, int* patch);

#endif /* SS_EMBED_H */
