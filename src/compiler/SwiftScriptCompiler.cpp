#include "stdafx_.h"
#include "ss_compiler.hpp"
#include "ss_lexer.hpp"
#include "ss_parser.hpp"
#include "ss_chunk.hpp"
#include "ss_project.hpp"
#include "ss_project_resolver.hpp"

using namespace swiftscript;

namespace COMPILE_CODE
{
    constexpr int ERROR = 1;
}

inline int CompileProject(const SSProject& proj, const std::string& buildType, const std::string& outputFile) {
    // entry load
    std::string source;
    // ...
    {
        std::ifstream f(proj.entry_file, std::ios::binary);
        if (!f.is_open()) throw std::runtime_error("cannot open entry: " + proj.entry_file.string());
        source.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    }

    // parse
    Lexer lexer(source);
    auto tokens = lexer.tokenize_all();
    Parser parser(std::move(tokens));
    auto program = parser.parse();

    // compile with resolver
    ProjectModuleResolver resolver(proj.import_roots);

    Compiler compiler;
    compiler.set_base_directory(proj.project_dir.string());
    compiler.set_module_resolver(&resolver);

    Assembly chunk = compiler.compile(program);

    for (const auto& v : chunk.constants) {
        if (v.type() == Value::Type::Object) {
            std::cerr << "Object constant detected in constants pool!\n";
			return COMPILE_CODE::ERROR;
        }
    }

    // 결과물 직렬화
    std::ofstream out(outputFile, std::ios::binary);
    if (!out) {
        std::cerr << "Cannot open output file: " << outputFile << "\n";
        return COMPILE_CODE::ERROR;
    }

    try
    {
        chunk.serialize(out);
    }
    catch (const std::exception& e)
    {
        std::cerr << "Serialization error: " << e.what() << "\n";
        return COMPILE_CODE::ERROR;
    }

    std::cout << "Build (" << buildType << ") complete: " << outputFile << "\n";
}

int main(int argc, char* argv[]) {
    std::string buildType = "Debug";
    std::string inputProject;
    std::string outputFile;

    // 명령행 인자 파싱
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("-compile:", 0) == 0) {
            buildType = arg.substr(9);
        } else if (arg == "-in" && i + 1 < argc) {
            inputProject = argv[++i];
        }
    }

    if (inputProject.empty()) {
        std::cerr << "Usage: SwiftScriptCompiler -compile:{Debug|Release} -in <project>.ssproject\n";
        return 1;
    }

    // 프로젝트 이름 추출 (확장자 제거)
    std::filesystem::path inPath(inputProject);
	std::filesystem::path outPath = inPath.parent_path() / "bin" / buildType / inPath.filename().replace_extension(".ssasm");
	std::filesystem::create_directories(outPath.parent_path());
    outputFile = outPath.string();

	SSProject project;
	std::string err;
	if (!LoadSSProject(inputProject, project, err)) {
		std::cerr << "Failed to load project: " << err << "\n";
		return 1;
	}

    // 컴파일: 소스 → AST → Assembly
    try 
    {
        return CompileProject(project, buildType, outputFile);
    } catch (const std::exception& e) {
        std::cerr << "Compilation error: " << e.what() << "\n";
        return COMPILE_CODE::ERROR;
	}
}
