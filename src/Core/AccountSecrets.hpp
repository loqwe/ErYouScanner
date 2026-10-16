#pragma once

#include <Windows.h>
#include <wincrypt.h>
#include <QByteArray>
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp>

namespace account_secrets
{
inline constexpr auto prefix = "dpapi:v1:";

inline std::string transform(const std::string& value, bool encrypt)
{
    if (value.empty()) return value;
    const bool protectedValue = value.starts_with(prefix);
    if (encrypt == protectedValue) return value;
    const QByteArray input = encrypt ? QByteArray::fromStdString(value) :
        QByteArray::fromBase64(QByteArray::fromStdString(value.substr(9)), QByteArray::AbortOnBase64DecodingErrors);
    if (input.isEmpty()) throw std::runtime_error("Invalid protected account data");
    DATA_BLOB source{ static_cast<DWORD>(input.size()), reinterpret_cast<BYTE*>(const_cast<char*>(input.constData())) };
    DATA_BLOB result{};
    const BOOL ok = encrypt ? CryptProtectData(&source, L"ErYouScanner", nullptr, nullptr, nullptr,
        CRYPTPROTECT_UI_FORBIDDEN, &result) : CryptUnprotectData(&source, nullptr, nullptr, nullptr, nullptr,
        CRYPTPROTECT_UI_FORBIDDEN, &result);
    if (!ok) throw std::runtime_error("Windows account protection failed");
    struct FreeBlob { BYTE* value; ~FreeBlob() { LocalFree(value); } } cleanup{ result.pbData };
    const QByteArray bytes(reinterpret_cast<const char*>(result.pbData), result.cbData);
    return encrypt ? std::string(prefix) + bytes.toBase64().toStdString() : bytes.toStdString();
}

inline void transformAccounts(nlohmann::json& config, bool encrypt)
{
    if (!config.contains("account") || !config["account"].is_array()) return;
    for (auto& account : config["account"])
    {
        if (account.contains("access_key") && account["access_key"].is_string())
            account["access_key"] = transform(account["access_key"].get<std::string>(), encrypt);
    }
}
}
