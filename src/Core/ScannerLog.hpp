#pragma once

#include <ctime>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <string_view>
#include <thread>

inline void WriteScannerLog(std::string_view message) noexcept
{
    // Logging must not throw while another exception is being handled.
    try
    {
        static std::mutex mutex;
        std::lock_guard lock(mutex);
        std::filesystem::create_directories("Config");
        std::error_code error;
        if (std::filesystem::file_size("Config/scanner.log", error) >= 1024 * 1024 && !error)
        {
            std::filesystem::remove("Config/scanner.log.3", error);
            for (int i = 2; i >= 1; --i)
                std::filesystem::rename("Config/scanner.log." + std::to_string(i),
                    "Config/scanner.log." + std::to_string(i + 1), error);
            std::filesystem::rename("Config/scanner.log", "Config/scanner.log.1", error);
        }
        std::ofstream file("Config/scanner.log", std::ios::app);
        if (!file) return;
        const std::time_t now = std::time(nullptr);
        std::tm local{};
        localtime_s(&local, &now);
        file << '[' << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << "] [thread="
             << std::this_thread::get_id() << "] " << message << '\n';
    }
    catch (...) {}
}
