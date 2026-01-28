#pragma once

#include <atomic>
#include <unordered_set>
#include <string>
#include <memory>
#include <cstdint>
#include <cassert>

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
    Module,
    Fiber,
    Range,
    Upvalue
};

// RC Management Structure
struct RCInfo {
    std::atomic<int32_t> strong_count{0};
    std::atomic<int32_t> weak_count{0};
    std::unordered_set<Value*> weak_refs;  // Weak reference slots to nil on dealloc
    
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
    
    // Weak reference operations
    static void weak_retain(Object* obj, Value* weak_slot);
    static void weak_release(Object* obj, Value* weak_slot);
    
    // Deferred cleanup
    static void process_deferred_releases(VM* vm);
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
