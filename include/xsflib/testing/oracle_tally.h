// SPDX-License-Identifier: MIT

// For the oracle tests, which run a function of the game code and the matching function of the model on the same
// inputs: a tally of the cases in which the two differ, and a small deterministic generator of inputs.

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "xsflib/testing/check.h"

namespace xsflib::testing
{

// A float in hex, as printf writes it with %a. The text shows every bit of the float.
inline std::string OracleText(float x)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%a", static_cast<double>(x));

    // Microsoft's CRT pads the digits after the point with zeros to 13 digits. macOS and glibc write the shortest
    // form. Dropping the padding gives the same text on every platform.
    std::string out = text;
    const std::size_t exponent = out.find('p');
    const std::size_t point = out.find('.');
    if (exponent != std::string::npos && point < exponent)
    {
        std::size_t end = exponent;
        while (end > point + 1 && out[end - 1] == '0')
        {
            --end;
        }
        if (end == point + 1)
        {
            end = point;
        }
        out.erase(end, exponent - end);
    }

    return out;
}

// Any other number in decimal.
template <typename T>
std::string OracleText(T x)
{
    return std::to_string(x);
}

// Counts the cases in which the game and the model differ, and reports the first five. At its end, it checks that none
// differed.
class Tally
{
public:
    explicit Tally(std::string what) : what_(std::move(what))
    {
    }

    Tally(const Tally&) = delete;
    Tally& operator=(const Tally&) = delete;

    ~Tally()
    {
        if (Check(differ_ == 0, what_ + " matches the game in all " + std::to_string(cases_) + " cases (" +
                                    std::to_string(differ_) + " differ)"))
        {
            std::printf("%s: all %d cases match the game\n", what_.c_str(), cases_);
        }
    }

    // A case of one value. A float matches when its bits do.
    void Compare(float game, float model, const std::string& input)
    {
        Count(std::bit_cast<uint32_t>(game) == std::bit_cast<uint32_t>(model), OracleText(game), OracleText(model),
              input);
    }

    void Compare(uint64_t game, uint64_t model, const std::string& input)
    {
        Count(game == model, OracleText(game), OracleText(model), input);
    }

    // A case of several values. It matches when the bits of every value match.
    template <typename T>
    void Compare(const std::vector<T>& game, const std::vector<T>& model, const std::string& input)
    {
        ++cases_;
        if (game.size() != model.size())
        {
            if (differ_++ < 5)
            {
                std::fprintf(stderr, "%s(%s): the game gives %zu values, the model %zu\n", what_.c_str(), input.c_str(),
                             game.size(), model.size());
            }
            return;
        }

        for (std::size_t i = 0; i < game.size(); ++i)
        {
            if (std::memcmp(&game[i], &model[i], sizeof(T)) != 0)
            {
                if (differ_++ < 5)
                {
                    std::fprintf(stderr, "%s(%s): value %zu differs: the game gives %s, the model %s\n", what_.c_str(),
                                 input.c_str(), i, OracleText(game[i]).c_str(), OracleText(model[i]).c_str());
                }
                return;
            }
        }
    }

    // The cases so far, and those in which the game and the model differ.
    int Cases() const
    {
        return cases_;
    }

    int Differ() const
    {
        return differ_;
    }

private:
    void Count(bool same, const std::string& game, const std::string& model, const std::string& input)
    {
        ++cases_;
        if (!same && differ_++ < 5)
        {
            std::fprintf(stderr, "%s(%s): the game gives %s, the model %s\n", what_.c_str(), input.c_str(),
                         game.c_str(), model.c_str());
        }
    }

    std::string what_;
    int cases_ = 0;
    int differ_ = 0;
};

// A small deterministic generator of inputs: a 64-bit linear congruential generator with Knuth's MMIX constants.
class OracleInputs
{
public:
    // A number from 0 to n - 1. n is 1 or more.
    uint32_t Below(uint32_t n)
    {
        x_ = x_ * 6364136223846793005ull + 1442695040888963407ull;

        return static_cast<uint32_t>((x_ >> 33) % n);
    }

private:
    uint64_t x_ = 0x9E3779B97F4A7C15ull;
};

} // namespace xsflib::testing
