#include "AccountPlatform.hpp"
#include "KuroApi.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void expect(bool condition, const char* label)
{
    if (!condition) throw std::runtime_error(label);
}

const std::string qr = "G152#KURO#synthetic-offline-qr";

void authentication()
{
    std::vector<KuroRequest> seen;
    KuroApi api("synthetic-token", [&](const KuroRequest& request) {
        seen.push_back(request);
        if (request.path == "/user/getPrivateSet") return KuroHttpResponse{200, R"({"code":200,"data":{}})"};
        return KuroHttpResponse{200, R"({"code":200,"data":{"mine":{"userId":12345,"userName":"test"}}})"};
    });
    const auto result = api.checkToken();
    expect(result.success() && result.uid == "12345" && result.name == "test", "account metadata decoding");
    expect(seen.size() == 2 && seen.front().path == "/user/getPrivateSet", "credential check precedes public profile");
    expect(seen.front().headers.at("token") == "synthetic-token", "authentication header");
    expect(seen.back().form.at("viewUserId").empty(), "self profile request");

    int calls = 0;
    KuroApi rejected("synthetic-token", [&](const KuroRequest&) {
        ++calls;
        return KuroHttpResponse{200, R"({"code":220,"msg":"synthetic-secret"})"};
    });
    const auto invalid = rejected.checkToken();
    expect(!invalid.success() && invalid.code == 220 && calls == 1, "invalid token does not fetch public profile");
    expect(invalid.message.find("synthetic-secret") == std::string::npos, "server messages not disclosed");

    KuroApi login({}, [&](const KuroRequest& request) {
        expect(request.path == "/user/sdkLogin", "login path");
        expect(request.headers.count("token") == 0, "login has no stale credential");
        expect(request.form.at("mobile") == "13800000000" && request.form.at("code") == "123456", "phone login fields");
        return KuroHttpResponse{200, R"({"code":200,"data":{"token":"synthetic-new-token","userId":"123"}})"};
    });
    expect(login.sdkLogin("13800000000", "123456").token == "synthetic-new-token", "phone login extracts token");
    expect(!login.sdkLogin("1", "123456").success(), "invalid phone rejected locally");
    KuroApi missing({}, [](const KuroRequest&) { return KuroHttpResponse{200, R"({"code":200,"data":{}})"}; });
    expect(!missing.sdkLogin("13800000000", "123456").success(), "HTTP and business success without token rejected");
}

void scanFlow()
{
    std::vector<KuroRequest> seen;
    KuroApi api("synthetic-token", [&](const KuroRequest& request) {
        seen.push_back(request);
        return KuroHttpResponse{200, R"({"code":2240})"};
    });
    expect(KuroApi::isGameQr(qr), "known game signature");
    expect(!KuroApi::isGameQr("G152#KURO"), "short QR rejected");
    expect(!KuroApi::isGameQr(qr + std::string(1, '\0')), "NUL QR rejected");
    expect(!KuroApi::isGameQr(std::string(4097, 'a') + qr), "oversized QR rejected");
    expect(!api.roleInfos("unknown-game").success() && seen.empty(), "unrecognized code never reaches transport");
    expect(api.roleInfos(qr).code == 2240, "precheck business state");
    expect(seen.back().path == "/user/auth/roleInfos", "role precheck path");
    expect(api.scanLogin(qr).code == 2240, "confirmation SMS state");
    expect(seen.back().path == "/user/auth/scanLogin" && seen.back().form.at("autoLogin") == "false", "confirmation defaults");
    const auto before = seen.size();
    expect(api.scanLogin(qr, "123456").code == 2240 && seen.size() == before + 1, "SMS resume makes one confirmation request");
    expect(seen.back().form.at("verifyCode") == "123456", "SMS continuation preserves code");
    const auto beforeSms = seen.size();
    expect(!api.sendScanSms("").success() && !api.sendScanSms("{}").success() && seen.size() == beforeSms, "scan SMS without proof never reaches transport");
    const std::string proof = R"({"lot_number":"offline-lot","captcha_output":"offline-output","pass_token":"offline-proof","gen_time":"123456"})";
    api.sendScanSms(proof);
    expect(seen.back().path == "/user/sms/scanSms" && seen.back().form.empty() &&
        seen.back().rawBody == "geeTestData=", "scan SMS follows upstream raw body");
    expect(seen.back().headers.at("Content-Type") == "application/x-www-form-urlencoded", "scan SMS content type");
    expect(seen.back().headers.at("token") == "synthetic-token" && seen.back().form.count("mobile") == 0, "scan SMS bound to current authenticated account");
    expect(seen.back().connectTimeoutMs == 4000 && seen.back().timeoutMs == 10000, "bounded transport timeouts");
}

void loginSms()
{
    const auto proof = nlohmann::json{{"lot_number", "offline-lot"}, {"captcha_output", "offline-output"},
        {"pass_token", "offline-proof"}, {"gen_time", "123456"},
        {"captcha_id", std::string(KuroCaptchaProof::captchaId)}, {"unexpected", "private-extra"}}.dump();
    const auto normalized = KuroCaptchaProof::normalize(proof);
    expect(!normalized.empty() && normalized.find("private-extra") == std::string::npos, "proof whitelists required fields");
    for (const auto& bad : {std::string{}, std::string("not-json"), std::string("[]"), std::string("{}"),
            std::string(R"({"lot_number":123})"), std::string(32769, 'x')})
        expect(KuroCaptchaProof::normalize(bad).empty(), "malformed proof rejected");
    auto wrongId = nlohmann::json::parse(proof);
    wrongId["captcha_id"] = "another-application";
    expect(KuroCaptchaProof::normalize(wrongId.dump()).empty(), "foreign application proof rejected");
    wrongId = nlohmann::json::parse(proof);
    wrongId["pass_token"] = std::string("abc\0def", 7);
    expect(KuroCaptchaProof::normalize(wrongId.dump()).empty(), "NUL proof rejected");

    int calls = 0;
    KuroApi api("stale-token", [&](const KuroRequest& request) {
        ++calls;
        expect(request.path == "/user/getSmsCode", "login SMS endpoint differs from scan SMS");
        expect(request.headers.count("token") == 0, "login SMS does not send an existing credential");
        expect(request.headers.at("devCode") == "offline-device", "SMS device identifier retained");
        expect(request.form.at("mobile") == "13800000000" && request.form.at("geeTestData") == normalized, "verified SMS payload");
        return KuroHttpResponse{200, R"({"success":true})"};
    }, "offline-device");
    expect(!api.sendLoginSms("invalid", proof).success() && calls == 0, "invalid phone never requests SMS");
    expect(!api.sendLoginSms("13800000000", "{}").success() && calls == 0, "SMS requires completed captcha");
    expect(api.sendLoginSms("13800000000", proof).success() && calls == 1, "boolean SMS success accepted");
    for (const auto& body : {R"({"success":false,"code":200,"msg":"private-server-text"})",
            R"({"success":"true"})", R"({"success":1})", R"({"code":200})", "[]", "not-json"}) {
        int attempts = 0;
        KuroApi failure({}, [&](const KuroRequest&) { ++attempts; return KuroHttpResponse{200, body}; });
        const auto result = failure.sendLoginSms("13800000000", proof);
        expect(!result.success() && attempts == 1, "SMS rejects malformed or refused responses without retry");
        expect(result.message.find("private-server-text") == std::string::npos, "SMS diagnostics redact server response");
    }
}

void failures()
{
    struct Case { KuroHttpResponse reply; KuroError error; };
    const std::vector<Case> cases{
        {{200, R"({"code":200})", true, true}, KuroError::Timeout},
        {{0, "synthetic-secret", true, false}, KuroError::Network},
        {{503, R"({"code":200})"}, KuroError::Http},
        {{302, R"({"code":200})"}, KuroError::Http},
        {{200, "<html>synthetic-secret</html>"}, KuroError::InvalidJson},
        {{200, R"({"code":true})"}, KuroError::Protocol},
        {{200, R"({"code":200.0})"}, KuroError::Protocol},
        {{200, R"({"code":18446744073709551615})"}, KuroError::Protocol},
        {{200, R"({"code":-9223372036854775808})"}, KuroError::Protocol},
        {{200, "[]"}, KuroError::Protocol},
    };
    for (const auto& item : cases) {
        int calls = 0;
        KuroApi api("synthetic-token", [&](const KuroRequest&) { ++calls; return item.reply; });
        const auto result = api.scanLogin(qr);
        expect(!result.success() && result.error == item.error && calls == 1, "failure classified without automatic retry");
        expect(result.message.find("synthetic-secret") == std::string::npos, "response body stays private");
    }
    KuroApi thrown("synthetic-token", [](const KuroRequest&) -> KuroHttpResponse { throw std::runtime_error("synthetic-secret"); });
    const auto result = thrown.scanLogin(qr);
    expect(result.error == KuroError::Network && result.message.find("synthetic-secret") == std::string::npos, "transport exceptions redacted");
}

void providers()
{
    expect(accountPlatform(nlohmann::json{{"type", "官服"}}) == AccountPlatform::Mhy, "legacy official account");
    expect(accountPlatform(nlohmann::json{{"type", "库街区官服"}}) == AccountPlatform::Kuro, "legacy Kuro account");
    expect(accountPlatform(nlohmann::json{{"provider", "kuro"}, {"type", "官服"}}) == AccountPlatform::Kuro, "stable provider wins");
    expect(accountPlatform(nlohmann::json{{"provider", "unknown"}, {"type", "官服"}}) == AccountPlatform::Unsupported, "unknown provider not guessed");
    expect(accountPlatform(nlohmann::json{{"provider", 42}, {"type", "官服"}}) == AccountPlatform::Unsupported, "malformed provider rejected");
    expect(accountPlatform(nlohmann::json{{"type", "库街区其他服"}}) == AccountPlatform::Unsupported, "unsupported Kuro type not guessed");
}
}

void diagnostics()
{
    // Live evidence from 2026-10-16: /user/sms/scanSms answered
    // {"code":242,"msg":"发送短信频繁"} while the request shape already matched
    // both upstream clients. Keep that reason visible in diagnostics so the
    // next investigation does not have to guess from the business code.
    const std::string proof = R"({"lot_number":"offline-lot","captcha_output":"offline-output","pass_token":"offline-proof","gen_time":"123456"})";
    KuroApi throttled("synthetic-token", [&](const KuroRequest&) {
        return KuroHttpResponse{200, R"({"code":242,"msg":"发送短信频繁"})"};
    });
    const auto refused = throttled.sendScanSms(proof);
    expect(refused.code == 242 && refused.serverMessage == "发送短信频繁", "server refusal reason captured");
    expect(refused.message.find("频繁") != std::string::npos, "242 explained as server-side throttling");

    // The captured text stays bounded and free of control characters, and the
    // user-facing message never becomes a verbatim copy of the server reply.
    const std::string noisyBody = R"({"code":500,"msg":")" + std::string(400, 'x') + R"(\u0001\u0002"})";
    KuroApi noisy("synthetic-token", [&](const KuroRequest&) { return KuroHttpResponse{200, noisyBody}; });
    const auto bounded = noisy.sendScanSms(proof);
    expect(bounded.serverMessage.size() == 200, "server text bounded");
    expect(bounded.serverMessage.find('\x01') == std::string::npos &&
        bounded.serverMessage.find('\x02') == std::string::npos, "control characters stripped");
    expect(bounded.message.find("xxxx") == std::string::npos, "user message is not the raw server reply");
}

int main()
{
    try {
        authentication();
        loginSms();
        scanFlow();
        failures();
        providers();
        diagnostics();
        std::cout << "Kuro protocol regression passed (injected transport, no live account requests)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
