#include "KuroScanSession.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void expect(bool value, const char* label)
{
    if (!value) throw std::runtime_error(label);
}
const std::string qr = "G152#KURO#session-synthetic-offline";
const std::string proof = R"({"lot_number":"offline-lot","captcha_output":"offline-output","pass_token":"offline-proof","gen_time":"123456"})";
const std::string scanDeviceCode = "3dee77f8-1cb9-4cf6-a585-8677a867dab6";
using Clock = std::chrono::steady_clock;

void smsContinuation()
{
    auto time = Clock::time_point{};
    std::vector<KuroRequest> calls;
    bool wrongCode = true;
    KuroScanSession session([&](const KuroRequest& request) {
        calls.push_back(request);
        if (request.path == "/user/auth/roleInfos" || request.path == "/user/sms/scanSms")
            return KuroHttpResponse{200, R"({"code":200})"};
        if (request.form.at("verifyCode").empty()) return KuroHttpResponse{200, R"({"code":2240})"};
        if (wrongCode) return KuroHttpResponse{200, R"({"code":2210})"};
        return KuroHttpResponse{200, R"({"code":200})"};
    }, [&] { return time; });
    session.setAccount("synthetic-account-one");
    expect(session.prepare(qr) == ScanRet::SUCCESS && calls.size() == 1, "precheck before confirmation");
    expect(calls.back().headers.at("devCode") == scanDeviceCode, "precheck uses upstream scan device");
    expect(session.confirm() == ScanRet::KURO_SMS_REQUIRED && calls.size() == 2, "confirmation enters SMS state");
    expect(calls.back().headers.at("devCode") == scanDeviceCode, "confirmation uses upstream scan device");
    expect(session.confirm() == ScanRet::KURO_SMS_REQUIRED && calls.size() == 2, "waiting does not repeat confirmation");
    expect(session.confirm("123456789") == ScanRet::KURO_SMS_REQUIRED && calls.size() == 2, "invalid SMS keeps pending session without I/O");
    expect(!session.sendSms("{}").success() && calls.size() == 2, "SMS requires captcha without consuming cooldown");
    expect(session.sendSms(proof).success() && calls.size() == 3, "explicit verified SMS send");
    expect(calls.back().form.empty() && calls.back().rawBody == "geeTestData=", "scan SMS follows upstream raw body");
    expect(calls.back().headers.at("devCode") == scanDeviceCode, "scan SMS uses upstream scan device");
    expect(session.sendSms(proof).error == KuroError::Cooldown && calls.size() == 3, "SMS send has cooldown");
    time += std::chrono::seconds(59);
    expect(session.sendSms(proof).retryAfterMs == 1000 && calls.size() == 3, "SMS cooldown before boundary");
    time += std::chrono::seconds(1);
    expect(session.sendSms(proof).success() && calls.size() == 4, "SMS send allowed at 60 seconds");
    expect(session.confirm("123456") == ScanRet::FAILURE_2 && calls.size() == 5, "wrong SMS retains pending session");
    wrongCode = false;
    expect(session.confirm("654321") == ScanRet::SUCCESS && calls.size() == 6, "corrected SMS resumes original request");
    expect(calls.back().path == "/user/auth/scanLogin" && calls.back().form.at("qrCode") == qr, "SMS resumes original QR without role lookup");
    expect(session.prepare(qr) == ScanRet::UNKNOW && calls.size() == 6, "successful same QR deduplicated");
    time += std::chrono::seconds(121);
    expect(session.prepare(qr) == ScanRet::SUCCESS && calls.size() == 7, "dedup expires after 180 seconds");
}

void expiryAndCancellation()
{
    auto time = Clock::time_point{std::chrono::seconds(1)};
    std::vector<KuroRequest> calls;
    KuroScanSession session([&](const KuroRequest& request) {
        calls.push_back(request);
        return KuroHttpResponse{200, R"({"code":200})"};
    }, [&] { return time; });
    session.setAccount("synthetic-account-one");
    expect(session.prepare(qr) == ScanRet::SUCCESS, "prepare expiring QR");
    time += std::chrono::seconds(91);
    expect(session.confirm() == ScanRet::QR_EXPIRED && calls.size() == 1, "90 second protection avoids stale submit");
    expect(session.sendSms(proof).error == KuroError::Expired && calls.size() == 1, "expired session sends no SMS");
    expect(session.prepare(qr + "-two") == ScanRet::SUCCESS, "prepare new QR");
    session.cancel();
    expect(session.confirm() != ScanRet::SUCCESS && calls.size() == 2, "cancel invalidates confirmation");
    expect(session.sendSms(proof).error == KuroError::Cancelled && calls.size() == 2, "cancel invalidates SMS");
    session.setAccount("synthetic-account-two");
    expect(session.prepare(qr) == ScanRet::SUCCESS && calls.back().headers.at("token") == "synthetic-account-two", "new account uses its own credential and dedup");
    session.setAccount("synthetic-account-three");
    expect(session.confirm() != ScanRet::SUCCESS && calls.size() == 3, "account switch drops old authorization");
}

void unknownOutcomes()
{
    auto time = Clock::time_point{std::chrono::seconds(1)};
    std::vector<KuroRequest> calls;
    bool precheckTimeout = true;
    KuroScanSession session([&](const KuroRequest& request) {
        calls.push_back(request);
        if (precheckTimeout || request.path == "/user/auth/scanLogin") return KuroHttpResponse{0, {}, true, true};
        return KuroHttpResponse{200, R"({"code":200})"};
    }, [&] { return time; });
    session.setAccount("synthetic-account-one");
    expect(session.prepare(qr) == ScanRet::REQUEST_UNKNOWN && calls.size() == 1, "precheck timeout is unknown");
    expect(session.prepare(qr) == ScanRet::UNKNOW && calls.size() == 1, "precheck timeout not automatically retried");
    precheckTimeout = false;
    expect(session.prepare(qr + "-two") == ScanRet::SUCCESS && calls.size() == 2, "different fresh QR can proceed");
    expect(session.confirm() == ScanRet::REQUEST_UNKNOWN && calls.size() == 3, "confirmation timeout is unknown");
    expect(session.confirm() != ScanRet::SUCCESS && calls.size() == 3, "unknown confirmation consumed exactly once");
    expect(session.prepare(qr + "-two") == ScanRet::UNKNOW && calls.size() == 3, "unknown confirmation QR stays deduplicated");
}

void staleQueuedJobs()
{
    std::vector<KuroRequest> calls;
    KuroScanSession session([&](const KuroRequest& request) {
        calls.push_back(request);
        return KuroHttpResponse{200, request.path == "/user/auth/roleInfos" ? R"({"code":200})" : R"({"code":2240})"};
    });
    session.setAccount("synthetic-account-one", 41);
    expect(session.prepare(qr, 41) == ScanRet::SUCCESS, "first session prepared");
    expect(session.confirm({}, 41) == ScanRet::KURO_SMS_REQUIRED, "first session awaiting SMS");
    session.cancel();
    session.setAccount("synthetic-account-two", 42);
    expect(session.prepare(qr + "-new", 42) == ScanRet::SUCCESS, "second session prepared");
    expect(session.confirm({}, 42) == ScanRet::KURO_SMS_REQUIRED, "second session awaiting SMS");
    const auto before = calls.size();
    expect(session.confirm("123456", 41) == ScanRet::REQUEST_CANCELLED, "old queued SMS rejected");
    expect(session.sendSms(proof, 41).error == KuroError::Cancelled, "old captcha proof cannot send on new account");
    expect(session.prepare(qr + "-stale", 41) == ScanRet::REQUEST_CANCELLED && calls.size() == before,
        "old generation sends no request on new account");
}
}

int main()
{
    try {
        smsContinuation();
        expiryAndCancellation();
        unknownOutcomes();
        staleQueuedJobs();
        std::cout << "Kuro session regression passed (synthetic clock and transport)\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
