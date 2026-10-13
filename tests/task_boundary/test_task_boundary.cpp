#include <QCoreApplication>
#include <QThreadPool>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "ScannerTask.hpp"
#include "StokenResponse.hpp"

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    try
    {
        require(!ParseStokenRetcode("[]").has_value(), "Array response accepted");
        require(!ParseStokenRetcode("null").has_value(), "Null response accepted");
        require(!ParseStokenRetcode("{\"retcode\":\"0\"}").has_value(), "String retcode accepted");
        require(!ParseStokenRetcode("{\"retcode\":0.5}").has_value(), "Float retcode accepted");
        require(!ParseStokenRetcode("{\"retcode\":18446744073709551615}").has_value(), "Overflow retcode accepted");
        require(!ParseStokenRetcode("{\"data\":{}}").has_value(), "Missing retcode accepted");
        require(!ParseStokenRetcode("not-json").has_value(), "Malformed JSON accepted");
        require(ParseStokenRetcode("{\"retcode\":0}") == 0, "Valid stoken rejected");
        require(ParseStokenRetcode("{\"retcode\":-100}") == -100, "API failure lost");

        const auto root = std::filesystem::temp_directory_path() /
            ("mhy-task-regression-" + std::to_string(QCoreApplication::applicationPid()));
        const auto originalDirectory = std::filesystem::current_path();
        std::filesystem::create_directories(root / "Config");
        std::filesystem::current_path(root);
        QThreadPool pool;
        pool.setMaxThreadCount(1);
        std::atomic<int> failures{0};
        std::atomic<int> completed{0};
        pool.start([&] {
            RunScannerTask("test: network", [] { throw std::runtime_error("access_key=private-test-secret"); },
                [&] { ++failures; });
        });
        pool.start([&] {
            RunScannerTask("test: json", [] {
                const auto value = nlohmann::json::parse("{\"retcode\":\"0\"}");
                (void)value.value("retcode", -1);
            }, [&] { ++failures; });
        });
        pool.start(MakeScannerTask("test: unknown", [] { throw 42; }, [&] { ++failures; }));
        pool.start([&] {
            RunScannerTask("test: handler", [] { throw std::bad_alloc(); }, [] { throw 7; });
        });
        pool.start([&] { RunScannerTask("test: healthy", [&] { ++completed; }); });
        require(pool.waitForDone(5000), "Pool did not complete");
        require(failures == 3 && completed == 1, "Exception lost or pool stopped after failure");
        std::ifstream file("Config/scanner.log");
        const std::string log((std::istreambuf_iterator<char>(file)), {});
        require(log.find("test: network") != std::string::npos, "Network exception context missing");
        require(log.find("json_id=302") != std::string::npos, "JSON exception id missing");
        require(log.find("test: unknown") != std::string::npos, "Unknown exception missing");
        require(log.find("failure-handler") != std::string::npos, "Handler exception missing");
        require(log.find("[thread=") != std::string::npos, "Thread diagnostic missing");
        require(log.find("private-test-secret") == std::string::npos, "Exception exposed a credential");
        std::filesystem::current_path(originalDirectory);
        std::cout << "JSON shape validation and real QThreadPool exception recovery passed; log: "
                  << root.string() << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
