#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>

#include "ApiDefs.hpp"
#include "UrlQuery.hpp"
#include "KuroScanSession.hpp"

class ScannerBase
{
public:
    bool kuroAccount{false};
    std::uint64_t resultGeneration{0};
    std::atomic_bool authorizationCancelled{false};
    KuroScanSession kuroSession;
    GameType gameType;
    std::string_view scanUrl{};
    std::string_view confirmUrl{};
    std::string lastTicket;
    std::string lastQrCode;
    std::string uid;
    std::string gameToken{};
    std::string mid{};
    std::map<std::string_view, std::function<void()>> setGameTypeByBizKey{
        { "bh3_cn", [this]() {
             gameType = GameType::Honkai3;
             scanUrl = api::mhy::bh3::qrcode_scan;
             confirmUrl = api::mhy::bh3::qrcode_confirm;
         } },
        { "hk4e_cn", [this]() {
             gameType = GameType::Genshin;
             scanUrl = api::mhy::hk4e::qrcode_scan;
             confirmUrl = api::mhy::hk4e::qrcode_confirm;
         } },
        { "hkrpg_cn", [this]() {
             gameType = GameType::HonkaiStarRail;
             scanUrl = api::mhy::hkrpg::qrcode_scan;
             confirmUrl = api::mhy::hkrpg::qrcode_confirm;
         } },
        { "nap_cn", [this]() {
             gameType = GameType::ZenlessZoneZero;
             scanUrl = api::mhy::nap::qrcode_scan;
             confirmUrl = api::mhy::nap::qrcode_confirm;
         } },
    };

    [[nodiscard]] bool setGameTypeByAppId(const std::string_view appId)
    {
        if (appId == "1")
        {
            setGameTypeByBizKey["bh3_cn"]();
            return true;
        }
        if (appId == "4")
        {
            setGameTypeByBizKey["hk4e_cn"]();
            return true;
        }
        if (appId == "8")
        {
            setGameTypeByBizKey["hkrpg_cn"]();
            return true;
        }
        if (appId == "12")
        {
            setGameTypeByBizKey["nap_cn"]();
            return true;
        }
        return false;
    }

    [[nodiscard]] bool parseOfficialQRCode(const std::string_view qrCode, std::string& ticket)
    {
        ticket = getUrlQueryParam(qrCode, "ticket");
        if (ticket.empty())
        {
            ticket = getUrlQueryParam(qrCode, "tk");
        }
        if (ticket.empty())
        {
            return false;
        }

        std::string bizKey = getUrlQueryParam(qrCode, "biz_key");
        if (bizKey.empty())
        {
            bizKey = getUrlQueryParam(qrCode, "game_biz");
        }
        if (auto it = setGameTypeByBizKey.find(bizKey); it != setGameTypeByBizKey.end())
        {
            it->second();
            return true;
        }

        const std::string appId = getUrlQueryParam(qrCode, "app_id");
        return setGameTypeByAppId(appId);
    }
};
