#include "../unique_mapping_probe.hpp"

#include <gtest/gtest.h>

namespace {

namespace p = unique_mapping_probe;

template<std::size_t N>
void expect_calls(const std::array<bool, N>& calls, bool expected) {
    for (std::size_t i = 0; i < N; ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(calls[i], expected);
    }
}

} // namespace

TEST(LinalgUniqueOutputs, OrdinaryWritesRequireAlwaysUniqueMapping) {
    expect_calls(p::matrix_writes<p::upper_matrix>, false);
    expect_calls(p::matrix_writes<p::lower_matrix>, false);
    expect_calls(p::matrix_writes<p::weak_matrix>, false);
    expect_calls(p::vector_writes<p::weak_vector>, false);
    expect_calls(p::complex_rotations<p::weak_complex_vector>, false);

    double data[] = {1.0, 2.0};
    p::weak_vector values(data);
    EXPECT_TRUE(values.mapping().is_unique());
    EXPECT_FALSE(decltype(values)::is_always_unique());
}

TEST(LinalgUniqueOutputs, StructuredWritesRetainOnlyThePackedException) {
    expect_calls(p::structured_writes<p::upper_matrix>, true);
    expect_calls(p::structured_writes<p::lower_matrix, p::la::lower_triangle_t>, true);
    expect_calls(p::structured_writes<p::weak_matrix>, false);
    expect_calls(p::structured_writes<p::matrix>, true);
}

TEST(LinalgUniqueOutputs, StaticDegeneratePackedMappingsRemainOrdinaryOutputs) {
    using extents = std::extents<int, 1, 1>;
    using scalar_packed = std::mdspan<double, extents, p::upper_layout>;
    double source[] = {3.0};
    double target[] = {0.0};
    scalar_packed output(target);
    std::mdspan<double, extents> input(source);
    EXPECT_TRUE(decltype(output)::is_always_unique());
    p::la::copy(input, output);
    p::la::scale(2.0, output);
    EXPECT_DOUBLE_EQ(target[0], 6.0);

    using dynamic_packed = std::mdspan<double, std::dextents<int, 2>, p::upper_layout>;
    dynamic_packed dynamic(target, std::dextents<int, 2>{1, 1});
    EXPECT_TRUE(dynamic.mapping().is_unique());
    EXPECT_FALSE(decltype(dynamic)::is_always_unique());
    expect_calls(p::matrix_writes<dynamic_packed>, false);
    expect_calls(p::structured_writes<dynamic_packed>, true);
}

TEST(LinalgUniqueOutputs, NonUniqueAndConservativeInputsRemainReadable) {
    const double packed_data[] = {1.0, 2.0, 3.0};
    double dense_data[4]{};
    std::mdspan<const double, p::extents_2x2, p::upper_layout> input(packed_data);
    p::matrix output(dense_data);
    p::la::copy(input, output);
    EXPECT_DOUBLE_EQ(dense_data[0], 1.0);
    EXPECT_DOUBLE_EQ(dense_data[1], 2.0);
    EXPECT_DOUBLE_EQ(dense_data[2], 2.0);
    EXPECT_DOUBLE_EQ(dense_data[3], 3.0);

    double vector_data[] = {-3.0, 4.0};
    p::weak_vector values(vector_data);
    EXPECT_DOUBLE_EQ(p::la::dot(values, values, 1.0), 26.0);
    EXPECT_DOUBLE_EQ(p::la::vector_abs_sum(values, 0.0), 7.0);
}

TEST(LinalgUniqueOutputs, WritableNoncopyableProxyOutputRemainsAccepted) {
    double source[] = {1.0, 2.0, 3.0, 4.0};
    double target[4]{};
    int accesses = 0;
    p::matrix input(source);
    p::proxy_matrix output(target, p::extents_2x2{}, p::proxy_accessor{&accesses});
    p::la::copy(input, output);
    for (std::size_t i = 0; i < 4; ++i) {
        EXPECT_DOUBLE_EQ(target[i], source[i]);
    }
    EXPECT_EQ(accesses, 4);
}
