// SPDX-License-Identifier: MIT

// Tests of the byte loads and stores and of Reader, on bytes built here.

#include "xsflib/binary.h"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "xsflib/bytes.h"
#include "xsflib/testing/check.h"

using namespace xsflib;
using xsflib::testing::Check;
using xsflib::testing::ErrorOf;

namespace
{

void TestLoadsAndStores()
{
    uint8_t b[8] = {};

    StoreBe16(b, 0x1234);

    XSFLIB_CHECK(b[0] == 0x12 && b[1] == 0x34);
    XSFLIB_CHECK(LoadBe16(b) == 0x1234);

    StoreLe16(b, 0x1234);

    XSFLIB_CHECK(b[0] == 0x34 && b[1] == 0x12);
    XSFLIB_CHECK(LoadLe16(b) == 0x1234);

    StoreBe32(b, 0x89abcdef);

    XSFLIB_CHECK(b[0] == 0x89 && b[3] == 0xef);
    XSFLIB_CHECK(LoadBe32(b) == 0x89abcdef);

    StoreLe32(b, 0x89abcdef);

    XSFLIB_CHECK(b[0] == 0xef && b[3] == 0x89);
    XSFLIB_CHECK(LoadLe32(b) == 0x89abcdef);

    StoreBe64(b, 0x0123456789abcdefULL);

    XSFLIB_CHECK(b[0] == 0x01 && b[7] == 0xef);
    XSFLIB_CHECK(LoadBe64(b) == 0x0123456789abcdefULL);

    StoreLe64(b, 0x0123456789abcdefULL);

    XSFLIB_CHECK(b[0] == 0xef && b[7] == 0x01);
    XSFLIB_CHECK(LoadLe64(b) == 0x0123456789abcdefULL);

    XSFLIB_CHECK(ByteSwap16(0x1234) == 0x3412);
    XSFLIB_CHECK(ByteSwap32(0x12345678) == 0x78563412);
}

// The same bytes read in each byte order, signed values and floats included.
void TestByteOrders()
{
    const std::vector<uint8_t> kData = {0xff, 0xfe, 0x3f, 0x80, 0x00, 0x00, 0x80, 0x3f};

    const Reader le(kData);
    const Reader be(kData, true);

    XSFLIB_CHECK(!le.BigEndian());
    XSFLIB_CHECK(be.BigEndian());
    XSFLIB_CHECK(le.Size() == 8);
    XSFLIB_CHECK(le.U8(1) == 0xfe);
    XSFLIB_CHECK(le.U16(0) == 0xfeff);
    XSFLIB_CHECK(be.U16(0) == 0xfffe);
    XSFLIB_CHECK(le.S16(0) == -257);
    XSFLIB_CHECK(be.S16(0) == -2);
    XSFLIB_CHECK(le.U32(0) == 0x803ffeff);
    XSFLIB_CHECK(be.U32(0) == 0xfffe3f80);
    XSFLIB_CHECK(le.S32(0) == static_cast<int32_t>(0x803ffeffU));
    XSFLIB_CHECK(be.F32(2) == 1.0f);
    XSFLIB_CHECK(le.F32(4) == 1.0f);
}

// A read past the end throws FormatError with the wording that the options select.
void TestBounds()
{
    const std::vector<uint8_t> kData = {1, 2, 3, 4, 5, 6};

    const Reader plain(kData);
    const Reader cut(kData, false, ReaderOptions{BoundsMessage::kCutShort});

    Check(ErrorOf([&] { plain.U32(4); }) == "read out of bounds at offset 4", "U32 past the end");
    Check(ErrorOf([&] { plain.U8(6); }) == "read out of bounds at offset 6", "U8 at the end");
    Check(ErrorOf([&] { plain.Sub(2, 5); }) == "read out of bounds at offset 2", "Sub past the end");
    Check(ErrorOf([&] { plain.Magic(3, "ABCD"); }) == "read out of bounds at offset 3", "Magic past the end");
    Check(ErrorOf([&] { cut.U16(5); }) == "the data ends before offset 5 (the file is cut short or damaged)",
          "the cut-short wording");

    const Reader past(kData, true, ReaderOptions{BoundsMessage::kRunsPastEnd});
    Check(ErrorOf([&] { past.U32(3); }) == "a read at offset 3 runs past the end of the data", "the runs-past wording");

    const Reader fixed(kData, true, ReaderOptions{.bounds_message = BoundsMessage::kFixed, .fixed_message = "damaged"});
    Check(ErrorOf([&] { fixed.U8(6); }) == "damaged", "a fixed message without the offset");

    Check(ErrorOf([&] { plain.U16(4); }).empty(), "U16 that ends at the end");
    Check(plain.Sub(2, 4).size() == 4 && plain.Sub(2, 4)[0] == 3, "Sub's bytes");

    bool caught = false;
    try
    {
        plain.S32(3);
    }
    catch (const std::runtime_error&)
    {
        caught = true;
    }

    XSFLIB_CHECK(caught);
}

void TestStringsAndMagic()
{
    const std::vector<uint8_t> kData = {'X', 'T', 'A', 'G', 'a', 'b', 0, 'c', 'd'};

    const Reader plain(kData);
    const Reader checked(kData, false, ReaderOptions{BoundsMessage::kOutOfBounds, true});

    XSFLIB_CHECK(plain.Magic(0, "XTAG"));
    XSFLIB_CHECK(!plain.Magic(0, "YTAG"));
    XSFLIB_CHECK(plain.CString(4, 64) == "ab");
    XSFLIB_CHECK(plain.CString(4, 1) == "a");
    XSFLIB_CHECK(plain.CString(7, 64) == "cd");
    XSFLIB_CHECK(plain.CString(9, 64).empty());
    XSFLIB_CHECK(plain.CString(100, 64).empty());
    XSFLIB_CHECK(checked.CString(9, 64).empty());
    Check(ErrorOf([&] { checked.CString(10, 64); }) == "read out of bounds at offset 10",
          "CString past the end with check_string_offset");
}

} // namespace

int main()
{
    TestLoadsAndStores();
    TestByteOrders();
    TestBounds();
    TestStringsAndMagic();

    return xsflib::testing::Summary("binary");
}
