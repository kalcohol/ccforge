class simd_iterator {
    template<class>
    friend class simd_iterator;

    template<size_t, class>
    friend class basic_mask;

    template<class, class>
    friend class basic_vec;

private:
    using simd_type = typename remove_const<V>::type;

    constexpr simd_iterator(V& value, simd_size_type index) noexcept
        : value_(addressof(value)), index_(index) {}

public:
    using value_type = typename simd_type::value_type;
    using difference_type = simd_size_type;
    using pointer = void;
    using reference = value_type;
    using iterator_category = input_iterator_tag;
    using iterator_concept = random_access_iterator_tag;

    constexpr simd_iterator() noexcept : value_(nullptr), index_(0) {}

    template<class U = V,
             typename enable_if<is_const<U>::value, int>::type = 0>
    constexpr simd_iterator(const simd_iterator<simd_type>& other) noexcept
        : value_(other.value_), index_(other.index_) {}

    constexpr value_type operator*() const noexcept {
        return (*value_)[index_];
    }

    constexpr value_type operator[](difference_type offset) const noexcept {
        return (*value_)[index_ + offset];
    }

    constexpr simd_iterator& operator++() noexcept {
        ++index_;
        return *this;
    }

    constexpr simd_iterator operator++(int) noexcept {
        simd_iterator tmp(*this);
        ++(*this);
        return tmp;
    }

    constexpr simd_iterator& operator--() noexcept {
        --index_;
        return *this;
    }

    constexpr simd_iterator operator--(int) noexcept {
        simd_iterator tmp(*this);
        --(*this);
        return tmp;
    }

    constexpr simd_iterator& operator+=(difference_type offset) noexcept {
        index_ += offset;
        return *this;
    }

    constexpr simd_iterator& operator-=(difference_type offset) noexcept {
        index_ -= offset;
        return *this;
    }

    friend constexpr simd_iterator operator+(simd_iterator it, difference_type offset) noexcept {
        it += offset;
        return it;
    }

    friend constexpr simd_iterator operator+(difference_type offset, simd_iterator it) noexcept {
        it += offset;
        return it;
    }

    friend constexpr simd_iterator operator-(simd_iterator it, difference_type offset) noexcept {
        it -= offset;
        return it;
    }

    friend constexpr difference_type operator-(const simd_iterator& left, const simd_iterator& right) noexcept {
        return left.index_ - right.index_;
    }

    friend constexpr bool operator==(const simd_iterator& left, const simd_iterator& right) noexcept = default;

    friend constexpr auto operator<=>(const simd_iterator& left, const simd_iterator& right) noexcept = default;

    friend constexpr bool operator==(const simd_iterator& it, default_sentinel_t) noexcept {
        return it.index_ == simd_type::size;
    }

    friend constexpr bool operator==(default_sentinel_t, const simd_iterator& it) noexcept {
        return it == default_sentinel;
    }

    friend constexpr bool operator!=(const simd_iterator& it, default_sentinel_t) noexcept {
        return !(it == default_sentinel);
    }

    friend constexpr bool operator!=(default_sentinel_t, const simd_iterator& it) noexcept {
        return !(it == default_sentinel);
    }

    friend constexpr difference_type operator-(default_sentinel_t, const simd_iterator& it) noexcept {
        return simd_type::size - it.index_;
    }

    friend constexpr difference_type operator-(const simd_iterator& it, default_sentinel_t) noexcept {
        return it.index_ - simd_type::size;
    }

private:
    V* value_;
    simd_size_type index_;
};
