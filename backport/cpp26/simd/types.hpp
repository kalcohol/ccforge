	class basic_mask {
    friend constexpr bool& detail::lane_ref<Bytes, Abi>(basic_mask& value, simd_size_type i) noexcept;

public:
    using value_type = bool;
    using abi_type = Abi;
    using iterator = simd_iterator<basic_mask>;
    using const_iterator = simd_iterator<const basic_mask>;
    inline static constexpr integral_constant<simd_size_type, abi_lane_count<Abi>::value> size{};

    constexpr basic_mask() noexcept : data_{} {}

    template<class U,
             typename enable_if<is_same<detail::remove_cvref_t<U>, bool>::value, int>::type = 0>
    constexpr explicit basic_mask(U value) noexcept : data_{} {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = value;
        }
    }

    template<size_t OtherBytes,
             class OtherAbi,
             typename enable_if<
                 static_cast<simd_size_type>(basic_mask<OtherBytes, OtherAbi>::size) == static_cast<simd_size_type>(size),
                 int>::type = 0>
    constexpr explicit basic_mask(const basic_mask<OtherBytes, OtherAbi>& other) noexcept : data_{} {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = other[i];
        }
    }

    template<class U,
             typename enable_if<
                 is_unsigned<detail::remove_cvref_t<U>>::value &&
                 !is_same<detail::remove_cvref_t<U>, bool>::value,
                 int>::type = 0>
    constexpr explicit basic_mask(U value) noexcept : data_{} {
        using unsigned_type = detail::remove_cvref_t<U>;
        unsigned_type bits = static_cast<unsigned_type>(value);
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = (bits & unsigned_type{1}) != 0;
            bits >>= 1;
        }
    }

    template<size_t N,
             typename enable_if<N == abi_lane_count<Abi>::value, int>::type = 0>
    basic_mask(const bitset<N>& bits) noexcept : data_{} {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = bits[static_cast<size_t>(i)];
        }
    }

    template<class G,
             typename enable_if<detail::is_simd_generator<G, bool, abi_lane_count<Abi>::value>::value &&
                 !is_same<detail::remove_cvref_t<G>, basic_mask>::value, int>::type = 0>
    constexpr explicit basic_mask(G&& gen)
        noexcept(detail::is_nothrow_simd_generator<G, bool, abi_lane_count<Abi>::value>::value)
        : data_(detail::generate_array<bool, abi_lane_count<Abi>::value>(gen)) {}

    constexpr bool operator[](simd_size_type i) const noexcept {
        return data_[i];
    }

    template<class Indices,
             typename enable_if<detail::is_simd_index_vector<Indices>::value, int>::type = 0>
    constexpr detail::permute_result_t<basic_mask, Indices> operator[](const Indices& indices) const {
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

	#if defined(__cpp_lib_constexpr_bitset) && __cpp_lib_constexpr_bitset >= 202207L
	    constexpr bitset<abi_lane_count<Abi>::value> to_bitset() const noexcept {
	        bitset<abi_lane_count<Abi>::value> result;
	        for (simd_size_type i = 0; i < size; ++i) {
	            result.set(static_cast<size_t>(i), data_[i]);
	        }
	        return result;
	    }
	#else
	    bitset<abi_lane_count<Abi>::value> to_bitset() const noexcept {
	        bitset<abi_lane_count<Abi>::value> result;
	        for (simd_size_type i = 0; i < size; ++i) {
	            result.set(static_cast<size_t>(i), data_[i]);
	        }
	        return result;
	    }
	#endif

    constexpr unsigned long long to_ullong() const noexcept {
        constexpr simd_size_type digit_count = static_cast<simd_size_type>(numeric_limits<unsigned long long>::digits);

        unsigned long long result = 0;
        const simd_size_type active_lanes = size < digit_count ? size : digit_count;
        for (simd_size_type i = 0; i < active_lanes; ++i) {
            if (data_[i]) {
                result |= (1ull << static_cast<unsigned>(i));
            }
        }
        return result;
    }

    template<class U, class A,
             typename enable_if<
                 static_cast<simd_size_type>(basic_vec<U, A>::size) == static_cast<simd_size_type>(size) &&
                 sizeof(U) == Bytes,
                 int>::type = 0>
    constexpr operator basic_vec<U, A>() const noexcept {
        return basic_vec<U, A>([&](auto lane) {
            return data_[static_cast<size_t>(decltype(lane)::value)] ? U{1} : U{0};
        });
    }

    template<class U, class A,
             typename enable_if<
                 static_cast<simd_size_type>(basic_vec<U, A>::size) == static_cast<simd_size_type>(size) &&
                 sizeof(U) != Bytes,
                 int>::type = 0>
    constexpr explicit operator basic_vec<U, A>() const noexcept {
        return basic_vec<U, A>([&](auto lane) {
            return data_[static_cast<size_t>(decltype(lane)::value)] ? U{1} : U{0};
        });
    }

    constexpr basic_mask& operator&=(const basic_mask& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = data_[i] && other[i];
        }
        return *this;
    }

    constexpr basic_mask& operator|=(const basic_mask& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = data_[i] || other[i];
        }
        return *this;
    }

    constexpr basic_mask& operator^=(const basic_mask& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = data_[i] != other[i];
        }
        return *this;
    }

    friend constexpr basic_mask operator!(const basic_mask& value) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = !value[i];
        }
        return result;
    }

    friend constexpr basic_mask operator&&(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] && right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator||(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
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

    friend constexpr basic_vec<typename detail::integer_from_size<Bytes>::type, Abi> operator+(const basic_mask& value) noexcept {
        using result_type = basic_vec<typename detail::integer_from_size<Bytes>::type, Abi>;
        result_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, value[i] ? typename result_type::value_type{1} : typename result_type::value_type{0});
        }
        return result;
    }

    friend constexpr basic_vec<typename detail::integer_from_size<Bytes>::type, Abi> operator-(const basic_mask& value) noexcept {
        using result_type = basic_vec<typename detail::integer_from_size<Bytes>::type, Abi>;
        result_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, value[i] ? typename result_type::value_type{-1} : typename result_type::value_type{0});
        }
        return result;
    }

    friend constexpr basic_vec<typename detail::integer_from_size<Bytes>::type, Abi> operator~(const basic_mask& value) noexcept {
        using result_type = basic_vec<typename detail::integer_from_size<Bytes>::type, Abi>;
        result_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            using lane_type = typename result_type::value_type;
            detail::set_lane(result, i, value[i] ? static_cast<lane_type>(~lane_type{1}) : static_cast<lane_type>(~lane_type{0}));
        }
        return result;
    }

    friend constexpr basic_mask operator==(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] == right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator!=(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] != right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator<(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] < right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator<=(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] <= right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator>(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] > right[i];
        }
        return result;
    }

    friend constexpr basic_mask operator>=(const basic_mask& left, const basic_mask& right) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = left[i] >= right[i];
        }
        return result;
    }

    friend constexpr basic_mask simd_select_impl(const basic_mask& cond, bool when_true, bool when_false) noexcept {
        basic_mask result;
        for (simd_size_type i = 0; i < size; ++i) {
            result.data_[i] = cond[i] ? when_true : when_false;
        }
        return result;
    }

    template<class U, typename enable_if<detail::is_supported_value<U>::value && sizeof(U) == Bytes, int>::type = 0>
    friend constexpr basic_vec<U, deduce_abi_t<U, abi_lane_count<Abi>::value>>
    simd_select_impl(const basic_mask& cond, const U& when_true, const U& when_false) noexcept {
        using result_type = basic_vec<U, deduce_abi_t<U, abi_lane_count<Abi>::value>>;
        result_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, cond[i] ? when_true : when_false);
        }
        return result;
    }

private:
    array<bool, abi_lane_count<Abi>::value> data_;
};

	template<class T, class Abi>
	class basic_vec {
    friend constexpr T& detail::lane_ref<T, Abi>(basic_vec& value, simd_size_type i) noexcept;

public:
    static_assert(detail::is_supported_value<T>::value, "std::simd::basic_vec only supports arithmetic non-bool value types");

    using value_type = T;
    using mask_type = basic_mask<sizeof(T), Abi>;
    using abi_type = Abi;
    using iterator = simd_iterator<basic_vec>;
    using const_iterator = simd_iterator<const basic_vec>;
	    inline static constexpr integral_constant<simd_size_type, simd_size<T, Abi>::value> size{};

	    constexpr basic_vec() noexcept : data_{} {}

    template<class U,
             typename enable_if<detail::is_explicitly_simd_convertible<U, T>::value &&
                 !detail::is_contiguous_load_store_range<U>::value &&
                 !detail::is_simd_generator<U, T, simd_size<T, Abi>::value>::value,
                 int>::type = 0>
    constexpr explicit(!detail::is_implicit_simd_broadcast<U, T>::value)
        basic_vec(U&& value) noexcept(noexcept(static_cast<T>(declval<const detail::remove_cvref_t<U>&>()))) : data_{} {
        const auto& source = value;
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = static_cast<T>(source);
        }
    }
    template<class G,
             typename enable_if<detail::is_simd_generator<G, T, simd_size<T, Abi>::value>::value &&
                 !is_same<detail::remove_cvref_t<G>, basic_vec>::value, int>::type = 0>
    constexpr explicit basic_vec(G&& gen)
        noexcept(detail::is_nothrow_simd_generator<G, T, simd_size<T, Abi>::value>::value)
        : data_(detail::generate_array<T, simd_size<T, Abi>::value>(gen)) {}

    template<class R, class... Flags,
             typename enable_if<detail::is_contiguous_load_store_range<R>::value &&
                 detail::has_matching_fixed_range_size<R, static_cast<simd_size_type>(size)>::value &&
                 !is_same<detail::remove_cvref_t<R>, basic_vec>::value, int>::type = 0>
    constexpr explicit
        basic_vec(R&& r, flags<Flags...> f = {}) noexcept : data_{} {
        auto* first = ranges::data(r);
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = detail::convert_or_copy<T>(first[i], f);
        }
    }

    template<class R, class... Flags,
             typename enable_if<detail::is_contiguous_load_store_range<R>::value &&
                 detail::has_matching_fixed_range_size<R, static_cast<simd_size_type>(size)>::value &&
                 !is_same<detail::remove_cvref_t<R>, basic_vec>::value, int>::type = 0>
    constexpr explicit basic_vec(R&& r, const mask_type& mask_value, flags<Flags...> f = {}) noexcept : data_{} {
        auto* first = ranges::data(r);
        for (simd_size_type i = 0; i < size; ++i) {
            if (mask_value[i]) {
                data_[i] = detail::convert_or_copy<T>(first[i], f);
            }
            // else: zero-initialized by data_{}
        }
    }

    template<class U, class OtherAbi,
             typename enable_if<detail::is_value_preserving_conversion<U, T>::value, int>::type = 0>
    constexpr basic_vec(const basic_vec<U, OtherAbi>& other) noexcept(noexcept(static_cast<T>(other[0]))) : data_{} {
        static_assert(basic_vec<U, OtherAbi>::size == size, "std::simd converting constructor requires matching lane count");

        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = static_cast<T>(other[i]);
        }
    }

    template<class U, class OtherAbi>
    constexpr explicit basic_vec(const basic_vec<U, OtherAbi>& other, flags<convert_flag>) noexcept(noexcept(static_cast<T>(other[0]))) : data_{} {
        static_assert(basic_vec<U, OtherAbi>::size == size, "std::simd converting constructor requires matching lane count");

        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] = static_cast<T>(other[i]);
        }
    }

    constexpr T operator[](simd_size_type i) const noexcept {
        return data_[i];
    }

    template<class Indices,
             typename enable_if<detail::is_simd_index_vector<Indices>::value, int>::type = 0>
    constexpr detail::permute_result_t<basic_vec, Indices> operator[](const Indices& indices) const {
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

    constexpr basic_vec operator+() const noexcept {
        return *this;
    }

	    constexpr basic_vec operator-() const noexcept {
	        basic_vec result;
	        for (simd_size_type i = 0; i < size; ++i) {
	            detail::set_lane(result, i, -data_[i]);
	        }
	        return result;
	    }

	    friend constexpr mask_type operator!(const basic_vec& value) noexcept {
	        mask_type result;
	        for (simd_size_type i = 0; i < size; ++i) {
	            detail::set_lane(result, i, !value[i]);
	        }
	        return result;
	    }

	    constexpr basic_vec& operator++() noexcept {
	        for (simd_size_type i = 0; i < size; ++i) {
	            ++data_[i];
	        }
	        return *this;
	    }

	    constexpr basic_vec operator++(int) noexcept {
	        basic_vec previous = *this;
	        ++(*this);
	        return previous;
	    }

	    constexpr basic_vec& operator--() noexcept {
	        for (simd_size_type i = 0; i < size; ++i) {
	            --data_[i];
	        }
	        return *this;
	    }

	    constexpr basic_vec operator--(int) noexcept {
	        basic_vec previous = *this;
	        --(*this);
	        return previous;
	    }

	    constexpr basic_vec& operator+=(const basic_vec& other) noexcept {
	        for (simd_size_type i = 0; i < size; ++i) {
	            data_[i] += other[i];
	        }
        return *this;
    }

    constexpr basic_vec& operator-=(const basic_vec& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] -= other[i];
        }
        return *this;
    }

    constexpr basic_vec& operator*=(const basic_vec& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] *= other[i];
        }
        return *this;
    }

    constexpr basic_vec& operator/=(const basic_vec& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] /= other[i];
        }
        return *this;
    }


    template<class U = T>
    constexpr typename enable_if<is_integral<U>::value, basic_vec&>::type operator%=(const basic_vec& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] %= other[i];
        }
        return *this;
    }

    template<class U = T>
    constexpr typename enable_if<is_integral<U>::value, basic_vec&>::type operator&=(const basic_vec& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] &= other[i];
        }
        return *this;
    }

    template<class U = T>
    constexpr typename enable_if<is_integral<U>::value, basic_vec&>::type operator|=(const basic_vec& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] |= other[i];
        }
        return *this;
    }

    template<class U = T>
    constexpr typename enable_if<is_integral<U>::value, basic_vec&>::type operator^=(const basic_vec& other) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] ^= other[i];
        }
        return *this;
    }

    template<class Shift, class U = T>
    constexpr typename enable_if<is_integral<U>::value && is_integral<Shift>::value, basic_vec&>::type operator<<=(Shift shift) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] <<= shift;
        }
        return *this;
    }

    template<class U = T>
    constexpr typename enable_if<is_integral<U>::value, basic_vec&>::type operator<<=(const basic_vec& shift) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] <<= shift[i];
        }
        return *this;
    }

    template<class Shift, class U = T>
    constexpr typename enable_if<is_integral<U>::value && is_integral<Shift>::value, basic_vec&>::type operator>>=(Shift shift) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] >>= shift;
        }
        return *this;
    }

    template<class U = T>
    constexpr typename enable_if<is_integral<U>::value, basic_vec&>::type operator>>=(const basic_vec& shift) noexcept {
        for (simd_size_type i = 0; i < size; ++i) {
            data_[i] >>= shift[i];
        }
        return *this;
    }

    friend constexpr basic_vec operator+(basic_vec left, const basic_vec& right) noexcept {
        left += right;
        return left;
    }

    friend constexpr basic_vec operator-(basic_vec left, const basic_vec& right) noexcept {
        left -= right;
        return left;
    }

    friend constexpr basic_vec operator*(basic_vec left, const basic_vec& right) noexcept {
        left *= right;
        return left;
    }

    friend constexpr basic_vec operator/(basic_vec left, const basic_vec& right) noexcept {
        left /= right;
        return left;
    }


    template<class U = T>
    friend constexpr typename enable_if<is_integral<U>::value, basic_vec>::type operator%(basic_vec left, const basic_vec& right) noexcept {
        left %= right;
        return left;
    }

    template<class U = T>
    friend constexpr typename enable_if<is_integral<U>::value, basic_vec>::type operator&(basic_vec left, const basic_vec& right) noexcept {
        left &= right;
        return left;
    }

    template<class U = T>
    friend constexpr typename enable_if<is_integral<U>::value, basic_vec>::type operator|(basic_vec left, const basic_vec& right) noexcept {
        left |= right;
        return left;
    }

    template<class U = T>
    friend constexpr typename enable_if<is_integral<U>::value, basic_vec>::type operator^(basic_vec left, const basic_vec& right) noexcept {
        left ^= right;
        return left;
    }

    template<class U = T>
    constexpr typename enable_if<is_integral<U>::value, basic_vec>::type operator~() const noexcept {
        basic_vec result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, ~data_[i]);
        }
        return result;
    }

    template<class Shift, class U = T>
    friend constexpr typename enable_if<is_integral<U>::value && is_integral<Shift>::value, basic_vec>::type operator<<(basic_vec left, Shift shift) noexcept {
        left <<= shift;
        return left;
    }

    template<class U = T>
    friend constexpr typename enable_if<is_integral<U>::value, basic_vec>::type operator<<(basic_vec left, const basic_vec& shift) noexcept {
        left <<= shift;
        return left;
    }

    template<class Shift, class U = T>
    friend constexpr typename enable_if<is_integral<U>::value && is_integral<Shift>::value, basic_vec>::type operator>>(basic_vec left, Shift shift) noexcept {
        left >>= shift;
        return left;
    }

    template<class U = T>
    friend constexpr typename enable_if<is_integral<U>::value, basic_vec>::type operator>>(basic_vec left, const basic_vec& shift) noexcept {
        left >>= shift;
        return left;
    }

    friend constexpr mask_type operator==(const basic_vec& left, const basic_vec& right) noexcept {
        mask_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, left[i] == right[i]);
        }
        return result;
    }

    friend constexpr mask_type operator!=(const basic_vec& left, const basic_vec& right) noexcept {
        mask_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, left[i] != right[i]);
        }
        return result;
    }

    friend constexpr mask_type operator<(const basic_vec& left, const basic_vec& right) noexcept {
        mask_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, left[i] < right[i]);
        }
        return result;
    }

    friend constexpr mask_type operator<=(const basic_vec& left, const basic_vec& right) noexcept {
        mask_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, left[i] <= right[i]);
        }
        return result;
    }

    friend constexpr mask_type operator>(const basic_vec& left, const basic_vec& right) noexcept {
        mask_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, left[i] > right[i]);
        }
        return result;
    }

    friend constexpr mask_type operator>=(const basic_vec& left, const basic_vec& right) noexcept {
        mask_type result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, left[i] >= right[i]);
        }
        return result;
    }

    friend constexpr basic_vec simd_select_impl(const mask_type& cond, const basic_vec& when_true, const basic_vec& when_false) noexcept {
        basic_vec result;
        for (simd_size_type i = 0; i < size; ++i) {
            detail::set_lane(result, i, cond[i] ? when_true[i] : when_false[i]);
        }
        return result;
    }

private:
    array<T, simd_size<T, Abi>::value> data_;
};

// This metadata models the ABI-aligned memory contract used by load/store APIs.
// It does not require alignof(basic_vec<T, Abi>) to equal alignment_v.
template<class T, class U>
struct alignment<basic_vec<T, U>, T> : integral_constant<size_t, alignof(T) * abi_lane_count<U>::value> {};

template<size_t Bytes, class Abi>
struct alignment<basic_mask<Bytes, Abi>, bool> : integral_constant<size_t, alignof(bool) * abi_lane_count<Abi>::value> {};

template<class T, class U, class Abi>
struct rebind<T, basic_vec<U, Abi>> {
    using type = basic_vec<T, deduce_abi_t<T, abi_lane_count<Abi>::value>>;
};

template<class T, size_t Bytes, class Abi>
struct rebind<T, basic_mask<Bytes, Abi>> {
    using type = basic_mask<sizeof(T), deduce_abi_t<T, abi_lane_count<Abi>::value>>;
};

template<simd_size_type N, class T, class Abi>
struct resize<N, basic_vec<T, Abi>> {
    using type = basic_vec<T, deduce_abi_t<T, N>>;
};

template<simd_size_type N, size_t Bytes, class Abi>
struct resize<N, basic_mask<Bytes, Abi>> {
    using type = basic_mask<Bytes, deduce_abi_t<typename detail::integer_from_size<Bytes>::type, N>>;
};

template<class V>
