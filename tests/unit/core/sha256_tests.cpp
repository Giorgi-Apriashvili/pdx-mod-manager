#include "core/sha256.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using pmm::sha256Hex;

TEST_CASE("SHA-256 matches FIPS 180-4 example vectors", "[core][sha256]") {
    CHECK(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(sha256Hex(
              "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklm"
              "nopqrlmnopqrsmnopqrstnopqrstu") ==
          "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST_CASE("SHA-256 of one million 'a' characters", "[core][sha256]") {
    const std::string million(1'000'000, 'a');
    CHECK(sha256Hex(million) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("SHA-256 incremental updates equal one-shot hashing", "[core][sha256]") {
    const std::string text = "The quick brown fox jumps over the lazy dog, repeatedly, across "
                             "several block boundaries to exercise buffering logic in update().";
    for (std::size_t split = 0; split <= text.size(); ++split) {
        pmm::Sha256 h;
        h.update(std::string_view(text).substr(0, split));
        h.update(std::string_view(text).substr(split));
        REQUIRE(pmm::Sha256::toHex(h.finish()) == sha256Hex(text));
    }
}
