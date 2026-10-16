#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include "ConfigDate.h"
#include "AccountSecrets.hpp"

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
QByteArray read(const QString& path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "cannot read fixture");
    return file.readAll();
}
void write(const QString& path, const QByteArray& data)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "cannot write fixture");
}
int child(const QString& directory, const QString& mode = "--load")
{
    QProcess process;
    process.setWorkingDirectory(directory);
    process.start(QCoreApplication::applicationFilePath(), {mode});
    require(process.waitForStarted() && process.waitForFinished(10000), "child timed out");
    require(process.exitStatus() == QProcess::NormalExit, "child crashed");
    return process.exitCode();
}
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains("--load") || app.arguments().contains("--save-failure"))
    {
        try
        {
            const auto config = nlohmann::json::parse(ConfigDate::getInstance().getConfig());
            if (!config["account"].empty())
                require(config["account"][0]["access_key"] == "synthetic-test-only", "decryption failed");
            if (app.arguments().contains("--save-failure"))
            {
                const auto before = ConfigDate::getInstance().getConfig();
                const auto path = QDir::current().absoluteFilePath("Config/userinfo.json").toStdWString();
                const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                require(handle != INVALID_HANDLE_VALUE, "cannot lock original");
                bool failed = false;
                try { auto changed = config; changed["auto_exit"] = true; ConfigDate::getInstance().updateConfig(changed.dump()); }
                catch (...) { failed = true; }
                CloseHandle(handle);
                require(failed && ConfigDate::getInstance().getConfig() == before, "failed commit changed memory");
                return 0;
            }
            ConfigDate::getInstance().updateConfig(config.dump());
            return 0;
        }
        catch (...) { return 2; }
    }
    try
    {
        const auto encrypted = account_secrets::transform("synthetic-test-only", true);
        require(encrypted.starts_with(account_secrets::prefix), "missing envelope");
        require(encrypted.find("synthetic-test-only") == std::string::npos, "plaintext persisted");
        require(account_secrets::transform(encrypted, false) == "synthetic-test-only", "DPAPI roundtrip failed");
        require(account_secrets::transform(encrypted, true) == encrypted, "double encryption");
        QTemporaryDir temporary;
        require(temporary.isValid(), "temporary directory failed");
        QDir dir(temporary.path());
        require(dir.mkpath("Config"), "fixture Config failed");
        const auto path = dir.filePath("Config/userinfo.json");
        const auto backup = dir.filePath("Config/userinfo.before-v2.0.1.json.bak");
        const QByteArray original = R"({"num":1,"account":[{"type":"库街区官服","access_key":"synthetic-test-only","uid":"123"}]})";
        write(path, original);
        require(child(temporary.path()) == 0, "legacy migration failed");
        require(read(backup) == original, "legacy backup changed");
        auto disk = nlohmann::json::parse(read(path).toStdString());
        require(disk["schema_version"] == 2 && disk["account"][0]["provider"] == "kuro", "schema migration failed");
        const auto accountId = disk["account"][0]["account_id"];
        require(read(path).indexOf("synthetic-test-only") == -1, "saved token exposed");
        require(child(temporary.path()) == 0, "protected reload failed");
        const auto beforeFailedSave = read(path);
        require(child(temporary.path(), "--save-failure") == 0 && read(path) == beforeFailedSave,
            "failed atomic commit overwrote original");
        disk = nlohmann::json::parse(read(path).toStdString());
        require(disk["account"][0]["account_id"] == accountId, "internal account ID changed");
        disk["account"][0]["access_key"] = "dpapi:v1:not-valid";
        const auto damaged = QByteArray::fromStdString(disk.dump());
        write(path, damaged);
        require(child(temporary.path()) == 2, "damaged ciphertext accepted");
        require(read(path) == damaged && read(backup) == original, "failed load overwrote data");
        write(path, "{invalid");
        require(child(temporary.path()) == 2 && read(path) == "{invalid", "invalid JSON overwritten");
        std::cout << "DPAPI roundtrip, atomic migration, identity stability and failed-load preservation passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
