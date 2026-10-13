#pragma once

#include <limits>
#include <optional>
#include <string_view>
#include <nlohmann/json.hpp>

inline std::optional<int> ParseStokenRetcode(std::string_view text)
{
    const auto value = nlohmann::json::parse(text, nullptr, false);
    if (!value.is_object()) return std::nullopt;
    const auto retcode = value.find("retcode");
    if (retcode == value.end() || !retcode->is_number_integer()) return std::nullopt;
    if (retcode->is_number_unsigned())
    {
        const auto code = retcode->get<std::uint64_t>();
        if (code > static_cast<std::uint64_t>((std::numeric_limits<int>::max)())) return std::nullopt;
        return static_cast<int>(code);
    }
    const auto code = retcode->get<std::int64_t>();
    if (code < (std::numeric_limits<int>::min)() || code > (std::numeric_limits<int>::max)()) return std::nullopt;
    return static_cast<int>(code);
}
