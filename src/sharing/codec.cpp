#include "sharing/codec.hpp"

#define ZLIB_CONST
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace pmm::sharing {
namespace {

constexpr std::string_view kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

constexpr std::array<std::int8_t, 256> makeDecodeTable() {
    std::array<std::int8_t, 256> table{};
    table.fill(-1);
    for (std::size_t i = 0; i < kAlphabet.size(); ++i)
        table[static_cast<unsigned char>(kAlphabet[i])] = static_cast<std::int8_t>(i);
    return table;
}
constexpr auto kDecodeTable = makeDecodeTable();

constexpr std::size_t kInflateChunk = 64 * 1024;

std::string zlibDetail(const z_stream& stream, int ret) {
    std::string detail = "zlib error " + std::to_string(ret);
    if (stream.msg) {
        detail += ": ";
        detail += stream.msg;
    }
    return detail;
}

} // namespace

std::string base64UrlEncode(std::string_view bytes) {
    std::string out;
    out.reserve((bytes.size() * 4 + 2) / 3);
    std::size_t i = 0;
    const auto at = [&](std::size_t k) { return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[k])); };
    for (; i + 3 <= bytes.size(); i += 3) {
        const std::uint32_t v = (at(i) << 16) | (at(i + 1) << 8) | at(i + 2);
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
        out += kAlphabet[(v >> 6) & 0x3F];
        out += kAlphabet[v & 0x3F];
    }
    const std::size_t rest = bytes.size() - i;
    if (rest == 1) {
        const std::uint32_t v = at(i) << 16;
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
    } else if (rest == 2) {
        const std::uint32_t v = (at(i) << 16) | (at(i + 1) << 8);
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
        out += kAlphabet[(v >> 6) & 0x3F];
    }
    return out;
}

Result<std::string> base64UrlDecode(std::string_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (kDecodeTable[c] >= 0)
            continue;
        if (c == '=')
            return makeError(ErrorCode::ParseError,
                             "The share code is malformed: it contains '=' padding, which is not "
                             "allowed.",
                             "base64url padding at offset " + std::to_string(i));
        return makeError(ErrorCode::ParseError,
                         "The share code is malformed: it contains a character that is not "
                         "allowed. Copy the code again without modifying it.",
                         "invalid base64url character (byte 0x" +
                             std::string(1, "0123456789abcdef"[c >> 4]) +
                             std::string(1, "0123456789abcdef"[c & 0xF]) + ") at offset " +
                             std::to_string(i));
    }
    const std::size_t rest = text.size() % 4;
    if (rest == 1)
        return makeError(ErrorCode::ParseError,
                         "The share code is malformed: its data section has an impossible "
                         "length. The code may be truncated or damaged.",
                         "base64url length % 4 == 1 (length " + std::to_string(text.size()) + ")");
    if (rest != 0) {
        // The final character carries 4 (rest 2) or 2 (rest 3) unused bits.
        const auto last = kDecodeTable[static_cast<unsigned char>(text.back())];
        const int unusedMask = rest == 2 ? 0x0F : 0x03;
        if ((last & unusedMask) != 0)
            return makeError(ErrorCode::ParseError,
                             "The share code is malformed: its data section is not canonically "
                             "encoded. The code may be damaged.",
                             "non-zero unused bits in final base64url character");
    }

    std::string out;
    out.reserve(text.size() / 4 * 3 + 2);
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char ch : text) {
        buffer = (buffer << 6) | static_cast<std::uint32_t>(kDecodeTable[static_cast<unsigned char>(ch)]);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buffer >> bits) & 0xFF);
        }
    }
    return out;
}

Result<std::string> zlibCompress(std::string_view data) {
    if (data.size() > std::numeric_limits<uInt>::max())
        return makeError(ErrorCode::LimitExceeded, "The data is too large to compress.");

    z_stream stream{};
    int ret = deflateInit2(&stream, 9, Z_DEFLATED, MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK)
        return makeError(ErrorCode::Internal, "Could not initialise compression.",
                         zlibDetail(stream, ret));

    std::string out(deflateBound(&stream, static_cast<uLong>(data.size())), '\0');
    stream.next_in = reinterpret_cast<const Bytef*>(data.data());
    stream.avail_in = static_cast<uInt>(data.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    ret = deflate(&stream, Z_FINISH);
    const std::size_t produced = out.size() - stream.avail_out;
    const std::string detail = ret == Z_STREAM_END ? std::string() : zlibDetail(stream, ret);
    deflateEnd(&stream);
    if (ret != Z_STREAM_END)
        return makeError(ErrorCode::Internal, "Compression failed.", detail);
    out.resize(produced);
    return out;
}

Result<std::string> zlibInflate(std::string_view compressed, std::size_t maxOutputBytes) {
    if (compressed.size() > std::numeric_limits<uInt>::max())
        return makeError(ErrorCode::LimitExceeded, "The compressed data is too large.");

    z_stream stream{};
    int ret = inflateInit(&stream); // zlib header required; raw/gzip streams rejected
    if (ret != Z_OK)
        return makeError(ErrorCode::Internal, "Could not initialise decompression.",
                         zlibDetail(stream, ret));
    struct Guard {
        z_stream* s;
        ~Guard() { inflateEnd(s); }
    } guard{&stream};

    stream.next_in = reinterpret_cast<const Bytef*>(compressed.data());
    stream.avail_in = static_cast<uInt>(compressed.size());

    std::string out;
    std::size_t produced = 0;
    for (;;) {
        // Never offer more than one byte beyond the cap, so a bomb is detected
        // after at most maxOutputBytes + 1 bytes of output.
        const std::size_t room = std::min(kInflateChunk, maxOutputBytes - produced + 1);
        out.resize(produced + room);
        stream.next_out = reinterpret_cast<Bytef*>(out.data() + produced);
        stream.avail_out = static_cast<uInt>(room);
        ret = inflate(&stream, Z_NO_FLUSH);
        produced += room - stream.avail_out;

        if (produced > maxOutputBytes)
            return makeError(ErrorCode::LimitExceeded,
                             "The share code expands to more data than allowed (limit " +
                                 std::to_string(maxOutputBytes / (1024 * 1024)) +
                                 " MiB). It was rejected to protect this computer.",
                             "decompressed output exceeded " + std::to_string(maxOutputBytes) +
                                 " bytes");
        if (ret == Z_STREAM_END)
            break;
        if (ret == Z_OK)
            continue;
        if (ret == Z_BUF_ERROR && stream.avail_in == 0)
            return makeError(ErrorCode::ParseError,
                             "The share code is malformed: its compressed data ends early.",
                             "zlib stream truncated (no Z_STREAM_END)");
        if (ret == Z_NEED_DICT)
            return makeError(ErrorCode::ParseError,
                             "The share code is malformed: its compressed data uses an unsupported "
                             "preset dictionary.",
                             zlibDetail(stream, ret));
        if (ret == Z_MEM_ERROR)
            return makeError(ErrorCode::Internal, "Out of memory while decompressing.",
                             zlibDetail(stream, ret));
        return makeError(ErrorCode::ParseError,
                         "The share code is malformed: its data is not valid compressed data.",
                         zlibDetail(stream, ret));
    }

    if (stream.avail_in != 0)
        return makeError(ErrorCode::ParseError,
                         "The share code is malformed: unexpected data follows the compressed "
                         "content.",
                         std::to_string(stream.avail_in) + " trailing bytes after zlib stream");
    out.resize(produced);
    return out;
}

} // namespace pmm::sharing
