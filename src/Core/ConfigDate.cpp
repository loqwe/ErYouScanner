#include "ConfigDate.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <fstream>
#include <filesystem>
#include <QFile>
#include <QSaveFile>
#include <QUuid>
#include "AccountSecrets.hpp"

ConfigDate::ConfigDate()
{
    m_config = loadConfig();
}

ConfigDate::~ConfigDate()
{
}

ConfigDate& ConfigDate::getInstance()
{
    static ConfigDate instance;
    return instance;
}

void ConfigDate::updateConfig(const std::string& config)
{
    std::lock_guard lock(m_mutex);
    auto stored = nlohmann::json::parse(config);
    stored["schema_version"] = 2;
    if (stored.contains("account") && stored["account"].is_array())
    {
        for (auto& account : stored["account"])
        {
            if (!account.contains("account_id"))
                account["account_id"] = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
            if (!account.contains("provider"))
            {
                const auto type = account.value("type", std::string{});
                account["provider"] = (type == "库街区官服" || type == "库洛官服" || type == "鸣潮官服") ? "kuro" :
                    (type == "官服" ? "mhy" : "unsupported");
            }
        }
    }
    const auto plain = stored.dump();
    account_secrets::transformAccounts(stored, true);
    std::filesystem::create_directories("Config");
    QSaveFile file(QString::fromUtf8(ConfigFilePath));
    const auto bytes = QByteArray::fromStdString(stored.dump(4));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        throw std::runtime_error("Cannot atomically save account configuration");
    m_config = plain;
}

std::string ConfigDate::getConfig() const
{
    std::lock_guard lock(m_mutex);
    return m_config;
}

std::string ConfigDate::loadConfig()
{
    if (std::filesystem::exists(ConfigFilePath))
    {
        const auto configContent = readConfigFile(ConfigFilePath);
        auto decoded = nlohmann::json::parse(configContent);
        account_secrets::transformAccounts(decoded, false);
        // Preserve the original file before upgrading old plaintext configurations.
        if (decoded.value("schema_version", 0) < 2)
        {
            QString backup = QStringLiteral("Config/userinfo.before-v2.0.1.json.bak");
            bool alreadyBackedUp = false;
            if (QFile::exists(backup))
            {
                QFile previous(backup);
                alreadyBackedUp = previous.open(QIODevice::ReadOnly) && previous.readAll() == QByteArray::fromStdString(configContent);
                if (!alreadyBackedUp) backup = QStringLiteral("Config/userinfo.before-v2.0.1-%1.json.bak")
                    .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
            }
            if (!alreadyBackedUp && !QFile::copy(QString::fromUtf8(ConfigFilePath), backup))
                throw std::runtime_error("Cannot back up legacy configuration");
            updateConfig(decoded.dump());
            return m_config;
        }
        return decoded.dump();
    }
    else
    {
        return defaultConfig();
    }
}

std::string ConfigDate::readConfigFile(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (file.is_open())
    {
        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();
        std::cout << "读取配置文件成功。\n";
        return content;
    }
    else
    {
        std::cout << "无法读取配置文件。\n";
        return "";
    }
}

std::string ConfigDate::defaultConfig()
{
    const static char* defaultConfig = R"({"auto_exit": false,"auto_login":false,"auto_start": false,"confirm_delay_seconds":0,"scan_interval_ms":200,"account":[],"last_account":0,"live_room_id":"","live_room_items":[],"num":0})";
    updateConfig(defaultConfig);
    return getConfig();
}
