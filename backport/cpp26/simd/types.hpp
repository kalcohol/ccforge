#pragma once

namespace __detail {

template<class U, class Mask>
struct mask_vector_generator {
    const Mask& value;

    template<class Lane>
    constexpr U operator()(Lane) const {
        return value[static_cast<__simd_size_type>(Lane::value)] ? U{1} : U{0};
    }
};

} // namespace __detail

template<size_t Bytes, class Abi>
    requires(__detail::is_enabled_basic_mask<Bytes, Abi>::value)
class basic_mask<Bytes, Abi> {
    friend constexpr bool& __detail::lane_ref<Bytes, Abi>(basic_mask& value, __simd_size_type i) noexcept;

public:
    using value_type = bool;
    using abi_type = Abi;
    using iterator = __simd_iterator<basic_mask>;
    using const_iterator = __simd_iterator<const basic_mask>;
    inline static constexpr integral_constant<__simd_size_type, __abi_lane_count<Abi>::value> size{};

    constexpr basic_mask() noexcept : data_{} {}

    template<class U,
             typename enable_if<is_same<__detail::remove_cvref_t<U>, bool>::value, int>::type = 0>
    constexpr explicit basic_mask(U value) noexcept : data_{} {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] = value;
        }
    }

    template<size_t OtherBytes,
             class OtherAbi,
             typename enable_if<
                 static_cast<__simd_size_type>(
                     basic_mask<OtherBytes, OtherAbi>::size) ==
                     static_cast<__simd_size_type>(size),
                 int>::type = 0>
    constexpr explicit basic_mask(const basic_mask<OtherBytes, OtherAbi>& other) noexcept : data_{} {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] = other[i];
        }
    }

    template<class U,
             typename enable_if<
                 is_unsigned<__detail::remove_cvref_t<U>>::value &&
                 !is_same<__detail::remove_cvref_t<U>, bool>::value,
                 int>::type = 0>
    constexpr explicit basic_mask(U value) noexcept : data_{} {
        using unsigned_type = __detail::remove_cvref_t<U>;
        unsigned_type bits = static_cast<unsigned_type>(value);
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] = (bits & unsigned_type{1}) != 0;
            bits >>= 1;
        }
    }

    template<size_t N,
             typename enable_if<N == __abi_lane_count<Abi>::value, int>::type = 0>
    constexpr basic_mask(const bitset<N>& bits) noexcept : data_{} {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] = bits[static_cast<size_t>(i)];
        }
    }

    template<class G,
             typename enable_if<__detail::is_simd_generator<G, bool, __abi_lane_count<Abi>::value>::value &&
                 !is_same<__detail::remove_cvref_t<G>, basic_mask>::value, int>::type = 0>
    constexpr explicit basic_mask(G&& gen)
        noexcept(__detail::is_nothrow_simd_generator<G, bool, __abi_lane_count<Abi>::value>::value)
        : data_(__detail::generate_array<bool, __abi_lane_count<Abi>::value>(gen)) {}

    constexpr bool operator[](__simd_size_type i) const noexcept {
        return data_[i];
    }

    template<class Indices,
             typename enable_if<__detail::is_simd_index_vector<Indices>::value, int>::type = 0>
    constexpr __detail::permute_result_t<basic_mask, Indices> operator[](const Indices& indices) const {
        return simd::permute(*this, indices);
    }


    constexpr iterator begin() noexcept {
        return iterator(*this, 0);
    }

    constexpr const_iterator begin() const noexcept {
        return const_iterator(*this, 0);
    }

    constexpr const_iterator cbegin() const noexcept {
        return const_iterator(*this, 0);
    }

    constexpr default_sentinel_t end() const noexcept {
        return default_sentinel;
    }

    constexpr default_sentinel_t cend() const noexcept {
        return default_sentinel;
    }

    constexpr bitset<__abi_lane_count<Abi>::value> to_bitset() const noexcept {
        if constexpr (
            __abi_lane_count<Abi>::value <=
            static_cast<__simd_size_type>(numeric_limits<unsigned long long>::digits)) {
            return bitset<__abi_lane_count<Abi>::value>(to_ullong());
        } else {
            bitset<__abi_lane_count<Abi>::value> result;
            for (__simd_size_type i = 0; i < size; ++i) {
                result.set(static_cast<size_t>(i), data_[i]);
            }
            return result;
        }
    }

    constexpr unsigned long long to_ullong() const noexcept {
        constexpr __simd_size_type digit_count = static_cast<__simd_size_type>(numeric_limits<unsigned long long>::digits);

        unsigned long long result = 0;
        const __simd_size_type active_lanes = size < digit_count ? size : digit_count;
        for (__simd_size_type i = 0; i < active_lanes; ++i) {
            if (data_[i]) {
                result |= (1ull << static_cast<unsigned>(i));
            }
        }
        return result;
    }

    template<class U, class A,
             typename enable_if<
                 static_cast<__simd_size_type>(basic_vec<U, A>::size) == static_cast<__simd_size_type>(size) &&
                 sizeof(U) == Bytes,
                 int>::type = 0>
    constexpr operator basic_vec<U, A>() const noexcept {
        return basic_vec<U, A>(__detail::mask_vector_generator<U, basic_mask>{*this});
    }

    template<class U, class A,
             typename enable_if<
                 static_cast<__simd_size_type>(basic_vec<U, A>::size) == static_cast<__simd_size_type>(size) &&
                 sizeof(U) != Bytes,
                 int>::type = 0>
    constexpr explicit operator basic_vec<U, A>() const noexcept {
        return basic_vec<U, A>(__detail::mask_vector_generator<U, basic_mask>{*this});
    }

    constexpr basic_mask& operator&=(const basic_mask& other) noexcept {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] = data_[i] && other[i];
        }
        return *this;
    }

    constexpr basic_mask& operator|=(const basic_mask& other) noexcept {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] = data_[i] || other[i];
        }
        return *this;
    }

    constexpr basic_mask& operator^=(const basic_mask& other) noexcept {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] = data_[i] != other[i];
        }
        return *this;
    }

    friend constexpr basic_mask operator!(const basic_mask& value) noexcept {
        basic_mask result;
        for (__simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = !value[i];
        }
        return result;
    }

    friend constexpr basic_mask operator&&(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (__simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] && right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator||(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (__simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] || right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator&(basic_mask left, const basic_mask& right) noexcept {
        left &= right;
        return left;
    }

    friend constexpr basic_mask operator|(basic_mask left, const basic_mask& right) noexcept {
        left |= right;
        return left;
    }

    friend constexpr basic_mask operator^(basic_mask left, const basic_mask& right) noexcept {
        left ^= right;
        return left;
    }

    // Deduce the result only after the constraint succeeds: some enabled
    // masks (e.g. complex<double>) have no matching signed integer type.
    constexpr auto operator+() const noexcept
        requires(__detail::has_vectorizable_signed_integer_of_size<Bytes>::value) {
        using result_type = basic_vec<typename __detail::integer_from_size<Bytes>::type, Abi>;
        result_type result;
        for (__simd_size_type i = 0; i < size; ++i) {
            __detail::set_lane(
                result,
                i,
                (*this)[i] ? typename result_type::value_type{1}
                         : typename result_type::value_type{0});
        }
        return result;
    }

    void operator+() const noexcept
        requires(!__detail::has_vectorizable_signed_integer_of_size<Bytes>::value) = delete;

    constexpr auto operator-() const noexcept
        requires(__detail::has_vectorizable_signed_integer_of_size<Bytes>::value) {
        using result_type = basic_vec<typename __detail::integer_from_size<Bytes>::type, Abi>;
        result_type result;
        for (__simd_size_type i = 0; i < size; ++i) {
            __detail::set_lane(
                result,
                i,
                (*this)[i] ? typename result_type::value_type{-1}
                         : typename result_type::value_type{0});
        }
        return result;
    }

    void operator-() const noexcept
        requires(!__detail::has_vectorizable_signed_integer_of_size<Bytes>::value) = delete;

    constexpr auto operator~() const noexcept
        requires(__detail::has_vectorizable_signed_integer_of_size<Bytes>::value) {
        using result_type = basic_vec<typename __detail::integer_from_size<Bytes>::type, Abi>;
        result_type result;
        for (__simd_size_type i = 0; i < size; ++i) {
            using lane_type = typename result_type::value_type;
            __detail::set_lane(
                result,
                i,
                (*this)[i] ? static_cast<lane_type>(~lane_type{1})
                         : static_cast<lane_type>(~lane_type{0}));
        }
        return result;
    }

    void operator~() const noexcept
        requires(!__detail::has_vectorizable_signed_integer_of_size<Bytes>::value) = delete;

    friend constexpr basic_mask operator==(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (__simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] == right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator!=(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (__simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] != right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator<(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (__simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] < right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator<=(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (__simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] <= right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator>(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (__simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] > right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator>=(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (__simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] >= right[i];
        }
        return result;
    }

    // Preserve the original hidden-friend parameter forms, including braces.
    [[deprecated("use std::simd::select")]]
    friend constexpr basic_mask simd_select_impl(
        const basic_mask& cond,
        const basic_mask& when_true,
        const basic_mask& when_false) noexcept {
        return __simd_select_impl(cond, when_true, when_false);
    }

    [[deprecated("use std::simd::select")]]
    friend constexpr basic_mask simd_select_impl(
        const basic_mask& cond,
        same_as<bool> auto when_true,
        same_as<bool> auto when_false) noexcept {
        return __simd_select_impl(cond, when_true, when_false);
    }

    template<class U, typename enable_if<__detail::is_supported_value<U>::value && sizeof(U) == Bytes, int>::type = 0>
    [[deprecated("use std::simd::select")]]
    friend constexpr basic_vec<U, __deduce_abi_t<U, __abi_lane_count<Abi>::value>>
    simd_select_impl(const basic_mask& cond, const U& when_true, const U& when_false) noexcept {
        return __simd_select_impl(cond, when_true, when_false);
    }

    friend constexpr basic_mask __simd_select_impl(
        const basic_mask& cond,
        const basic_mask& when_true,
        const basic_mask& when_false) noexcept {
        basic_mask result;
        for (__simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = cond[i] ? when_true[i] : when_false[i];
        }
        return result;
    }

    friend constexpr basic_mask __simd_select_impl(
        const basic_mask& cond,
        same_as<bool> auto when_true,
        same_as<bool> auto when_false) noexcept {
        basic_mask result;
        for (__simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = cond[i] ? when_true : when_false;
        }
        return result;
    }

    template<class U, typename enable_if<__detail::is_supported_value<U>::value && sizeof(U) == Bytes, int>::type = 0>
    friend constexpr basic_vec<U, __deduce_abi_t<U, __abi_lane_count<Abi>::value>>
    __simd_select_impl(const basic_mask& cond, const U& when_true, const U& when_false) noexcept {
        using result_type = basic_vec<U, __deduce_abi_t<U, __abi_lane_count<Abi>::value>>;
        result_type result;
        for (__simd_size_type i = 0; i < size; ++i) {
            __detail::set_lane(result, i, cond[i] ? when_true : when_false);
        }
        return result;
    }

private:
    array<bool, __abi_lane_count<Abi>::value> data_;
};

template<size_t Bytes, class Abi>
    requires(!__detail::is_enabled_basic_mask<Bytes, Abi>::value)
class basic_mask<Bytes, Abi> {
public:
    using value_type = bool;
    using abi_type = Abi;

    basic_mask() = delete;
    ~basic_mask() = delete;
    basic_mask(const basic_mask&) = delete;
    basic_mask& operator=(const basic_mask&) = delete;
};

template<class T, class Abi>
    requires(__detail::is_enabled_basic_vec<T, Abi>::value)
class basic_vec<T, Abi> {
    friend constexpr T& __detail::lane_ref<T, Abi>(basic_vec& value, __simd_size_type i) noexcept;

public:
    using value_type = T;
    using __real_type = __real_simd_t<T, basic_vec>;
    // Member alias diagnostics are unreliable on GCC/Clang (even when named
    // non-dependently); retain the attribute and document the limitation.
    using real_type [[deprecated("use std::simd::rebind_t<typename T::value_type, basic_vec> for complex T")]] = __real_type;
    using mask_type = basic_mask<sizeof(T), Abi>;
    using abi_type = Abi;
    using iterator = __simd_iterator<basic_vec>;
    using const_iterator = __simd_iterator<const basic_vec>;
    inline static constexpr integral_constant<__simd_size_type, __simd_size<T, Abi>::value> size{};

    constexpr basic_vec() noexcept : data_{} {}

    constexpr basic_vec(const __real_type& reals, const __real_type& imags = {}) noexcept
        requires(__detail::is_complex_value<T>::value)
        : data_{} {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] = value_type(reals[i], imags[i]);
        }
    }

    template<class U,
             typename enable_if<__detail::is_implicit_simd_broadcast<U, T>::value &&
                 !__detail::is_contiguous_load_store_range<U>::value &&
                 !__detail::is_simd_generator<U, T, __simd_size<T, Abi>::value>::value,
                 int>::type = 0>
    constexpr basic_vec(U&& value) noexcept : data_{} {
        const T converted = static_cast<T>(static_cast<U&&>(value));
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] = converted;
        }
    }
    template<class G,
             typename enable_if<__detail::is_simd_generator<G, T, __simd_size<T, Abi>::value>::value &&
                 !is_same<__detail::remove_cvref_t<G>, basic_vec>::value, int>::type = 0>
    constexpr explicit basic_vec(G&& gen)
        noexcept(__detail::is_nothrow_simd_generator<G, T, __simd_size<T, Abi>::value>::value)
        : data_(__detail::generate_array<T, __simd_size<T, Abi>::value>(gen)) {}

    template<class R, class... Flags,
             typename enable_if<__detail::is_contiguous_load_store_range<R>::value &&
                 __detail::has_matching_fixed_range_size<R, static_cast<__simd_size_type>(size)>::value &&
                 __detail::is_explicitly_simd_convertible<
                     typename ranges::range_value_t<R>,
                     T>::value &&
                 !is_same<__detail::remove_cvref_t<R>, basic_vec>::value, int>::type = 0>
    constexpr basic_vec(R&& r, flags<Flags...> f = {}) noexcept : data_{} {
        auto* first = ranges::data(r);
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] = __detail::convert_or_copy<T>(first[i], f);
        }
    }

    template<class R, class... Flags,
             typename enable_if<__detail::is_contiguous_load_store_range<R>::value &&
                 __detail::has_matching_fixed_range_size<R, static_cast<__simd_size_type>(size)>::value &&
                 __detail::is_explicitly_simd_convertible<
                     typename ranges::range_value_t<R>,
                     T>::value &&
                 !is_same<__detail::remove_cvref_t<R>, basic_vec>::value, int>::type = 0>
    constexpr basic_vec(R&& r, const mask_type& mask_value, flags<Flags...> f = {}) noexcept : data_{} {
        auto* first = ranges::data(r);
        for (__simd_size_type i = 0; i < size; ++i) {
            if (mask_value[i]) {
                data_[i] = __detail::convert_or_copy<T>(first[i], f);
            }
            // else: zero-initialized by data_{}
        }
    }

    template<class U, class OtherAbi,
             typename enable_if<
                 __simd_size<U, OtherAbi>::value == __simd_size<T, Abi>::value &&
                 __detail::is_explicitly_simd_convertible<U, T>::value, int>::type = 0>
    constexpr explicit(__detail::is_explicit_simd_vector_conversion<U, T>::value)
        basic_vec(const basic_vec<U, OtherAbi>& other) noexcept : data_{} {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] = static_cast<T>(other[i]);
        }
    }

    template<class U, class OtherAbi,
             typename enable_if<
                 __simd_size<U, OtherAbi>::value == __simd_size<T, Abi>::value &&
                 __detail::is_explicitly_simd_convertible<U, T>::value, int>::type = 0>
    constexpr explicit basic_vec(
        const basic_vec<U, OtherAbi>& other,
        flags<__convert_flag>) noexcept(noexcept(static_cast<T>(other[0])))
        : data_{} {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] = static_cast<T>(other[i]);
        }
    }

    constexpr __real_type real() const noexcept
        requires(__detail::is_complex_value<T>::value) {
        __real_type result;
        for (__simd_size_type i = 0; i < size; ++i) {
            __detail::set_lane(result, i, data_[i].real());
        }
        return result;
    }

    constexpr void real(const __real_type& reals) noexcept
        requires(__detail::is_complex_value<T>::value) {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i].real(reals[i]);
        }
    }

    constexpr __real_type imag() const noexcept
        requires(__detail::is_complex_value<T>::value) {
        __real_type result;
        for (__simd_size_type i = 0; i < size; ++i) {
            __detail::set_lane(result, i, data_[i].imag());
        }
        return result;
    }

    constexpr void imag(const __real_type& imags) noexcept
        requires(__detail::is_complex_value<T>::value) {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i].imag(imags[i]);
        }
    }

    constexpr T operator[](__simd_size_type i) const noexcept {
        return data_[i];
    }

    template<class Indices,
             typename enable_if<__detail::is_simd_index_vector<Indices>::value, int>::type = 0>
    constexpr __detail::permute_result_t<basic_vec, Indices> operator[](const Indices& indices) const {
        return simd::permute(*this, indices);
    }


    constexpr iterator begin() noexcept {
        return iterator(*this, 0);
    }

    constexpr const_iterator begin() const noexcept {
        return const_iterator(*this, 0);
    }

    constexpr const_iterator cbegin() const noexcept {
        return const_iterator(*this, 0);
    }

    constexpr default_sentinel_t end() const noexcept {
        return default_sentinel;
    }

    constexpr default_sentinel_t cend() const noexcept {
        return default_sentinel;
    }

    constexpr basic_vec operator+() const noexcept
        requires requires(const T& value) { +value; } {
        return *this;
    }

    constexpr basic_vec operator-() const noexcept
        requires requires(const T& value) { -value; } {
        basic_vec result;
        for (__simd_size_type i = 0; i < size; ++i) {
            __detail::set_lane(result, i, -data_[i]);
        }
        return result;
    }

    friend constexpr mask_type operator!(const basic_vec& value) noexcept
        requires requires(const T& lane) { static_cast<bool>(!lane); } {
        mask_type result;
        for (__simd_size_type i = 0; i < size; ++i) {
            __detail::set_lane(result, i, !value[i]);
        }
        return result;
    }

    constexpr basic_vec& operator++() noexcept
        requires requires(T& lane) { ++lane; } {
        for (__simd_size_type i = 0; i < size; ++i) {
            ++data_[i];
        }
        return *this;
    }

    constexpr basic_vec operator++(int) noexcept
        requires requires(T& lane) { ++lane; } {
        basic_vec previous = *this;
        ++(*this);
        return previous;
    }

    constexpr basic_vec& operator--() noexcept
        requires requires(T& lane) { --lane; } {
        for (__simd_size_type i = 0; i < size; ++i) {
            --data_[i];
        }
        return *this;
    }

    constexpr basic_vec operator--(int) noexcept
        requires requires(T& lane) { --lane; } {
        basic_vec previous = *this;
        --(*this);
        return previous;
    }

    constexpr basic_vec& operator+=(const basic_vec& other) noexcept
        requires requires(T& left, const T& right) { left += right; } {
        if consteval {
            for (__simd_size_type i = 0; i < size; ++i) data_[i] += other[i];
        } else {
            __detail::__simd_add<T, size>(data_.data(), other.data_.data());
        }
        return *this;
    }

    constexpr basic_vec& operator-=(const basic_vec& other) noexcept
        requires requires(T& left, const T& right) { left -= right; } {
        if consteval {
            for (__simd_size_type i = 0; i < size; ++i) data_[i] -= other[i];
        } else {
            __detail::__simd_sub<T, size>(data_.data(), other.data_.data());
        }
        return *this;
    }

    constexpr basic_vec& operator*=(const basic_vec& other) noexcept
        requires requires(T& left, const T& right) { left *= right; } {
        if consteval {
            for (__simd_size_type i = 0; i < size; ++i) data_[i] *= other[i];
        } else {
            __detail::__simd_mul<T, size>(data_.data(), other.data_.data());
        }
        return *this;
    }

    constexpr basic_vec& operator/=(const basic_vec& other) noexcept
        requires requires(T& left, const T& right) { left /= right; } {
        if consteval {
            for (__simd_size_type i = 0; i < size; ++i) data_[i] /= other[i];
        } else {
            __detail::__simd_div<T, size>(data_.data(), other.data_.data());
        }
        return *this;
    }


    constexpr basic_vec& operator%=(const basic_vec& other) noexcept
        requires requires(T& left, const T& right) { left %= right; } {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] %= other[i];
        }
        return *this;
    }

    constexpr basic_vec& operator&=(const basic_vec& other) noexcept
        requires requires(T& left, const T& right) { left &= right; } {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] &= other[i];
        }
        return *this;
    }

    constexpr basic_vec& operator|=(const basic_vec& other) noexcept
        requires requires(T& left, const T& right) { left |= right; } {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] |= other[i];
        }
        return *this;
    }

    constexpr basic_vec& operator^=(const basic_vec& other) noexcept
        requires requires(T& left, const T& right) { left ^= right; } {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] ^= other[i];
        }
        return *this;
    }

    constexpr basic_vec& operator<<=(__simd_size_type shift) noexcept
        requires requires(T& value) { value <<= __simd_size_type{}; } {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] <<= shift;
        }
        return *this;
    }

    constexpr basic_vec& operator<<=(const basic_vec& shift) noexcept
        requires requires(T& left, const T& right) { left <<= right; } {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] <<= shift[i];
        }
        return *this;
    }

    constexpr basic_vec& operator>>=(__simd_size_type shift) noexcept
        requires requires(T& value) { value >>= __simd_size_type{}; } {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] >>= shift;
        }
        return *this;
    }

    constexpr basic_vec& operator>>=(const basic_vec& shift) noexcept
        requires requires(T& left, const T& right) { left >>= right; } {
        for (__simd_size_type i = 0; i < size; ++i) {
            data_[i] >>= shift[i];
        }
        return *this;
    }

    friend constexpr basic_vec operator+(basic_vec left, const basic_vec& right) noexcept
        requires requires(basic_vec& left_value, const basic_vec& right_value) { left_value += right_value; } {
        left += right;
        return left;
    }

    friend constexpr basic_vec operator-(basic_vec left, const basic_vec& right) noexcept
        requires requires(basic_vec& left_value, const basic_vec& right_value) { left_value -= right_value; } {
        left -= right;
        return left;
    }

    friend constexpr basic_vec operator*(basic_vec left, const basic_vec& right) noexcept
        requires requires(basic_vec& left_value, const basic_vec& right_value) { left_value *= right_value; } {
        left *= right;
        return left;
    }

    friend constexpr basic_vec operator/(basic_vec left, const basic_vec& right) noexcept
        requires requires(basic_vec& left_value, const basic_vec& right_value) { left_value /= right_value; } {
        left /= right;
        return left;
    }


    friend constexpr basic_vec operator%(basic_vec left, const basic_vec& right) noexcept
        requires requires(basic_vec& left_value, const basic_vec& right_value) { left_value %= right_value; } {
        left %= right;
        return left;
    }

    friend constexpr basic_vec operator&(basic_vec left, const basic_vec& right) noexcept
        requires requires(basic_vec& left_value, const basic_vec& right_value) { left_value &= right_value; } {
        left &= right;
        return left;
    }

    friend constexpr basic_vec operator|(basic_vec left, const basic_vec& right) noexcept
        requires requires(basic_vec& left_value, const basic_vec& right_value) { left_value |= right_value; } {
        left |= right;
        return left;
    }

    friend constexpr basic_vec operator^(basic_vec left, const basic_vec& right) noexcept
        requires requires(basic_vec& left_value, const basic_vec& right_value) { left_value ^= right_value; } {
        left ^= right;
        return left;
    }

    constexpr basic_vec operator~() const noexcept
        requires requires(const T& value) { ~value; } {
        basic_vec result;
        for (__simd_size_type i = 0; i < size; ++i) {
            __detail::set_lane(result, i, ~data_[i]);
        }
        return result;
    }

    friend constexpr basic_vec operator<<(basic_vec left, __simd_size_type shift) noexcept
        requires requires(basic_vec& left_value) { left_value <<= __simd_size_type{}; } {
        left <<= shift;
        return left;
    }

    friend constexpr basic_vec operator<<(basic_vec left, const basic_vec& shift) noexcept
        requires requires(basic_vec& left_value, const basic_vec& right_value) { left_value <<= right_value; } {
        left <<= shift;
        return left;
    }

    friend constexpr basic_vec operator>>(basic_vec left, __simd_size_type shift) noexcept
        requires requires(basic_vec& left_value) { left_value >>= __simd_size_type{}; } {
        left >>= shift;
        return left;
    }

    friend constexpr basic_vec operator>>(basic_vec left, const basic_vec& shift) noexcept
        requires requires(basic_vec& left_value, const basic_vec& right_value) { left_value >>= right_value; } {
        left >>= shift;
        return left;
    }

    friend constexpr mask_type operator==(const basic_vec& left, const basic_vec& right) noexcept
        requires requires(const T& left_value, const T& right_value) { static_cast<bool>(left_value == right_value); } {
        mask_type result;
        for (__simd_size_type i = 0; i < size; ++i) {
            __detail::set_lane(result, i, left[i] == right[i]);
        }
        return result;
    }

    friend constexpr mask_type operator!=(const basic_vec& left, const basic_vec& right) noexcept
        requires requires(const T& left_value, const T& right_value) { static_cast<bool>(left_value != right_value); } {
        mask_type result;
        for (__simd_size_type i = 0; i < size; ++i) {
            __detail::set_lane(result, i, left[i] != right[i]);
        }
        return result;
    }

    friend constexpr mask_type operator<(const basic_vec& left, const basic_vec& right) noexcept
        requires requires(const T& left_value, const T& right_value) { static_cast<bool>(left_value < right_value); } {
        mask_type result;
        for (__simd_size_type i = 0; i < size; ++i) {
            __detail::set_lane(result, i, left[i] < right[i]);
        }
        return result;
    }

    friend constexpr mask_type operator<=(const basic_vec& left, const basic_vec& right) noexcept
        requires requires(const T& left_value, const T& right_value) { static_cast<bool>(left_value <= right_value); } {
        mask_type result;
        for (__simd_size_type i = 0; i < size; ++i) {
            __detail::set_lane(result, i, left[i] <= right[i]);
        }
        return result;
    }

    friend constexpr mask_type operator>(const basic_vec& left, const basic_vec& right) noexcept
        requires requires(const T& left_value, const T& right_value) { static_cast<bool>(left_value > right_value); } {
        mask_type result;
        for (__simd_size_type i = 0; i < size; ++i) {
            __detail::set_lane(result, i, left[i] > right[i]);
        }
        return result;
    }

    friend constexpr mask_type operator>=(const basic_vec& left, const basic_vec& right) noexcept
        requires requires(const T& left_value, const T& right_value) { static_cast<bool>(left_value >= right_value); } {
        mask_type result;
        for (__simd_size_type i = 0; i < size; ++i) {
            __detail::set_lane(result, i, left[i] >= right[i]);
        }
        return result;
    }

    [[deprecated("use std::simd::select")]]
    friend constexpr basic_vec simd_select_impl(
        const mask_type& cond,
        const basic_vec& when_true,
        const basic_vec& when_false) noexcept {
        return __simd_select_impl(cond, when_true, when_false);
    }

    friend constexpr basic_vec __simd_select_impl(
        const mask_type& cond,
        const basic_vec& when_true,
        const basic_vec& when_false) noexcept {
        basic_vec result;
        for (__simd_size_type i = 0; i < size; ++i) {
            __detail::set_lane(result, i, cond[i] ? when_true[i] : when_false[i]);
        }
        return result;
    }

private:
    array<T, __simd_size<T, Abi>::value> data_;
};

template<class T, class Abi>
    requires(!__detail::is_enabled_basic_vec<T, Abi>::value)
class basic_vec<T, Abi> {
public:
    using value_type = T;
    using abi_type = Abi;
    using mask_type = basic_mask<sizeof(T), Abi>;

    basic_vec() = delete;
    ~basic_vec() = delete;
    basic_vec(const basic_vec&) = delete;
    basic_vec& operator=(const basic_vec&) = delete;
};

template<class R, class... Ts>
    requires(
        __detail::is_contiguous_load_store_range<R>::value &&
        __detail::fixed_range_size<remove_reference_t<R>>::value > 0 &&
        __detail::is_deduce_abi_available<
            typename ranges::range_value_t<R>,
            __detail::fixed_range_size<remove_reference_t<R>>::value>::value)
basic_vec(R&&, Ts...)
    -> basic_vec<
        typename ranges::range_value_t<R>,
        __deduce_abi_t<
            typename ranges::range_value_t<R>,
            __detail::fixed_range_size<remove_reference_t<R>>::value>>;

template<size_t Bytes, class Abi>
    requires(
        __detail::is_enabled_basic_mask<Bytes, Abi>::value &&
        __detail::has_vectorizable_signed_integer_of_size<Bytes>::value)
basic_vec(basic_mask<Bytes, Abi>)
    -> basic_vec<typename __detail::integer_from_size<Bytes>::type, Abi>;

// This metadata models the ABI-aligned memory contract used by load/store APIs.
// It does not require alignof(basic_vec<T, Abi>) to equal alignment_v.
// Disabled specializations have zero lanes and use the scalar alignment.
template<class T, class Abi, class U>
    requires(__detail::is_supported_value<U>::value)
struct alignment<basic_vec<T, Abi>, U>
    : integral_constant<size_t,
        __simd_size<T, Abi>::value == 0
            ? alignof(U)
            : bit_ceil(alignof(U) * static_cast<size_t>(__simd_size<T, Abi>::value))> {};

template<class T, class U, class Abi>
    requires(__detail::is_enabled_basic_vec<U, Abi>::value &&
             __detail::is_deduce_abi_available<T, __abi_lane_count<Abi>::value>::value)
struct rebind<T, basic_vec<U, Abi>> {
    using type = basic_vec<T, __deduce_abi_t<T, __abi_lane_count<Abi>::value>>;
};

template<class T, size_t Bytes, class Abi>
    requires(__detail::is_enabled_basic_mask<Bytes, Abi>::value &&
             __detail::is_deduce_abi_available<T, __abi_lane_count<Abi>::value>::value)
struct rebind<T, basic_mask<Bytes, Abi>> {
    using type = basic_mask<sizeof(T), __deduce_abi_t<T, __abi_lane_count<Abi>::value>>;
};

template<__simd_size_type N, class T, class Abi>
    requires(__detail::is_enabled_basic_vec<T, Abi>::value &&
             __detail::is_deduce_abi_available<T, N>::value)
struct resize<N, basic_vec<T, Abi>> {
    using type = basic_vec<T, __deduce_abi_t<T, N>>;
};

template<__simd_size_type N, size_t Bytes, class Abi>
    requires(__detail::is_enabled_basic_mask<Bytes, Abi>::value &&
             __detail::has_mask_representative_value<Bytes>::value &&
             __detail::is_deduce_abi_available<__detail::mask_representative_value_t<Bytes>, N>::value)
struct resize<N, basic_mask<Bytes, Abi>> {
    using type = basic_mask<Bytes, __deduce_abi_t<__detail::mask_representative_value_t<Bytes>, N>>;
};
