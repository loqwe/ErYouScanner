#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include "ScannerLog.hpp"
#include "KuroCaptchaProof.hpp"

// Game protocol: Rocszzz/tide-scanner, commit d0e81c2d6fb36d939c0031f3d79cee9e20ff5839.
// Account metadata: official kurobbs.com assets/useConfig-DLqk4N_9.js (2026-10-16).
// No server response messages or request secrets are copied into diagnostic messages.
enum class KuroError { None, Input, Network, Timeout, Http, InvalidJson, Protocol, Cooldown, Cancelled, Expired };

struct KuroResponse {
    KuroError error{ KuroError::None };
    long httpStatus{};
    int code{ -1 };
    std::string stage;
    std::string message;
    // Sanitized server-side "msg". Kept so diagnostics show the reason the
    // service gives instead of guessing from the business code alone.
    std::string serverMessage;
    std::string token;
    std::string uid;
    std::string name;
    int retryAfterMs{};

    [[nodiscard]] bool success() const { return error == KuroError::None && code == 200; }
};

struct KuroRequest {
    std::string path;
    std::map<std::string, std::string> headers;
    std::map<std::string, std::string> form;
    int connectTimeoutMs{ 4000 };
    int timeoutMs{ 10000 };
    std::optional<std::string> rawBody;
};

struct KuroHttpResponse {
    long status{};
    std::string body;
    bool networkError{};
    bool timeout{};
};

// A client belongs to one worker task; it does not mutate credentials or retry requests.
class KuroApi final {
public:
    using Transport = std::function<KuroHttpResponse(const KuroRequest&)>;

    explicit KuroApi(std::string token = {}, Transport transport = {}, std::string deviceCode = {})
        : token_(std::move(token)), deviceCode_(std::move(deviceCode)),
          transport_(transport ? std::move(transport) : Transport{ postHttp }) {}

    static bool isGameQr(std::string_view qr)
    {
        if (qr.size() < 24 || qr.size() > 4096 || qr.find('\0') != std::string_view::npos) return false;
        std::string upper(qr);
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) {
            return static_cast<char>(std::toupper(c));
        });
        return upper.find("G152#KURO") != std::string::npos;
    }

    KuroResponse checkToken(std::string_view communityUid = {}) const
    {
        if (!validToken()) return invalid("checkToken", "请提供有效格式的库街区 Token");
        // mineV2 is a public profile API and alone cannot establish credential validity.
        auto checked = post("/user/getPrivateSet", {}, "checkToken");
        if (!checked.result.success()) return std::move(checked.result);
        if (!checked.payload.contains("data") || !checked.payload["data"].is_object())
            return protocol(std::move(checked.result), "账号校验响应缺少私有设置数据");
        auto profile = post("/user/mineV2", {{"viewUserId", std::string(communityUid)}}, "accountInfo");
        if (profile.result.success()) {
            const auto data = profile.payload.find("data");
            if (data != profile.payload.end() && data->is_object()) {
                const auto mine = data->find("mine");
                if (mine != data->end() && mine->is_object()) fillIdentity(checked.result, *mine);
            }
        } else if (profile.result.error == KuroError::None && profile.result.code == 220) {
            return std::move(profile.result);
        }
        checked.result.message = checked.result.uid.empty()
            ? "Token 校验通过，未获取到社区账号资料" : "Token 校验通过";
        return std::move(checked.result);
    }

    KuroResponse sdkLogin(std::string_view phone, std::string_view code) const
    {
        if (phone.size() != 11 || phone.front() != '1' || !digits(phone) || !validSms(code))
            return invalid("sdkLogin", "手机号或短信验证码格式不正确");
        auto reply = post("/user/sdkLogin", {{"mobile", std::string(phone)}, {"code", std::string(code)}}, "sdkLogin", false);
        if (!reply.result.success()) return std::move(reply.result);
        const auto data = reply.payload.find("data");
        if (data == reply.payload.end() || !data->is_object())
            return protocol(std::move(reply.result), "登录响应缺少账号数据");
        const auto token = data->find("token");
        if (token == data->end() || !token->is_string() || !isToken(token->get_ref<const std::string&>()))
            return protocol(std::move(reply.result), "登录响应缺少有效 Token");
        reply.result.token = token->get<std::string>();
        fillIdentity(reply.result, *data);
        return std::move(reply.result);
    }

    KuroResponse sendLoginSms(std::string_view phone, std::string_view captcha) const
    {
        if (phone.size() != 11 || phone.front() != '1' || !digits(phone))
            return invalid("getSmsCode", "手机号码格式不正确");
        const auto proof = KuroCaptchaProof::normalize(captcha);
        if (proof.empty()) return invalid("getSmsCode", "请重新完成人机验证");
        return post("/user/getSmsCode", {{"mobile", std::string(phone)}, {"geeTestData", proof}}, "getSmsCode", false).result;
    }

    KuroResponse roleInfos(std::string_view qr) const
    {
        if (!isGameQr(qr)) return invalid("roleInfos", "不是支持的鸣潮官服二维码");
        if (!validToken()) return invalid("roleInfos", "请先登录库街区账号");
        return post("/user/auth/roleInfos", {{"qrCode", std::string(qr)}}, "roleInfos").result;
    }

    KuroResponse scanLogin(std::string_view qr, std::string_view verifyCode = {}, bool autoLogin = false) const
    {
        if (!isGameQr(qr)) return invalid("scanLogin", "不是支持的鸣潮官服二维码");
        if (!validToken()) return invalid("scanLogin", "请先登录库街区账号");
        if (!verifyCode.empty() && !validSms(verifyCode)) return invalid("scanLogin", "短信验证码格式不正确");
        // The caller owns the role precheck/session and resumes SMS on the original QR.
        return post("/user/auth/scanLogin", {{"qrCode", std::string(qr)}, {"autoLogin", autoLogin ? "true" : "false"},
            {"id", ""}, {"verifyCode", std::string(verifyCode)}}, "scanLogin").result;
    }

    KuroResponse sendScanSms(std::string_view captcha) const
    {
        if (!validToken()) return invalid("scanSms", "请先登录库街区账号");
        const auto proof = KuroCaptchaProof::normalize(captcha);
        if (proof.empty()) return invalid("scanSms", "请重新完成人机验证");
        // Both reference clients send an empty form field here: DSVVA/KuRo_Scanner
        // uses cpr::Body{ "geeTestData=" } and Rocszzz/tide-scanner sends
        // {"geeTestData": ""}. Live probing on 2026-10-16 shows the endpoint
        // throttles the account before it inspects this field, so a 242 reply
        // says nothing about the field itself. Match the upstream wire format
        // exactly: a raw form body rather than CPR's Payload container.
        return post("/user/sms/scanSms", {}, "scanSms", true, std::string("geeTestData=")).result;
    }

private:
    struct Reply { KuroResponse result; nlohmann::json payload; };
    struct RequestLog {
        const KuroResponse& result;
        const std::string stage;
        const std::chrono::steady_clock::time_point started{ std::chrono::steady_clock::now() };
        ~RequestLog() noexcept
        {
            try {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
                WriteScannerLog("kuro request: stage=" + stage + " http=" + std::to_string(result.httpStatus) +
                    " code=" + std::to_string(result.code) + " error=" + std::to_string(static_cast<int>(result.error)) +
                    (result.serverMessage.empty() ? std::string() : " msg=" + result.serverMessage) +
                    " elapsed_ms=" + std::to_string(elapsed));
            } catch (...) {}
        }
    };
    std::string token_;
    std::string deviceCode_;
    Transport transport_;

    static bool digits(std::string_view value)
    {
        return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
    }
    static bool validSms(std::string_view value) { return value.size() >= 4 && value.size() <= 8 && digits(value); }
    static bool isToken(std::string_view value)
    {
        return !value.empty() && value.size() <= 16384 && std::none_of(value.begin(), value.end(), [](unsigned char c) {
            return c <= 32 || c == 127;
        });
    }
    bool validToken() const { return isToken(token_); }

    static KuroResponse invalid(std::string stage, std::string message)
    {
        KuroResponse result;
        result.error = KuroError::Input;
        result.stage = std::move(stage);
        result.message = std::move(message);
        return result;
    }
    static KuroResponse protocol(KuroResponse result, std::string message)
    {
        result.error = KuroError::Protocol;
        result.message = std::move(message);
        return result;
    }
    // The service explains refusals in "msg" (for example "发送短信频繁").
    // Keep that text for diagnostics, bounded and stripped of control
    // characters. It never carries credentials, codes or QR contents.
    static std::string sanitizeMessage(const nlohmann::json& payload)
    {
        if (!payload.is_object()) return {};
        const auto value = payload.find("msg");
        if (value == payload.end() || !value->is_string()) return {};
        std::string text = value->get<std::string>();
        if (text.size() > 200) text.resize(200);
        std::string kept;
        kept.reserve(text.size());
        for (const auto character : text) {
            const auto byte = static_cast<unsigned char>(character);
            if (byte >= 32 && byte != 127) kept.push_back(character);
        }
        return kept;
    }
    static void fillIdentity(KuroResponse& result, const nlohmann::json& data)
    {
        const auto uid = data.find("userId");
        if (uid != data.end()) {
            if (uid->is_string()) result.uid = uid->get<std::string>();
            else if (uid->is_number_unsigned()) result.uid = std::to_string(uid->get<std::uint64_t>());
            else if (uid->is_number_integer() && uid->get<std::int64_t>() > 0) result.uid = std::to_string(uid->get<std::int64_t>());
        }
        const auto name = data.find("userName");
        if (name != data.end() && name->is_string()) result.name = name->get<std::string>();
    }
    static std::string businessMessage(int code)
    {
        switch (code) {
        case 200: return "服务器已接受请求";
        case 220: return "账号凭据失效，请重新登录";
        case 2209: return "二维码已失效，请等待新码";
        case 2240: return "需要短信验证，请点击短信验证";
        // Verified against the live endpoint on 2026-10-16: /user/sms/scanSms
        // answers {"code":242,"msg":"发送短信频繁"}. It is a server-side
        // throttle on the account, not a missing form field and not the local
        // 60-second cooldown.
        case 242: return "发送短信频繁（服务端限流）";
        default: return "库街区拒绝请求（状态码 " + std::to_string(code) + "）";
        }
    }
    Reply post(std::string path, std::map<std::string, std::string> form, std::string stage,
        bool authenticated = true, std::optional<std::string> rawBody = std::nullopt) const
    {
        Reply reply;
        reply.result.stage = std::move(stage);
        const RequestLog log{ reply.result, reply.result.stage };
        KuroRequest request{ std::move(path), {{"source", "android"}, {"version", "2.5.0"}, {"versionCode", "2500"},
            {"devCode", deviceCode_}, {"User-Agent", "okhttp/4.9.0"}, {"Accept", "application/json"},
            {"Content-Type", "application/x-www-form-urlencoded"}}, std::move(form) };
        request.rawBody = std::move(rawBody);
        if (authenticated) request.headers.emplace("token", token_);
        if (request.path == "/user/sms/scanSms") {
            WriteScannerLog("kuro request-shape: method=POST path=/user/sms/scanSms content_type=" +
                request.headers.at("Content-Type") + " body_bytes=" +
                std::to_string(request.rawBody ? request.rawBody->size() : 0) +
                " token=" + (request.headers.find("token") != request.headers.end() ? "present" : "missing") +
                " devCode=" + (request.headers.at("devCode").empty() ? "missing" : "present"));
        }
        KuroHttpResponse response;
        try { response = transport_(request); }
        catch (...) {
            reply.result.error = KuroError::Network;
            reply.result.message = "库街区请求异常，未自动重试";
            return reply;
        }
        reply.result.httpStatus = response.status;
        if (response.timeout || response.networkError) {
            reply.result.error = response.timeout ? KuroError::Timeout : KuroError::Network;
            reply.result.message = response.timeout ? "接口超时，结果未知；请检查登录状态，未自动重试" : "库街区网络请求失败，未自动重试";
            return reply;
        }
        if (response.status < 200 || response.status >= 300) {
            reply.result.error = KuroError::Http;
            reply.result.message = "库街区 HTTP 请求失败（状态码 " + std::to_string(response.status) + "）";
            return reply;
        }
        reply.payload = nlohmann::json::parse(response.body, nullptr, false);
        if (reply.payload.is_discarded()) {
            reply.result.error = KuroError::InvalidJson;
            reply.result.message = "库街区返回非 JSON 数据";
            return reply;
        }
        reply.result.serverMessage = sanitizeMessage(reply.payload);
        // Login SMS uses a boolean success envelope, unlike the game's code=200 API.
        if (reply.result.stage == "getSmsCode") {
            if (!reply.payload.is_object() || !reply.payload.contains("success") || !reply.payload["success"].is_boolean())
                reply.result = protocol(std::move(reply.result), "短信接口响应格式不兼容");
            else {
                reply.result.code = reply.payload["success"].get<bool>() ? 200 : -1;
                reply.result.message = reply.result.code == 200 ? "验证码已发送" : "短信发送被拒绝，请稍后重新验证";
            }
            return reply;
        }
        if (!reply.payload.is_object() || !reply.payload.contains("code") || !reply.payload["code"].is_number_integer()) {
            reply.result = protocol(std::move(reply.result), "库街区响应格式不兼容");
            return reply;
        }
        const auto& code = reply.payload["code"];
        if ((code.is_number_unsigned() && code.get<std::uint64_t>() > static_cast<std::uint64_t>((std::numeric_limits<int>::max)())) ||
            (!code.is_number_unsigned() && (code.get<std::int64_t>() < (std::numeric_limits<int>::min)() || code.get<std::int64_t>() > (std::numeric_limits<int>::max)()))) {
            reply.result = protocol(std::move(reply.result), "库街区业务状态码超出范围");
            return reply;
        }
        reply.result.code = code.get<int>();
        reply.result.message = businessMessage(reply.result.code);
        return reply;
    }
    static KuroHttpResponse postHttp(const KuroRequest& request)
    {
        cpr::Header headers(request.headers.begin(), request.headers.end());
        if (request.rawBody) {
            const auto response = cpr::Post(cpr::Url{ "https://api.kurobbs.com" + request.path }, headers,
                cpr::Body{ *request.rawBody }, cpr::ConnectTimeout{ request.connectTimeoutMs },
                cpr::Timeout{ request.timeoutMs }, cpr::Redirect{ false });
            return { response.status_code, response.text, response.error.code != cpr::ErrorCode::OK,
                response.error.code == cpr::ErrorCode::OPERATION_TIMEDOUT };
        }
        cpr::Payload payload{};
        for (const auto& [key, value] : request.form) payload.Add({ key, value });
        const auto response = cpr::Post(cpr::Url{ "https://api.kurobbs.com" + request.path }, headers, payload,
            cpr::ConnectTimeout{ request.connectTimeoutMs }, cpr::Timeout{ request.timeoutMs }, cpr::Redirect{ false });
        return { response.status_code, response.text, response.error.code != cpr::ErrorCode::OK,
            response.error.code == cpr::ErrorCode::OPERATION_TIMEDOUT };
    }
};
