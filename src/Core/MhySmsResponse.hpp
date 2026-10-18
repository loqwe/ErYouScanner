#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>
#include "ApiDefs.hpp"

inline constexpr int MhySmsInvalidResponse = -1000001;
inline constexpr int MhySmsTransportError = -1000002;

inline std::string MhySmsResponseShape(std::string_view body, std::string_view aigis)
{
    nlohmann::json shape = nlohmann::json::object();
    const auto j = nlohmann::json::parse(body, nullptr, false);
    shape["body_type"] = j.type_name();
    if (j.is_object())
    {
        if (j.contains("retcode") && j["retcode"].is_number_integer()) shape["retcode"] = j["retcode"];
        if (j.contains("data"))
        {
            const auto& data = j["data"];
            shape["data_type"] = data.type_name();
            for (const auto* field : {"action_type", "countdown", "sent_new"})
                if (data.is_object() && data.contains(field)) shape[field] = data[field].type_name();
        }
    }
    const auto proof = nlohmann::json::parse(aigis, nullptr, false);
    shape["proof_type"] = proof.type_name();
    if (proof.is_object())
    {
        for (const auto* field : {"session_id", "mmt_type", "data"})
            if (proof.contains(field)) shape[std::string("proof_") + field] = proof[field].type_name();
        if (proof.contains("mmt_type") && proof["mmt_type"].is_number_integer())
            shape["mmt_type"] = proof["mmt_type"];
        if (proof.contains("session_id") && proof["session_id"].is_string())
            shape["session_nonempty"] = !proof["session_id"].get_ref<const std::string&>().empty();
        if (proof.contains("data") && proof["data"].is_string())
        {
            const auto challenge = nlohmann::json::parse(proof["data"].get<std::string>(), nullptr, false);
            for (const auto* field : {"gt", "challenge", "captcha_id", "use_v4", "risk_type", "new_captcha", "success"})
                if (challenge.is_object() && challenge.contains(field))
                    shape[std::string("challenge_") + field] = challenge[field].type_name();
            if (challenge.is_object() && challenge.contains("use_v4") && challenge["use_v4"].is_boolean())
                shape["use_v4"] = challenge["use_v4"];
            if (challenge.is_object() && challenge.contains("gt") && challenge["gt"].is_string())
                shape["gt_nonempty"] = !challenge["gt"].get_ref<const std::string&>().empty();
        }
    }
    return shape.dump();
}

struct MhySmsResult
{
    int retcode = MhySmsInvalidResponse;
    GeetestData data;
    int countdown = 60;
};

inline MhySmsResult ParseMhySmsResponse(std::string_view body, std::string_view aigis = {})
{
    MhySmsResult result;
    try
    {
        const auto j = nlohmann::json::parse(body, nullptr, false);
        if (!j.is_object() || !j.contains("retcode") || !j["retcode"].is_number_integer())
            return result;
        result.retcode = j["retcode"].get<int>();
        if (result.retcode == 0)
        {
            result.data.action_type = j.at("data").at("action_type").get<std::string>();
            if (result.data.action_type.empty()) return { MhySmsInvalidResponse };
            result.countdown = std::clamp(j["data"].value("countdown", 60), 1, 600);
        }
        else if (result.retcode == -3101 && !aigis.empty())
        {
            const auto proof = nlohmann::json::parse(aigis);
            const auto challenge = nlohmann::json::parse(proof.at("data").get<std::string>());
            result.data.session_id = proof.at("session_id").get<std::string>();
            result.data.mmt_type = proof.at("mmt_type").get<int>();
            result.data.gt = challenge.at("gt").get<std::string>();
            result.data.use_v4 = challenge.value("use_v4", false);
            result.data.risk_type = challenge.value("risk_type", std::string{});
            if (!result.data.use_v4)
            {
                if (challenge.contains("challenge") && challenge["challenge"].is_string())
                    result.data.challenge = challenge["challenge"].get<std::string>();
                if (challenge.contains("new_captcha"))
                    result.data.new_captcha = challenge["new_captcha"] == true || challenge["new_captcha"] == 1;
                if (challenge.contains("success"))
                    result.data.offline = challenge["success"] == 0 || challenge["success"] == false;
            }
            if (result.data.session_id.empty() || result.data.mmt_type != 1 || result.data.gt.empty())
                return { MhySmsInvalidResponse };
            result.data.GeeTestType = ServerType::Official;
        }
    }
    catch (...)
    {
        return { MhySmsInvalidResponse };
    }
    return result;
}

struct MhyMobileLoginResult
{
    int retcode = MhySmsInvalidResponse;
    struct
    {
        std::string V2Token;
        std::string aid;
        std::string mid;
    } data;
};

inline MhyMobileLoginResult ParseMhyMobileLoginResponse(std::string_view body)
{
    MhyMobileLoginResult result;
    try
    {
        const auto j = nlohmann::json::parse(body, nullptr, false);
        if (!j.is_object() || !j.contains("retcode") || !j["retcode"].is_number_integer())
            return result;
        result.retcode = j["retcode"].get<int>();
        if (result.retcode == 0)
        {
            const auto& data = j.at("data");
            result.data.V2Token = data.at("token").at("token").get<std::string>();
            result.data.aid = data.at("user_info").at("aid").get<std::string>();
            result.data.mid = data.at("user_info").at("mid").get<std::string>();
            if (result.data.V2Token.empty() || result.data.aid.empty() || result.data.mid.empty())
                return { MhySmsInvalidResponse };
        }
    }
    catch (...)
    {
        return { MhySmsInvalidResponse };
    }
    return result;
}
