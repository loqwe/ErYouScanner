#include "MhyApi.hpp"
#include <iostream>
#include <cstdlib>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const bool invalid = std::string_view(argv[1]) == "--invalid-mobile";
    const char* mobile = invalid ? "00000000000" : std::getenv("ERYOU_SMS_TEST_MOBILE");
    if (!invalid && (std::string_view(argv[1]) != "--authorized-mobile" || !mobile)) return 2;
    const auto result = CreateLoginCaptcha(mobile);
    std::cout << "SMS request: retcode=" << result.retcode << '\n';
    return invalid ? (result.retcode == -3008 ? 0 : 1) : 0;
}
