#pragma once

#include <string>
#include <mutex>

class ConfigDate final
{
public:
    static ConfigDate& getInstance();
    void updateConfig(const std::string& config);
    std::string getConfig() const;
    std::string defaultConfig();

private:
    ConfigDate();
    ~ConfigDate();
    std::string m_config;
    mutable std::mutex m_mutex;
    ConfigDate(const ConfigDate& config) = delete;
    const ConfigDate& operator=(const ConfigDate& config) = delete;
    std::string loadConfig();
    std::string readConfigFile(const std::string& filePath);
    inline static const char* ConfigFilePath{ "./Config/userinfo.json" };
};
