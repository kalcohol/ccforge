#include <mdspan>
#include <array>
#include <cstddef>
#include <utility>

namespace {

template<class Layout>
bool check_submdspan() {
    std::array<int, 32> data{};
    for (std::size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<int>(i + 1);
    }
    using extents_t = std::extents<std::size_t, 3, 4>;
    using mapping_t = typename Layout::template mapping<extents_t>;
    std::mdspan<int, extents_t, Layout> source(data.data(), mapping_t(extents_t{}));
    auto full = std::submdspan(source, std::full_extent, std::full_extent);
    auto row = std::submdspan(source, 1, std::full_extent);
    auto column = std::submdspan(source, std::full_extent, 2);
    auto window = std::submdspan(source, std::pair{1, 3}, std::pair{1, 4});
    auto scalar = std::submdspan(source, 2, 3);

    static_assert(decltype(full)::rank() == 2);
    static_assert(decltype(row)::rank() == 1);
    static_assert(decltype(column)::rank() == 1);
    static_assert(decltype(window)::rank() == 2);
    static_assert(decltype(scalar)::rank() == 0);
    if (full.data_handle() != source.data_handle() ||
        full.extent(0) != 3 || full.extent(1) != 4 ||
        full.stride(0) != source.stride(0) ||
        full.stride(1) != source.stride(1)) {
        return false;
    }
    if (row.extent(0) != 4 || row.stride(0) != source.stride(1) ||
        row.data_handle() != data.data() + source.mapping()(1, 0)) {
        return false;
    }
    if (column.extent(0) != 3 || column.stride(0) != source.stride(0) ||
        column.data_handle() != data.data() + source.mapping()(0, 2)) {
        return false;
    }
    if (window.extent(0) != 2 || window.extent(1) != 3 ||
        window.stride(0) != source.stride(0) ||
        window.stride(1) != source.stride(1) ||
        window.data_handle() != data.data() + source.mapping()(1, 1)) {
        return false;
    }
    if (scalar.data_handle() != data.data() + source.mapping()(2, 3) ||
        scalar[] != source[2, 3]) {
        return false;
    }
    for (std::size_t i = 0; i < 3; ++i) {
        if (column[i] != source[i, 2]) {
            return false;
        }
        for (std::size_t j = 0; j < 4; ++j) {
            if (full[i, j] != source[i, j]) {
                return false;
            }
        }
    }
    for (std::size_t j = 0; j < 4; ++j) {
        if (row[j] != source[1, j]) {
            return false;
        }
    }
    for (std::size_t i = 0; i < 2; ++i) {
        for (std::size_t j = 0; j < 3; ++j) {
            if (window[i, j] != source[i + 1, j + 1]) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

int main() {
    if (!check_submdspan<std::layout_left>()) {
        return 1;
    }
    if (!check_submdspan<std::layout_right>()) {
        return 2;
    }
#if defined(FORGE_TEST_SUBMDSPAN_PADDED)
    if (!check_submdspan<std::layout_left_padded<8>>()) {
        return 3;
    }
    if (!check_submdspan<std::layout_right_padded<8>>()) {
        return 4;
    }
#endif
}
