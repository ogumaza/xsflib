// SPDX-License-Identifier: MIT

// Checks SHA-256 against the standard test vectors, NIST's examples among them, and that feeding a message in pieces
// gives the same hash as feeding it whole.

#include "xsflib/sha256.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "xsflib/testing/check.h"

using xsflib::testing::Check;

namespace
{

std::string HashOf(const std::string& text)
{
    return xsflib::Sha256::Hex(xsflib::Sha256::Of({reinterpret_cast<const uint8_t*>(text.data()), text.size()}));
}

} // namespace

int main()
{
    XSFLIB_CHECK(HashOf("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    XSFLIB_CHECK(HashOf("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    XSFLIB_CHECK(HashOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
                 "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    XSFLIB_CHECK(HashOf("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmno"
                        "ijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu") ==
                 "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
    XSFLIB_CHECK(HashOf(std::string(1000000, 'a')) ==
                 "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    // Pieces of every size from 1 to 130 bytes, across block boundaries.
    std::vector<uint8_t> message(1000);
    for (std::size_t i = 0; i < message.size(); ++i)
    {
        message[i] = static_cast<uint8_t>(i * 7 + 3);
    }

    const xsflib::Sha256::Digest whole = xsflib::Sha256::Of(message);
    for (std::size_t piece = 1; piece <= 130; ++piece)
    {
        xsflib::Sha256 sha256;
        for (std::size_t i = 0; i < message.size(); i += piece)
        {
            sha256.Update(std::span(message).subspan(i, std::min(piece, message.size() - i)));
        }

        Check(sha256.Finish() == whole, "pieces of " + std::to_string(piece) + " bytes");
    }

    return xsflib::testing::Summary("sha256");
}
