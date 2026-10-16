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
    m_roomID(roomID)
{
}

LiveStreamInfo LiveDouyin::GetLiveStreamInfo()
{
    try
    {
        // 构建请求参数
        std::string user_agent =
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/92.0.4515.159 Safari/537.36";

        const cpr::Header headers = {
            { "User-Agent", user_agent },
            { "referer", "https://live.douyin.com/" },
        };
        std::string params =
            "aid=6383&app_name=douyin_web&live_id=1&device_platform=web&"
            "browser_language=zh-CN&browser_platform=Win32&browser_name=Edge&"
            "browser_version=139.0.0.0&is_need_double_stream=false&web_rid=" +
            m_roomID;
        const std::string url = std::string(api::live::douyin::room) + params;
        auto response = cpr::Get(cpr::Url{ url }, headers);
        if (response.error || response.status_code != 200 || response.text.empty())
        {
            return { LiveStreamStatus::Error, "" };
        }
        auto streamInfo = nlohmann::json::parse(response.text);
        int status_code = streamInfo["status_code"].get<int>();
        if (status_code != 0)
        {
            return { LiveStreamStatus::Absent, "" };
        }
        // 获取直播数据
        const auto& data = streamInfo["data"]["data"][0];
        int status = data["status"].get<int>();
        // 抖音 status == 2 代表是开播的状态
        if (status == 2)
        {
            std::string link = GetStreamLinkFromResponse(data);
            if (link.empty())
            {
                return { LiveStreamStatus::Error, "" };
            }
            return { LiveStreamStatus::Normal, link };
        }
        // status == 4 代表未开播
        else if (status == 4)
        {
            return { LiveStreamStatus::NotLive, "" };
        }
        return { LiveStreamStatus::Error, "" };
    }
    catch (...)
    {
        return { LiveStreamStatus::Error, "" };
    }
}

std::string LiveDouyin::GetStreamLinkFromResponse(const nlohmann::json& data)
{
    try
    {
        const auto& stream_url = data["stream_url"];

        if (stream_url.contains("pull_datas"))
        {
            const auto& pullDatas = stream_url["pull_datas"];
            if (!pullDatas.empty())
            {
                auto doubleScreenStreams = pullDatas.begin().value();
                std::string stream_data_str = doubleScreenStreams["stream_data"].get<std::string>();
                auto streamData = nlohmann::json::parse(stream_data_str);

                return streamData["data"]["origin"]["main"]["flv"].get<std::string>();
            }
        }
        if (stream_url.contains("live_core_sdk_data"))
        {
            std::string stream_data_str =
                stream_url["live_core_sdk_data"]["pull_data"]["stream_data"].get<std::string>();
            auto streamData = nlohmann::json::parse(stream_data_str);

            return streamData["data"]["origin"]["main"]["flv"].get<std::string>();
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
