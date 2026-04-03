#include "preprocessor.h"

void generateCode(const std::vector<std::unique_ptr<ASTNode>>& ast, std::ofstream& f, bool useReachabilityFilter = true);
void semanticPass(const std::vector<std::unique_ptr<ASTNode>>& ast);

int main(int argc, char** argv)
{
    auto printUsage = [&](const char* exe)
    {
        std::cerr
            << "Usage:\n"
            << "  " << exe << " <input.c> <output-base>\n"
            << "  " << exe << " [options] <input.c>\n\n"
            << "Options:\n"
            << "  -E              Preprocess only (write to stdout or -o path)\n"
            << "  -S              Compile only to assembly (.asm)\n"
            << "  --emit-asm      Alias for -S\n"
            << "  -c              Compile and assemble to object (.o)\n"
            << "  --emit-obj      Alias for -c\n"
            << "  --emit-exe      Force full pipeline to executable (default)\n"
            << "                  (without -S/-c, full pipeline to executable)\n"
            << "  -o <path>       Output path.\n"
            << "                  -E: preprocessed output path\n"
            << "                  -S: asm path\n"
            << "                  -c: object path\n"
            << "                  link: executable path\n"
            << "  --asm-out <p>   Explicit assembly output path\n"
            << "  --obj-out <p>   Explicit object output path\n"
            << "  --exe-out <p>   Explicit executable output path\n"
            << "  --run           Run produced executable (link mode only)\n"
            << "  -D<name>[=val]  Define preprocessor macro\n"
            << "  -D <name>[=val] Define preprocessor macro\n"
            << "  -I<dir>         Add include directory for system preprocessor\n"
            << "  -I <dir>        Add include directory for system preprocessor\n"
            << "  --cpp <cmd>     System preprocessor command (default: cc)\n"
            << "  --no-system-pp  Disable system preprocessor pass\n"
            << "  --fasm <cmd>    Assembler command (default: fasm)\n"
            << "  --cc <cmd>      Linker C compiler command (default: gcc)\n"
                << "  -l<lib>         Link with library (for example: -lm)\n"
                << "  -l <lib>        Link with library (for example: -l m)\n"
                << "  -L<dir>         Add library search path\n"
                << "  -L <dir>        Add library search path\n"
                << "  --link-arg <a>  Pass raw argument to linker compiler\n"
            << "  -h, --help      Show this help\n";
    };

    auto shellQuote = [](const std::string& s) -> std::string
    {
        std::string out = "'";
        for (char c : s)
        {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        out += "'";
        return out;
    };

    auto stripExtension = [](const std::string& path) -> std::string
    {
        size_t slash = path.find_last_of("/\\");
        size_t dot = path.find_last_of('.');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
            return path.substr(0, dot);
        return path;
    };

    auto makeRunnablePath = [](const std::string& path) -> std::string
    {
        if (path.empty())
            return path;
        // If the user already provided a path (absolute or relative with separator), keep it.
        if (path[0] == '/' || path[0] == '\\' || path.find('/') != std::string::npos || path.find('\\') != std::string::npos)
            return path;
        // Bare file name in current directory must be prefixed with ./ for shell execution.
        return "./" + path;
    };

    // Backward-compatible mode: gvc <input file> <output base>
    if (argc == 3 && argv[1][0] != '-')
    {
        sourceFileName = argv[1];
        std::ifstream inFile(argv[1]);
        if (!inFile.is_open())
        {
            std::cerr << "Error opening file: " << argv[1] << std::endl;
            return -1;
        }

        std::stringstream buff;
        buff << inFile.rdbuf();
        std::string source = buff.str();
        inFile.close();

        Preprocessor preprocessor;
        std::unordered_map<std::string, std::string> defines;
        source = preprocessor.processCode(source, defines);

        Lexer lexer(source);
        Parser parser(lexer);
        auto ast = parser.parse();
        semanticPass(ast);

        if (!compileErrors.empty())
        {
            std::cerr << "Compilation failed with " << compileErrors.size() << " error(s)\n";
            return 1;
        }

        std::string asmFileName = std::string(argv[2]) + ".asm";
        std::ofstream file(asmFileName);
        if (!file.is_open())
        {
            std::cerr << "Error creating output assembly file!" << std::endl;
            return -1;
        }
        generateCode(ast, file, false);
        file.close();
        return 0;
    }

    bool flagS = false;
    bool flagC = false;
    bool flagE = false;
    bool flagRun = false;
    std::vector<std::string> inputPaths;
    std::string outputPath;
    std::string asmOutPath;
    std::string objOutPath;
    std::string exeOutPath;
    std::string cppCmd = "cc -std=c11";
    bool useSystemPreprocessor = true;
    std::string fasmCmd = "fasm";
    std::string ccCmd = "gcc";
    std::vector<std::string> preprocDefines;
    std::vector<std::string> preprocIncludes;
    std::vector<std::string> linkArgs;
    std::string compatIncludeDir;

    try
    {
        std::filesystem::path exePath = std::filesystem::absolute(argv[0]);
        std::filesystem::path candidate = exePath.parent_path() / "gvc_compat";
        if (std::filesystem::exists(candidate) && std::filesystem::is_directory(candidate))
            compatIncludeDir = candidate.string();
    }
    catch (...)
    {
        // Optional compatibility include directory; ignore discovery failures.
    }

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "-S")
        {
            flagS = true;
        }
        else if (arg == "--emit-asm")
        {
            flagS = true;
        }
        else if (arg == "-c")
        {
            flagC = true;
        }
        else if (arg == "--emit-obj")
        {
            flagC = true;
        }
        else if (arg == "-E")
        {
            flagE = true;
        }
        else if (arg == "--emit-exe")
        {
            flagE = false;
            flagS = false;
            flagC = false;
        }
        else if (arg == "--run")
        {
            flagRun = true;
        }
        else if (arg == "-o")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after -o\n";
                return 1;
            }
            outputPath = argv[++i];
        }
        else if (arg == "--asm-out")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after --asm-out\n";
                return 1;
            }
            asmOutPath = argv[++i];
        }
        else if (arg == "--obj-out")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after --obj-out\n";
                return 1;
            }
            objOutPath = argv[++i];
        }
        else if (arg == "--exe-out")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after --exe-out\n";
                return 1;
            }
            exeOutPath = argv[++i];
        }
        else if (arg == "--cpp")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after --cpp\n";
                return 1;
            }
            cppCmd = argv[++i];
        }
        else if (arg == "--no-system-pp")
        {
            useSystemPreprocessor = false;
        }
        else if (arg == "--fasm")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after --fasm\n";
                return 1;
            }
            fasmCmd = argv[++i];
        }
        else if (arg == "--cc")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after --cc\n";
                return 1;
            }
            ccCmd = argv[++i];
        }
        else if (arg == "--link-arg")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after --link-arg\n";
                return 1;
            }
            linkArgs.push_back(argv[++i]);
        }
        else if (arg == "-l")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after -l\n";
                return 1;
            }
            linkArgs.push_back("-l" + std::string(argv[++i]));
        }
        else if (arg == "-L")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after -L\n";
                return 1;
            }
            linkArgs.push_back("-L" + std::string(argv[++i]));
        }
        else if (arg == "-D")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after -D\n";
                return 1;
            }
            preprocDefines.push_back(argv[++i]);
        }
        else if (arg == "-I")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after -I\n";
                return 1;
            }
            preprocIncludes.push_back(argv[++i]);
        }
        else if (arg.size() > 2 && arg.rfind("-D", 0) == 0)
        {
            preprocDefines.push_back(arg.substr(2));
        }
        else if (arg.size() > 2 && arg.rfind("-I", 0) == 0)
        {
            preprocIncludes.push_back(arg.substr(2));
        }
        else if (arg.rfind("-l", 0) == 0 || arg.rfind("-L", 0) == 0 || arg.rfind("-Wl,", 0) == 0)
        {
            linkArgs.push_back(arg);
        }
        else if (!arg.empty() && arg[0] == '-')
        {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        }
        else
        {
            inputPaths.push_back(arg);
        }
    }

    if (inputPaths.empty())
    {
        printUsage(argv[0]);
        return 1;
    }

    if (flagS && flagC)
    {
        std::cerr << "Cannot use -S and -c together\n";
        return 1;
    }

    if (flagE && (flagS || flagC))
    {
        std::cerr << "Cannot combine -E with -S or -c\n";
        return 1;
    }

    if (flagRun && (flagS || flagC || flagE))
    {
        std::cerr << "--run can only be used in full link mode\n";
        return 1;
    }

    if (inputPaths.size() > 1)
    {
        if (!asmOutPath.empty() || !objOutPath.empty())
        {
            std::cerr << "--asm-out/--obj-out require exactly one input file\n";
            return 1;
        }
        if ((flagS || flagC || flagE) && !outputPath.empty())
        {
            std::cerr << "-o with -E/-S/-c requires exactly one input file\n";
            return 1;
        }
    }

    auto runSystemPreprocessor = [&](const std::string& inPath, std::string& outText) -> bool
    {
        std::string tmpStem = std::to_string(static_cast<long long>(std::time(nullptr))) + "_" + std::to_string(std::rand());
        std::string ppOutPath = "/tmp/gvc_pp_" + tmpStem + ".i";
        std::string ppErrPath = "/tmp/gvc_pp_" + tmpStem + ".err";

        std::string cmd = cppCmd + " -E -P";
        if (!compatIncludeDir.empty())
            cmd += " -I" + shellQuote(compatIncludeDir);
        for (const auto& d : preprocDefines)
            cmd += " -D" + shellQuote(d);
        for (const auto& inc : preprocIncludes)
            cmd += " -I" + shellQuote(inc);
        cmd += " " + shellQuote(inPath) + " -o " + shellQuote(ppOutPath) + " 2> " + shellQuote(ppErrPath);
        int rc = std::system(cmd.c_str());
        if (rc != 0)
        {
            std::ifstream errIn(ppErrPath);
            if (errIn.is_open())
            {
                std::stringstream errBuff;
                errBuff << errIn.rdbuf();
                std::string errText = errBuff.str();
                if (!errText.empty())
                    std::cerr << errText;
            }
            std::error_code ec;
            std::filesystem::remove(ppOutPath, ec);
            std::filesystem::remove(ppErrPath, ec);
            return false;
        }

        std::ifstream ppIn(ppOutPath);
        if (!ppIn.is_open())
        {
            std::error_code ec;
            std::filesystem::remove(ppOutPath, ec);
            std::filesystem::remove(ppErrPath, ec);
            return false;
        }

        std::stringstream ppBuff;
        ppBuff << ppIn.rdbuf();
        outText = ppBuff.str();

        std::error_code ec;
        std::filesystem::remove(ppOutPath, ec);
        std::filesystem::remove(ppErrPath, ec);
        return true;
    };

    auto loadAndPreprocessSource = [&](const std::string& inputPath, std::string& source) -> bool
    {
        sourceFileName = inputPath;
        std::ifstream inFile(inputPath);
        if (!inFile.is_open())
        {
            std::cerr << "Error opening file: " << inputPath << std::endl;
            return false;
        }

        std::stringstream buff;
        buff << inFile.rdbuf();
        source = buff.str();
        inFile.close();

        if (useSystemPreprocessor && source.find("#include <") != std::string::npos)
        {
            std::string externalPreprocessed;
            if (runSystemPreprocessor(inputPath, externalPreprocessed))
                source = externalPreprocessed;
        }

        Preprocessor preprocessor;
        std::unordered_map<std::string, std::string> defines;
        for (const auto& d : preprocDefines)
        {
            size_t eq = d.find('=');
            if (eq == std::string::npos)
                defines[d] = "1";
            else
                defines[d.substr(0, eq)] = d.substr(eq + 1);
        }
        source = preprocessor.processCode(source, defines);
        return true;
    };

    if (flagE)
    {
        if (!outputPath.empty() && inputPaths.size() > 1)
        {
            std::cerr << "-E with -o supports exactly one input file\n";
            return 1;
        }

        if (outputPath.empty())
        {
            for (size_t i = 0; i < inputPaths.size(); ++i)
            {
                std::string source;
                if (!loadAndPreprocessSource(inputPaths[i], source))
                    return -1;
                std::cout << source;
                if (i + 1 < inputPaths.size())
                    std::cout << "\n";
            }
        }
        else
        {
            std::string source;
            if (!loadAndPreprocessSource(inputPaths[0], source))
                return -1;
            std::ofstream ppOut(outputPath);
            if (!ppOut.is_open())
            {
                std::cerr << "Error creating preprocessed output file: " << outputPath << std::endl;
                return -1;
            }
            ppOut << source;
        }
        return 0;
    }

    struct UnitOutput
    {
        std::string inputPath;
        std::string asmFile;
        std::string objFile;
    };

    std::vector<UnitOutput> units;
    units.reserve(inputPaths.size());
    for (const auto& in : inputPaths)
    {
        std::string base = stripExtension(in);
        UnitOutput u;
        u.inputPath = in;
        u.asmFile = base + ".asm";
        u.objFile = base + ".o";
        units.push_back(std::move(u));
    }

    std::string exeFile = stripExtension(inputPaths[0]);

    if (!outputPath.empty())
    {
        if (flagS && inputPaths.size() == 1) units[0].asmFile = outputPath;
        else if (flagC && inputPaths.size() == 1) units[0].objFile = outputPath;
        else if (!flagS && !flagC) exeFile = outputPath;
    }

    if (!asmOutPath.empty()) units[0].asmFile = asmOutPath;
    if (!objOutPath.empty()) units[0].objFile = objOutPath;
    if (!exeOutPath.empty()) exeFile = exeOutPath;

    for (const auto& unit : units)
    {
        std::string source;
        if (!loadAndPreprocessSource(unit.inputPath, source))
            return -1;

        size_t errBefore = compileErrors.size();
        Lexer lexer(source);
        Parser parser(lexer);
        auto ast = parser.parse();
        semanticPass(ast);

        if (compileErrors.size() > errBefore)
        {
            std::cerr << "Compilation failed with " << compileErrors.size() << " error(s)\n";
            return 1;
        }

        std::ofstream file(unit.asmFile);
        if (!file.is_open())
        {
            std::cerr << "Error creating output assembly file: " << unit.asmFile << std::endl;
            return -1;
        }
        // Multi-file builds need cross-TU callable functions preserved.
        bool useReachabilityFilter = !flagS && !flagC && inputPaths.size() == 1;
        generateCode(ast, file, useReachabilityFilter);
    }

    if (flagS)
    {
        return 0;
    }

    for (const auto& unit : units)
    {
        std::string cmd = fasmCmd + " " + shellQuote(unit.asmFile) + " " + shellQuote(unit.objFile);
        int rc = std::system(cmd.c_str());
        if (rc != 0)
        {
            std::cerr << "Assembler command failed: " << cmd << "\n";
            return rc;
        }
    }

    if (flagC)
    {
        return 0;
    }

    {
        std::string cmd = ccCmd;
        for (const auto& unit : units)
            cmd += " " + shellQuote(unit.objFile);
        cmd += " -o " + shellQuote(exeFile);
        for (const auto& a : linkArgs)
            cmd += " " + shellQuote(a);
        int rc = std::system(cmd.c_str());
        if (rc != 0)
        {
            std::cerr << "Link command failed: " << cmd << "\n";
            return rc;
        }
    }

    if (flagRun)
    {
        std::string cmd = shellQuote(makeRunnablePath(exeFile));
        int rc = std::system(cmd.c_str());
        if (rc != 0)
        {
            std::cerr << "Run command failed: " << cmd << "\n";
            return rc;
        }
    }

    return 0;
}
