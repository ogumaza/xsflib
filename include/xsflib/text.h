// SPDX-License-Identifier: MIT

// Numbers as text, the same in every locale: hex for messages, and whole numbers and assignments "N=V" from options and
// tags.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace xsflib
{

// `value` in hex with a 0x prefix and at least `digits` digits: "0x1f", or "0x0000001f" with 8 digits. With `upper`,
// the digits are capitals: "0x0000001F".
std::string Hex(uint32_t value, int digits = 0, bool upper = false);

// A number read from the start of a text, and the number of characters it took.
struct DecimalNumber
{
    double value;
    std::size_t length;
};

// How ReadDecimal reads a number.
struct DecimalRules
{
    // C's rules, those of std::strtod in the current C locale: white space before the number, hex, infinity and NaN,
    // and the locale's decimal point alone. A program whose options have always taken numbers that way keeps its
    // messages.
    bool c_rules = false;
};

// Reads a decimal number from the start of `text` the same way in every locale: an optional sign, digits with an
// optional '.' or ',' before decimals, and an optional exponent ("1.5e3"). Returns nullopt if `text` doesn't start with
// one. Unlike std::strtod, ReadDecimal reads no white space, hex, infinity or NaN. With C's rules in `rules`, it reads
// them as std::strtod does.
std::optional<DecimalNumber> ReadDecimal(std::string_view text, const DecimalRules& rules = {});

// How ParseInteger reads a number.
struct IntegerRules
{
    // The base. With 0, the number is decimal, or hex after a 0x prefix. With 16, the 0x prefix may be there or not.
    int base = 0;

    // C's rules, those of std::strtoll: white space before the number, a plus sign, any number of digits with the value
    // held to 64 bits, and std::strtoll's bases. A program whose options have always taken numbers that way keeps its
    // messages.
    bool c_rules = false;

    // Whether a minus sign may come first. Without C's rules only.
    bool minus = true;
};

// Why ParseInteger found no number: the text isn't one, or its digits require more than 32 bits.
enum class IntegerError
{
    kNotNumber,
    kOutOfRange,
};

// A whole number, the whole of `text`. Without C's rules, ParseInteger doesn't depend on the C locale. It then takes no
// white space or other characters around the number, and the digits hold at most 32 bits. Returns nothing if `text`
// isn't such a number. A given `error` then holds the reason.
std::optional<int64_t> ParseInteger(std::string_view text, const IntegerRules& rules = {},
                                    IntegerError* error = nullptr);

// An assignment "N=V" of two whole numbers, such as a sequence variable's number and its value.
struct Assignment
{
    int64_t index = 0;
    int64_t value = 0;
};

// One assignment "N=V", the whole of `text`: ParseInteger's numbers by `rules` before and after the first '='. Returns
// nothing if `text` has no '=' or either side isn't such a number. The caller checks each number's range.
std::optional<Assignment> ParseAssignment(std::string_view text, const IntegerRules& rules = {});

} // namespace xsflib
