#include <gtest/gtest.h>

#include <forge/io/buffer.hpp>
#include <forge/io/result.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>

namespace {

auto sample_success() -> forge::io::io_result<std::size_t> {
    return forge::io::io_result<std::size_t>::success(4);
}

auto sample_error() -> forge::io::io_result<std::size_t> {
    return forge::io::io_result<std::size_t>::failure(
        std::make_error_code(std::errc::connection_reset),
        2);
}

} // namespace

static_assert(std::tuple_size_v<forge::io::io_result<>> == 1);
static_assert(std::tuple_size_v<forge::io::io_result<std::size_t>> == 2);
static_assert(std::is_same_v<
    std::tuple_element_t<0, forge::io::io_result<std::size_t>>,
    std::error_code>);
static_assert(std::is_same_v<
    std::tuple_element_t<1, forge::io::io_result<std::size_t>>,
    std::size_t>);

TEST(ForgeByteVocabularyTest, IoResultSupportsStructuredBinding) {
    auto result = sample_success();

    auto [ec, count] = result;

    EXPECT_FALSE(ec);
    EXPECT_EQ(count, 4u);
    EXPECT_TRUE(result);
    EXPECT_TRUE(result.has_value());
}

TEST(ForgeByteVocabularyTest, IoResultRetainsPayloadOnError) {
    EXPECT_NO_THROW(([&] {
        auto result = sample_error();

        auto [ec, count] = result;

        EXPECT_EQ(ec, std::make_error_code(std::errc::connection_reset));
        EXPECT_EQ(count, 2u);
        EXPECT_FALSE(result);
        EXPECT_FALSE(result.has_value());
    }()));
}

TEST(ForgeByteVocabularyTest, IoResultCanExposeReferences) {
    forge::io::io_result<std::size_t> result{{}, 1};

    auto& [ec, count] = result;
    count = 9;
    ec = std::make_error_code(std::errc::timed_out);

    EXPECT_EQ(forge::io::get<1>(result), 9u);
    EXPECT_EQ(result.error(), std::make_error_code(std::errc::timed_out));
}

TEST(ForgeByteVocabularyTest, IoResultWithoutPayloadStillBindsError) {
    forge::io::io_result<> result{std::make_error_code(std::errc::broken_pipe)};

    auto [ec] = result;

    EXPECT_EQ(ec, std::make_error_code(std::errc::broken_pipe));
}

TEST(ForgeByteVocabularyTest, BuffersExposeBorrowedByteRegions) {
    std::array<std::byte, 4> bytes{};
    forge::io::mutable_buffer mut{std::span{bytes}};
    forge::io::const_buffer con{mut};

    EXPECT_EQ(mut.data(), bytes.data());
    EXPECT_EQ(mut.size(), bytes.size());
    EXPECT_EQ(con.data(), bytes.data());
    EXPECT_EQ(con.size(), bytes.size());
    EXPECT_FALSE(forge::io::buffer_empty(mut));
    EXPECT_EQ(forge::io::buffer_size(con), bytes.size());
}

TEST(ForgeByteVocabularyTest, BuffersAcceptCharacterSpansExplicitly) {
    char text[] = "abcd";
    forge::io::mutable_buffer mut{std::span{text, 4}};
    forge::io::const_buffer con{std::span<const char>{text, 4}};

    EXPECT_EQ(mut.size(), 4u);
    EXPECT_EQ(con.size(), 4u);
}

TEST(ForgeByteVocabularyTest, BufferPrefixClampsSize) {
    std::array<std::byte, 8> bytes{};

    auto mut = forge::io::buffer_prefix(
        3,
        forge::io::mutable_buffer{std::span{bytes}});
    auto con = forge::io::buffer_prefix(
        99,
        forge::io::const_buffer{std::span{bytes}});

    EXPECT_EQ(mut.size(), 3u);
    EXPECT_EQ(mut.data(), bytes.data());
    EXPECT_EQ(con.size(), bytes.size());
}

TEST(ForgeByteVocabularyTest, BufferSizeSumsSequences) {
    std::array<std::byte, 2> first{};
    std::array<std::byte, 3> second{};
    std::array<forge::io::const_buffer, 2> buffers{
        forge::io::const_buffer{std::span{first}},
        forge::io::const_buffer{std::span{second}}};

    EXPECT_EQ(forge::io::buffer_size(buffers), 5u);
    EXPECT_FALSE(forge::io::buffer_empty(buffers));
}

TEST(ForgeByteVocabularyTest, BufferCopyCopiesSingleBuffers) {
    constexpr std::string_view source = "forge";
    std::array<char, 8> dest{};

    auto copied = forge::io::buffer_copy(
        forge::io::mutable_buffer{std::span{dest}},
        forge::io::const_buffer{source.data(), source.size()});

    EXPECT_EQ(copied, source.size());
    EXPECT_EQ(std::string_view(dest.data(), copied), source);
}

TEST(ForgeByteVocabularyTest, BufferCopyCrossesScatterGatherBoundaries) {
    constexpr std::string_view first = "ab";
    constexpr std::string_view second = "cdef";
    std::array<char, 3> out1{};
    std::array<char, 3> out2{};

    std::array<forge::io::const_buffer, 2> sources{
        forge::io::const_buffer{first.data(), first.size()},
        forge::io::const_buffer{second.data(), second.size()}};
    std::array<forge::io::mutable_buffer, 2> dests{
        forge::io::mutable_buffer{std::span{out1}},
        forge::io::mutable_buffer{std::span{out2}}};

    auto copied = forge::io::buffer_copy(dests, sources);

    EXPECT_EQ(copied, 6u);
    EXPECT_EQ(std::string_view(out1.data(), out1.size()), "abc");
    EXPECT_EQ(std::string_view(out2.data(), out2.size()), "def");
}
