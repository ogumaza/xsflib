// SPDX-License-Identifier: MIT

#include "xsflib/sha256.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "xsflib/bytes.h"

namespace xsflib
{
namespace
{

// The constants of the 64 rounds (FIPS 180-4, 4.2.2): the first 32 bits of the fractional parts of the cube roots of
// the first 64 primes.
constexpr std::array<uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

} // namespace

// The initial hash value (FIPS 180-4, 5.3.3): the first 32 bits of the fractional parts of the square roots of the
// first 8 primes.
Sha256::Sha256()
    : state_{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19}
{
}

void Sha256::Update(std::span<const uint8_t> data)
{
    length_ += data.size();
    while (!data.empty())
    {
        const std::size_t n = std::min(data.size(), buffer_.size() - buffered_);
        std::copy_n(data.begin(), n, buffer_.begin() + static_cast<std::ptrdiff_t>(buffered_));
        buffered_ += n;
        data = data.subspan(n);

        if (buffered_ == buffer_.size())
        {
            Block(buffer_.data());
            buffered_ = 0;
        }
    }
}

Sha256::Digest Sha256::Finish()
{
    // Pad with a 1 bit, zeros up to 8 bytes short of a block, and the length in bits.
    const uint64_t bits = length_ * 8;
    const uint8_t kOne = 0x80;
    Update({&kOne, 1});

    const std::array<uint8_t, 64> kZeros{};
    Update(std::span(kZeros).first((buffer_.size() * 2 - 8 - buffered_) % buffer_.size()));

    std::array<uint8_t, 8> length;
    StoreBe64(length.data(), bits);
    Update(length);

    Digest digest;
    for (std::size_t i = 0; i < state_.size(); ++i)
    {
        StoreBe32(digest.data() + i * 4, state_[i]);
    }

    return digest;
}

Sha256::Digest Sha256::Of(std::span<const uint8_t> data)
{
    Sha256 sha256;
    sha256.Update(data);

    return sha256.Finish();
}

std::string Sha256::Hex(const Digest& digest)
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

// One block of the message (FIPS 180-4, 6.2.2).
void Sha256::Block(const uint8_t* block)
{
    // The message schedule.
    std::array<uint32_t, 64> w;
    for (std::size_t i = 0; i < 16; ++i)
    {
        w[i] = LoadBe32(block + i * 4);
    }

    for (std::size_t i = 16; i < w.size(); ++i)
    {
        const uint32_t sigma0 = std::rotr(w[i - 15], 7) ^ std::rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const uint32_t sigma1 = std::rotr(w[i - 2], 17) ^ std::rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + sigma0 + w[i - 7] + sigma1;
    }

    // The rounds, on the working variables a to h.
    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];
    for (std::size_t i = 0; i < w.size(); ++i)
    {
        const uint32_t sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const uint32_t choice = (e & f) ^ (~e & g);
        const uint32_t t1 = h + sum1 + choice + kRoundConstants[i] + w[i];
        const uint32_t sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

} // namespace xsflib
