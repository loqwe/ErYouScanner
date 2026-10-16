#pragma once

#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

namespace KuroCaptchaProof {
// Public GeeTest application ID used by DSVVA/KuRo_Scanner 1.0.0.
inline constexpr std::string_view captchaId = "3f7e2d848ce0cb7e7d019d621e556ce2";

inline std::string normalize(std::string_view text)
{
    if (text.empty() || text.size() > 32768) return {};
    const auto input = nlohmann::json::parse(text, nullptr, false);
    if (!input.is_object()) return {};
    nlohmann::json proof = nlohmann::json::object();
    for (const auto* field : {"lot_number", "captcha_output", "pass_token", "gen_time"}) {
        const auto value = input.find(field);
        if (value == input.end() || !value->is_string()) return {};
        const auto& string = value->get_ref<const std::string&>();
        if (string.empty() || string.size() > 8192 || string.find('\0') != std::string::npos) return {};
        proof[field] = string;
    }
    const auto id = input.find("captcha_id");
    if (id != input.end()) {
        if (!id->is_string() || id->get_ref<const std::string&>() != captchaId) return {};
        proof["captcha_id"] = *id;
    }
    return proof.dump();
}
}
