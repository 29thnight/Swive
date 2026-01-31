#pragma once
#include "ss_vm_opcodes_basic.inl"

namespace swiftscript {

    // ============================================================================
    // Arithmetic Operations
    // ============================================================================

    template<>
    struct OpCodeHandler<OpCode::OP_ADD> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            if (a.is_int() && b.is_int()) {
                vm.push(Value::from_int(a.as_int() + b.as_int()));
            }
            else {
                auto fa = a.try_as<Float>();
                auto fb = b.try_as<Float>();
                if (!fa || !fb) {
                    if (auto overloaded = vm.call_operator_overload(a, b, "+")) {
                        vm.push(*overloaded);
                        return;
                    }
                    throw std::runtime_error("Operands must be numbers for addition.");
                }
                vm.push(Value::from_float(*fa + *fb));
            }
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_SUBTRACT> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            auto fa = a.try_as<Float>();
            auto fb = b.try_as<Float>();
            if (!fa || !fb) {
                if (auto overloaded = vm.call_operator_overload(a, b, "-")) {
                    vm.push(*overloaded);
                    return;
                }
                throw std::runtime_error("Operands must be numbers for subtraction.");
            }
            if (a.is_int() && b.is_int()) {
                vm.push(Value::from_int(a.as_int() - b.as_int()));
            }
            else {
                vm.push(Value::from_float(*fa - *fb));
            }
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_MULTIPLY> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            auto fa = a.try_as<Float>();
            auto fb = b.try_as<Float>();
            if (!fa || !fb) {
                if (auto overloaded = vm.call_operator_overload(a, b, "*")) {
                    vm.push(*overloaded);
                    return;
                }
                throw std::runtime_error("Operands must be numbers for multiplication.");
            }
            if (a.is_int() && b.is_int()) {
                vm.push(Value::from_int(a.as_int() * b.as_int()));
            }
            else {
                vm.push(Value::from_float(*fa * *fb));
            }
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_DIVIDE> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            auto fa = a.try_as<Float>();
            auto fb = b.try_as<Float>();
            if (!fa || !fb) {
                if (auto overloaded = vm.call_operator_overload(a, b, "/")) {
                    vm.push(*overloaded);
                    return;
                }
                throw std::runtime_error("Operands must be numbers for division.");
            }
            vm.push(Value::from_float(*fa / *fb));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_MODULO> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            if (!a.is_int() || !b.is_int()) {
                if (auto overloaded = vm.call_operator_overload(a, b, "%")) {
                    vm.push(*overloaded);
                    return;
                }
                throw std::runtime_error("Operands must be integers for modulo.");
            }
            vm.push(Value::from_int(a.as_int() % b.as_int()));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_NEGATE> {
        static void execute(VM& vm) {
            Value a = vm.pop();
            if (a.is_int()) {
                vm.push(Value::from_int(-a.as_int()));
            }
            else if (a.is_float()) {
                vm.push(Value::from_float(-a.as_float()));
            }
            else {
                throw std::runtime_error("Operand must be number for negation.");
            }
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_BITWISE_NOT> {
        static void execute(VM& vm) {
            Value value = vm.pop();
            if (!value.is_int()) {
                throw std::runtime_error("Operand must be integer for bitwise NOT.");
            }
            vm.push(Value::from_int(~value.as_int()));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_BITWISE_AND> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            if (!a.is_int() || !b.is_int()) {
                throw std::runtime_error("Operands must be integers for bitwise AND.");
            }
            vm.push(Value::from_int(a.as_int() & b.as_int()));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_BITWISE_OR> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            if (!a.is_int() || !b.is_int()) {
                throw std::runtime_error("Operands must be integers for bitwise OR.");
            }
            vm.push(Value::from_int(a.as_int() | b.as_int()));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_BITWISE_XOR> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            if (!a.is_int() || !b.is_int()) {
                throw std::runtime_error("Operands must be integers for bitwise XOR.");
            }
            vm.push(Value::from_int(a.as_int() ^ b.as_int()));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_LEFT_SHIFT> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            if (!a.is_int() || !b.is_int()) {
                throw std::runtime_error("Operands must be integers for left shift.");
            }
            vm.push(Value::from_int(a.as_int() << b.as_int()));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_RIGHT_SHIFT> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            if (!a.is_int() || !b.is_int()) {
                throw std::runtime_error("Operands must be integers for right shift.");
            }
            vm.push(Value::from_int(a.as_int() >> b.as_int()));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_EQUAL> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            vm.push(Value::from_bool(a.equals(b)));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_NOT_EQUAL> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            vm.push(Value::from_bool(!a.equals(b)));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_LESS> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            auto fa = a.try_as<Float>();
            auto fb = b.try_as<Float>();
            if (!fa || !fb) {
                throw std::runtime_error("Operands must be numbers for less-than comparison.");
            }
            std::string op_name{ "<" };
            if (auto overloaded = vm.call_operator_overload(a, b, op_name)) {
                vm.push(*overloaded);
            }
            vm.push(Value::from_bool(*fa < *fb));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_GREATER> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            auto fa = a.try_as<Float>();
            auto fb = b.try_as<Float>();
            if (!fa || !fb) {
                throw std::runtime_error("Operands must be numbers for greater-than comparison.");
            }
            std::string op_name{ ">" };
            if (auto overloaded = vm.call_operator_overload(a, b, op_name)) {
                vm.push(*overloaded);
            }
            vm.push(Value::from_bool(*fa > *fb));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_LESS_EQUAL> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            auto fa = a.try_as<Float>();
            auto fb = b.try_as<Float>();
            if (!fa || !fb) {
                throw std::runtime_error("Operands must be numbers for less-equal comparison.");
            }
            std::string op_name{ "<=" };
            if (auto overloaded = vm.call_operator_overload(a, b, op_name)) {
                vm.push(*overloaded);
            }
            vm.push(Value::from_bool(*fa <= *fb));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_GREATER_EQUAL> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            auto fa = a.try_as<Float>();
            auto fb = b.try_as<Float>();
            if (!fa || !fb) {
                throw std::runtime_error("Operands must be numbers for greater-equal comparison.");
            }
            std::string op_name{ ">=" };
            if (auto overloaded = vm.call_operator_overload(a, b, op_name)) {
                vm.push(*overloaded);
            }
            vm.push(Value::from_bool(*fa >= *fb));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_NOT> {
        static void execute(VM& vm) {
            Value value = vm.pop();
            vm.push(Value::from_bool(!vm.is_truthy(value)));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_AND> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            vm.push(Value::from_bool(vm.is_truthy(a) && vm.is_truthy(b)));
        }
    };

    template<>
    struct OpCodeHandler<OpCode::OP_OR> {
        static void execute(VM& vm) {
            Value b = vm.pop();
            Value a = vm.pop();
            vm.push(Value::from_bool(vm.is_truthy(a) || vm.is_truthy(b)));
        }
    };
} // namespace swiftscript