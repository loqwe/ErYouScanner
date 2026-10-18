#include "MhySmsResponse.hpp"
#include <iostream>
#include <stdexcept>

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main()
{
    const auto sent = ParseMhySmsResponse(R"({"retcode":0,"data":{"action_type":"login","countdown":47,"sent_new":true}})");
    require(sent.retcode == 0 && sent.data.action_type == "login" && sent.countdown == 47, "send success");
    for (int code : {-3001, -3006, -3008})
        require(ParseMhySmsResponse(nlohmann::json{{"retcode", code}}.dump()).retcode == code, "failure must not count as sent");
    for (const auto* body : {"", "<html>", "null", "{}", R"({"retcode":0})", R"({"retcode":0,"data":{"action_type":""}})", R"({"retcode":"0"})"})
        require(ParseMhySmsResponse(body).retcode == MhySmsInvalidResponse, "malformed send response");
    const auto proof = nlohmann::json{{"session_id", "test-session"}, {"mmt_type", 1},
        {"data", nlohmann::json{{"gt", "test-gt"}, {"challenge", "test-challenge"}}.dump()}}.dump();
    const auto challenge = ParseMhySmsResponse(R"({"retcode":-3101})", proof);
    require(challenge.retcode == -3101 && challenge.data.mmt_type == 1 && challenge.data.session_id == "test-session", "challenge extraction");
    const auto v4Proof = nlohmann::json{{"session_id", "test-session"}, {"mmt_type", 1},
        {"data", nlohmann::json{{"gt", "test-captcha-id"}, {"use_v4", true}, {"risk_type", "slide"},
            {"new_captcha", 1}, {"success", 1}}.dump()}}.dump();
    const auto v4 = ParseMhySmsResponse(R"({"retcode":-3101,"data":null})", v4Proof);
    require(v4.retcode == -3101 && v4.data.use_v4 && v4.data.challenge.empty() &&
        v4.data.risk_type == "slide", "v4 challenge must not require legacy challenge field");
    const auto shape = MhySmsResponseShape(R"({"retcode":-3101,"data":null})", v4Proof);
    require(shape.find("test-session") == std::string::npos && shape.find("test-captcha-id") == std::string::npos,
        "response metadata must not expose challenge credentials");
    require(ParseMhySmsResponse(R"({"retcode":-3101})", "bad").retcode == MhySmsInvalidResponse, "bad challenge");
    require(ParseMhySmsResponse(R"({"retcode":-3101})").retcode != 0, "missing challenge must not count as sent");
    require(ParseMhyMobileLoginResponse(R"({"retcode":-3205})").retcode == -3205, "wrong SMS code");
    for (const auto* body : {"", "null", "{}", R"({"retcode":0,"data":{}})"})
        require(ParseMhyMobileLoginResponse(body).retcode == MhySmsInvalidResponse, "incomplete login must not succeed");
    const auto login = ParseMhyMobileLoginResponse(R"({"retcode":0,"data":{"token":{"token":"synthetic-token"},"user_info":{"aid":"test-aid","mid":"test-mid"}}})");
    require(login.retcode == 0 && login.data.aid == "test-aid", "login success");
    std::cout << "SMS send, challenge, login and failure parsing passed\n";
}
