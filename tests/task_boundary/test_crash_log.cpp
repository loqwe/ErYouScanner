#include <QCoreApplication>
#include <QProcess>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include "ScannerCrash.hpp"

int main(int argc, char** argv)
{
    if (argc == 2)
    {
        InstallScannerCrashHandlers();
        if (std::string_view(argv[1]) == "--terminate")
            std::terminate();
        if (std::string_view(argv[1]) == "--cpp-escape")
        {
            throw std::runtime_error("do-not-log-private-value");
        }
        if (std::string_view(argv[1]) == "--worker-escape")
        {
            std::thread worker([] { throw std::runtime_error("do-not-log-private-value"); });
            worker.join();
        }
        RaiseException(0xE0000042, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return 8;
    }
    QCoreApplication app(argc, argv);
    try
    {
        const auto root = std::filesystem::temp_directory_path() /
            ("mhy-crash-regression-" + std::to_string(QCoreApplication::applicationPid()));
        std::filesystem::create_directories(root);
        for (const char* mode : {"--terminate", "--cpp-escape", "--worker-escape", "--seh"})
        {
            QProcess child;
            child.setWorkingDirectory(QString::fromStdWString(root.wstring()));
            child.start(QCoreApplication::applicationFilePath(), {QString::fromLatin1(mode)});
            if (!child.waitForStarted(5000) || !child.waitForFinished(10000) || child.exitCode() == 0)
                throw std::runtime_error("Crash child did not terminate within timeout");
        }
        std::ifstream file(root / "Config/scanner.log");
        const std::string log((std::istreambuf_iterator<char>(file)), {});
        if (log.find("fatal: std::terminate") == std::string::npos ||
            log.find("fatal: windows-exception") == std::string::npos ||
            log.find("fatal: SIGABRT") == std::string::npos ||
            log.find("0xE06D7363") == std::string::npos ||
            log.find("0xE0000042") == std::string::npos ||
            log.find("do-not-log-private-value") != std::string::npos)
            throw std::runtime_error("Fatal logs are missing or disclose private exception text");
        int dumps = 0;
        for (const auto& entry : std::filesystem::directory_iterator(root / "Config"))
            if (entry.path().extension() == ".dmp" && entry.file_size() > 0) ++dumps;
        if (dumps != 4) throw std::runtime_error("Matching minidumps not generated");
        std::cout << "terminate and Windows exception child processes logged fatal events and generated minidumps\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
