// Example: std::submdspan usage (C++26 backport via Forge)
//
// Demonstrates subspan extraction from mdspan for numerical computing patterns:
// - Row/column extraction
// - Sub-block extraction (for blocked algorithms)
// - Strided access patterns

#include <mdspan>
#include <cstdio>
#include <array>

// Print a 2-D mdspan as a matrix
template<class M>
void print_matrix(const char* name, const M& m) {
    std::printf("%s [%zu x %zu]:\n",
                name,
                static_cast<std::size_t>(m.extent(0)),
                static_cast<std::size_t>(m.extent(1)));
    for (std::size_t i = 0; i < m.extent(0); ++i) {
        for (std::size_t j = 0; j < m.extent(1); ++j)
            std::printf("  %3d", m[i, j]);
        std::printf("\n");
    }
}

// Zero all elements in a rank-2 mdspan (any layout)
template<class T, class E, class L, class A>
void zero_2d(std::mdspan<T, E, L, A> a) {
    static_assert(a.rank() == 2);
    for (std::size_t i = 0; i < a.extent(0); ++i)
        for (std::size_t j = 0; j < a.extent(1); ++j)
            a[i, j] = 0;
}

int main() {
    // -----------------------------------------------------------------------
    // Example 1: Row and column extraction from a 4x6 matrix
    // -----------------------------------------------------------------------
    std::printf("=== Example 1: Row/column extraction ===\n");

    std::array<int, 24> data{};
    for (int i = 0; i < 24; ++i) data[i] = i + 1;

    // 4x6 row-major matrix
    std::mdspan<int, std::dextents<int,2>> A(data.data(), 4, 6);
    print_matrix("A (4x6)", A);

    // Extract row 1 (second row)
    auto row1 = std::submdspan(A, 1, std::full_extent);
    std::printf("row 1: ");
    for (std::size_t j = 0; j < row1.extent(0); ++j)
        std::printf("%d ", row1[j]);
    std::printf("\n");

    // Extract column 2 (third column) — produces layout_stride result
    auto col2 = std::submdspan(A, std::full_extent, 2);
    std::printf("col 2: ");
    for (std::size_t i = 0; i < col2.extent(0); ++i)
        std::printf("%d ", col2[i]);
    std::printf("\n");

    // -----------------------------------------------------------------------
    // Example 2: Sub-block extraction (like BLAS/LAPACK blocked algorithms)
    // -----------------------------------------------------------------------
    std::printf("\n=== Example 2: Sub-block extraction ===\n");

    // Extract rows [1,3) × cols [2,5) — a 2x3 sub-block
    auto sub_block = std::submdspan(A,
        std::pair{1, 3},
        std::pair{2, 5});
    print_matrix("A[1:3, 2:5]", sub_block);

    // -----------------------------------------------------------------------
    // Example 3: Strided access (every 2nd element)
    // -----------------------------------------------------------------------
    std::printf("\n=== Example 3: Strided slice ===\n");

    // Select every 2nd row of A: rows 0, 2
    // strided_slice{offset=0, extent=4, stride=2}: spans [0,4) with stride 2
    auto even_rows = std::submdspan(A,
        std::strided_slice{0, 4, 2},
        std::full_extent);
    print_matrix("Even rows of A (0, 2)", even_rows);

    // -----------------------------------------------------------------------
    // Example 4: Surface zeroing (from the standard's own example)
    // -----------------------------------------------------------------------
    std::printf("\n=== Example 4: Surface zeroing (rank-3) ===\n");

    std::array<int, 24> grid_data{};
    for (int i = 0; i < 24; ++i) grid_data[i] = i + 1;

    // 2x3x4 grid (Z x Y x X)
    std::mdspan<int, std::dextents<int,3>> grid(grid_data.data(), 2, 3, 4);

    // Zero the six faces of the 3D grid
    zero_2d(std::submdspan(grid, 0, std::full_extent, std::full_extent));
    zero_2d(std::submdspan(grid, grid.extent(0)-1, std::full_extent, std::full_extent));
    zero_2d(std::submdspan(grid, std::full_extent, 0, std::full_extent));
    zero_2d(std::submdspan(grid, std::full_extent, grid.extent(1)-1, std::full_extent));
    zero_2d(std::submdspan(grid, std::full_extent, std::full_extent, 0));
    zero_2d(std::submdspan(grid, std::full_extent, std::full_extent, grid.extent(2)-1));

    // Count remaining non-zero interior elements
    int non_zero = 0;
    for (int v : grid_data) if (v != 0) ++non_zero;
    std::printf("After surface zeroing: %d interior elements remain non-zero\n", non_zero);
    // Interior of 2x3x4: (2-2)*(3-2)*(4-2) = 0*1*2 = 0 (grid is too thin)
    // For a grid large enough (e.g. 4x4x4): interior = 2*2*2 = 8
    std::printf("(grid 2x3x4 has no interior elements — all surface)\n");

    // -----------------------------------------------------------------------
    // Example 5: layout_left (column-major) submdspan
    // -----------------------------------------------------------------------
    std::printf("\n=== Example 5: layout_left column-major ===\n");

    std::array<int, 12> ldata{};
    for (int i = 0; i < 12; ++i) ldata[i] = i;
    std::mdspan<int, std::dextents<int,2>, std::layout_left> L(ldata.data(), 3, 4);

    // Extract first column (full extent on dim 0, index 0 on dim 1)
    // layout_left is preserved when dim 0 is full_extent and dim 1 is index
    auto first_col = std::submdspan(L, std::full_extent, 0);
    static_assert(first_col.rank() == 1);
    std::printf("L column 0: ");
    for (std::size_t i = 0; i < first_col.extent(0); ++i)
        std::printf("%d ", first_col[i]);
    std::printf("\n");

    std::printf("\nAll submdspan examples completed successfully.\n");
    return 0;
}
