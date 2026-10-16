#pragma once

#include <string_view>
#include <nlohmann/json.hpp>

enum class AccountPlatform { Unsupported, Mhy, Kuro };

inline AccountPlatform accountPlatform(std::string_view provider, std::string_view legacyType = {})
{
    if (!provider.empty()) {
        if (provider == "mhy") return AccountPlatform::Mhy;
        if (provider == "kuro") return AccountPlatform::Kuro;
        return AccountPlatform::Unsupported;
    }
    if (legacyType == "官服") return AccountPlatform::Mhy;
    if (legacyType == "库街区官服" || legacyType == "库洛官服" || legacyType == "鸣潮官服")
        return AccountPlatform::Kuro;
    return AccountPlatform::Unsupported;
}

inline AccountPlatform accountPlatform(const nlohmann::json& account)
{
    if (!account.is_object()) return AccountPlatform::Unsupported;
    const auto provider = account.find("provider");
    if (provider != account.end()) {
        if (!provider->is_string()) return AccountPlatform::Unsupported;
        const auto& value = provider->get_ref<const std::string&>();
        // An explicit provider must not silently fall back to a legacy label.
        if (value.empty()) return AccountPlatform::Unsupported;
        return accountPlatform(std::string_view(value), {});
    }
    const auto type = account.find("type");
    return type != account.end() && type->is_string()
        ? accountPlatform({}, type->get_ref<const std::string&>()) : AccountPlatform::Unsupported;
}

inline std::string_view accountProviderId(AccountPlatform platform)
{
    switch (platform) {
    case AccountPlatform::Mhy: return "mhy";
    case AccountPlatform::Kuro: return "kuro";
    default: return "unsupported";
    }
}
