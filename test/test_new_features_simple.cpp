//#include <iostream>
//#include <cassert>
//#include "ss_compiler.hpp"
//#include "ss_lexer.hpp"
//#include "ss_parser.hpp"
//
//using namespace swiftscript;
//
//void test_ast_creation() {
//    std::cout << "Testing AST node creation...\n";
//    
//    // Test compound assignment
//    auto assign_expr = std::make_unique<AssignExpr>("x", 
//        std::make_unique<LiteralExpr>(Value::from_int(5)), 
//        TokenType::PlusEqual);
//    assert(assign_expr->op == TokenType::PlusEqual);
//    std::cout << "  ✓ Compound assignment AST node\n";
//    
//    // Test Range expression
//    auto range = std::make_unique<RangeExpr>(
//        std::make_unique<LiteralExpr>(Value::from_int(1)),
//        std::make_unique<LiteralExpr>(Value::from_int(10)),
//        true
//    );
//    assert(range->inclusive == true);
//    std::cout << "  ✓ Range expression AST node\n";
//    
//    // Test ForIn statement
//    auto for_in = std::make_unique<ForInStmt>("i", 
//        std::move(range), 
//        std::make_unique<BlockStmt>());
//    assert(for_in->variable == "i");
//    std::cout << "  ✓ For-in statement AST node\n";
//    
//    // Test Break statement
//    auto break_stmt = std::make_unique<BreakStmt>();
//    assert(break_stmt->kind == StmtKind::Break);
//    std::cout << "  ✓ Break statement AST node\n";
//    
//    // Test Continue statement
//    auto continue_stmt = std::make_unique<ContinueStmt>();
//    assert(continue_stmt->kind == StmtKind::Continue);
//    std::cout << "  ✓ Continue statement AST node\n";
//    
//    std::cout << "All AST tests passed!\n\n";
//}
//
//void test_lexer_tokenization() {
//    std::cout << "Testing lexer tokenization...\n";
//    
//    // Test compound operators
//    {
//        Lexer lexer("x += 5");
//        auto tokens = lexer.tokenize();
//        bool found_plus_equal = false;
//        for (const auto& token : tokens) {
//            if (token.type == TokenType::PlusEqual) {
//                found_plus_equal = true;
//                break;
//            }
//        }
//        assert(found_plus_equal);
//        std::cout << "  ✓ += tokenization\n";
//    }
//    
//    // Test range operators
//    {
//        Lexer lexer("1...10");
//        auto tokens = lexer.tokenize();
//        bool found_range = false;
//        for (const auto& token : tokens) {
//            if (token.type == TokenType::DotDotDot) {
//                found_range = true;
//                break;
//            }
//        }
//        assert(found_range);
//        std::cout << "  ✓ ... tokenization\n";
//    }
//    
//    // Test keywords
//    {
//        Lexer lexer("for i in break continue");
//        auto tokens = lexer.tokenize();
//        bool has_for = false, has_in = false, has_break = false, has_continue = false;
//        for (const auto& token : tokens) {
//            if (token.type == TokenType::For) has_for = true;
//            if (token.type == TokenType::In) has_in = true;
//            if (token.type == TokenType::Break) has_break = true;
//            if (token.type == TokenType::Continue) has_continue = true;
//        }
//        assert(has_for && has_in && has_break && has_continue);
//        std::cout << "  ✓ for/in/break/continue keywords\n";
//    }
//    
//    std::cout << "All lexer tests passed!\n\n";
//}
//
//int main() {
//    std::cout << "========================================\n";
//    std::cout << "  SIMPLE NEW FEATURES TEST\n";
//    std::cout << "========================================\n\n";
//    
//    try {
//        test_ast_creation();
//        test_lexer_tokenization();
//        
//        std::cout << "========================================\n";
//        std::cout << "  ALL SIMPLE TESTS PASSED!\n";
//        std::cout << "========================================\n";
//        return 0;
//    } catch (const std::exception& e) {
//        std::cerr << "\nTest failed: " << e.what() << "\n";
//        return 1;
//    }
//}