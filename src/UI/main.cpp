#include "WindowMain.h"

#include <Windows.h>
#include <DbgHelp.h>

#include <QMessageBox>
#include <QStyleHints>
#include <QDir>
#include "ScannerCrash.hpp"
#include "ScannerLog.hpp"
#include <opencv2/core/utils/logger.hpp>

static bool isOpen()
{
    HANDLE hMutex = CreateMutex(NULL, true, TEXT("T08lJ8CJmJiyoxdV"));
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

int main(int argc, char* argv[])
{
    UINT utf8 = 65001;
    SetConsoleOutputCP(utf8);
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
    if (length && length < ARRAYSIZE(executable))
    {
        for (DWORD i = length; i > 0; --i)
        {
            if (executable[i - 1] == L'\\')
            {
                executable[i - 1] = L'\0';
                SetCurrentDirectoryW(executable);
                break;
            }
        }
    }
    InstallScannerCrashHandlers();
    WriteScannerLog("startup: initializing application version=" MHY_Scanner_VERSION);
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_ERROR);
    QApplication a(argc, argv);
    if (!QDir::setCurrent(QCoreApplication::applicationDirPath()))
    {
        QMessageBox::information(nullptr, "启动失败", "无法进入程序所在目录。", QMessageBox::Yes);
        return 2;
    }
    QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    if (isOpen())
    {
        WriteScannerLog("startup: existing instance detected; activating its window");
        HWND hwnd = FindWindowW(NULL, L"MHY扫码器");
        if (hwnd == NULL)
        {
            return 0;
        }
        if (GetForegroundWindow() == hwnd)
        {
            return 0;
        }
        ShowWindow(hwnd, SW_MINIMIZE);
        ShowWindow(hwnd, SW_SHOWNORMAL);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
        SetForegroundWindow(hwnd);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE);
        return -1;
    }
    WindowMain w;
    const HANDLE logCheck = CreateFileW(L"Config/scanner.log", FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (logCheck == INVALID_HANDLE_VALUE)
        QMessageBox::information(&w, "日志无法写入", "程序目录没有写入权限，日志和配置无法保存。请将完整压缩包解压到可写目录。", QMessageBox::Yes);
    else
        CloseHandle(logCheck);
    w.show();
    const int result = a.exec();
    WriteScannerLog("shutdown: event loop finished code=" + std::to_string(result));
    return result;
}
