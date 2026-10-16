#pragma once

#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <atomic>
#include "KuroApi.hpp"
#include "ApiDefs.hpp"

// All operations are serialized; cancellation invalidates UI continuations by generation.
class KuroScanSession
{
    using Clock = std::chrono::steady_clock;
    // The upstream scan client uses one stable device identifier for the whole
    // QR session. Keep it separate from the temporary identifier used by the
    // phone-login flow so all scan endpoints see the same device.
    static constexpr std::string_view scanDeviceCode = "3dee77f8-1cb9-4cf6-a585-8677a867dab6";
    mutable std::mutex mutex;
    KuroApi::Transport transport;
    std::function<Clock::time_point()> now;
    std::string token;
    std::string qr;
    std::map<std::string, Clock::time_point> attempted;
    Clock::time_point preparedAt{};
    std::optional<Clock::time_point> lastSms;
    bool awaitingSms{false};
    bool ready{false};
    std::atomic_uint64_t epoch{0};
    std::uint64_t serial{0};

    bool current(std::uint64_t expected) const { return expected && expected == epoch.load(); }

    static ScanRet result(const KuroResponse& response)
    {
        if (response.success()) return ScanRet::SUCCESS;
        if (response.code == 2240) return ScanRet::KURO_SMS_REQUIRED;
        if (response.code == 220) return ScanRet::ACCOUNT_EXPIRED;
        if (response.code == 2209) return ScanRet::QR_EXPIRED;
        if (response.code < 0) return ScanRet::REQUEST_UNKNOWN;
        return ScanRet::FAILURE_2;
    }

public:
    explicit KuroScanSession(KuroApi::Transport http = {},
        std::function<Clock::time_point()> clock = [] { return Clock::now(); })
        : transport(std::move(http)), now(std::move(clock)) {}

    std::uint64_t generation() const { return epoch.load(); }

    void setAccount(const std::string& value, std::uint64_t generation = 0)
    {
        std::lock_guard lock(mutex);
        if (token != value) { attempted.clear(); lastSms.reset(); }
        token = value;
        qr.clear();
        ready = awaitingSms = false;
        if (generation) serial = (std::max)(serial, generation);
        epoch.store(generation ? generation : ++serial);
    }

    // The caller filters QR contents before calling. Record before I/O, including timeouts.
    ScanRet prepare(const std::string& value, std::uint64_t generation = 0)
    {
        const auto expected = generation ? generation : epoch.load();
        std::lock_guard lock(mutex);
        if (!current(expected)) return ScanRet::REQUEST_CANCELLED;
        const auto currentTime = now();
        for (auto it = attempted.begin(); it != attempted.end();)
            if (currentTime - it->second > std::chrono::seconds(180)) it = attempted.erase(it); else ++it;
        if (!KuroApi::isGameQr(value) || attempted.contains(value)) return ScanRet::UNKNOW;
        attempted[value] = currentTime;
        qr = value;
        preparedAt = currentTime;
        const auto response = KuroApi(token, transport, std::string(scanDeviceCode)).roleInfos(qr);
        if (!current(expected)) return ScanRet::REQUEST_CANCELLED;
        ready = response.success();
        awaitingSms = response.code == 2240;
        if (ready || awaitingSms) return ScanRet::SUCCESS;
        qr.clear();
        return result(response);
    }

    ScanRet confirm(const std::string& sms = {}, std::uint64_t generation = 0)
    {
        const auto expected = generation ? generation : epoch.load();
        std::lock_guard lock(mutex);
        if (!current(expected)) return ScanRet::REQUEST_CANCELLED;
        if (qr.empty() || now() - preparedAt > std::chrono::seconds(90))
        {
            qr.clear(); ready = awaitingSms = false;
            return ScanRet::QR_EXPIRED;
        }
        if (awaitingSms && sms.empty()) return ScanRet::KURO_SMS_REQUIRED;
        if (!ready && !awaitingSms) return ScanRet::REQUEST_CANCELLED;
        if (!sms.empty() && !awaitingSms) return ScanRet::REQUEST_CANCELLED;
        if (!sms.empty() && (sms.size() < 4 || sms.size() > 8 ||
            !std::all_of(sms.begin(), sms.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })))
            return ScanRet::KURO_SMS_REQUIRED;
        // Consume the ordinary attempt before sending. Never automatically retry unknown outcomes.
        ready = false;
        const auto response = KuroApi(token, transport, std::string(scanDeviceCode)).scanLogin(qr, sms, false);
        if (!current(expected)) return ScanRet::REQUEST_CANCELLED;
        const auto status = result(response);
        awaitingSms = status == ScanRet::KURO_SMS_REQUIRED || (!sms.empty() && status == ScanRet::FAILURE_2);
        if (!awaitingSms) qr.clear();
        return status;
    }

    KuroResponse sendSms(std::string_view proof, std::uint64_t generation = 0)
    {
        const auto expected = generation ? generation : epoch.load();
        std::lock_guard lock(mutex);
        KuroResponse rejected;
        rejected.stage = "scanSms";
        if (!current(expected)) { rejected.error = KuroError::Cancelled; return rejected; }
        if (!awaitingSms || qr.empty() || now() - preparedAt > std::chrono::seconds(90))
        { rejected.error = KuroError::Expired; return rejected; }
        if (KuroCaptchaProof::normalize(proof).empty())
        { rejected.error = KuroError::Input; rejected.message = "请重新完成人机验证"; return rejected; }
        if (lastSms && now() - *lastSms < std::chrono::seconds(60)) {
            rejected.error = KuroError::Cooldown;
            rejected.retryAfterMs = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::seconds(60) - (now() - *lastSms)).count());
            return rejected;
        }
        lastSms = now();
        auto response = KuroApi(token, transport, std::string(scanDeviceCode)).sendScanSms(proof);
        if (current(expected)) return response;
        rejected.error = KuroError::Cancelled;
        return rejected;
    }

    void cancel()
    {
        // UI cancellation never waits for the network mutex. Stale jobs fail their expected epoch.
        epoch.store(0);
    }
};
