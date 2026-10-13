#pragma once

#include <exception>
#include <string>
#include <system_error>
#include <typeinfo>
#include <utility>
#include <nlohmann/json.hpp>
#include "ScannerLog.hpp"

// Call only from a catch block. Exception messages can contain server bodies,
// URLs or credentials, so persist type/code and task context instead of what().
inline void LogScannerException(const char* stage) noexcept
{
    try
    {
        try { throw; }
        catch (const nlohmann::json::exception& error)
        {
            WriteScannerLog(std::string(stage) + ": exception=json json_id=" + std::to_string(error.id));
        }
        catch (const std::system_error& error)
        {
            WriteScannerLog(std::string(stage) + ": exception=system code=" +
                std::to_string(error.code().value()));
        }
        catch (const std::exception& error)
        {
            WriteScannerLog(std::string(stage) + ": exception=" + typeid(error).name());
        }
        catch (...)
        {
            WriteScannerLog(std::string(stage) + ": exception=unknown");
        }
    }
    catch (...) {}
}

struct IgnoreScannerFailure { void operator()() const noexcept {} };

template<class Work, class Failure = IgnoreScannerFailure>
inline bool RunScannerTask(const char* stage, Work&& work, Failure&& failed = {}) noexcept
{
    try
    {
        work();
        return true;
    }
    catch (...)
    {
        LogScannerException(stage);
        try { failed(); }
        catch (...) { LogScannerException("task: failure-handler"); }
        return false;
    }
}

template<class Work, class Failure = IgnoreScannerFailure>
inline auto MakeScannerTask(const char* stage, Work&& work, Failure&& failed = {})
{
    return [stage, work = std::forward<Work>(work), failed = std::forward<Failure>(failed)]() mutable noexcept {
        RunScannerTask(stage, work, failed);
    };
}
