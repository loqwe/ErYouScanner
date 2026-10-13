#pragma once

#include <Windows.h>
#include <DbgHelp.h>
#include <cstdio>
#include <exception>
#include <csignal>

inline void RecordScannerFatal(const char* phase, DWORD code, EXCEPTION_POINTERS* exception) noexcept
{
    // Avoid the normal logger's mutex and heap allocation on the fatal path.
    CreateDirectoryW(L"Config", nullptr);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char line[256]{};
    const int count = std::snprintf(line, sizeof(line),
        "[%04u-%02u-%02u %02u:%02u:%02u] [thread=%lu] fatal: %s code=0x%08lX version=%s\r\n",
        static_cast<unsigned int>(now.wYear), static_cast<unsigned int>(now.wMonth),
        static_cast<unsigned int>(now.wDay), static_cast<unsigned int>(now.wHour),
        static_cast<unsigned int>(now.wMinute), static_cast<unsigned int>(now.wSecond),
        GetCurrentThreadId(), phase, code, MHY_Scanner_VERSION);
    const HANDLE log = CreateFileW(L"Config/scanner.log", FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log != INVALID_HANDLE_VALUE)
    {
        DWORD written{};
        if (count > 0 && count < static_cast<int>(sizeof(line)))
            WriteFile(log, line, static_cast<DWORD>(count), &written, nullptr);
        FlushFileBuffers(log);
        CloseHandle(log);
    }
    char dumpPath[256]{};
    std::snprintf(dumpPath, sizeof(dumpPath),
        "Config/MHY_Scanner_v%s_%04u%02u%02u_%02u%02u%02u_%lu_%lu.dmp",
        MHY_Scanner_VERSION, static_cast<unsigned int>(now.wYear), static_cast<unsigned int>(now.wMonth),
        static_cast<unsigned int>(now.wDay), static_cast<unsigned int>(now.wHour),
        static_cast<unsigned int>(now.wMinute), static_cast<unsigned int>(now.wSecond),
        GetCurrentProcessId(), GetCurrentThreadId());
    const HANDLE dump = CreateFileA(dumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (dump != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, MiniDumpNormal,
            exception ? &info : nullptr, nullptr, nullptr);
        CloseHandle(dump);
    }
}

inline LONG WINAPI ScannerWindowsException(EXCEPTION_POINTERS* exception) noexcept
{
    RecordScannerFatal("windows-exception", exception->ExceptionRecord->ExceptionCode, exception);
    return EXCEPTION_EXECUTE_HANDLER;
}

inline void ScannerFatalExit(const char* phase) noexcept
{
    CONTEXT context{};
    RtlCaptureContext(&context);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = 0xE0000001;
    record.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
    EXCEPTION_POINTERS exception{&record, &context};
    RecordScannerFatal(phase, record.ExceptionCode, &exception);
    ExitProcess(3);
}

inline void ScannerTerminate() noexcept { ScannerFatalExit("std::terminate"); }
inline void ScannerAbortSignal(int) noexcept { ScannerFatalExit("SIGABRT"); }

inline void InstallScannerCrashHandlers() noexcept
{
    std::set_terminate(ScannerTerminate);
    std::signal(SIGABRT, ScannerAbortSignal);
    SetUnhandledExceptionFilter(ScannerWindowsException);
}
