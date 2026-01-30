#pragma once

#include "ss_core.hpp"
#include "ss_value.hpp"
#include "ss_chunk.hpp"
#include <vector>
#include <unordered_map>
#include <string>
#include <optional>

namespace swiftscript {

    // Forward declarations
    class Fiber;
    class CallFrame;

    // VM Configuration
    struct VMConfig {
        size_t initial_stack_size = 256;
        size_t max_stack_size = 65536;
        size_t deferred_cleanup_threshold = 100;
        bool enable_debug = false;
    };

    // Virtual Machine
    class VM {
    private:
        VMConfig config_;

        // Object management
        Object* objects_head_{ nullptr };  // Linked list of all objects
        std::vector<Object*> deferred_releases_;

        // Execution state
        std::vector<Value> stack_;
        std::vector<CallFrame> call_frames_;
        const Chunk* chunk_{ nullptr };
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
        Value execute(const Chunk& chunk);

        // Statistics
        const MemoryStats& get_stats() const { return stats_; }
        void print_stats() const;
        void record_allocation_delta(Object& obj, size_t new_size);

        // Configuration
        const VMConfig& config() const { return config_; }

        friend class RC;

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
    };

    // Call Frame for function calls
    class CallFrame {
    public:
        size_t stack_base;      // Base of this frame's stack
        size_t return_address;  // Where to return after call
        const Chunk* chunk;
        std::string function_name;
        ClosureObject* closure; // Current closure for upvalue access (nullptr for plain functions)
        bool is_initializer;

        CallFrame(size_t base, size_t ret_addr, const Chunk* call_chunk, std::string name, ClosureObject* c, bool initializer)
            : stack_base(base),
              return_address(ret_addr),
              chunk(call_chunk),
              function_name(std::move(name)),
              closure(c),
              is_initializer(initializer) {
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

} // namespace swiftscript
