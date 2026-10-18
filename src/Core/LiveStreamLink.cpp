#include "LiveStreamLink.h"

#include <format>
#include <fstream>
#include <regex>
#include <random>
#include <chrono>
#include <cctype>
#include <utility>

#include <nlohmann/json.hpp>
#include <cpr/cpr.h>
#include <mutex>
#include <vector>
#include "ScannerLog.hpp"

namespace
{
const std::string kBiliUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "Chrome/126.0.0.0 Safari/537.36";

std::string Trim(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    {
        value.pop_back();
    }
    return value;
}

std::string NormalizeBiliRoomID(std::string value)
{
    value = Trim(std::move(value));
    std::smatch match;
    const std::regex liveUrlPattern(R"(live\.bilibili\.com/(?:blanc/)?(\d+))", std::regex::icase);
    if (std::regex_search(value, match, liveUrlPattern) && match.size() > 1)
    {
        return match[1].str();
    }
    const std::regex queryPattern(R"((?:[?&](?:room_id|id)=)(\d+))", std::regex::icase);
    if (std::regex_search(value, match, queryPattern) && match.size() > 1)
    {
        return match[1].str();
    }
    return value;
}

cpr::Header BiliHeaders(const std::string& roomID)
{
    const std::string referer = roomID.empty()
        ? "https://live.bilibili.com/"
        : std::format("https://live.bilibili.com/{}", roomID);
    return cpr::Header{
        { "User-Agent", kBiliUserAgent },
        { "Referer", referer },
        { "Origin", "https://live.bilibili.com" },
        { "Accept", "application/json, text/plain, */*" },
    };
}

std::string BuildBiliStreamUrl(const nlohmann::json& codec)
{
    if (!codec.contains("base_url") || !codec.contains("url_info") || !codec["url_info"].is_array())
    {
        return "";
    }
    const std::string baseUrl = codec["base_url"].get<std::string>();
    for (const auto& urlInfo : codec["url_info"])
    {
        if (!urlInfo.contains("host") || !urlInfo.contains("extra"))
        {
            continue;
        }
        const std::string host = urlInfo["host"].get<std::string>();
        const std::string extra = urlInfo["extra"].get<std::string>();
        if (!host.empty() && !baseUrl.empty())
        {
            return host + baseUrl + extra;
        }
    }
    return "";
}

std::string NormalizeDouyinRoomID(std::string value)
{
    value = Trim(std::move(value));
    if (value.empty()) return value;

    std::smatch match;
    const std::regex shortUrlPattern(R"(https?://v\.douyin\.com/[a-zA-Z0-9]+)", std::regex::icase);
    if (std::regex_search(value, match, shortUrlPattern))
    {
        std::string shortUrl = match[0].str();
        auto redirectResp = cpr::Get(
            cpr::Url{ shortUrl },
            cpr::Header{ { "User-Agent", kBiliUserAgent } },
            cpr::Redirect{ true },
            cpr::Timeout{ 5000 },
            cpr::ConnectTimeout{ 3000 }
        );
        if (!redirectResp.url.str().empty())
        {
            value = redirectResp.url.str();
        }
    }

    const std::regex liveUrlPattern(R"(live\.douyin\.com/([a-zA-Z0-9_]+))", std::regex::icase);
    if (std::regex_search(value, match, liveUrlPattern) && match.size() > 1)
    {
        return match[1].str();
    }

    const std::regex queryPattern(R"((?:[?&](?:web_rid|room_id|id)=)([a-zA-Z0-9_]+))", std::regex::icase);
    if (std::regex_search(value, match, queryPattern) && match.size() > 1)
    {
        return match[1].str();
    }

    size_t qPos = value.find('?');
    if (qPos != std::string::npos)
    {
        value = value.substr(0, qPos);
    }
    while (!value.empty() && (value.back() == '/' || value.back() == '\\'))
    {
        value.pop_back();
    }
    size_t slashPos = value.find_last_of("/\\");
    if (slashPos != std::string::npos)
    {
        value = value.substr(slashPos + 1);
    }

    return value;
}

std::string s_douyinTtwid;
std::mutex s_douyinTtwidMutex;

std::string FetchDouyinTtwid(const std::string& roomID)
{
    std::string targetUrl = roomID.empty()
        ? "https://live.douyin.com/"
        : std::format("https://live.douyin.com/{}", roomID);

    auto resp = cpr::Get(
        cpr::Url{ targetUrl },
        cpr::Header{
            { "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36" },
            { "Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,image/webp,*/*;q=0.8" },
            { "Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8" }
        },
        cpr::Timeout{ 5000 },
        cpr::ConnectTimeout{ 3000 }
    );

    std::smatch match;
    const std::regex ttwidRegex(R"(ttwid=([^;\s]+))", std::regex::icase);
    if (std::regex_search(resp.raw_header, match, ttwidRegex) && match.size() > 1)
    {
        return match[1].str();
    }

    for (const auto& [k, v] : resp.header)
    {
        if (std::regex_search(v, match, ttwidRegex) && match.size() > 1)
        {
            return match[1].str();
        }
    }

    return "";
}

std::string GetOrRefreshDouyinTtwid(const std::string& roomID, bool forceRefresh)
{
    std::lock_guard lock(s_douyinTtwidMutex);
    if (!forceRefresh && !s_douyinTtwid.empty())
    {
        return s_douyinTtwid;
    }

    std::string ttwid = FetchDouyinTtwid(roomID);
    if (!ttwid.empty())
    {
        s_douyinTtwid = ttwid;
        WriteScannerLog("douyin: fetched new ttwid successfully");
    }
    else
    {
        WriteScannerLog("douyin: failed to fetch ttwid");
    }
    return s_douyinTtwid;
}
}

LiveBili::LiveBili(const std::string& roomID) :
    roomID(NormalizeBiliRoomID(roomID))
{
}

LiveStreamInfo LiveBili::GetLiveStreamInfo()
{
    // 获取房间初始化信息
    auto r = cpr::Get(
        cpr::Url{ std::format("{}?id={}", api::live::bili::room_init.c_str(), roomID) },
        BiliHeaders(roomID));
    if (r.error || r.status_code != 200 || r.text.empty())
    {
        return { LiveStreamStatus::Error, "" };
    }
    try
    {
        auto roomInfo = nlohmann::json::parse(r.text, nullptr, false);
        if (roomInfo.is_discarded())
        {
            return { LiveStreamStatus::Error, "" };
        }
        int code = roomInfo["code"].get<int>();
        if (code == 60004)
        {
            return { LiveStreamStatus::Absent, "" };
        }
        if (code != 0)
        {
            return { LiveStreamStatus::Error, "" };
        }
        const auto& data = roomInfo["data"];
        int liveStatus = data["live_status"].get<int>();
        if (liveStatus != 1)
        {
            return { LiveStreamStatus::NotLive, "" };
        }
        // 更新真实房间ID
        if (data.contains("room_id"))
        {
            realRoomID = std::to_string(data["room_id"].get<long long>());
        }

        std::string link = GetLinkByRealRoomID(realRoomID);
        if (link.empty())
        {
            return { LiveStreamStatus::Error, "" };
        }
        return { LiveStreamStatus::Normal, link };
    }
    catch (const nlohmann::json::exception& e)
    {
        return { LiveStreamStatus::Error, "" };
    }
}

std::string LiveBili::GetLinkByRealRoomID(const std::string& realRoomID)
{
    const cpr::Parameters params = {
#if 0
        appkey:iVGUTjsxvpLeuDCf
        build:6215200
        c_locale:zh_CN
#endif
        { "codec", "0,1" },
#if 0
        device:web
        device_name:VTR-AL00
        dolby:1
#endif
        { "format", "0,1,2" },
#if 0
        free_type:0
        http:1
        mask:0
        mobi_app:web
        network:wifi
        no_playurl:0
#endif
        { "only_audio", "0" },
        { "only_video", "0" },
#if 0
        //TODO platform 会影响下载时使用的referer
        {"platform", "h5" },
        play_type:0
#endif
        { "platform", "web" },
        { "protocol", "0,1" },
        { "ptype", "8" },
        { "qn", "10000" },
        { "room_id", realRoomID },
#if 0
        s_locale:zh_CN
        statistics:{\"appId\":1,\"platform\":3,\"version\":\"6.21.5\",\"abtest\":\"\"}
#endif
    };
    return GetStreamUrl(params);
}

std::string LiveBili::GetStreamUrl(const cpr::Parameters param)
{
    auto r = cpr::Get(cpr::Url{ api::live::bili::v2_play_info }, param, BiliHeaders(realRoomID));
    if (r.error || r.status_code != 200 || r.text.empty())
    {
        return "";
    }
    try
    {
        auto playInfo = nlohmann::json::parse(r.text, nullptr, false);
        if (playInfo.is_discarded())
        {
            return "";
        }
        if (!playInfo.contains("code") || playInfo["code"].get<int>() != 0)
        {
            return "";
        }
        const auto& data = playInfo["data"];
        const auto& playurl_info = data["playurl_info"];
        const auto& playurl = playurl_info["playurl"];
        if (!playurl.contains("stream") || !playurl["stream"].is_array())
        {
            return "";
        }

        std::string fallbackUrl;
        for (const auto& stream : playurl["stream"])
        {
            const bool preferredProtocol = stream.value("protocol_name", "") == "http_stream";
            if (!stream.contains("format") || !stream["format"].is_array())
            {
                continue;
            }
            for (const auto& format : stream["format"])
            {
                const bool preferredFormat = format.value("format_name", "") == "flv";
                if (!format.contains("codec") || !format["codec"].is_array())
                {
                    continue;
                }
                for (const auto& codec : format["codec"])
                {
                    const std::string streamUrl = BuildBiliStreamUrl(codec);
                    if (streamUrl.empty())
                    {
                        continue;
                    }
                    if (fallbackUrl.empty())
                    {
                        fallbackUrl = streamUrl;
                    }
                    if (preferredProtocol && preferredFormat && codec.value("codec_name", "") == "avc")
                    {
                        return streamUrl;
                    }
                }
            }
        }
        return fallbackUrl;
    }
    catch (const nlohmann::json::exception& e)
    {
        return "";
    }
}

LiveDouyin::LiveDouyin(const std::string& roomID) :
    m_roomID(NormalizeDouyinRoomID(roomID))
{
}

LiveStreamInfo LiveDouyin::GetLiveStreamInfo()
{
    if (m_roomID.empty())
    {
        WriteScannerLog("douyin: empty roomID");
        return { LiveStreamStatus::Absent, "" };
    }

    auto fetchInfo = [this](bool forceRefreshCookie) -> std::pair<cpr::Response, std::string> {
        std::string ttwid = GetOrRefreshDouyinTtwid(m_roomID, forceRefreshCookie);

        const std::string user_agent =
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

        cpr::Header headers = {
            { "User-Agent", user_agent },
            { "Referer", "https://live.douyin.com/" },
            { "Accept", "application/json, text/plain, */*" },
            { "Accept-Language", "zh-CN,zh;q=0.9,en;q=0.8" }
        };
        if (!ttwid.empty())
        {
            headers.emplace("Cookie", "ttwid=" + ttwid);
        }

        std::string params =
            "aid=6383&app_name=douyin_web&live_id=1&device_platform=web&"
            "browser_language=zh-CN&browser_platform=Win32&browser_name=Edge&"
            "browser_version=139.0.0.0&is_need_double_stream=false&web_rid=" +
            m_roomID;
        const std::string url = std::string(api::live::douyin::room) + params;

        auto response = cpr::Get(cpr::Url{ url }, headers, cpr::Timeout{ 6000 }, cpr::ConnectTimeout{ 4000 });
        return { response, ttwid };
    };

    try
    {
        auto [response, ttwid] = fetchInfo(false);

        // 如果没有 cookie 或者返回空（被抖音风控拦截），强制刷新 cookie 重试一次
        if (response.error || response.status_code != 200 || response.text.empty())
        {
            WriteScannerLog(std::format("douyin: initial request status={} empty={}, retrying with fresh cookie",
                response.status_code, response.text.empty()));
            std::tie(response, ttwid) = fetchInfo(true);
        }

        if (response.error || response.status_code != 200 || response.text.empty())
        {
            WriteScannerLog(std::format("douyin: request failed after retry status={} empty={}",
                response.status_code, response.text.empty()));
            return { LiveStreamStatus::Error, "" };
        }

        auto streamInfo = nlohmann::json::parse(response.text, nullptr, false);
        if (streamInfo.is_discarded())
        {
            WriteScannerLog("douyin: json parse failed");
            return { LiveStreamStatus::Error, "" };
        }

        int status_code = streamInfo.value("status_code", -1);
        if (status_code != 0)
        {
            WriteScannerLog(std::format("douyin: api status_code={}", status_code));
            return { LiveStreamStatus::Absent, "" };
        }

        const auto& dataArr = streamInfo["data"]["data"];
        if (!dataArr.is_array() || dataArr.empty())
        {
            WriteScannerLog("douyin: room data array empty");
            return { LiveStreamStatus::Absent, "" };
        }

        const auto& data = dataArr[0];
        int status = data.value("status", 4);
        // 抖音 status == 2 代表是开播的状态
        if (status == 2)
        {
            std::string link = GetStreamLinkFromResponse(data);
            if (link.empty())
            {
                WriteScannerLog("douyin: stream link extraction empty");
                return { LiveStreamStatus::Error, "" };
            }
            WriteScannerLog("douyin: stream link retrieved successfully");
            return { LiveStreamStatus::Normal, link };
        }
        // status == 4 代表未开播
        else if (status == 4)
        {
            WriteScannerLog("douyin: room is not live (status=4)");
            return { LiveStreamStatus::NotLive, "" };
        }

        WriteScannerLog(std::format("douyin: unknown room status={}", status));
        return { LiveStreamStatus::Error, "" };
    }
    catch (const std::exception& e)
    {
        WriteScannerLog(std::format("douyin: exception in GetLiveStreamInfo: {}", e.what()));
        return { LiveStreamStatus::Error, "" };
    }
    catch (...)
    {
        WriteScannerLog("douyin: unknown exception in GetLiveStreamInfo");
        return { LiveStreamStatus::Error, "" };
    }
}

std::string LiveDouyin::GetStreamLinkFromResponse(const nlohmann::json& data)
{
    try
    {
        if (!data.contains("stream_url") || !data["stream_url"].is_object())
        {
            return "";
        }
        const auto& stream_url = data["stream_url"];

        // 策略1：优先从 live_core_sdk_data 解析不同清晰度的流 (origin -> uhd -> hd -> sd -> ld)
        if (stream_url.contains("live_core_sdk_data") && stream_url["live_core_sdk_data"].is_object())
        {
            const auto& sdk_data = stream_url["live_core_sdk_data"];
            if (sdk_data.contains("pull_data") && sdk_data["pull_data"].contains("stream_data"))
            {
                std::string stream_data_str = sdk_data["pull_data"]["stream_data"].get<std::string>();
                auto streamData = nlohmann::json::parse(stream_data_str, nullptr, false);
                if (!streamData.is_discarded() && streamData.contains("data") && streamData["data"].is_object())
                {
                    const auto& resolutions = streamData["data"];
                    const std::vector<std::string> preferredQualities = { "origin", "uhd", "hd", "sd", "ld", "md" };
                    for (const auto& quality : preferredQualities)
                    {
                        if (resolutions.contains(quality) && resolutions[quality].contains("main"))
                        {
                            const auto& mainObj = resolutions[quality]["main"];
                            if (mainObj.contains("flv") && mainObj["flv"].is_string())
                            {
                                std::string flvUrl = mainObj["flv"].get<std::string>();
                                if (!flvUrl.empty()) return flvUrl;
                            }
                        }
                    }
                    for (auto it = resolutions.begin(); it != resolutions.end(); ++it)
                    {
                        if (it.value().contains("main") && it.value()["main"].contains("flv") && it.value()["main"]["flv"].is_string())
                        {
                            std::string flvUrl = it.value()["main"]["flv"].get<std::string>();
                            if (!flvUrl.empty()) return flvUrl;
                        }
                    }
                    for (const auto& quality : preferredQualities)
                    {
                        if (resolutions.contains(quality) && resolutions[quality].contains("main"))
                        {
                            const auto& mainObj = resolutions[quality]["main"];
                            if (mainObj.contains("hls") && mainObj["hls"].is_string())
                            {
                                std::string hlsUrl = mainObj["hls"].get<std::string>();
                                if (!hlsUrl.empty()) return hlsUrl;
                            }
                        }
                    }
                }
            }
        }

        // 策略2：从 pull_datas 解析 (如果是双屏直播)
        if (stream_url.contains("pull_datas") && stream_url["pull_datas"].is_object())
        {
            const auto& pullDatas = stream_url["pull_datas"];
            if (!pullDatas.empty())
            {
                auto doubleScreenStreams = pullDatas.begin().value();
                if (doubleScreenStreams.contains("stream_data"))
                {
                    std::string stream_data_str = doubleScreenStreams["stream_data"].get<std::string>();
                    auto streamData = nlohmann::json::parse(stream_data_str, nullptr, false);
                    if (!streamData.is_discarded() && streamData.contains("data"))
                    {
                        const auto& resData = streamData["data"];
                        if (resData.contains("origin") && resData["origin"].contains("main") && resData["origin"]["main"].contains("flv"))
                        {
                            std::string flvUrl = resData["origin"]["main"]["flv"].get<std::string>();
                            if (!flvUrl.empty()) return flvUrl;
                        }
                    }
                }
            }
        }

        // 策略3：直接从 flv_pull_url 读取 (例如 FULL_HD1, HD1, SD1)
        if (stream_url.contains("flv_pull_url") && stream_url["flv_pull_url"].is_object())
        {
            const auto& flvMap = stream_url["flv_pull_url"];
            const std::vector<std::string> keys = { "FULL_HD1", "HD1", "SD1", "SD2" };
            for (const auto& k : keys)
            {
                if (flvMap.contains(k) && flvMap[k].is_string())
                {
                    std::string url = flvMap[k].get<std::string>();
                    if (!url.empty()) return url;
                }
            }
            for (auto it = flvMap.begin(); it != flvMap.end(); ++it)
            {
                if (it.value().is_string() && !it.value().get<std::string>().empty())
                {
                    return it.value().get<std::string>();
                }
            }
        }

        // 策略4：从 hls_pull_url_map 读取
        if (stream_url.contains("hls_pull_url_map") && stream_url["hls_pull_url_map"].is_object())
        {
            const auto& hlsMap = stream_url["hls_pull_url_map"];
            for (auto it = hlsMap.begin(); it != hlsMap.end(); ++it)
            {
                if (it.value().is_string() && !it.value().get<std::string>().empty())
                {
                    return it.value().get<std::string>();
                }
            }
        }

        return "";
    }
    catch (...)
    {
        return "";
    }
}

LiveStreamInfo GetLiveInfo(const LivePlatform platform, const std::string& roomID)
{
    switch (platform)
    {
    case LivePlatform::Douyin:
        return GetLiveInfo<LiveDouyin>(roomID);
    case LivePlatform::BiliBili:
        return GetLiveInfo<LiveBili>(roomID);
    default:
        return LiveStreamInfo{ LiveStreamStatus::Error };
    }
}
