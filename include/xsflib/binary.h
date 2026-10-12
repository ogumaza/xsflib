// SPDX-License-Identifier: MIT

// Bounds-checked reads of binary files in either byte order.

#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "xsflib/bytes.h"

namespace xsflib
{

// Invalid or unsupported input.
class FormatError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// The wording of the message for a read past the end of the data.
enum class BoundsMessage
{
    kOutOfBounds, // "read out of bounds at offset N"
    kCutShort,    // "the data ends before offset N (the file is cut short or damaged)"
    kRunsPastEnd, // "a read at offset N runs past the end of the data"
    kFixed,       // ReaderOptions::fixed_message, without an offset
};

// How a Reader reports bad input. Messages are a program's output. The options select the wording that a program
// prints and whether CString throws for an offset past the end of the data.
struct ReaderOptions
{
    BoundsMessage bounds_message = BoundsMessage::kOutOfBounds;

    // CString throws when its offset lies past the end of the data. Without this, it returns an empty string.
    bool check_string_offset = false;

    // The message for BoundsMessage::kFixed. The text has to outlive the Reader.
    std::string_view fixed_message = {};
};

// Reads values from a byte span in the file's byte order. A read past the end throws FormatError.
class Reader
{
public:
    explicit Reader(std::span<const uint8_t> data, bool big_endian = false, const ReaderOptions& options = {})
        : data_(data), big_endian_(big_endian), options_(options)
    {
    }

    std::size_t Size() const
    {
        return data_.size();
    }

    bool BigEndian() const
    {
        return big_endian_;
    }

    std::span<const uint8_t> Sub(std::size_t off, std::size_t len) const
    {
        Check(off, len);

        return data_.subspan(off, len);
    }

    uint8_t U8(std::size_t off) const
    {
        Check(off, 1);

        return data_[off];
    }

    uint16_t U16(std::size_t off) const
    {
        return Load<uint16_t>(off);
    }

    int16_t S16(std::size_t off) const
    {
        return Load<int16_t>(off);
    }

    uint32_t U32(std::size_t off) const
    {
        return Load<uint32_t>(off);
    }

    int32_t S32(std::size_t off) const
    {
        return Load<int32_t>(off);
    }

    float F32(std::size_t off) const
    {
        return Load<float>(off);
    }

    // Reads a null-terminated string at `off`, up to `max_len` characters or the end of the data.
    std::string CString(std::size_t off, std::size_t max_len) const
    {
        if (options_.check_string_offset)
        {
            Check(off, 0);
        }

        std::string s;
        while (off < data_.size() && data_[off] && s.size() < max_len)
        {
            s.push_back(static_cast<char>(data_[off++]));
        }

        return s;
    }

    // Returns true if the 4 bytes at `off` are `m`.
    bool Magic(std::size_t off, const char* m) const
    {
        Check(off, 4);

        return std::memcmp(data_.data() + off, m, 4) == 0;
    }

private:
    template <typename T>
    T Load(std::size_t off) const
    {
        Check(off, sizeof(T));

        const uint8_t* p = data_.data() + off;
        if constexpr (sizeof(T) == 2)
        {
            return std::bit_cast<T>(big_endian_ ? LoadBe16(p) : LoadLe16(p));
        }
        else
        {
            static_assert(sizeof(T) == 4);
            return std::bit_cast<T>(big_endian_ ? LoadBe32(p) : LoadLe32(p));
        }
    }

    void Check(std::size_t off, std::size_t len) const
    {
        if (off > data_.size() || len > data_.size() - off)
        {
            throw FormatError(OutOfBounds(off));
        }
    }

    std::string OutOfBounds(std::size_t off) const
    {
        switch (options_.bounds_message)
        {
        case BoundsMessage::kCutShort:
            return "the data ends before offset " + std::to_string(off) + " (the file is cut short or damaged)";
        case BoundsMessage::kRunsPastEnd:
            return "a read at offset " + std::to_string(off) + " runs past the end of the data";
        case BoundsMessage::kFixed:
            return std::string(options_.fixed_message);
        case BoundsMessage::kOutOfBounds:
            break;
        }

        return "read out of bounds at offset " + std::to_string(off);
    }

    std::span<const uint8_t> data_;
    bool big_endian_;
    ReaderOptions options_;
};

} // namespace xsflib
