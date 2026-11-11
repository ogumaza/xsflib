// SPDX-License-Identifier: MIT

#include "xsflib/sha1.h"

#include <bit>
#include <cstdint>
#include <span>
#include <string>

#include "sha_blocks.h"
#include "xsflib/bytes.h"

namespace xsflib
{

Sha1::Sha1() : state_{0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}
{
}

void Sha1::Update(std::span<const uint8_t> data)
{
    detail::ShaUpdate(data, buffer_, buffered_, length_, [this](const uint8_t* block) { Block(block); });
}

Sha1::Digest Sha1::Finish()
{
    detail::ShaPad(buffer_, buffered_, length_, [this](const uint8_t* block) { Block(block); });

    return detail::ShaDigest(state_);
}

Sha1::Digest Sha1::Of(std::span<const uint8_t> data)
{
    return detail::ShaOf<Sha1>(data);
}

std::string Sha1::Hex(const Digest& digest)
{
    return detail::ShaHex(digest);
}

void Sha1::Block(const uint8_t* block)
{
    uint32_t w[80];
    for (int i = 0; i < 16; ++i)
    {
        w[i] = LoadBe32(block + i * 4);
    }

    for (int i = 16; i < 80; ++i)
    {
        w[i] = std::rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    for (int i = 0; i < 80; ++i)
    {
        uint32_t f;
        uint32_t k;
        if (i < 20)
        {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        }
        else
        {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }

        const uint32_t t = std::rotl(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = std::rotl(b, 30);
        b = a;
        a = t;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
}

} // namespace xsflib
