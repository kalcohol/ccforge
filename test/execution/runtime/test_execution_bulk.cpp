#include <gtest/gtest.h>

#include <execution>

#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <vector>

namespace {

template<class Sender>
using signatures_t = decltype(std::execution::get_completion_signatures(
    std::declval<Sender>(),
    std::execution::empty_env{}));

} // namespace

TEST(BulkTest, UnchunkedDirectVisitsEveryIndex) {
    std::vector<int> indexes;

    auto sndr = std::execution::bulk_unchunked(
        std::execution::just(10),
        4,
        [&indexes](int index, int& value) {
            indexes.push_back(index);
            value += index;
        });

    using cs_t = signatures_t<decltype(sndr)>;
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(std::exception_ptr)>>);

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(indexes, (std::vector<int>{0, 1, 2, 3}));
    EXPECT_EQ(std::get<0>(*result), 16);
}

#if defined(__GLIBCXX__) || defined(_MSC_VER)
TEST(BulkTest, UnchunkedAcceptsStandardPolicyParameter) {
    static_assert(std::is_execution_policy_v<
                  std::remove_cvref_t<decltype(std::execution::seq)>>);

    std::vector<int> indexes;

    auto sndr = std::execution::bulk_unchunked(
        std::execution::just(5),
        std::execution::seq,
        3,
        [&indexes](int index, int& value) {
            indexes.push_back(index);
            value += index;
        });

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(indexes, (std::vector<int>{0, 1, 2}));
    EXPECT_EQ(std::get<0>(*result), 8);
}
#endif

TEST(BulkTest, UnchunkedPipeVisitsEveryIndex) {
    int sum = 0;

    auto sndr = std::execution::just(1)
              | std::execution::bulk_unchunked(5, [&sum](int index, int& value) {
                    sum += index;
                    value += 2 * index;
                });
    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(sum, 0 + 1 + 2 + 3 + 4);
    EXPECT_EQ(std::get<0>(*result), 1 + 2 * (0 + 1 + 2 + 3 + 4));
}

#if defined(__GLIBCXX__) || defined(_MSC_VER)
TEST(BulkTest, ChunkedPipeAcceptsStandardPolicyParameter) {
    int calls = 0;

    auto sndr = std::execution::just(2)
              | std::execution::bulk_chunked(
                    std::execution::seq,
                    4,
                    [&calls](int begin, int end, int& value) {
                        ++calls;
                        value += end - begin;
                    });
    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(std::get<0>(*result), 6);
}
#endif

TEST(BulkTest, ChunkedDirectUsesSingleSerialChunk) {
    std::vector<std::pair<int, int>> chunks;

    auto sndr = std::execution::bulk_chunked(
        std::execution::just(3),
        6,
        [&chunks](int begin, int end, int& value) {
            chunks.emplace_back(begin, end);
            for (int index = begin; index != end; ++index) {
                value += index;
            }
        });

    using cs_t = signatures_t<decltype(sndr)>;
    static_assert(std::is_same_v<cs_t,
        std::execution::completion_signatures<
            std::execution::set_value_t(int),
            std::execution::set_error_t(std::exception_ptr)>>);

    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(chunks.size(), 1u);
    EXPECT_EQ(chunks[0], (std::pair<int, int>{0, 6}));
    EXPECT_EQ(std::get<0>(*result), 3 + (0 + 1 + 2 + 3 + 4 + 5));
}

TEST(BulkTest, ChunkedPipeUsesSingleSerialChunk) {
    int calls = 0;

    auto sndr = std::execution::just(2)
              | std::execution::bulk_chunked(4, [&calls](int begin, int end, int& value) {
                    ++calls;
                    value *= end - begin;
                });
    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(std::get<0>(*result), 8);
}

TEST(BulkTest, ZeroShapeDoesNotCallFunction) {
    int unchunked_calls = 0;
    auto unchunked = std::execution::bulk_unchunked(
        std::execution::just(7),
        0,
        [&unchunked_calls](int, int&) {
            ++unchunked_calls;
        });

    int chunked_calls = 0;
    auto chunked = std::execution::bulk_chunked(
        std::execution::just(9),
        0,
        [&chunked_calls](int, int, int&) {
            ++chunked_calls;
        });

    auto unchunked_result = std::execution::sync_wait(std::move(unchunked));
    auto chunked_result = std::execution::sync_wait(std::move(chunked));

    ASSERT_TRUE(unchunked_result.has_value());
    ASSERT_TRUE(chunked_result.has_value());
    EXPECT_EQ(unchunked_calls, 0);
    EXPECT_EQ(chunked_calls, 0);
    EXPECT_EQ(std::get<0>(*unchunked_result), 7);
    EXPECT_EQ(std::get<0>(*chunked_result), 9);
}

TEST(BulkTest, NegativeShapeDoesNotCallFunction) {
    constexpr auto negative_shape = std::int8_t{-1};

    int bulk_calls = 0;
    auto bulk = std::execution::bulk(
        std::execution::just(5),
        negative_shape,
        [&bulk_calls](std::int8_t, int&) {
            ++bulk_calls;
        });

    int unchunked_calls = 0;
    auto unchunked = std::execution::bulk_unchunked(
        std::execution::just(7),
        negative_shape,
        [&unchunked_calls](std::int8_t, int&) {
            ++unchunked_calls;
        });

    int chunked_calls = 0;
    auto chunked = std::execution::bulk_chunked(
        std::execution::just(9),
        negative_shape,
        [&chunked_calls](std::int8_t, std::int8_t, int&) {
            ++chunked_calls;
        });

    auto bulk_result = std::execution::sync_wait(std::move(bulk));
    auto unchunked_result = std::execution::sync_wait(std::move(unchunked));
    auto chunked_result = std::execution::sync_wait(std::move(chunked));

    ASSERT_TRUE(bulk_result.has_value());
    ASSERT_TRUE(unchunked_result.has_value());
    ASSERT_TRUE(chunked_result.has_value());
    EXPECT_EQ(bulk_calls, 0);
    EXPECT_EQ(unchunked_calls, 0);
    EXPECT_EQ(chunked_calls, 0);
    EXPECT_EQ(std::get<0>(*bulk_result), 5);
    EXPECT_EQ(std::get<0>(*unchunked_result), 7);
    EXPECT_EQ(std::get<0>(*chunked_result), 9);
}

TEST(BulkTest, UnchunkedFunctionExceptionBecomesExceptionPtrError) {
    auto sndr = std::execution::bulk_unchunked(
        std::execution::just(0),
        3,
        [](int index, int&) {
            if (index == 1) {
                throw std::runtime_error("bulk unchunked");
            }
        });

    EXPECT_THROW((void)std::execution::sync_wait(std::move(sndr)), std::runtime_error);
}

TEST(BulkTest, ChunkedFunctionExceptionBecomesExceptionPtrError) {
    auto sndr = std::execution::bulk_chunked(
        std::execution::just(0),
        3,
        [](int, int, int&) {
            throw std::runtime_error("bulk chunked");
        });

    EXPECT_THROW((void)std::execution::sync_wait(std::move(sndr)), std::runtime_error);
}

TEST(BulkTest, MoveOnlySenderUsesExplicitMove) {
    auto move_only = std::execution::just(4)
                   | std::execution::then(
                         [owned = std::make_unique<int>(5)](int value) {
                             return value + *owned;
                         });

    auto sndr = std::execution::bulk_unchunked(
        std::move(move_only),
        2,
        [](int index, int& value) {
            value += index;
        });
    auto result = std::execution::sync_wait(std::move(sndr));

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(std::get<0>(*result), 10);
}
