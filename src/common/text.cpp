// SPDX-License-Identifier: MIT

#include "xsflib/text.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <system_error>

namespace xsflib
{

std::string Hex(uint32_t value, int digits, bool upper)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), upper ? "0x%0*X" : "0x%0*x", digits, static_cast<unsigned>(value));

    return buf;
}

std::optional<int64_t> ParseInteger(std::string_view text, const IntegerRules& rules, IntegerError* error)
{
    if (error)
    {
        *error = IntegerError::kNotNumber;
    }

    if (rules.c_rules)
    {
        // std::strtoll reads up to a null character. It holds a number that's too large to 64 bits.
        const std::string terminated(text);
        char* end = nullptr;
        const long long value = std::strtoll(terminated.c_str(), &end, rules.base);
        if (terminated.empty() || *end != '\0')
        {
            return std::nullopt;
        }

        return value;
    }

    const bool negative = rules.minus && text.starts_with('-');
    if (negative)
    {
        text.remove_prefix(1);
    }

    const bool prefix = text.starts_with("0x") || text.starts_with("0X");
    if (prefix && rules.base != 10)
    {
        text.remove_prefix(2);
    }

    const bool hex = rules.base == 16 || (rules.base == 0 && prefix);

    uint32_t n = 0;
    const char* last = text.data() + text.size();
    const auto [end, result] = std::from_chars(text.data(), last, n, hex ? 16 : 10);
    if (text.empty() || result == std::errc::invalid_argument || end != last)
    {
        return std::nullopt;
    }
    if (result == std::errc::result_out_of_range)
    {
        if (error)
        {
            *error = IntegerError::kOutOfRange;
        }

        return std::nullopt;
    }

    return negative ? -static_cast<int64_t>(n) : static_cast<int64_t>(n);
}

std::optional<Assignment> ParseAssignment(std::string_view text, const IntegerRules& rules)
{
    const std::size_t equals = text.find('=');
    if (equals == std::string_view::npos)
    {
        return std::nullopt;
    }

    const std::optional<int64_t> index = ParseInteger(text.substr(0, equals), rules);
    const std::optional<int64_t> value = ParseInteger(text.substr(equals + 1), rules);
    if (!index || !value)
    {
        return std::nullopt;
    }

    return Assignment{*index, *value};
}

std::optional<DecimalNumber> ReadDecimal(std::string_view text, const DecimalRules& rules)
{
    if (rules.c_rules)
    {
        // std::strtod reads up to a null character.
        const std::string terminated(text);
        char* end = nullptr;
        const double value = std::strtod(terminated.c_str(), &end);
        if (end == terminated.c_str())
        {
            return std::nullopt;
        }

        return DecimalNumber{value, static_cast<std::size_t>(end - terminated.c_str())};
    }

    std::size_t at = 0;
    const bool negative = !text.empty() && text[0] == '-';
    if (!text.empty() && (text[0] == '-' || text[0] == '+'))
    {
        at++;
    }

    // Up to 19 significant digits. They fit in 64 bits. Later digits before the decimal point scale the value. Later
    // digits after it are dropped.
    constexpr int kMaxDigits = 19;
    uint64_t digits = 0;
    int significant = 0;
    int exponent = 0; // of ten
    bool any_digit = false;
    bool point = false;
    for (; at < text.size(); at++)
    {
        const char c = text[at];
        if ((c == '.' || c == ',') && !point)
        {
            point = true;
            continue;
        }

        if (c < '0' || c > '9')
        {
            break;
        }

        any_digit = true;
        if (significant < kMaxDigits)
        {
            significant += digits != 0 || c != '0' ? 1 : 0;
            digits = digits * 10 + static_cast<uint64_t>(c - '0');
            exponent -= point ? 1 : 0;
        }
        else if (!point)
        {
            exponent++;
        }
    }

    if (!any_digit)
    {
        return std::nullopt;
    }

    // An exponent counts only with a digit: "1e" is the number 1 followed by a letter.
    constexpr int kMaxExponent = 100'000;
    std::size_t end = at;
    if (at < text.size() && (text[at] == 'e' || text[at] == 'E'))
    {
        std::size_t e = at + 1;
        const bool negative_exponent = e < text.size() && text[e] == '-';
        if (e < text.size() && (text[e] == '-' || text[e] == '+'))
        {
            e++;
        }

        const std::size_t first = e;
        int value = 0;
        for (; e < text.size() && text[e] >= '0' && text[e] <= '9'; e++)
        {
            value = std::min(value * 10 + (text[e] - '0'), kMaxExponent);
        }

        if (e > first)
        {
            exponent += negative_exponent ? -value : value;
            end = e;
        }
    }

    // While the digits fit in a double's 53 bits and the power of ten is at most 10^22, both are exact. One
    // multiplication or division then rounds the value once, to what std::strtod gives. Longer numbers and larger
    // exponents round more than once. They can differ from std::strtod in the last bit.
    constexpr std::array<double, 23> kPowersOfTen = {1e0,  1e1,  1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                                     1e8,  1e9,  1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                                     1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    constexpr int kLargestPower = static_cast<int>(kPowersOfTen.size()) - 1;
    double value = static_cast<double>(digits);
    for (; exponent > kLargestPower; exponent -= kLargestPower)
    {
        value *= kPowersOfTen[kLargestPower];
    }

    for (; exponent < -kLargestPower; exponent += kLargestPower)
    {
        value /= kPowersOfTen[kLargestPower];
    }

    value = exponent < 0 ? value / kPowersOfTen[-exponent] : value * kPowersOfTen[exponent];

    return DecimalNumber{negative ? -value : value, end};
}

} // namespace xsflib
