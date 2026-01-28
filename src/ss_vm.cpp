#include "ss_vm.hpp"
#include <iostream>
#include <iomanip>

namespace swiftscript {

    VM::VM(VMConfig config)
        : config_(config) {
        stack_.reserve(config_.initial_stack_size);
    }

    VM::~VM() {
        // Clean up all remaining objects
        Object* obj = objects_head_;
        while (obj) {
            Object* next = obj->next;

            // Nil out weak references
            for (Value* weak_slot : obj->rc.weak_refs) {
                *weak_slot = Value::null();
            }

            delete obj;
            obj = next;
        }

        if (config_.enable_debug) {
            print_stats();
        }
    }

    void VM::push(Value val) {
        if (stack_.size() >= config_.max_stack_size) {
            throw std::runtime_error("Stack overflow");
        }

        // Handle RC for object values
        if (val.is_object() && val.ref_type() == RefType::Strong) {
            RC::retain(val.as_object());
            stats_.retain_count++;
            record_rc_operation();
        }

        stack_.push_back(val);
    }

    Value VM::pop() {
        if (stack_.empty()) {
            throw std::runtime_error("Stack underflow");
        }

        Value val = stack_.back();
        stack_.pop_back();

        // Handle RC for object values
        if (val.is_object() && val.ref_type() == RefType::Strong) {
            RC::release(this, val.as_object());
            stats_.release_count++;
            record_rc_operation();
        }

        return val;
    }

    Value VM::peek(size_t offset) const {
        if (offset >= stack_.size()) {
            throw std::runtime_error("Stack peek out of bounds");
        }
        return stack_[stack_.size() - 1 - offset];
    }

    void VM::set_global(const std::string& name, Value val) {
        // Release old value if exists
        auto it = globals_.find(name);
        if (it != globals_.end()) {
            if (it->second.is_object() && it->second.ref_type() == RefType::Strong) {
                RC::release(this, it->second.as_object());
                record_rc_operation();
            }
        }

        // Retain new value
        if (val.is_object() && val.ref_type() == RefType::Strong) {
            RC::retain(val.as_object());
            stats_.retain_count++;
            record_rc_operation();
        }

        globals_[name] = val;
    }

    Value VM::get_global(const std::string& name) const {
        auto it = globals_.find(name);
        if (it == globals_.end()) {
            return Value::undefined();
        }
        return it->second;
    }

    bool VM::has_global(const std::string& name) const {
        return globals_.find(name) != globals_.end();
    }

    void VM::add_deferred_release(Object* obj) {
        deferred_releases_.push_back(obj);
    }

    void VM::remove_from_objects_list(Object* obj) {
        if (!objects_head_) return;

        // Special case: object is head
        if (objects_head_ == obj) {
            objects_head_ = obj->next;
            return;
        }

        // Find and remove from linked list
        Object* current = objects_head_;
        while (current->next) {
            if (current->next == obj) {
                current->next = obj->next;
                return;
            }
            current = current->next;
        }
    }

    void VM::run_cleanup() {
        if (is_collecting_ || deferred_releases_.empty()) {
            return;
        }
        is_collecting_ = true;
        RC::process_deferred_releases(this);
        is_collecting_ = false;
    }

    void VM::collect_if_needed() {
        if (!is_collecting_ && rc_operations_ >= config_.deferred_cleanup_threshold) {
            run_cleanup();
            rc_operations_ = 0;
        }
    }

    void VM::record_rc_operation() {
        ++rc_operations_;
        collect_if_needed();
    }

    void VM::record_deallocation(const Object& obj) {
        stats_.total_freed += obj.memory_size();
        stats_.current_objects--;
    }

    void VM::print_stats() const {
        std::cout << "\n=== SwiftScript VM Statistics ===\n";
        std::cout << "Total Allocated:  " << std::setw(10) << stats_.total_allocated << " bytes\n";
        std::cout << "Total Freed:      " << std::setw(10) << stats_.total_freed << " bytes\n";
        std::cout << "Current Objects:  " << std::setw(10) << stats_.current_objects << "\n";
        std::cout << "Peak Objects:     " << std::setw(10) << stats_.peak_objects << "\n";
        std::cout << "Total Retains:    " << std::setw(10) << stats_.retain_count << "\n";
        std::cout << "Total Releases:   " << std::setw(10) << stats_.release_count << "\n";
        std::cout << "Memory Leaked:    " << std::setw(10)
            << (stats_.total_allocated - stats_.total_freed) << " bytes\n";
        std::cout << "=================================\n";
    }

} // namespace swiftscript
