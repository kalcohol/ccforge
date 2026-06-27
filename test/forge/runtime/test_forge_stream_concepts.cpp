#include <gtest/gtest.h>

#include <forge/io/memory_stream.hpp>
#include <forge/io/stream.hpp>
#include "forge_io_test_bytes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

struct not_a_stream {};

struct wrong_read_result {
    auto read_some(forge::io::mutable_buffer) -> std::size_t;
};

struct read_takes_const_buffer {
    auto read_some(forge::io::const_buffer)
        -> forge::io::io_result<std::size_t>;
};

struct read_takes_const_buffer_rvalue {
    auto read_some(forge::io::const_buffer&&)
        -> forge::io::io_result<std::size_t>;
};

struct read_takes_mutable_buffer_lvalue {
    auto read_some(forge::io::mutable_buffer&)
        -> forge::io::io_result<std::size_t>;
};

struct write_takes_mutable_buffer {
    auto write_some(forge::io::mutable_buffer)
        -> forge::io::io_result<std::size_t>;
};

struct write_takes_const_buffer_lvalue {
    auto write_some(forge::io::const_buffer&)
        -> forge::io::io_result<std::size_t>;
};

class write_error_after_prefix_stream {
public:
    [[nodiscard]] auto bytes() const noexcept -> std::span<const std::byte> {
        return std::span<const std::byte>{storage_.data(), storage_.size()};
    }

    [[nodiscard]] auto write_some(forge::io::const_buffer input)
        -> forge::io::io_result<std::size_t> {
        if (first_) {
            first_ = false;
            const auto count = std::min<std::size_t>(2, input.size());
            storage_.resize(count);
            forge::io::buffer_copy(
                forge::io::mutable_buffer{storage_.data(), storage_.size()},
                forge::io::buffer_prefix(count, input));
            return forge::io::io_result<std::size_t>::success(count);
        }
        return forge::io::io_result<std::size_t>::failure(
            std::make_error_code(std::errc::connection_reset),
            0);
    }

private:
    bool first_ = true;
    std::vector<std::byte> storage_{};
};

auto read_erased_packet(forge::io::any_read_stream& stream)
    -> forge::io::io_result<std::string> {
    std::array<std::byte, 1> length_storage{};
    auto length_result = forge::io::read_exactly(
        stream,
        forge::io::mutable_buffer{std::span{length_storage}});
    auto [length_error, length_count] = length_result;
    if (length_error) {
        return forge::io::io_result<std::string>::failure(
            length_error,
            std::string{});
    }
    if (length_result.eof()) {
        return forge::io::io_result<std::string>::end_of_file(std::string{});
    }

    EXPECT_EQ(length_count, 1u);
    const auto expected =
        static_cast<std::size_t>(std::to_integer<unsigned char>(
            length_storage[0]));
    std::string payload(expected, '\0');
    auto payload_result = forge::io::read_exactly(
        stream,
        forge::io::mutable_buffer{payload.data(), payload.size()});
    auto [payload_error, payload_count] = payload_result;
    if (payload_error) {
        payload.resize(payload_count);
        return forge::io::io_result<std::string>::failure(
            payload_error,
            std::move(payload));
    }
    if (payload_result.eof()) {
        payload.resize(payload_count);
        return forge::io::io_result<std::string>::end_of_file(
            std::move(payload));
    }

    EXPECT_EQ(payload_count, expected);
    return forge::io::io_result<std::string>::success(std::move(payload));
}

} // namespace

static_assert(forge::io::read_stream<forge::io::memory_read_stream>);
static_assert(forge::io::write_stream<forge::io::memory_write_stream>);
static_assert(forge::io::read_write_stream<forge::io::memory_stream>);
static_assert(forge::io::read_stream<forge::io::scripted_read_stream>);
static_assert(forge::io::read_stream<forge::io::any_read_stream>);
static_assert(forge::io::write_stream<forge::io::any_write_stream>);

static_assert(!forge::io::read_stream<not_a_stream>);
static_assert(!forge::io::write_stream<not_a_stream>);
static_assert(!forge::io::read_stream<wrong_read_result>);
static_assert(!forge::io::read_stream<read_takes_const_buffer>);
static_assert(!forge::io::read_stream<read_takes_const_buffer_rvalue>);
static_assert(!forge::io::read_stream<read_takes_mutable_buffer_lvalue>);
static_assert(!forge::io::write_stream<write_takes_mutable_buffer>);
static_assert(!forge::io::write_stream<write_takes_const_buffer_lvalue>);
static_assert(!forge::io::write_stream<forge::io::memory_read_stream>);
static_assert(!forge::io::read_stream<forge::io::memory_write_stream>);

TEST(ForgeStreamConceptsTest, ReadExactlyConsumesShortReads) {
    forge::io::memory_read_stream stream{"hello", 2};
    std::array<char, 5> output{};

    auto [error, count] = forge::io::read_exactly(
        stream,
        forge::io::mutable_buffer{std::span{output}});

    EXPECT_FALSE(error);
    EXPECT_EQ(count, output.size());
    EXPECT_EQ(std::string_view(output.data(), output.size()), "hello");
}

TEST(ForgeStreamConceptsTest, ReadExactlyReturnsPartialCountOnEof) {
    forge::io::memory_read_stream stream{"hi"};
    std::array<char, 4> output{};

    auto result = forge::io::read_exactly(
        stream,
        forge::io::mutable_buffer{std::span{output}});
    auto [error, count] = result;

    EXPECT_FALSE(error);
    EXPECT_TRUE(result.eof());
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(std::string_view(output.data(), count), "hi");
}

TEST(ForgeStreamConceptsTest, ReadExactlyPropagatesErrorWithProgress) {
    forge::io::scripted_read_stream stream{
        forge::io::scripted_read_step::bytes("ab"),
        forge::io::scripted_read_step::bytes_then_error(
            "c",
            std::make_error_code(std::errc::connection_reset))};
    std::array<char, 4> output{};

    auto [error, count] = forge::io::read_exactly(
        stream,
        forge::io::mutable_buffer{std::span{output}});

    EXPECT_EQ(error, std::make_error_code(std::errc::connection_reset));
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(std::string_view(output.data(), count), "abc");
}

TEST(ForgeStreamConceptsTest, ReadUntilConsumesShortReads) {
    forge::io::memory_read_stream stream{"ab\nrest", 1};
    std::string line{"stale"};

    auto [error, count] = forge::io::read_until(stream, line);

    EXPECT_FALSE(error);
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(line, "ab\n");
    EXPECT_EQ(stream.position(), 3u);
}

TEST(ForgeStreamConceptsTest, ReadUntilReturnsPartialLineOnEof) {
    forge::io::memory_read_stream stream{"partial", 2};
    std::string line;

    auto result = forge::io::read_until(stream, line);
    auto [error, count] = result;

    EXPECT_FALSE(error);
    EXPECT_TRUE(result.eof());
    EXPECT_EQ(count, 7u);
    EXPECT_EQ(line, "partial");
}

TEST(ForgeStreamConceptsTest, ReadUntilPropagatesErrorWithProgress) {
    forge::io::scripted_read_stream stream{
        forge::io::scripted_read_step::bytes("ab"),
        forge::io::scripted_read_step::bytes_then_error(
            "c",
            std::make_error_code(std::errc::connection_reset))};
    std::string line;

    auto [error, count] = forge::io::read_until(stream, line);

    EXPECT_EQ(error, std::make_error_code(std::errc::connection_reset));
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(line, "abc");
}

TEST(ForgeStreamConceptsTest, ReadUntilLimitsRecordSize) {
    forge::io::memory_read_stream stream{"abcd\n", 2};
    std::string line;

    auto [error, count] = forge::io::read_until(stream, line, '\n', 3);

    EXPECT_EQ(error, std::make_error_code(std::errc::message_size));
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(line, "abc");
}

TEST(ForgeStreamConceptsTest, WriteAllConsumesShortWrites) {
    forge::io::memory_write_stream stream;

    auto [error, count] = forge::io::write_all(
        stream,
        forge::io::const_buffer{"forge", 5});

    EXPECT_FALSE(error);
    EXPECT_EQ(count, 5u);
    EXPECT_EQ(forge_test::to_string(stream.bytes()), "forge");
}

TEST(ForgeStreamConceptsTest, WriteAllReturnsPartialCountOnCapacity) {
    forge::io::memory_write_stream stream{2};

    auto [error, count] = forge::io::write_all(
        stream,
        forge::io::const_buffer{"abcd", 4});

    EXPECT_EQ(error, std::make_error_code(std::errc::io_error));
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(forge_test::to_string(stream.bytes()), "ab");
}

TEST(ForgeStreamConceptsTest, WriteAllPropagatesStreamErrorWithProgress) {
    write_error_after_prefix_stream stream;

    auto [error, count] = forge::io::write_all(
        stream,
        forge::io::const_buffer{"abcd", 4});

    EXPECT_EQ(error, std::make_error_code(std::errc::connection_reset));
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(forge_test::to_string(stream.bytes()), "ab");
}

TEST(ForgeStreamConceptsTest, AnyReadStreamWrapsBorrowedStream) {
    forge::io::memory_read_stream stream{"forge", 2};
    forge::io::any_read_stream erased{stream};
    std::array<char, 5> output{};

    auto [error, count] = forge::io::read_exactly(
        erased,
        forge::io::mutable_buffer{std::span{output}});

    EXPECT_TRUE(erased);
    EXPECT_FALSE(error);
    EXPECT_EQ(count, output.size());
    EXPECT_EQ(std::string_view(output.data(), output.size()), "forge");
}

TEST(ForgeStreamConceptsTest, AnyWriteStreamWrapsBorrowedStream) {
    forge::io::memory_write_stream stream;
    forge::io::any_write_stream erased{stream};

    auto [error, count] = forge::io::write_all(
        erased,
        forge::io::const_buffer{"io", 2});

    EXPECT_TRUE(erased);
    EXPECT_FALSE(error);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(forge_test::to_string(stream.bytes()), "io");
}

TEST(ForgeStreamConceptsTest, GenericProtocolAcceptsErasedReadStream) {
    auto prefix = forge_test::to_bytes("he");
    prefix.insert(prefix.begin(), std::byte{5});
    forge::io::scripted_read_stream concrete{
        forge::io::scripted_read_step::bytes(
            forge::io::const_buffer{prefix.data(), prefix.size()}),
        forge::io::scripted_read_step::bytes("llo")};
    forge::io::any_read_stream erased{concrete};

    auto [error, payload] = read_erased_packet(erased);

    EXPECT_FALSE(error);
    EXPECT_EQ(payload, "hello");
}

TEST(ForgeStreamConceptsTest, ErasedStreamCopiesShareBorrowedTarget) {
    forge::io::memory_read_stream stream{"ab"};
    forge::io::any_read_stream first{stream};
    forge::io::any_read_stream second = first;
    std::array<char, 1> output{};

    auto [second_error, second_count] = second.read_some(
        forge::io::mutable_buffer{std::span{output}});
    EXPECT_FALSE(second_error);
    EXPECT_EQ(second_count, 1u);
    EXPECT_EQ(output[0], 'a');

    auto [first_error, first_count] = first.read_some(
        forge::io::mutable_buffer{std::span{output}});
    EXPECT_FALSE(first_error);
    EXPECT_EQ(first_count, 1u);
    EXPECT_EQ(output[0], 'b');
}

TEST(ForgeStreamConceptsTest, EmptyErasedStreamsReportError) {
    forge::io::any_read_stream read;
    forge::io::any_write_stream write;
    std::array<char, 1> output{};

    auto [read_error, read_count] = read.read_some(
        forge::io::mutable_buffer{std::span{output}});
    auto [write_error, write_count] = write.write_some(
        forge::io::const_buffer{"x", 1});

    EXPECT_EQ(read_error, std::make_error_code(std::errc::bad_address));
    EXPECT_EQ(read_count, 0u);
    EXPECT_EQ(write_error, std::make_error_code(std::errc::bad_address));
    EXPECT_EQ(write_count, 0u);
}
