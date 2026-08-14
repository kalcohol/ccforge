#include <gtest/gtest.h>

#include <forge/io/buffer.hpp>
#include <forge/io/error.hpp>
#include <forge/io/result.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <span>
#include <stdexcept>
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

auto sample_eof() -> forge::io::io_result<std::size_t> {
    return forge::io::io_result<std::size_t>::end_of_file(3);
}

} // namespace

static_assert(std::tuple_size_v<forge::io::io_result<>> == 1);
static_assert(std::tuple_size_v<forge::io::io_result<std::size_t>> == 2);
static_assert(
    std::tuple_size_v<forge::io::io_result<std::size_t, std::string_view>> == 3);
static_assert(std::is_same_v<
    std::tuple_element_t<0, forge::io::io_result<std::size_t>>,
    std::error_code>);
static_assert(std::is_same_v<
    std::tuple_element_t<1, forge::io::io_result<std::size_t>>,
    std::size_t>);
static_assert(std::is_same_v<
    std::tuple_element_t<
        2,
        forge::io::io_result<std::size_t, std::string_view>>,
    std::string_view>);

TEST(ForgeByteVocabularyTest, IoResultSupportsStructuredBinding) {
    auto result = sample_success();

    auto [ec, count] = result;

    EXPECT_FALSE(ec);
    EXPECT_EQ(count, 4u);
    EXPECT_TRUE(result);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.status(), forge::io::io_status::value);
    EXPECT_FALSE(result.eof());
}

TEST(ForgeByteVocabularyTest, NonSystemExceptionsMapToUnknownTypedErrors) {
    auto error = forge::io::typed_detail::from_exception(
        std::make_exception_ptr(std::runtime_error{"typed IO failure"}));

    EXPECT_EQ(error.kind, forge::io::error_kind::unknown);
    EXPECT_FALSE(error.code);
}

TEST(ForgeByteVocabularyTest, IoResultRetainsPayloadOnError) {
    EXPECT_NO_THROW(([&] {
        auto result = sample_error();

        auto [ec, count] = result;

        EXPECT_EQ(ec, std::make_error_code(std::errc::connection_reset));
        EXPECT_EQ(count, 2u);
        EXPECT_FALSE(result);
        EXPECT_FALSE(result.has_value());
        EXPECT_EQ(result.status(), forge::io::io_status::error);
        EXPECT_FALSE(result.eof());
    }()));
}

TEST(ForgeByteVocabularyTest, IoResultRetainsPayloadOnEof) {
    auto result = sample_eof();

    auto [ec, count] = result;

    EXPECT_FALSE(ec);
    EXPECT_EQ(count, 3u);
    EXPECT_FALSE(result);
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.eof());
    EXPECT_EQ(result.status(), forge::io::io_status::eof);
}

TEST(ForgeByteVocabularyTest, IoResultSupportsMultiplePayloadValues) {
    using result_t = forge::io::io_result<std::size_t, std::string_view>;
    auto result = result_t::end_of_file(7, "tail");

    auto [ec, count, label] = result;

    EXPECT_FALSE(ec);
    EXPECT_EQ(count, 7u);
    EXPECT_EQ(label, "tail");
    EXPECT_FALSE(result.has_value());
    EXPECT_TRUE(result.eof());
    EXPECT_EQ(forge::io::get<1>(result), 7u);
    EXPECT_EQ(forge::io::get<2>(result), "tail");
}

TEST(ForgeByteVocabularyTest, IoResultCanExposeReferences) {
    forge::io::io_result<std::size_t> result{{}, 1};

    auto& [ec, count] = result;
    count = 9;
    ec = std::make_error_code(std::errc::timed_out);

    EXPECT_EQ(forge::io::get<1>(result), 9u);
    EXPECT_EQ(result.error(), std::make_error_code(std::errc::timed_out));
    EXPECT_EQ(result.status(), forge::io::io_status::error);
    EXPECT_FALSE(result.has_value());
}

TEST(ForgeByteVocabularyTest, IoResultWithoutPayloadStillBindsError) {
    forge::io::io_result<> result{std::make_error_code(std::errc::broken_pipe)};

    auto [ec] = result;

    EXPECT_EQ(ec, std::make_error_code(std::errc::broken_pipe));
    EXPECT_EQ(result.status(), forge::io::io_status::error);
}

TEST(ForgeByteVocabularyTest, IoResultWithoutPayloadCanRepresentEof) {
    auto result = forge::io::io_result<>::end_of_file();

    auto [ec] = result;

    EXPECT_FALSE(ec);
    EXPECT_FALSE(result);
    EXPECT_TRUE(result.eof());
    EXPECT_EQ(result.status(), forge::io::io_status::eof);
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

TEST(ForgeByteVocabularyTest, BufferCopySupportsOverlappingSingleBuffers) {
    constexpr std::size_t size = 4096;
    std::array<char, size + 1> shift_left{};
    for (std::size_t i = 0; i < shift_left.size(); ++i) {
        shift_left[i] = static_cast<char>(i % 127);
    }
    const auto original_left = shift_left;
    auto left_count = forge::io::buffer_copy(
        forge::io::mutable_buffer{shift_left.data(), size},
        forge::io::const_buffer{shift_left.data() + 1, size});

    EXPECT_EQ(left_count, size);
    EXPECT_TRUE(std::equal(
        shift_left.begin(),
        shift_left.begin() + static_cast<std::ptrdiff_t>(size),
        original_left.begin() + 1));

    std::array<char, size + 1> shift_right{};
    for (std::size_t i = 0; i < shift_right.size(); ++i) {
        shift_right[i] = static_cast<char>(i % 127);
    }
    const auto original_right = shift_right;
    auto right_count = forge::io::buffer_copy(
        forge::io::mutable_buffer{shift_right.data() + 1, size},
        forge::io::const_buffer{shift_right.data(), size});

    EXPECT_EQ(right_count, size);
    EXPECT_TRUE(std::equal(
        shift_right.begin() + 1,
        shift_right.end(),
        original_right.begin()));
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
