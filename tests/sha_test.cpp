// SPDX-License-Identifier: MIT

// Checks SHA-1 against the test vectors of FIPS 180 and RFC 3174, and SHA-256 against the standard test vectors, NIST's
// examples among them. Checks for each hash that feeding a message in pieces gives the same hash as feeding it whole.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "xsflib/sha1.h"
#include "xsflib/sha256.h"
#include "xsflib/testing/check.h"

using xsflib::testing::Check;

namespace
{

template <typename Hash>
std::string HashOf(const std::string& text)
{
    return Hash::Hex(Hash::Of({reinterpret_cast<const uint8_t*>(text.data()), text.size()}));
}

void TestSha1Vectors()
{
    XSFLIB_CHECK(HashOf<xsflib::Sha1>("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    XSFLIB_CHECK(HashOf<xsflib::Sha1>("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
    XSFLIB_CHECK(HashOf<xsflib::Sha1>("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
                 "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    XSFLIB_CHECK(HashOf<xsflib::Sha1>(std::string(1000000, 'a')) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

void TestSha256Vectors()
{
    XSFLIB_CHECK(HashOf<xsflib::Sha256>("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    XSFLIB_CHECK(HashOf<xsflib::Sha256>("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    XSFLIB_CHECK(HashOf<xsflib::Sha256>("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
                 "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    XSFLIB_CHECK(HashOf<xsflib::Sha256>("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
                                        "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
                 "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
    XSFLIB_CHECK(HashOf<xsflib::Sha256>(std::string(1000000, 'a')) ==
                 "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// Pieces of every size from 1 to 130 bytes, across block boundaries, give the hash of the whole message.
template <typename Hash>
void TestPieces(const std::string& what)
{
    std::vector<uint8_t> message(1000);
    for (std::size_t i = 0; i < message.size(); ++i)
    {
        message[i] = static_cast<uint8_t>(i * 7 + 3);
    }

    const typename Hash::Digest whole = Hash::Of(message);
    for (std::size_t piece = 1; piece <= 130; ++piece)
    {
        Hash hash;
        for (std::size_t i = 0; i < message.size(); i += piece)
        {
            hash.Update(std::span(message).subspan(i, std::min(piece, message.size() - i)));
        }

        Check(hash.Finish() == whole, what + ": pieces of " + std::to_string(piece) + " bytes");
    }
}

} // namespace

int main()
{
    const auto tests = []
    {
        TestSha1Vectors();
        TestSha256Vectors();
        TestPieces<xsflib::Sha1>("SHA-1");
        TestPieces<xsflib::Sha256>("SHA-256");
    };

    return xsflib::testing::RunTest("sha", tests);
}
