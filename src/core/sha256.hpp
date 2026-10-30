#pragma once

// Minimal SHA-256 (FIPS 180-4). Used for share-code corruption detection, local
// mod fingerprints and checking a downloaded installer against the release's
// SHA256SUMS.txt (docs/updates.md says what that does and does not protect
// against). Verified against the NIST example vectors in
// tests/unit/core/sha256_tests.cpp.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace pmm {

class Sha256 {
public:
    using Digest = std::array<std::uint8_t, 32>;

    Sha256() noexcept;
    void update(std::span<const std::uint8_t> data) noexcept;
    void update(std::string_view data) noexcept;
    Digest finish() noexcept; // resets the object afterwards

    static Digest hash(std::span<const std::uint8_t> data) noexcept;
    static Digest hash(std::string_view data) noexcept;
    static std::string toHex(const Digest& digest); // lowercase

private:
    void compress(const std::uint8_t* block) noexcept;
    void reset() noexcept;

    std::array<std::uint32_t, 8> m_state{};
    std::array<std::uint8_t, 64> m_buffer{};
    std::size_t m_bufferLen = 0;
    std::uint64_t m_totalLen = 0;
};

std::string sha256Hex(std::string_view data);
std::string sha256Hex(std::span<const std::uint8_t> data);

} // namespace pmm
