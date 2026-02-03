#pragma once

namespace swiftscript {

// Forward declarations
class VM;
class Object;
class Value;

// Reference type enumeration
enum class RefType : uint8_t {
    Strong = 0,   // Default strong reference
    Weak = 1,     // Weak reference (auto nil when deallocated)
    Unowned = 2   // Unowned reference (no RC, dangling possible)
};

// Object type enumeration
enum class ObjectType : uint8_t {
    String,
    List,
    Map,
    Function,
    Closure,
    Class,
    Instance,
    Struct,          // Struct type definition
    StructInstance,  // Struct instance (value type)
    Enum,            // Enum type definition
    EnumCase,        // Enum case instance
    Protocol,        // Protocol definition
    Module,
    Fiber,
    Range,
    Upvalue,
    BuiltinMethod,
    BoundMethod,
    Tuple            // Tuple object
};

// Utility: ObjectType to string
inline const char* object_type_name(ObjectType t) {
    switch (t) {
        case ObjectType::String:   return "String";
        case ObjectType::List:     return "List";
        case ObjectType::Map:      return "Map";
        case ObjectType::Function: return "Function";
        case ObjectType::Closure:  return "Closure";
        case ObjectType::Class:    return "Class";
        case ObjectType::Instance: return "Instance";
        case ObjectType::Struct:   return "Struct";
        case ObjectType::StructInstance: return "StructInstance";
        case ObjectType::Enum:     return "Enum";
        case ObjectType::EnumCase: return "EnumCase";
        case ObjectType::Protocol: return "Protocol";
        case ObjectType::BoundMethod: return "BoundMethod";
        case ObjectType::Module:   return "Module";
        case ObjectType::Fiber:    return "Fiber";
        case ObjectType::Range:    return "Range";
        case ObjectType::Upvalue:  return "Upvalue";
        case ObjectType::BuiltinMethod: return "BuiltinMethod";
        case ObjectType::Tuple: return "Tuple";
    }
    return "Unknown";
}

// RC Management Structure
struct RCInfo {
    std::atomic<int32_t> strong_count{1};  // Creator owns the initial reference.
    std::atomic<bool> has_creator_ref{true};
    std::atomic<int32_t> weak_count{0};
    std::unordered_set<Value*> weak_refs;  // Weak reference slots to nil on dealloc
    bool is_dead{false};  // Marks object as logically deallocated (strong_count == 0)

    RCInfo() = default;
    RCInfo(const RCInfo&) = delete;
    RCInfo& operator=(const RCInfo&) = delete;
};

// Base Object class for all heap-allocated objects
class Object {
public:
    ObjectType type;
    RCInfo rc;
    Object* next{nullptr};  // For VM's object linked list
    size_t tracked_size{0};

    explicit Object(ObjectType t) : type(t) {}
    virtual ~Object() = default;

    // Virtual methods for type-specific behavior
    virtual std::string to_string() const = 0;
    virtual size_t memory_size() const = 0;

    // Prevent copying
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
};

// RC Operations
class RC {
public:
    // Strong reference operations
    static void retain(Object* obj);
    static void release(VM* vm, Object* obj);
    static void adopt(Object* obj);

    // Weak reference operations
    static void weak_retain(Object* obj, Value* weak_slot);
    static void weak_release(Object* obj, Value* weak_slot);

    // Deferred cleanup
    static void process_deferred_releases(VM* vm);

    // Nil out all weak reference slots for an object
    static void nil_weak_refs(Object* obj);

private:
    // Internal: release child objects of containers
    static void release_children(VM* vm, Object* obj, std::unordered_set<Object*>& deleted_set);
};

// Memory statistics
struct MemoryStats {
    size_t total_allocated{0};
    size_t total_freed{0};
    size_t current_objects{0};
    size_t peak_objects{0};
    size_t retain_count{0};
    size_t release_count{0};
};

// Debug utilities
#ifdef SS_DEBUG
    #define SS_DEBUG_RC(fmt, ...) \
        printf("[RC] " fmt "\n", ##__VA_ARGS__)
#else
    #define SS_DEBUG_RC(fmt, ...)
#endif

#define SS_ASSERT(cond, msg) assert((cond) && (msg))

} // namespace swiftscript
