#pragma once

#include <string>
#include <string_view>
#include <format>
#include <random>
#include <sstream>
#include <optional>
#include <iostream>
#include <tuple>
#include <utility>
#include <fstream>

#include <nlohmann/json.hpp>
#include <cpr/cpr.h>

#include "ApiDefs.hpp"
#include "CreateUUID.hpp"
#include "CryptoKit.h"
#include "UtilString.hpp"
#include "TimeStamp.hpp"
#include "UrlQuery.hpp"
#include "ScannerLog.hpp"
#include "StokenResponse.hpp"
#include "MhySmsResponse.hpp"

static const std::string device_id{ CreateUUID::CreateUUID4() };
static GameType loginType{ GameType::HonkaiStarRail };

[[nodiscard]] inline std::string DataSignAlgorithmVersionGen1()
{
    return "";
}

[[nodiscard]] inline std::string DataSignAlgorithmVersionGen2(const std::string_view body, const std::string_view query)
{
    const std::string time_now{ std::to_string(GetUnixTimeStampSeconds()) };
    std::random_device rd{};
    std::mt19937 gen{ rd() };
    int lower_bound{ 100001 };
    int upper_bound{ 200000 };
    std::uniform_int_distribution<int> dist(lower_bound, upper_bound);
    const std::string rand{ std::to_string(dist(gen)) };
    std::string m{ "salt=" + std::string(mihoyobbs_salt_x6) + "&t=" + time_now + "&r=" + rand + "&b=" + std::string(body) + "&q=" + std::string(query) };
    return time_now + "," + rand + "," + Md5(m);
}

inline std::string Encrypt(const std::string_view source)
{
    static constinit const char* PublicKey{
        "-----BEGIN PUBLIC KEY-----\n"
        "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDDvekdPMHN3AYhm/vktJT+YJr7"
        "cI5DcsNKqdsx5DZX0gDuWFuIjzdwButrIYPNmRJ1G8ybDIF7oDW2eEpm5sMbL9zs"
        "9ExXCdvqrn51qELbqj0XxtMTIpaCHFSI50PfPpTFV9Xt/hmyVwokoOXFlAEgCn+Q"
        "CgGs52bFoYMtyi+xEQIDAQAB\n"
        "-----END PUBLIC KEY-----"
    };
    return rsaEncrypt(source.data(), PublicKey);
}

inline cpr::Header GetRequestHeader()
{
    static cpr::Header headers{
        { "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) miHoYoBBS/2.76.1" },
        { "Accept", "application/json" },
        { "x-rpc-app_id", "bll8iq97cem8" },
        { "x-rpc-app_version", "2.76.1" },
        { "x-rpc-client_type", "2" },
        { "x-rpc-device_id", device_id },
        { "x-rpc-device_name", "" },
        { "x-rpc-game_biz", "bbs_cn" },
        { "x-rpc-sdk_version", "2.16.0" }
    };
    return headers;
}

inline std::tuple<int, std::string, std::string> GetLoginQrcodeUrl()
{
    auto res = cpr::Post(
        cpr::Url{ api::mhy::passport::create_qr_login },
        cpr::Header{
            { "Content-Type", "application/json" },
            { "x-rpc-app_id", "dw9y09jqjpxc" },
            { "x-rpc-device_id", device_id },
        });

    auto data = nlohmann::json::parse(res.text);
    std::string ticket = data["data"]["ticket"].get<std::string>();
    std::string qrcodeUrl = data["data"]["url"].get<std::string>();
    return { 0, qrcodeUrl, ticket };
}

inline std::tuple<LoginQRCodeState, std::string, std::string, std::string> GetQRCodeState(
    const std::string_view ticket)
{
    const auto response = cpr::Post(
        cpr::Url{ api::mhy::passport::query_qr_login_status },
        cpr::Body{ nlohmann::json{ { "ticket", ticket } }.dump() },
        cpr::Header{
            { "Content-Type", "application/json" },
            { "x-rpc-app_id", "dw9y09jqjpxc" },
            { "x-rpc-device_id", device_id },
        });

    const auto data = nlohmann::json::parse(response.text);

    if (data.value("retcode", -1) != 0)
        return { LoginQRCodeState::Expired, {}, {}, {} };

    static const std::unordered_map<std::string, LoginQRCodeState> stateMap{
        { "Created", LoginQRCodeState::Init },
        { "Scanned", LoginQRCodeState::Scanned },
        { "Confirmed", LoginQRCodeState::Confirmed },
    };

    const auto stat = data["data"]["status"].get<std::string>();
    const auto it = stateMap.find(stat);

    if (it == stateMap.end())
        return { LoginQRCodeState::Expired, {}, {}, {} };

    if (it->second == LoginQRCodeState::Confirmed)
    {
        return { LoginQRCodeState::Confirmed,
                 data["data"]["tokens"][0]["token"].get<std::string>(),
                 data["data"]["user_info"]["mid"].get<std::string>(),
                 data["data"]["user_info"]["aid"].get<std::string>() };
    }

    return { it->second, {}, {}, {} };
}

inline std::string getMysUserName(const std::string_view uid)
{
    static constexpr std::string_view url = api::mhy::mys::userinfo;
    const auto response = cpr::Get(
        cpr::Url{ std::format("{}?uid={}", url, uid) });

    const auto data = nlohmann::json::parse(response.text);
    return data["data"]["user_info"]["nickname"].get<std::string>();
}

inline std::tuple<int, std::string, std::string> GetStokenByGameToken(
    const std::string_view uid,
    const std::string_view game_token)
{
    const auto response = cpr::Post(
        cpr::Url{ api::mhy::takumi::game_token_stoken },
        cpr::Body{ nlohmann::json{ { "account_id", std::stoi(uid.data()) }, { "game_token", game_token } }.dump() },
        GetRequestHeader());

    const auto j = nlohmann::json::parse(response.text);
    const int retcode = j.value("retcode", -1);

    if (retcode != 0)
        return { retcode, {}, {} };

    return { 0,
             j["data"]["user_info"]["mid"].get<std::string>(),
             j["data"]["token"]["token"].get<std::string>() };
}

inline std::tuple<int, std::string> GetGameTokenByStoken(
    const std::string_view stoken,
    const std::string_view mid)
{
    auto parseResponse = [](const cpr::Response& response) -> std::tuple<int, std::string> {
        if (response.error || response.status_code != 200 || response.text.empty())
        {
            return { -1, {} };
        }

        const auto j = nlohmann::json::parse(response.text, nullptr, false);
        if (j.is_discarded())
        {
            return { -1, {} };
        }

        const int retcode = j.value("retcode", -1);
        if (retcode != 0)
        {
            return { retcode, {} };
        }

        return { 0, j["data"]["game_token"].get<std::string>() };
    };

    auto requestByCookie = [&](const std::string_view tokenName) {
        const std::string cookie = std::string(tokenName) + "=" + std::string(stoken) + "; mid=" + std::string(mid);
        const auto response = cpr::Get(
            cpr::Url{ api::mhy::takumi::game_token },
            cpr::Header{ { "Cookie", cookie } });
        return parseResponse(response);
    };

    std::tuple<int, std::string> lastError{ -1, {} };

    // Current SToken v2 credentials are verified from Cookie instead of query parameters.
    if (stoken.starts_with("v2_"))
    {
        auto result = requestByCookie("stoken_v2");
        if (std::get<0>(result) == 0)
        {
            return result;
        }
        lastError = std::move(result);
    }

    auto cookieResult = requestByCookie("stoken");
    if (std::get<0>(cookieResult) == 0)
    {
        return cookieResult;
    }
    if (std::get<0>(lastError) == -1 || std::get<0>(cookieResult) != -1)
    {
        lastError = std::move(cookieResult);
    }

    const auto response = cpr::Get(
        cpr::Url{ api::mhy::takumi::game_token },
        cpr::Parameters{
            { "stoken", stoken.data() },
            { "mid", mid.data() } });

    auto queryResult = parseResponse(response);
    if (std::get<0>(queryResult) == 0)
    {
        return queryResult;
    }

    return std::get<0>(lastError) != -1 ? lastError : queryResult;
}

inline bool CheckStokenValid(const std::string_view stoken, const std::string_view mid)
{
    const std::string cookie = "stoken=" + std::string(stoken) + ";mid=" + std::string(mid);
    const auto response = cpr::Get(
        cpr::Url{ api::mhy::takumi::cookie_account_info },
        cpr::Header{
            { "Accept", "application/json" },
            { "Cookie", cookie } },
        cpr::Timeout{10000});

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        WriteScannerLog("check stoken: http=" + std::to_string(response.status_code) +
            " network_error=" + std::to_string(static_cast<int>(response.error.code)));
        return false;
    }

    const auto retcode = ParseStokenRetcode(response.text);
    if (!retcode)
    {
        WriteScannerLog("check stoken: invalid JSON or retcode type");
        return false;
    }

    WriteScannerLog("check stoken: retcode=" + std::to_string(*retcode));
    return *retcode == 0;
}

inline std::string PandaScanQRCode(
    const std::string_view url,
    const std::string_view ticket,
    const GameType gameType)
{
    if (url.empty() || ticket.empty())
    {
        WriteScannerLog("panda scan: missing url or ticket");
        return {};
    }

    const nlohmann::json body{
        { "passport_app_id", "bll8iq97cem8" },
        { "ticket", ticket },
        { "app_id", static_cast<int>(gameType) },
        { "device", device_id },
        { "ts", GetUnixTimeStampSeconds() }
    };

    const auto response = cpr::Post(
        cpr::Url{ std::string(url) },
        cpr::Body{ body.dump() },
        cpr::Header{
            { "Content-Type", "application/json" },
            { "x-rpc-app_id", "bll8iq97cem8" },
            { "x-rpc-device_id", device_id } });

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        WriteScannerLog("panda scan: http=" + std::to_string(response.status_code));
        return {};
    }

    const auto j = nlohmann::json::parse(response.text, nullptr, false);
    if (j.is_discarded())
    {
        WriteScannerLog("panda scan: invalid json");
        return {};
    }

    const int retcode = j.value("retcode", -1);
    const auto passportUrl = j.contains("data") && j["data"].contains("passport_qr_url") ?
        j["data"]["passport_qr_url"].get<std::string>() :
        std::string{};
    WriteScannerLog("panda scan: app_id=" + std::to_string(static_cast<int>(gameType)) +
                    " retcode=" + std::to_string(retcode) +
                    " message=" + j.value("message", std::string{}) +
                    " has_passport_url=" + std::to_string(!passportUrl.empty()));

    return retcode == 0 ? passportUrl : std::string{};
}

[[nodiscard]] inline std::string getPassportQRParam(const std::string_view qrCode, const std::string_view key, const std::string_view terminators)
{
    const std::string needle = std::string(key) + "=";
    const auto begin = qrCode.find(needle);
    if (begin == std::string_view::npos)
    {
        return {};
    }

    const auto valueBegin = begin + needle.size();
    auto valueEnd = qrCode.find_first_of(terminators, valueBegin);
    if (valueEnd == std::string_view::npos)
    {
        valueEnd = qrCode.size();
    }

    return std::string(qrCode.substr(valueBegin, valueEnd - valueBegin));
}

inline bool PassportQRCodeLogin(
    const std::string_view qrCode,
    const std::string_view stoken,
    const std::string_view mid,
    const bool confirm)
{
    const std::string ticket = getPassportQRParam(qrCode, "tk", "&");
    const std::string tokenTypes = getPassportQRParam(qrCode, "token_types", "#");
    if (ticket.empty() || tokenTypes.empty())
    {
        WriteScannerLog(std::string(confirm ? "passport confirm" : "passport scan") + ": missing qr params");
        return false;
    }

    const std::string cookie = "stoken=" + std::string(stoken) + ";mid=" + std::string(mid);
    const nlohmann::json body{
        { "ticket", ticket },
        { "token_types", nlohmann::json::array({ tokenTypes }) }
    };
    const std::string url = confirm ?
        std::string(std::string_view(api::mhy::passport::confirm_qr_login)) :
        std::string(std::string_view(api::mhy::passport::scan_qr_login));
    const auto response = cpr::Post(
        cpr::Url{ url },
        cpr::Body{ body.dump() },
        cpr::Header{
            { "Content-Type", "application/json" },
            { "x-rpc-app_id", "bll8iq97cem8" },
            { "x-rpc-device_id", device_id },
            { "Cookie", cookie } });

    const std::string phase = confirm ? "passport confirm" : "passport scan";
    if (response.error || response.status_code != 200 || response.text.empty())
    {
        WriteScannerLog(phase + ": http=" + std::to_string(response.status_code));
        return false;
    }

    const auto j = nlohmann::json::parse(response.text, nullptr, false);
    if (j.is_discarded())
    {
        WriteScannerLog(phase + ": invalid json");
        return false;
    }

    const int retcode = j.value("retcode", -1);
    WriteScannerLog(phase + ": retcode=" + std::to_string(retcode) +
                    " message=" + j.value("message", std::string{}));
    return retcode == 0;
}
inline bool ScanPassportQRLogin(
    const std::string_view qrCode,
    const std::string_view stoken,
    const std::string_view mid)
{
    return PassportQRCodeLogin(qrCode, stoken, mid, false);
}

inline bool ConfirmPassportQRLogin(
    const std::string_view qrCode,
    const std::string_view stoken,
    const std::string_view mid)
{
    return PassportQRCodeLogin(qrCode, stoken, mid, true);
}

inline MhySmsResult CreateLoginCaptcha(
    const std::string_view mobile,
    const std::string_view aigis = "")
{
    try
    {
        const auto body = nlohmann::json{{"area_code", Encrypt("+86")}, {"mobile", Encrypt(mobile)}}.dump();
        auto headers = GetRequestHeader();
        headers["DS"] = DataSignAlgorithmVersionGen2(body, "");
        if (!aigis.empty()) headers["X-Rpc-Aigis"] = aigis;
        const auto response = cpr::Post(cpr::Url{api::mhy::passport::create_captcha},
            cpr::Body{body}, cpr::Header{headers}, cpr::Timeout{15000});
        WriteScannerLog(std::format("mhy sms: stage=createLoginCaptcha http={} transport={} bytes={} proof={}",
            response.status_code, static_cast<int>(response.error.code), response.text.size(), !aigis.empty()));
        if (response.error || response.status_code != 200)
            return { MhySmsTransportError };
        const auto it = response.header.find("X-Rpc-Aigis");
        WriteScannerLog("mhy sms: stage=response shape=" + MhySmsResponseShape(
            response.text, it == response.header.end() ? "" : it->second));
        auto result = ParseMhySmsResponse(response.text, it == response.header.end() ? "" : it->second);
        WriteScannerLog(std::format("mhy sms: stage=createLoginCaptcha retcode={} challenge={}",
            result.retcode, result.data.mmt_type != 0));
        return result;
    }
    catch (...)
    {
        WriteScannerLog("mhy sms: stage=createLoginCaptcha exception; result unknown");
        return { MhySmsTransportError };
    }
}

inline auto LoginByMobileCaptcha(const std::string_view actionType, const std::string_view mobile, const std::string_view captcha, const std::string_view aigis = "")
{
    try
    {
        const auto body = nlohmann::json{{"area_code", Encrypt("+86")}, {"mobile", Encrypt(mobile)},
            {"action_type", actionType}, {"captcha", captcha}}.dump();
        auto headers = GetRequestHeader();
        headers["DS"] = DataSignAlgorithmVersionGen2(body, "");
        if (!aigis.empty()) headers["X-Rpc-Aigis"] = aigis;
        const auto response = cpr::Post(cpr::Url{api::mhy::passport::login_by_mobile_captcha},
            cpr::Header{headers}, cpr::Body{body}, cpr::Timeout{15000});
        WriteScannerLog(std::format("mhy sms: stage=loginByMobileCaptcha http={} transport={} bytes={}",
            response.status_code, static_cast<int>(response.error.code), response.text.size()));
        if (response.error || response.status_code != 200)
            return MhyMobileLoginResult{MhySmsTransportError};
        auto result = ParseMhyMobileLoginResponse(response.text);
        WriteScannerLog(std::format("mhy sms: stage=loginByMobileCaptcha retcode={}", result.retcode));
        return result;
    }
    catch (...)
    {
        WriteScannerLog("mhy sms: stage=loginByMobileCaptcha exception; result unknown");
        return MhyMobileLoginResult{MhySmsTransportError};
    }
}

inline bool ScanQRLogin(const std::string_view url, const std::string_view ticket, GameType gameType)
{
    const auto response = cpr::Post(
        cpr::Url{ url },
        cpr::Body{ nlohmann::json{
            { "app_id", static_cast<int>(gameType) },
            { "device", device_id },
            { "ticket", ticket } }
                       .dump() },
        cpr::Header{ { "Content-Type", "application/json" } });

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        WriteScannerLog("scan: http=" + std::to_string(response.status_code));
        return false;
    }

    const auto j = nlohmann::json::parse(response.text, nullptr, false);
    if (j.is_discarded())
    {
        WriteScannerLog("scan: invalid json");
        return false;
    }

    const int retcode = j.value("retcode", -1);
    WriteScannerLog("scan: app_id=" + std::to_string(static_cast<int>(gameType)) +
                    " retcode=" + std::to_string(retcode) +
                    " message=" + j.value("message", std::string{}));
    return retcode == 0;
}

inline bool ConfirmQRLogin(const std::string_view url, const std::string_view uid, const std::string_view gameToken, const std::string_view ticket, GameType gameType)
{
    const auto response = cpr::Post(
        cpr::Url{ url },
        cpr::Body{ nlohmann::json{
            { "app_id", static_cast<int>(gameType) },
            { "device", device_id },
            { "ticket", ticket },
            { "payload", { { "proto", "Account" }, { "raw", nlohmann::json{ { "uid", uid }, { "token", gameToken } }.dump() } } } }
                       .dump() },
        cpr::Header{ { "Content-Type", "application/json" } });

    if (response.error || response.status_code != 200 || response.text.empty())
    {
        WriteScannerLog("confirm: http=" + std::to_string(response.status_code));
        return false;
    }

    const auto j = nlohmann::json::parse(response.text, nullptr, false);
    if (j.is_discarded())
    {
        WriteScannerLog("confirm: invalid json");
        return false;
    }

    const int retcode = j.value("retcode", -1);
    WriteScannerLog("confirm: app_id=" + std::to_string(static_cast<int>(gameType)) +
                    " retcode=" + std::to_string(retcode) +
                    " message=" + j.value("message", std::string{}));
    return retcode == 0;
}
