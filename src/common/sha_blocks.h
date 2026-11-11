// SPDX-License-Identifier: MIT

// What SHA-1 and SHA-256 share (FIPS 180-4): a message hashed in blocks of 64 bytes, its padding, the digest of the
// state words, and the hex of a digest.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "xsflib/bytes.h"

namespace xsflib::detail
{

using ShaBlock = std::array<uint8_t, 64>;

// Adds `data` to a message of `length` bytes so far. The message's last `buffered` bytes wait in `buffer`. Calls
// `block` with each block that fills.
template <typename Block>
void ShaUpdate(std::span<const uint8_t> data, ShaBlock& buffer, std::size_t& buffered, uint64_t& length,
               const Block& block)
{
    length += data.size();
    while (!data.empty())
    {
        const std::size_t n = std::min(data.size(), buffer.size() - buffered);
        std::copy_n(data.begin(), n, buffer.begin() + static_cast<std::ptrdiff_t>(buffered));
        buffered += n;
        data = data.subspan(n);

        if (buffered == buffer.size())
        {
            block(buffer.data());
            buffered = 0;
        }
    }
}

// Ends a message of ShaUpdate. Pads it with a 1 bit, zeros up to 8 bytes short of a block, and the length in bits.
template <typename Block>
void ShaPad(ShaBlock& buffer, std::size_t& buffered, uint64_t& length, const Block& block)
{
    const uint64_t bits = length * 8;
    const uint8_t kOne = 0x80;
    ShaUpdate({&kOne, 1}, buffer, buffered, length, block);

    const ShaBlock kZeros{};
    const std::size_t zeros = (buffer.size() * 2 - 8 - buffered) % buffer.size();
    ShaUpdate(std::span(kZeros).first(zeros), buffer, buffered, length, block);

    std::array<uint8_t, 8> bits_bytes;
    StoreBe64(bits_bytes.data(), bits);
    ShaUpdate(bits_bytes, buffer, buffered, length, block);
}

// The digest of the state words: each word in big-endian order.
template <std::size_t N>
std::array<uint8_t, 4 * N> ShaDigest(const std::array<uint32_t, N>& state)
{
    std::array<uint8_t, 4 * N> digest;
    for (std::size_t i = 0; i < N; ++i)
    {
        StoreBe32(digest.data() + i * 4, state[i]);
    }

    return digest;
}

// The hash of `data` alone, by `Hash`: Sha1 or Sha256.
template <typename Hash>
typename Hash::Digest ShaOf(std::span<const uint8_t> data)
{
    Hash hash;
    hash.Update(data);

    return hash.Finish();
}

// A digest as lowercase hex digits.
inline std::string ShaHex(std::span<const uint8_t> digest)
{
    static const char kDigits[] = "0123456789abcdef";
    std::string out;
    for (const uint8_t byte : digest)
    {
        out += kDigits[byte >> 4];
        out += kDigits[byte & 15];
    }

    return out;
}

} // namespace xsflib::detail
