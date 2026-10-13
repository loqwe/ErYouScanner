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
