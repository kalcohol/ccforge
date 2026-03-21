template<class Mask, class V>
class where_expression;

template<class Mask, class V>
class const_where_expression;

template<size_t Bytes, class Abi, class T>
class where_expression<basic_mask<Bytes, Abi>, basic_vec<T, Abi>> {
public:
	using mask_type = basic_mask<Bytes, Abi>;
	using value_type = basic_vec<T, Abi>;

	constexpr where_expression(const mask_type& mask_value, value_type& value) noexcept
		: mask_(mask_value), value_(&value) {
		static_assert(Bytes == sizeof(T), "std::simd::where requires mask bytes to match vector value type");
		static_assert(static_cast<simd_size_type>(mask_type::size) == static_cast<simd_size_type>(value_type::size),
			"std::simd::where requires matching lane count");
	}

	constexpr where_expression& operator=(const value_type& other) noexcept {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) = other[i];
			}
		}
		return *this;
	}

	constexpr where_expression& operator=(const T& scalar) noexcept {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) = scalar;
			}
		}
		return *this;
	}

	constexpr where_expression& operator+=(const value_type& other) noexcept(noexcept(std::declval<T&>() += std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) += other[i];
			}
		}
		return *this;
	}

	constexpr where_expression& operator-=(const value_type& other) noexcept(noexcept(std::declval<T&>() -= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) -= other[i];
			}
		}
		return *this;
	}

	constexpr where_expression& operator*=(const value_type& other) noexcept(noexcept(std::declval<T&>() *= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) *= other[i];
			}
		}
		return *this;
	}

	constexpr where_expression& operator/=(const value_type& other) noexcept(noexcept(std::declval<T&>() /= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) /= other[i];
			}
		}
		return *this;
	}

	constexpr where_expression& operator+=(const T& scalar) noexcept(noexcept(std::declval<T&>() += std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) += scalar;
			}
		}
		return *this;
	}

	constexpr where_expression& operator-=(const T& scalar) noexcept(noexcept(std::declval<T&>() -= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) -= scalar;
			}
		}
		return *this;
	}

	constexpr where_expression& operator*=(const T& scalar) noexcept(noexcept(std::declval<T&>() *= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) *= scalar;
			}
		}
		return *this;
	}

	constexpr where_expression& operator/=(const T& scalar) noexcept(noexcept(std::declval<T&>() /= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) /= scalar;
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator%=(const value_type& other) noexcept(noexcept(std::declval<T&>() %= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) %= other[i];
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator&=(const value_type& other) noexcept(noexcept(std::declval<T&>() &= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) &= other[i];
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator|=(const value_type& other) noexcept(noexcept(std::declval<T&>() |= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) |= other[i];
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator^=(const value_type& other) noexcept(noexcept(std::declval<T&>() ^= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) ^= other[i];
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator%=(const T& scalar) noexcept(noexcept(std::declval<T&>() %= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) %= scalar;
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator&=(const T& scalar) noexcept(noexcept(std::declval<T&>() &= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) &= scalar;
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator|=(const T& scalar) noexcept(noexcept(std::declval<T&>() |= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) |= scalar;
			}
		}
		return *this;
	}

	template<class U = T, typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator^=(const T& scalar) noexcept(noexcept(std::declval<T&>() ^= std::declval<T>())) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) ^= scalar;
			}
		}
		return *this;
	}

	template<class Shift, class U = T,
	         typename enable_if<is_integral<U>::value && is_integral<Shift>::value, int>::type = 0>
	constexpr where_expression& operator<<=(Shift shift) noexcept(noexcept(std::declval<T&>() <<= shift)) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) <<= shift;
			}
		}
		return *this;
	}

	template<class U = T,
	         typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator<<=(const value_type& shift) noexcept {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) <<= shift[i];
			}
		}
		return *this;
	}

	template<class Shift, class U = T,
	         typename enable_if<is_integral<U>::value && is_integral<Shift>::value, int>::type = 0>
	constexpr where_expression& operator>>=(Shift shift) noexcept(noexcept(std::declval<T&>() >>= shift)) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) >>= shift;
			}
		}
		return *this;
	}

	template<class U = T,
	         typename enable_if<is_integral<U>::value, int>::type = 0>
	constexpr where_expression& operator>>=(const value_type& shift) noexcept {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) >>= shift[i];
			}
		}
		return *this;
	}

	template<class U, class OtherAbi>
	constexpr where_expression& operator=(const basic_vec<U, OtherAbi>& other) noexcept {
		static_assert(static_cast<simd_size_type>(basic_vec<U, OtherAbi>::size) == static_cast<simd_size_type>(value_type::size),
			"std::simd::where assignment requires matching lane count");
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) = static_cast<T>(other[i]);
			}
		}
		return *this;
	}

	template<class U, class... Flags>
	constexpr void copy_from(const U* first, flags<Flags...> f = {}) {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) = detail::convert_or_copy<T>(first[static_cast<size_t>(i)], f);
			}
		}
	}

	template<class U, class... Flags>
	constexpr void copy_to(U* first, flags<Flags...> f = {}) const {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				first[static_cast<size_t>(i)] = detail::convert_or_copy<U>((*value_)[i], f);
			}
		}
	}

private:
	mask_type mask_;
	value_type* value_;
};

template<size_t Bytes, class Abi>
class where_expression<basic_mask<Bytes, Abi>, basic_mask<Bytes, Abi>> {
public:
	using mask_type = basic_mask<Bytes, Abi>;
	using value_type = basic_mask<Bytes, Abi>;

	constexpr where_expression(const mask_type& mask_value, value_type& value) noexcept
		: mask_(mask_value), value_(&value) {}

	constexpr where_expression& operator=(const value_type& other) noexcept {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) = other[i];
			}
		}
		return *this;
	}

	constexpr where_expression& operator=(bool scalar) noexcept {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				detail::lane_ref(*value_, i) = scalar;
			}
		}
		return *this;
	}

private:
	mask_type mask_;
	value_type* value_;
};

template<size_t Bytes, class Abi, class T>
class const_where_expression<basic_mask<Bytes, Abi>, basic_vec<T, Abi>> {
public:
	using mask_type = basic_mask<Bytes, Abi>;
	using value_type = basic_vec<T, Abi>;

	constexpr const_where_expression(const mask_type& mask_value, const value_type& value) noexcept
		: mask_(mask_value), value_(&value) {
		static_assert(Bytes == sizeof(T), "std::simd::where requires mask bytes to match vector value type");
		static_assert(static_cast<simd_size_type>(mask_type::size) == static_cast<simd_size_type>(value_type::size),
			"std::simd::where requires matching lane count");
	}

	template<class U, class... Flags>
	constexpr void copy_to(U* first, flags<Flags...> f = {}) const {
		for (simd_size_type i = 0; i < static_cast<simd_size_type>(value_type::size); ++i) {
			if (mask_[i]) {
				first[static_cast<size_t>(i)] = detail::convert_or_copy<U>((*value_)[i], f);
			}
		}
	}

private:
	mask_type mask_;
	const value_type* value_;
};

template<size_t Bytes, class Abi>
class const_where_expression<basic_mask<Bytes, Abi>, basic_mask<Bytes, Abi>> {
public:
	using mask_type = basic_mask<Bytes, Abi>;
	using value_type = basic_mask<Bytes, Abi>;

	constexpr const_where_expression(const mask_type& mask_value, const value_type& value) noexcept
		: mask_(mask_value), value_(&value) {}

private:
	mask_type mask_;
	const value_type* value_;
};

template<size_t Bytes, class Abi, class T>
constexpr where_expression<basic_mask<Bytes, Abi>, basic_vec<T, Abi>> where(const basic_mask<Bytes, Abi>& mask_value,
                                                                            basic_vec<T, Abi>& value) noexcept {
	return where_expression<basic_mask<Bytes, Abi>, basic_vec<T, Abi>>(mask_value, value);
}

template<size_t Bytes, class Abi, class T>
constexpr const_where_expression<basic_mask<Bytes, Abi>, basic_vec<T, Abi>> where(const basic_mask<Bytes, Abi>& mask_value,
                                                                                  const basic_vec<T, Abi>& value) noexcept {
	return const_where_expression<basic_mask<Bytes, Abi>, basic_vec<T, Abi>>(mask_value, value);
}

template<size_t Bytes, class Abi>
constexpr where_expression<basic_mask<Bytes, Abi>, basic_mask<Bytes, Abi>> where(const basic_mask<Bytes, Abi>& mask_value,
                                                                                 basic_mask<Bytes, Abi>& value) noexcept {
	return where_expression<basic_mask<Bytes, Abi>, basic_mask<Bytes, Abi>>(mask_value, value);
}

template<size_t Bytes, class Abi>
constexpr const_where_expression<basic_mask<Bytes, Abi>, basic_mask<Bytes, Abi>> where(const basic_mask<Bytes, Abi>& mask_value,
                                                                                       const basic_mask<Bytes, Abi>& value) noexcept {
	return const_where_expression<basic_mask<Bytes, Abi>, basic_mask<Bytes, Abi>>(mask_value, value);
}

template<class T, class Abi>
constexpr where_expression<typename basic_vec<T, Abi>::mask_type, basic_vec<T, Abi>> where(bool cond, basic_vec<T, Abi>& value) noexcept {
	typename basic_vec<T, Abi>::mask_type mask_value(cond);
	return where(mask_value, value);
}

template<class T, class Abi>
constexpr const_where_expression<typename basic_vec<T, Abi>::mask_type, basic_vec<T, Abi>> where(bool cond, const basic_vec<T, Abi>& value) noexcept {
	typename basic_vec<T, Abi>::mask_type mask_value(cond);
	return where(mask_value, value);
}

