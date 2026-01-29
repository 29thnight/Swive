#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <memory>
#include <map>

namespace swiftscript {
namespace test {

// Test result tracking
struct TestResult {
    std::string name;
    bool passed;
    std::string error_message;
    double duration_ms;
};

// Test statistics
class TestStatistics {
private:
    std::vector<TestResult> results_;
    int total_tests_ = 0;
    int passed_tests_ = 0;
    int failed_tests_ = 0;

public:
    void add_result(const TestResult& result) {
        results_.push_back(result);
        total_tests_++;
        if (result.passed) {
            passed_tests_++;
        } else {
            failed_tests_++;
        }
    }

    void print_summary() const {
        std::cout << "\n======================================\n";
        std::cout << "  TEST SUMMARY\n";
        std::cout << "======================================\n";
        std::cout << "Total Tests: " << total_tests_ << "\n";
        std::cout << "Passed: " << passed_tests_ << " ?\n";
        std::cout << "Failed: " << failed_tests_ << " ?\n";
        
        if (failed_tests_ > 0) {
            std::cout << "\nFailed Tests:\n";
            for (const auto& result : results_) {
                if (!result.passed) {
                    std::cout << "  ? " << result.name << "\n";
                    std::cout << "    Error: " << result.error_message << "\n";
                }
            }
        }
        
        std::cout << "\nPerformance:\n";
        double total_time = 0.0;
        for (const auto& result : results_) {
            total_time += result.duration_ms;
            std::cout << "  " << result.name << ": " 
                      << result.duration_ms << " ms\n";
        }
        std::cout << "  Total: " << total_time << " ms\n";
        std::cout << "======================================\n";
    }

    bool all_passed() const { return failed_tests_ == 0; }
    int get_failed_count() const { return failed_tests_; }
};

// Memory allocation tracker for leak detection
class MemoryTracker {
private:
    struct AllocationInfo {
        size_t size;
        std::string location;
    };
    
    std::map<void*, AllocationInfo> allocations_;
    size_t total_allocated_ = 0;
    size_t total_freed_ = 0;
    size_t peak_memory_ = 0;
    bool tracking_enabled_ = false;

public:
    static MemoryTracker& instance() {
        static MemoryTracker tracker;
        return tracker;
    }

    void enable_tracking() { tracking_enabled_ = true; }
    void disable_tracking() { tracking_enabled_ = false; }
    bool is_tracking() const { return tracking_enabled_; }

    void track_allocation(void* ptr, size_t size, const std::string& location = "") {
        if (!tracking_enabled_ || ptr == nullptr) return;
        
        allocations_[ptr] = {size, location};
        total_allocated_ += size;
        
        size_t current_usage = total_allocated_ - total_freed_;
        if (current_usage > peak_memory_) {
            peak_memory_ = current_usage;
        }
    }

    void track_deallocation(void* ptr) {
        if (!tracking_enabled_ || ptr == nullptr) return;
        
        auto it = allocations_.find(ptr);
        if (it != allocations_.end()) {
            total_freed_ += it->second.size;
            allocations_.erase(it);
        }
    }

    void reset() {
        allocations_.clear();
        total_allocated_ = 0;
        total_freed_ = 0;
        peak_memory_ = 0;
    }

    bool has_leaks() const {
        return !allocations_.empty();
    }

    void print_leak_report() const {
        if (allocations_.empty()) {
            std::cout << "? No memory leaks detected\n";
            return;
        }

        std::cout << "? Memory Leaks Detected!\n";
        std::cout << "  Leaked allocations: " << allocations_.size() << "\n";
        
        size_t total_leaked = 0;
        for (const auto& [ptr, info] : allocations_) {
            total_leaked += info.size;
            std::cout << "  - " << info.size << " bytes at " << ptr;
            if (!info.location.empty()) {
                std::cout << " (" << info.location << ")";
            }
            std::cout << "\n";
        }
        
        std::cout << "  Total leaked: " << total_leaked << " bytes\n";
    }

    void print_memory_stats() const {
        std::cout << "\nMemory Statistics:\n";
        std::cout << "  Total allocated: " << total_allocated_ << " bytes\n";
        std::cout << "  Total freed: " << total_freed_ << " bytes\n";
        std::cout << "  Peak usage: " << peak_memory_ << " bytes\n";
        std::cout << "  Current usage: " << (total_allocated_ - total_freed_) << " bytes\n";
    }
};

// Enhanced assertion helper
class AssertHelper {
public:
    static void assert_true(bool condition, const std::string& message) {
        if (!condition) {
            throw std::runtime_error("Assertion failed: " + message);
        }
    }

    static void assert_false(bool condition, const std::string& message) {
        if (condition) {
            throw std::runtime_error("Assertion failed: " + message);
        }
    }

    static void assert_contains(const std::string& text, const std::string& substring,
                               const std::string& context = "") {
        if (text.find(substring) == std::string::npos) {
            std::ostringstream oss;
            oss << "Expected to find '" << substring << "' in output";
            if (!context.empty()) {
                oss << " (" << context << ")";
            }
            oss << "\nActual output: " << text;
            throw std::runtime_error(oss.str());
        }
    }

    static void assert_not_contains(const std::string& text, const std::string& substring,
                                    const std::string& context = "") {
        if (text.find(substring) != std::string::npos) {
            std::ostringstream oss;
            oss << "Expected NOT to find '" << substring << "' in output";
            if (!context.empty()) {
                oss << " (" << context << ")";
            }
            oss << "\nActual output: " << text;
            throw std::runtime_error(oss.str());
        }
    }

    static void assert_equals(const std::string& expected, const std::string& actual,
                             const std::string& context = "") {
        if (expected != actual) {
            std::ostringstream oss;
            oss << "Strings not equal";
            if (!context.empty()) {
                oss << " (" << context << ")";
            }
            oss << "\nExpected: " << expected;
            oss << "\nActual: " << actual;
            throw std::runtime_error(oss.str());
        }
    }

    static void assert_order(const std::string& text, const std::string& first,
                            const std::string& second, const std::string& context = "") {
        size_t pos_first = text.find(first);
        size_t pos_second = text.find(second);
        
        if (pos_first == std::string::npos) {
            throw std::runtime_error("First string '" + first + "' not found in output");
        }
        if (pos_second == std::string::npos) {
            throw std::runtime_error("Second string '" + second + "' not found in output");
        }
        if (pos_first >= pos_second) {
            std::ostringstream oss;
            oss << "Expected '" << first << "' to appear before '" << second << "'";
            if (!context.empty()) {
                oss << " (" << context << ")";
            }
            oss << "\nActual output: " << text;
            throw std::runtime_error(oss.str());
        }
    }

    static void assert_error(const std::string& output, const std::string& context = "") {
        if (output.find("ERROR") == std::string::npos) {
            std::ostringstream oss;
            oss << "Expected error in output";
            if (!context.empty()) {
                oss << " (" << context << ")";
            }
            oss << "\nActual output: " << output;
            throw std::runtime_error(oss.str());
        }
    }

    static void assert_no_error(const std::string& output, const std::string& context = "") {
        if (output.find("ERROR") != std::string::npos) {
            std::ostringstream oss;
            oss << "Unexpected error in output";
            if (!context.empty()) {
                oss << " (" << context << ")";
            }
            oss << "\nActual output: " << output;
            throw std::runtime_error(oss.str());
        }
    }

    template<typename T>
    static void assert_equals(const T& expected, const T& actual, const std::string& context = "") {
        if (expected != actual) {
            std::ostringstream oss;
            oss << "Values not equal";
            if (!context.empty()) {
                oss << " (" << context << ")";
            }
            oss << "\nExpected: " << expected;
            oss << "\nActual: " << actual;
            throw std::runtime_error(oss.str());
        }
    }

    template<typename T>
    static void assert_not_equals(const T& expected, const T& actual, const std::string& context = "") {
        if (expected == actual) {
            std::ostringstream oss;
            oss << "Values should not be equal";
            if (!context.empty()) {
                oss << " (" << context << ")";
            }
            oss << "\nUnexpected value: " << actual;
            throw std::runtime_error(oss.str());
        }
    }

    template<typename T>
    static void assert_greater(const T& value, const T& min, const std::string& context = "") {
        if (!(value > min)) {
            std::ostringstream oss;
            oss << "Expected value > " << min;
            if (!context.empty()) {
                oss << " (" << context << ")";
            }
            oss << "\nActual: " << value;
            throw std::runtime_error(oss.str());
        }
    }

    template<typename T>
    static void assert_less(const T& value, const T& max, const std::string& context = "") {
        if (!(value < max)) {
            std::ostringstream oss;
            oss << "Expected value < " << max;
            if (!context.empty()) {
                oss << " (" << context << ")";
            }
            oss << "\nActual: " << value;
            throw std::runtime_error(oss.str());
        }
    }
};

// Test runner with automatic tracking
class TestRunner {
private:
    TestStatistics stats_;
    std::string current_test_name_;
    std::chrono::high_resolution_clock::time_point start_time_;

public:
    void begin_test(const std::string& name) {
        current_test_name_ = name;
        std::cout << "Test: " << name << " ... " << std::flush;
        start_time_ = std::chrono::high_resolution_clock::now();
    }

    void end_test(bool passed, const std::string& error_message = "") {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time_);
        
        TestResult result{
            current_test_name_,
            passed,
            error_message,
            static_cast<double>(duration.count())
        };
        
        stats_.add_result(result);
        
        if (passed) {
            std::cout << "? PASSED";
        } else {
            std::cout << "? FAILED";
        }
        std::cout << " (" << result.duration_ms << " ms)\n";
        
        if (!passed && !error_message.empty()) {
            std::cout << "  Error: " << error_message << "\n";
        }
    }

    template<typename Func>
    void run_test(const std::string& name, Func test_func) {
        begin_test(name);
        try {
            test_func();
            end_test(true);
        } catch (const std::exception& e) {
            end_test(false, e.what());
        } catch (...) {
            end_test(false, "Unknown exception");
        }
    }

    const TestStatistics& get_statistics() const { return stats_; }
    
    void print_summary() const {
        stats_.print_summary();
    }

    bool all_passed() const {
        return stats_.all_passed();
    }
};

// VM State Verifier
class VMStateVerifier {
public:
    struct VMState {
        size_t stack_size;
        size_t object_count;
        size_t string_pool_size;
        bool has_error;
        std::string error_message;
    };

    // This would need to be implemented based on your VM's API
    // Placeholder for now
    static VMState capture_state() {
        // TODO: Implement actual VM state capture
        return VMState{0, 0, 0, false, ""};
    }

    static void assert_clean_state(const VMState& state) {
        if (state.stack_size != 0) {
            throw std::runtime_error(
                "VM stack not clean: " + std::to_string(state.stack_size) + " items remaining");
        }
        if (state.has_error) {
            throw std::runtime_error("VM has error state: " + state.error_message);
        }
    }

    static void assert_no_leaks(const VMState& before, const VMState& after) {
        if (after.object_count > before.object_count) {
            size_t leaked = after.object_count - before.object_count;
            throw std::runtime_error(
                "Potential object leak: " + std::to_string(leaked) + " objects not freed");
        }
    }
};

// Output matcher with exact matching
class OutputMatcher {
public:
    static std::vector<std::string> split_lines(const std::string& text) {
        std::vector<std::string> lines;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) {
            // Remove trailing whitespace
            while (!line.empty() && std::isspace(line.back())) {
                line.pop_back();
            }
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
        return lines;
    }

    static void assert_exact_output(const std::string& actual, 
                                    const std::vector<std::string>& expected_lines) {
        auto actual_lines = split_lines(actual);
        
        if (actual_lines.size() != expected_lines.size()) {
            std::ostringstream oss;
            oss << "Line count mismatch\n";
            oss << "Expected " << expected_lines.size() << " lines, got " 
                << actual_lines.size() << " lines\n";
            oss << "Expected:\n";
            for (const auto& line : expected_lines) {
                oss << "  " << line << "\n";
            }
            oss << "Actual:\n";
            for (const auto& line : actual_lines) {
                oss << "  " << line << "\n";
            }
            throw std::runtime_error(oss.str());
        }

        for (size_t i = 0; i < expected_lines.size(); ++i) {
            if (actual_lines[i] != expected_lines[i]) {
                std::ostringstream oss;
                oss << "Line " << (i + 1) << " mismatch\n";
                oss << "Expected: " << expected_lines[i] << "\n";
                oss << "Actual:   " << actual_lines[i];
                throw std::runtime_error(oss.str());
            }
        }
    }

    static void assert_contains_all(const std::string& text,
                                    const std::vector<std::string>& substrings) {
        for (const auto& substring : substrings) {
            if (text.find(substring) == std::string::npos) {
                throw std::runtime_error("Expected to find: " + substring);
            }
        }
    }

    static void assert_output_order(const std::string& text,
                                   const std::vector<std::string>& ordered_substrings) {
        size_t last_pos = 0;
        for (const auto& substring : ordered_substrings) {
            size_t pos = text.find(substring, last_pos);
            if (pos == std::string::npos) {
                throw std::runtime_error("Expected to find in order: " + substring);
            }
            last_pos = pos + substring.length();
        }
    }
};

// Performance profiler
class PerformanceProfiler {
private:
    std::string name_;
    std::chrono::high_resolution_clock::time_point start_time_;
    bool running_ = false;

public:
    explicit PerformanceProfiler(const std::string& name) : name_(name) {}

    void start() {
        start_time_ = std::chrono::high_resolution_clock::now();
        running_ = true;
    }

    double stop_and_get_ms() {
        if (!running_) return 0.0;
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - start_time_);
        running_ = false;
        
        return duration.count() / 1000.0;
    }

    void stop_and_print() {
        double ms = stop_and_get_ms();
        std::cout << "  ? " << name_ << ": " << ms << " ms\n";
    }

    ~PerformanceProfiler() {
        if (running_) {
            stop_and_print();
        }
    }
};

// RAII helper for memory tracking
class MemoryTrackingScope {
private:
    std::string scope_name_;
    size_t initial_allocations_;

public:
    explicit MemoryTrackingScope(const std::string& name) : scope_name_(name) {
        auto& tracker = MemoryTracker::instance();
        tracker.enable_tracking();
        tracker.reset();
    }

    ~MemoryTrackingScope() {
        auto& tracker = MemoryTracker::instance();
        if (tracker.has_leaks()) {
            std::cout << "\n? Memory leaks in scope: " << scope_name_ << "\n";
            tracker.print_leak_report();
        }
        tracker.disable_tracking();
    }
};

} // namespace test
} // namespace swiftscript
