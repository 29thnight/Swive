#pragma once
#include <unordered_map>
#include "ss_core.hpp"
#include "ss_value.hpp"
#include "ss_chunk.hpp"

namespace swiftscript {
    // Primary OpCodeHandler template. Specializations in
    // `ss_vm_opcodes.inl` override `execute`. The primary implementation
    // provides a default that throws for unhandled opcodes.
    template<OpCode op>
    struct OpCodeHandler {
        static void execute(VM& vm) {
            (void)vm;
            throw std::runtime_error("Unhandled opcode (no handler specialization)");
        }
    };
    // Forward declarations
    class CallFrame;
    class ClosureObject;
    class FunctionObject;
    class UpvalueObject;
    class InstanceObject;

    // VM Configuration
    struct VMConfig {
        size_t initial_stack_size = 256;
        size_t max_stack_size = 65536;
        size_t deferred_cleanup_threshold = 100;
        bool enable_debug = false;
    };

    // Virtual Machine
    class VM {
        // Friend declaration for OpCode handlers
        template<OpCode op>
        friend struct OpCodeHandler;
        
    private:
        VMConfig config_;

        // Object management
        Object* objects_head_{ nullptr };  // Linked list of all objects
        std::vector<Object*> deferred_releases_;

        // Execution state
        std::vector<Value> stack_;
        std::vector<CallFrame> call_frames_;
        const Assembly* chunk_{ nullptr };
        size_t ip_{ 0 };
        UpvalueObject* open_upvalues_{ nullptr };

        // Global scope
        std::unordered_map<std::string, Value> globals_;

        // Statistics
        MemoryStats stats_;

        // Counters
        uint32_t rc_operations_{ 0 };
        bool is_collecting_{ false };

        void record_deallocation(const Object& obj);

    public:
        explicit VM(VMConfig config = VMConfig{});
        ~VM();

        // Prevent copying
        VM(const VM&) = delete;
        VM& operator=(const VM&) = delete;

        // Object lifecycle
        template<typename T, typename... Args>
        T* allocate_object(Args&&... args);

        void add_deferred_release(Object* obj);
        std::vector<Object*>& get_deferred_releases() { return deferred_releases_; }

        // Remove object from linked list (used when object is deallocated)
        void remove_from_objects_list(Object* obj);

        // Stack operations
        void push(Value val);
        Value pop();
        Value peek(size_t offset = 0) const;
        size_t stack_size() const { return stack_.size(); }

        // Deinit execution
        void execute_deinit(InstanceObject* inst, Value deinit_method);

        // Global variables
        void set_global(const std::string& name, Value val);
        Value get_global(const std::string& name) const;
        bool has_global(const std::string& name) const;

        // Execution control
        void run_cleanup();
        void collect_if_needed();
        void record_rc_operation();
        Value interpret(const std::string& source);
        Value execute(const Assembly& chunk);

        // Statistics
        const MemoryStats& get_stats() const { return stats_; }
        void print_stats() const;
        void record_allocation_delta(Object& obj, size_t new_size);

        // Configuration
        const VMConfig& config() const { return config_; }

        friend class RC;

        // 공통: Value callee(Function/Closure)를 분석해서 호출 프레임을 설정한다.
        // - self 포함 인자들은 호출자가 stack에 push 완료한 상태여야 함.
        // - base_slot: CallFrame이 가리킬 첫 번째 인자 슬롯(보통 callee_index + 1)
        // - return: 성공하면 true, 아니면 예외
        static inline bool InvokeCallableWithPreparedStack(
            VM& vm,
            const Value& callee,
            size_t base_slot,
            size_t expected_param_count,
            const char* error_prefix,
            bool is_initializer = false
        ) {
            if (!callee.is_object() || !callee.as_object()) {
                throw std::runtime_error(std::string(error_prefix) + ": callee is not an object.");
            }

            Object* obj_callee = callee.as_object();
            FunctionObject* func = nullptr;
            ClosureObject* closure = nullptr;

            if (obj_callee->type == ObjectType::Closure) {
                closure = static_cast<ClosureObject*>(obj_callee);
                func = closure->function;
            }
            else if (obj_callee->type == ObjectType::Function) {
                func = static_cast<FunctionObject*>(obj_callee);
            }
            else {
                throw std::runtime_error(std::string(error_prefix) + ": callee must be a function/closure.");
            }

            if (!func || !func->chunk) {
                throw std::runtime_error(std::string(error_prefix) + ": function has no body.");
            }
            if (func->params.size() != expected_param_count) {
                throw std::runtime_error(std::string(error_prefix) + ": incorrect parameter count.");
            }

            vm.call_frames_.emplace_back(base_slot, vm.ip_, vm.chunk_, func->name, closure, is_initializer);
            vm.chunk_ = func->chunk.get();
            vm.ip_ = 0;
            return true;
        }

        // computed getter: stack에 [getter, self]를 push하고 호출 프레임 구성
        static inline bool TryInvokeComputedGetter(VM& vm, const Value& getter, const Value& self) {
            vm.push(getter);
            vm.push(self);
            const size_t callee_index = vm.stack_.size() - 2; // [getter, self]
            return InvokeCallableWithPreparedStack(
                vm,
                getter,
                /*base_slot*/ callee_index + 1,
                /*expected_param_count*/ 1,
                "Computed getter",
                /*is_initializer*/ false
            );
        }

        // computed setter: stack에 [setter, self, value]를 push하고 호출 프레임 구성
        static inline bool TryInvokeComputedSetter(VM& vm, const Value& setter, const Value& self, const Value& value) {
            vm.push(setter);
            vm.push(self);
            vm.push(value);
            const size_t callee_index = vm.stack_.size() - 3; // [setter, self, value]
            return InvokeCallableWithPreparedStack(
                vm,
                setter,
                /*base_slot*/ callee_index + 1,
                /*expected_param_count*/ 2,
                "Computed setter",
                /*is_initializer*/ false
            );
        }

    private:
        Value run();
        uint8_t read_byte();
        uint16_t read_short();
        Value read_constant();
        const std::string& read_string();
        size_t current_stack_base() const;
        bool is_truthy(const Value& value) const;
        Value get_property(const Value& object, const std::string& name);
        bool find_method_on_class(ClassObject* klass, const std::string& name, Value& out_method) const;
        std::optional<Value> call_operator_overload(const Value& left, const Value& right, const std::string& name);
        void build_param_defaults(const FunctionPrototype& proto,
                                  std::vector<Value>& defaults,
                                  std::vector<bool>& has_defaults);
        void apply_positional_defaults(uint16_t& arg_count,
                                       FunctionObject* func,
                                       bool has_receiver);
        void apply_named_arguments(size_t callee_index,
                                   uint16_t& arg_count,
                                   FunctionObject* func,
                                   bool has_receiver,
                                   const std::vector<std::optional<std::string>>& arg_names);

        UpvalueObject* capture_upvalue(Value* local);
        void close_upvalues(Value* last);
        
        // Property observer helpers
        void call_property_observer(Value observer, Value instance, Value argument);
        
        // Execute a function synchronously (for nested calls like observers)
        Value execute_function(FunctionObject* func, ClosureObject* closure, const std::vector<Value>& args);
    };

    // Call Frame for function calls
    class CallFrame {
    public:
        size_t stack_base;      // Base of this frame's stack
        size_t return_address;  // Where to return after call
        const Assembly* chunk;
        std::string function_name;
        ClosureObject* closure; // Current closure for upvalue access (nullptr for plain functions)
        bool is_initializer;
        bool is_mutating;       // True if this is a mutating struct method
        size_t receiver_index;  // Stack index of the receiver (for mutating methods)

        CallFrame(size_t base, size_t ret_addr, const Assembly* call_chunk, std::string name, ClosureObject* c, bool initializer, bool mutating = false, size_t recv_idx = 0)
            : stack_base(base),
              return_address(ret_addr),
              chunk(call_chunk),
              function_name(std::move(name)),
              closure(c),
              is_initializer(initializer),
              is_mutating(mutating),
              receiver_index(recv_idx) {
        }
    };

    // Template implementation
    template<typename T, typename... Args>
    T* VM::allocate_object(Args&&... args) {
        T* obj = new T(std::forward<Args>(args)...);

        // Add to linked list
        obj->next = objects_head_;
        objects_head_ = obj;

        // Update stats
        obj->tracked_size = obj->memory_size();
        stats_.total_allocated += obj->tracked_size;
        stats_.current_objects++;
        if (stats_.current_objects > stats_.peak_objects) {
            stats_.peak_objects = stats_.current_objects;
        }

        SS_DEBUG_RC("ALLOCATE %p [%s] size: %zu bytes",
            obj, object_type_name(obj->type), obj->tracked_size);

        return obj;
    }

    // Opcode dispatch table (function pointer type) instantiated at program start
    using OpHandlerFunc = void(*)(VM&);
    extern const std::array<OpHandlerFunc, 256> g_opcode_handlers;

    // Build the handler table at program startup
    constexpr std::array<OpHandlerFunc, 256> make_handler_table();

} // namespace swiftscript

#include "ss_vm_opcodes.inl"