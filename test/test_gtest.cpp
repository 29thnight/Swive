#include <gtest/gtest.h>
#include "test_helpers.hpp"
#include "ss_compiler.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_vm.hpp"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>

using namespace swiftscript::test;

// Custom test listener for logging to file
class FileTestListener : public ::testing::EmptyTestEventListener {
private:
    std::ofstream log_file_;
    std::chrono::steady_clock::time_point test_start_time_;
    std::chrono::steady_clock::time_point suite_start_time_;
    
public:
    FileTestListener(const std::string& filename) {
        log_file_.open(filename);
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
#else
        localtime_r(&time_t, &tm_buf);
#endif
        
        log_file_ << "========================================\n";
        log_file_ << "SwiftScript Test Results\n";
        log_file_ << "Date: " << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << "\n";
        log_file_ << "========================================\n\n";
    }
    
    ~FileTestListener() {
        if (log_file_.is_open()) {
            log_file_.close();
        }
    }
    
    void OnTestProgramStart(const ::testing::UnitTest& unit_test) override {
        log_file_ << "Starting " << unit_test.total_test_suite_count() 
                  << " test suites with " << unit_test.total_test_count() 
                  << " tests.\n\n";
    }
    
    void OnTestSuiteStart(const ::testing::TestSuite& test_suite) override {
        suite_start_time_ = std::chrono::steady_clock::now();
        log_file_ << "[ Test Suite ] " << test_suite.name() << "\n";
    }
    
    void OnTestStart(const ::testing::TestInfo& test_info) override {
        test_start_time_ = std::chrono::steady_clock::now();
        log_file_ << "  [ RUN      ] " << test_info.test_suite_name() 
                  << "." << test_info.name() << "\n";
    }
    
    void OnTestEnd(const ::testing::TestInfo& test_info) override {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - test_start_time_).count();
        
        if (test_info.result()->Passed()) {
            log_file_ << "  [       OK ] " << test_info.test_suite_name() 
                      << "." << test_info.name() 
                      << " (" << duration << " ms)\n";
        } else {
            log_file_ << "  [  FAILED  ] " << test_info.test_suite_name() 
                      << "." << test_info.name() 
                      << " (" << duration << " ms)\n";
            
            // Log failure details
            for (int i = 0; i < test_info.result()->total_part_count(); ++i) {
                const auto& part = test_info.result()->GetTestPartResult(i);
                if (part.failed()) {
                    log_file_ << "    " << part.file_name() << ":" 
                              << part.line_number() << "\n";
                    log_file_ << "    " << part.summary() << "\n";
                }
            }
        }
    }
    
    void OnTestSuiteEnd(const ::testing::TestSuite& test_suite) override {
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - suite_start_time_).count();
        
        log_file_ << "  Tests passed: " << test_suite.successful_test_count() 
                  << "/" << test_suite.total_test_count() 
                  << " (" << duration << " ms)\n\n";
    }
    
    void OnTestProgramEnd(const ::testing::UnitTest& unit_test) override {
        log_file_ << "========================================\n";
        log_file_ << "Test Summary\n";
        log_file_ << "========================================\n";
        log_file_ << "Total test suites: " << unit_test.total_test_suite_count() << "\n";
        log_file_ << "Total tests: " << unit_test.total_test_count() << "\n";
        log_file_ << "Passed: " << unit_test.successful_test_count() << "\n";
        log_file_ << "Failed: " << unit_test.failed_test_count() << "\n";
        log_file_ << "Elapsed time: " << unit_test.elapsed_time() << " ms\n";
        log_file_ << "========================================\n";
        
        if (unit_test.Passed()) {
            log_file_ << "\nALL TESTS PASSED!\n";
        } else {
            log_file_ << "\nSOME TESTS FAILED!\n";
        }
    }
};

// Forward declarations
namespace swiftscript { 
namespace test {

// Class tests
void test_simple_class_method();
void test_initializer_called();
void test_stored_property_defaults();
void test_inherited_method_call();
void test_super_method_call();
void test_inherited_property_defaults();
void test_override_required();
void test_override_without_base_method();
void test_override_init_allowed();
void test_deinit_called();
void test_deinit_with_properties();

// Computed Properties tests
void test_computed_property_getter_only();
void test_computed_property_getter_setter();
void test_computed_property_read_only_shorthand();
void test_computed_property_temperature_conversion();
void test_computed_property_with_logic();
void test_computed_property_access_other_properties();
void test_computed_property_multiple_in_class();

// Struct tests
void test_basic_struct();
void test_memberwise_init();
void test_custom_init();
void test_non_mutating_method();
void test_mutating_method();
void test_value_semantics();
void test_self_access();
void test_multiple_methods();
void test_property_modification();
void test_nested_struct();

// Closure tests
void test_closure_basic();
void test_closure_no_params();
void test_closure_single_param();
void test_closure_as_argument();
void test_closure_multiple_statements();
void test_function_returning_closure();
void test_closure_variable_assignment();
void test_closure_captures_outer_variable();
void test_nested_closure_captures_after_scope_exit();

// Switch tests
void test_switch_basic();
void test_switch_default();
void test_switch_range();
void test_switch_multiple_patterns();

// Enum tests
void test_enum_basic();
void test_enum_raw_values();
void test_enum_switch();
void test_enum_associated_values();
void test_enum_comparison();
void test_enum_methods();
void test_enum_computed_properties();
void test_multiple_enums();
void test_enum_basic_inline();
void test_enum_raw_values_inline();
void test_enum_comparison_inline();
void test_enum_with_method_inline();
void test_enum_method_with_self_switch_inline();
void test_enum_in_switch_statement_inline();
void test_enum_simple_computed_property_inline();

// Protocol tests
void test_protocol_basic_declaration();
void test_protocol_method_requirements();
void test_protocol_property_requirements();
void test_protocol_inheritance();
void test_protocol_mutating_method();
void test_protocol_struct_conformance();
void test_protocol_class_conformance();
void test_protocol_class_superclass_and_protocol();
void test_protocol_multiple_conformance();
void test_protocol_method_parameters();

// Extension tests
void test_extension_basic_method();
void test_extension_enum();
void test_extension_computed_property();
void test_extension_multiple_methods();
void test_extension_class();
void test_multiple_extensions();
void test_extension_self_usage();
void test_extension_with_parameters();
void test_extension_enum_with_switch();
void test_extension_simple_computed_property();

} // namespace test
} // namespace swiftscript

// ============================================================================
// Class Tests
// ============================================================================

TEST(ClassTests, SimpleClassMethod) {
    EXPECT_NO_THROW(swiftscript::test::test_simple_class_method());
}

TEST(ClassTests, InitializerCalled) {
    EXPECT_NO_THROW(swiftscript::test::test_initializer_called());
}

TEST(ClassTests, StoredPropertyDefaults) {
    EXPECT_NO_THROW(swiftscript::test::test_stored_property_defaults());
}

TEST(ClassTests, InheritedMethodCall) {
    EXPECT_NO_THROW(swiftscript::test::test_inherited_method_call());
}

TEST(ClassTests, SuperMethodCall) {
    EXPECT_NO_THROW(swiftscript::test::test_super_method_call());
}

TEST(ClassTests, InheritedPropertyDefaults) {
    EXPECT_NO_THROW(swiftscript::test::test_inherited_property_defaults());
}

TEST(ClassTests, OverrideRequired) {
    EXPECT_NO_THROW(swiftscript::test::test_override_required());
}

TEST(ClassTests, OverrideWithoutBaseMethod) {
    EXPECT_NO_THROW(swiftscript::test::test_override_without_base_method());
}

TEST(ClassTests, OverrideInitAllowed) {
    EXPECT_NO_THROW(swiftscript::test::test_override_init_allowed());
}

TEST(ClassTests, DeinitCalled) {
    EXPECT_NO_THROW(swiftscript::test::test_deinit_called());
}

TEST(ClassTests, DeinitWithProperties) {
    EXPECT_NO_THROW(swiftscript::test::test_deinit_with_properties());
}

// ============================================================================
// Computed Properties Tests
// ============================================================================

TEST(ComputedPropertiesTests, GetterOnly) {
    EXPECT_NO_THROW(swiftscript::test::test_computed_property_getter_only());
}

TEST(ComputedPropertiesTests, GetterSetter) {
    EXPECT_NO_THROW(swiftscript::test::test_computed_property_getter_setter());
}

TEST(ComputedPropertiesTests, ReadOnlyShorthand) {
    EXPECT_NO_THROW(swiftscript::test::test_computed_property_read_only_shorthand());
}

TEST(ComputedPropertiesTests, TemperatureConversion) {
    EXPECT_NO_THROW(swiftscript::test::test_computed_property_temperature_conversion());
}

TEST(ComputedPropertiesTests, WithLogic) {
    EXPECT_NO_THROW(swiftscript::test::test_computed_property_with_logic());
}

TEST(ComputedPropertiesTests, AccessOtherProperties) {
    EXPECT_NO_THROW(swiftscript::test::test_computed_property_access_other_properties());
}

TEST(ComputedPropertiesTests, MultipleInClass) {
    EXPECT_NO_THROW(swiftscript::test::test_computed_property_multiple_in_class());
}

// ============================================================================
// Struct Tests
// ============================================================================

TEST(StructTests, BasicStruct) {
    EXPECT_NO_THROW(swiftscript::test::test_basic_struct());
}

TEST(StructTests, MemberwiseInit) {
    EXPECT_NO_THROW(swiftscript::test::test_memberwise_init());
}

TEST(StructTests, CustomInit) {
    EXPECT_NO_THROW(swiftscript::test::test_custom_init());
}

TEST(StructTests, NonMutatingMethod) {
    EXPECT_NO_THROW(swiftscript::test::test_non_mutating_method());
}

TEST(StructTests, MutatingMethod) {
    EXPECT_NO_THROW(swiftscript::test::test_mutating_method());
}

TEST(StructTests, ValueSemantics) {
    EXPECT_NO_THROW(swiftscript::test::test_value_semantics());
}

TEST(StructTests, SelfAccess) {
    EXPECT_NO_THROW(swiftscript::test::test_self_access());
}

TEST(StructTests, MultipleMethods) {
    EXPECT_NO_THROW(swiftscript::test::test_multiple_methods());
}

TEST(StructTests, PropertyModification) {
    EXPECT_NO_THROW(swiftscript::test::test_property_modification());
}

TEST(StructTests, NestedStruct) {
    // This test may skip if not supported
    EXPECT_NO_THROW(swiftscript::test::test_nested_struct());
}

// ============================================================================
// Closure Tests
// ============================================================================

TEST(ClosureTests, BasicClosure) {
    EXPECT_NO_THROW(swiftscript::test::test_closure_basic());
}

TEST(ClosureTests, ClosureNoParams) {
    EXPECT_NO_THROW(swiftscript::test::test_closure_no_params());
}

TEST(ClosureTests, ClosureSingleParam) {
    EXPECT_NO_THROW(swiftscript::test::test_closure_single_param());
}

TEST(ClosureTests, ClosureAsArgument) {
    EXPECT_NO_THROW(swiftscript::test::test_closure_as_argument());
}

TEST(ClosureTests, ClosureMultipleStatements) {
    EXPECT_NO_THROW(swiftscript::test::test_closure_multiple_statements());
}

TEST(ClosureTests, FunctionReturningClosure) {
    EXPECT_NO_THROW(swiftscript::test::test_function_returning_closure());
}

TEST(ClosureTests, ClosureVariableAssignment) {
    EXPECT_NO_THROW(swiftscript::test::test_closure_variable_assignment());
}

TEST(ClosureTests, ClosureCapturesOuterVariable) {
    EXPECT_NO_THROW(swiftscript::test::test_closure_captures_outer_variable());
}

TEST(ClosureTests, NestedClosureAfterScopeExit) {
    EXPECT_NO_THROW(swiftscript::test::test_nested_closure_captures_after_scope_exit());
}

// ============================================================================
// Switch Tests
// ============================================================================

TEST(SwitchTests, BasicSwitch) {
    EXPECT_NO_THROW(swiftscript::test::test_switch_basic());
}

TEST(SwitchTests, SwitchDefault) {
    EXPECT_NO_THROW(swiftscript::test::test_switch_default());
}

TEST(SwitchTests, SwitchRange) {
    EXPECT_NO_THROW(swiftscript::test::test_switch_range());
}

TEST(SwitchTests, SwitchMultiplePatterns) {
    EXPECT_NO_THROW(swiftscript::test::test_switch_multiple_patterns());
}

// ============================================================================
// Enum Tests
// ============================================================================

TEST(EnumTests, BasicEnum) {
    EXPECT_NO_THROW(swiftscript::test::test_enum_basic());
}

TEST(EnumTests, EnumRawValues) {
    EXPECT_NO_THROW(swiftscript::test::test_enum_raw_values());
}

TEST(EnumTests, EnumSwitch) {
    EXPECT_NO_THROW(swiftscript::test::test_enum_switch());
}

TEST(EnumTests, EnumAssociatedValues) {
    EXPECT_NO_THROW(swiftscript::test::test_enum_associated_values());
}

TEST(EnumTests, EnumComparison) {
    EXPECT_NO_THROW(swiftscript::test::test_enum_comparison());
}

TEST(EnumTests, EnumMethods) {
    EXPECT_NO_THROW(swiftscript::test::test_enum_methods());
}

TEST(EnumTests, EnumComputedProperties) {
    EXPECT_NO_THROW(swiftscript::test::test_enum_computed_properties());
}

TEST(EnumTests, MultipleEnums) {
    EXPECT_NO_THROW(swiftscript::test::test_multiple_enums());
}

// ============================================================================
// Protocol Tests
// ============================================================================

TEST(ProtocolTest, BasicProtocolDeclaration) {
    EXPECT_NO_THROW(swiftscript::test::test_protocol_basic_declaration());
}

TEST(ProtocolTest, ProtocolMethodRequirements) {
    EXPECT_NO_THROW(swiftscript::test::test_protocol_method_requirements());
}

TEST(ProtocolTest, ProtocolPropertyRequirements) {
    EXPECT_NO_THROW(swiftscript::test::test_protocol_property_requirements());
}

TEST(ProtocolTest, ProtocolInheritance) {
    EXPECT_NO_THROW(swiftscript::test::test_protocol_inheritance());
}

TEST(ProtocolTest, ProtocolMutatingMethod) {
    EXPECT_NO_THROW(swiftscript::test::test_protocol_mutating_method());
}

TEST(ProtocolTest, StructProtocolConformance) {
    EXPECT_NO_THROW(swiftscript::test::test_protocol_struct_conformance());
}

TEST(ProtocolTest, ClassProtocolConformance) {
    EXPECT_NO_THROW(swiftscript::test::test_protocol_class_conformance());
}

TEST(ProtocolTest, ClassSuperclassAndProtocol) {
    EXPECT_NO_THROW(swiftscript::test::test_protocol_class_superclass_and_protocol());
}

TEST(ProtocolTest, MultipleProtocolConformance) {
    EXPECT_NO_THROW(swiftscript::test::test_protocol_multiple_conformance());
}

TEST(ProtocolTest, ProtocolMethodParameters) {
    EXPECT_NO_THROW(swiftscript::test::test_protocol_method_parameters());
}

// ============================================================================
// Inline Enum Tests (Quick Verification)
// ============================================================================

TEST(EnumInlineTests, BasicEnumDeclaration) {
    EXPECT_NO_THROW(swiftscript::test::test_enum_basic_inline());
}

TEST(EnumInlineTests, EnumRawValues) {
    EXPECT_NO_THROW(swiftscript::test::test_enum_raw_values_inline());
}

TEST(EnumInlineTests, EnumComparison) {
    EXPECT_NO_THROW(swiftscript::test::test_enum_comparison_inline());
}

TEST(EnumInlineTests, EnumWithMethod) {
    EXPECT_NO_THROW(swiftscript::test::test_enum_with_method_inline());
}

TEST(EnumInlineTests, EnumMethodWithSelfSwitch) {
    EXPECT_NO_THROW(swiftscript::test::test_enum_method_with_self_switch_inline());
}

TEST(EnumInlineTests, EnumInSwitchStatement) {
    EXPECT_NO_THROW(swiftscript::test::test_enum_in_switch_statement_inline());
}

TEST(EnumInlineTests, EnumSimpleComputedProperty) {
    EXPECT_NO_THROW(swiftscript::test::test_enum_simple_computed_property_inline());
}

// ============================================================================
// Extension Tests
// ============================================================================

TEST(ExtensionTest, BasicMethod) {
    EXPECT_NO_THROW(swiftscript::test::test_extension_basic_method());
}

TEST(ExtensionTest, ExtensionEnum) {
    EXPECT_NO_THROW(swiftscript::test::test_extension_enum());
}

TEST(ExtensionTest, ComputedProperty) {
    EXPECT_NO_THROW(swiftscript::test::test_extension_computed_property());
}

TEST(ExtensionTest, MultipleMethods) {
    EXPECT_NO_THROW(swiftscript::test::test_extension_multiple_methods());
}

TEST(ExtensionTest, ExtensionClass) {
    EXPECT_NO_THROW(swiftscript::test::test_extension_class());
}

TEST(ExtensionTest, MultipleExtensions) {
    EXPECT_NO_THROW(swiftscript::test::test_multiple_extensions());
}

TEST(ExtensionTest, SelfUsage) {
    EXPECT_NO_THROW(swiftscript::test::test_extension_self_usage());
}

TEST(ExtensionTest, WithParameters) {
    EXPECT_NO_THROW(swiftscript::test::test_extension_with_parameters());
}

TEST(ExtensionTest, EnumWithSwitch) {
    EXPECT_NO_THROW(swiftscript::test::test_extension_enum_with_switch());
}

TEST(ExtensionTest, SimpleComputedProperty) {
    EXPECT_NO_THROW(swiftscript::test::test_extension_simple_computed_property());
}

// ============================================================================
// Main function for Google Test
// ============================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    
    // Add custom listener for file logging
    ::testing::TestEventListeners& listeners = 
        ::testing::UnitTest::GetInstance()->listeners();
    listeners.Append(new FileTestListener("testlog.txt"));
    
    std::cout << "Running tests... Results will be saved to testlog.txt\n\n";
    
    int result = RUN_ALL_TESTS();
    
    std::cout << "\nTest results have been saved to testlog.txt\n";
    
    return result;
}
