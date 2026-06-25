#include <gtest/gtest.h>

#include <forge/io/memory_stream.hpp>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

auto to_bytes(std::string_view text) -> std::vector<std::byte> {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (char ch : text) {
        bytes.push_back(std::byte{static_cast<unsigned char>(ch)});
    }
    return bytes;
}

auto to_string(std::span<const std::byte> bytes) -> std::string {
    std::string text;
    text.reserve(bytes.size());
    for (std::byte byte : bytes) {
        text.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return text;
}

template<class Stream>
auto read_length_prefixed_packet(Stream& stream)
    -> forge::io::io_result<std::string> {
    std::array<std::byte, 1> length_storage{};
    auto [length_error, length_count] = stream.read_some(
        forge::io::mutable_buffer{std::span{length_storage}});
    if (length_error) {
        return forge::io::io_result<std::string>::failure(
            length_error,
            std::string{});
    }
    if (length_count != 1) {
        return forge::io::io_result<std::string>::failure(
            std::make_error_code(std::errc::io_error),
            std::string{});
    }

    const auto expected =
        static_cast<std::size_t>(std::to_integer<unsigned char>(
            length_storage[0]));
    std::string payload(expected, '\0');
    std::size_t offset = 0;
    while (offset < expected) {
        auto [read_error, read_count] = stream.read_some(
            forge::io::mutable_buffer{
                payload.data() + offset,
                payload.size() - offset});
        offset += read_count;
        if (read_error) {
            payload.resize(offset);
            return forge::io::io_result<std::string>::failure(
                read_error,
                std::move(payload));
        }
        if (read_count == 0) {
            payload.resize(offset);
            return forge::io::io_result<std::string>::failure(
                std::make_error_code(std::errc::io_error),
                std::move(payload));
        }
    }

    return forge::io::io_result<std::string>::success(std::move(payload));
}

} // namespace

TEST(ForgeMemoryStreamsTest, MemoryReadStreamReadsAllBytesInChunks) {
    forge::io::memory_read_stream stream{"abcdef", 2};
    std::array<char, 4> buffer{};

    auto [first_error, first_count] = stream.read_some(
        forge::io::mutable_buffer{std::span{buffer}});
    EXPECT_FALSE(first_error);
    EXPECT_EQ(first_count, 2u);
    EXPECT_EQ(std::string_view(buffer.data(), first_count), "ab");
    EXPECT_EQ(stream.remaining(), 4u);

    auto [second_error, second_count] = stream.read_some(
        forge::io::mutable_buffer{std::span{buffer}});
    EXPECT_FALSE(second_error);
    EXPECT_EQ(second_count, 2u);
    EXPECT_EQ(std::string_view(buffer.data(), second_count), "cd");

    auto [third_error, third_count] = stream.read_some(
        forge::io::mutable_buffer{std::span{buffer}});
    EXPECT_FALSE(third_error);
    EXPECT_EQ(third_count, 2u);
    EXPECT_EQ(std::string_view(buffer.data(), third_count), "ef");

    auto [eof_error, eof_count] = stream.read_some(
        forge::io::mutable_buffer{std::span{buffer}});
    EXPECT_FALSE(eof_error);
    EXPECT_EQ(eof_count, 0u);
    EXPECT_TRUE(stream.eof());
}

TEST(ForgeMemoryStreamsTest, MemoryReadStreamZeroLengthDoesNotConsume) {
    forge::io::memory_read_stream stream{"abc"};
    std::array<char, 1> output{};

    auto [zero_error, zero_count] = stream.read_some(
        forge::io::mutable_buffer{output.data(), 0});
    EXPECT_FALSE(zero_error);
    EXPECT_EQ(zero_count, 0u);
    EXPECT_EQ(stream.position(), 0u);

    auto [read_error, read_count] = stream.read_some(
        forge::io::mutable_buffer{std::span{output}});
    EXPECT_FALSE(read_error);
    EXPECT_EQ(read_count, 1u);
    EXPECT_EQ(output[0], 'a');
}

TEST(ForgeMemoryStreamsTest, MemoryWriteStreamAppendsAndShortWritesOnCapacity) {
    forge::io::memory_write_stream stream{5};

    auto [first_error, first_count] = stream.write_some(
        forge::io::const_buffer{"abc", 3});
    EXPECT_FALSE(first_error);
    EXPECT_EQ(first_count, 3u);

    auto [second_error, second_count] = stream.write_some(
        forge::io::const_buffer{"defg", 4});
    EXPECT_FALSE(second_error);
    EXPECT_EQ(second_count, 2u);
    EXPECT_EQ(to_string(stream.bytes()), "abcde");

    auto [full_error, full_count] = stream.write_some(
        forge::io::const_buffer{"z", 1});
    EXPECT_FALSE(full_error);
    EXPECT_EQ(full_count, 0u);
}

TEST(ForgeMemoryStreamsTest, MemoryWriteStreamCanUseBorrowedOutput) {
    std::array<char, 4> storage{};
    forge::io::memory_write_stream stream{
        forge::io::mutable_buffer{std::span{storage}}};

    auto [error, count] = stream.write_some(
        forge::io::const_buffer{"forge", 5});

    EXPECT_FALSE(error);
    EXPECT_EQ(count, 4u);
    EXPECT_EQ(std::string_view(storage.data(), storage.size()), "forg");
    EXPECT_EQ(to_string(stream.bytes()), "forg");
}

TEST(ForgeMemoryStreamsTest, MemoryWriteStreamZeroLengthDoesNotGrow) {
    forge::io::memory_write_stream stream;

    auto [error, count] = stream.write_some(forge::io::const_buffer{nullptr, 0});

    EXPECT_FALSE(error);
    EXPECT_EQ(count, 0u);
    EXPECT_TRUE(stream.bytes().empty());
}

TEST(ForgeMemoryStreamsTest, MemoryStreamCombinesReadAndWriteSides) {
    forge::io::memory_stream stream{forge::io::const_buffer{"rx", 2}, 3};
    std::array<char, 4> input{};

    auto [read_error, read_count] = stream.read_some(
        forge::io::mutable_buffer{std::span{input}});
    EXPECT_FALSE(read_error);
    EXPECT_EQ(read_count, 2u);
    EXPECT_EQ(std::string_view(input.data(), read_count), "rx");

    auto [write_error, write_count] = stream.write_some(
        forge::io::const_buffer{"reply", 5});
    EXPECT_FALSE(write_error);
    EXPECT_EQ(write_count, 3u);
    EXPECT_EQ(to_string(stream.written_bytes()), "rep");
}

TEST(ForgeMemoryStreamsTest, ScriptedReadStreamForcesShortReadsAndEof) {
    forge::io::scripted_read_stream stream{
        forge::io::scripted_read_step::bytes("he"),
        forge::io::scripted_read_step::bytes("llo"),
        forge::io::scripted_read_step::eof()};
    std::array<char, 8> buffer{};

    auto [first_error, first_count] = stream.read_some(
        forge::io::mutable_buffer{std::span{buffer}});
    EXPECT_FALSE(first_error);
    EXPECT_EQ(first_count, 2u);
    EXPECT_EQ(std::string_view(buffer.data(), first_count), "he");

    auto [second_error, second_count] = stream.read_some(
        forge::io::mutable_buffer{std::span{buffer}});
    EXPECT_FALSE(second_error);
    EXPECT_EQ(second_count, 3u);
    EXPECT_EQ(std::string_view(buffer.data(), second_count), "llo");

    auto [eof_error, eof_count] = stream.read_some(
        forge::io::mutable_buffer{std::span{buffer}});
    EXPECT_FALSE(eof_error);
    EXPECT_EQ(eof_count, 0u);
}

TEST(ForgeMemoryStreamsTest, ScriptedReadStreamKeepsResidualBytes) {
    forge::io::scripted_read_stream stream{
        forge::io::scripted_read_step::bytes("abcdef")};
    std::array<char, 2> buffer{};

    auto [first_error, first_count] = stream.read_some(
        forge::io::mutable_buffer{std::span{buffer}});
    EXPECT_FALSE(first_error);
    EXPECT_EQ(first_count, 2u);
    EXPECT_EQ(std::string_view(buffer.data(), first_count), "ab");

    auto [second_error, second_count] = stream.read_some(
        forge::io::mutable_buffer{std::span{buffer}});
    EXPECT_FALSE(second_error);
    EXPECT_EQ(second_count, 2u);
    EXPECT_EQ(std::string_view(buffer.data(), second_count), "cd");

    auto [third_error, third_count] = stream.read_some(
        forge::io::mutable_buffer{std::span{buffer}});
    EXPECT_FALSE(third_error);
    EXPECT_EQ(third_count, 2u);
    EXPECT_EQ(std::string_view(buffer.data(), third_count), "ef");
    EXPECT_TRUE(stream.empty());
}

TEST(ForgeMemoryStreamsTest, ScriptedReadStreamReturnsErrorWithByteCount) {
    forge::io::scripted_read_stream stream{
        forge::io::scripted_read_step::error(
            std::make_error_code(std::errc::connection_reset),
            3)};
    std::array<char, 8> buffer{};

    auto [error, count] = stream.read_some(
        forge::io::mutable_buffer{std::span{buffer}});

    EXPECT_EQ(error, std::make_error_code(std::errc::connection_reset));
    EXPECT_EQ(count, 3u);
}

TEST(ForgeMemoryStreamsTest, ScriptedReadStreamCanErrorAfterBytes) {
    forge::io::scripted_read_stream stream{
        forge::io::scripted_read_step::bytes_then_error(
            "abc",
            std::make_error_code(std::errc::timed_out))};
    std::array<char, 8> buffer{};

    auto [error, count] = stream.read_some(
        forge::io::mutable_buffer{std::span{buffer}});

    EXPECT_EQ(error, std::make_error_code(std::errc::timed_out));
    EXPECT_EQ(count, 3u);
    EXPECT_EQ(std::string_view(buffer.data(), count), "abc");
}

TEST(ForgeMemoryStreamsTest, ProtocolCanInspectPayloadOnPartialError) {
    auto prefix = to_bytes("hel");
    prefix.insert(prefix.begin(), std::byte{5});
    forge::io::scripted_read_stream stream{
        forge::io::scripted_read_step::bytes(
            forge::io::const_buffer{prefix.data(), prefix.size()}),
        forge::io::scripted_read_step::bytes_then_error(
            "lo",
            std::make_error_code(std::errc::connection_reset))};

    auto [error, payload] = read_length_prefixed_packet(stream);

    EXPECT_EQ(error, std::make_error_code(std::errc::connection_reset));
    EXPECT_EQ(payload, "hello");
}
