template<class T,
         class Abi,
         class BinaryOperation = plus<>,
         typename enable_if<
             !is_same<detail::remove_cvref_t<BinaryOperation>, typename basic_vec<T, Abi>::mask_type>::value &&
             detail::is_reduction_binary_operation<T, BinaryOperation>::value,
             int>::type = 0>
constexpr T reduce(const basic_vec<T, Abi>& value, BinaryOperation binary_op = {}) noexcept(
	noexcept(std::declval<const BinaryOperation&>()(std::declval<const vec<T, 1>&>(), std::declval<const vec<T, 1>&>()))) {
	vec<T, 1> result(value[0]);
	for (simd_size_type i = 1; i < basic_vec<T, Abi>::size; ++i) {
		result = binary_op(result, vec<T, 1>(value[i]));
	}
	return result[0];
}

template<class T,
         class Abi,
         class BinaryOperation = plus<>,
         typename enable_if<detail::is_reduction_binary_operation<T, BinaryOperation>::value, int>::type = 0>
constexpr T reduce(const basic_vec<T, Abi>& value,
                   const typename basic_vec<T, Abi>::mask_type& mask_value,
                   BinaryOperation binary_op = {},
                   T identity_element = detail::reduction_identity<T, BinaryOperation>::value()) noexcept(
	noexcept(std::declval<const BinaryOperation&>()(std::declval<const vec<T, 1>&>(), std::declval<const vec<T, 1>&>()))) {
	vec<T, 1> result(identity_element);
	for (simd_size_type i = 0; i < basic_vec<T, Abi>::size; ++i) {
		if (mask_value[i]) {
			result = binary_op(result, vec<T, 1>(value[i]));
		}
	}
	return result[0];
}

template<class T, class Abi>
constexpr T reduce_min(const basic_vec<T, Abi>& value) noexcept {
	T result = value[0];
	for (simd_size_type i = 1; i < basic_vec<T, Abi>::size; ++i) {
		if (value[i] < result) {
			result = value[i];
		}
	}
	return result;
}

template<class T, class Abi>
constexpr T reduce_min(const basic_vec<T, Abi>& value, const typename basic_vec<T, Abi>::mask_type& mask_value) noexcept {
	simd_size_type first = 0;
	while (first < basic_vec<T, Abi>::size && !mask_value[first]) {
		++first;
	}

	if (first == basic_vec<T, Abi>::size) {
		return numeric_limits<T>::max();
	}

	T result = value[first];
	for (simd_size_type i = first + 1; i < basic_vec<T, Abi>::size; ++i) {
		if (mask_value[i] && value[i] < result) {
			result = value[i];
		}
	}
	return result;
}

template<class T, class Abi>
constexpr T reduce_max(const basic_vec<T, Abi>& value) noexcept {
	T result = value[0];
	for (simd_size_type i = 1; i < basic_vec<T, Abi>::size; ++i) {
		if (value[i] > result) {
			result = value[i];
		}
	}
	return result;
}

template<class T, class Abi>
constexpr T reduce_max(const basic_vec<T, Abi>& value, const typename basic_vec<T, Abi>::mask_type& mask_value) noexcept {
	simd_size_type first = 0;
	while (first < basic_vec<T, Abi>::size && !mask_value[first]) {
		++first;
	}

	if (first == basic_vec<T, Abi>::size) {
		return numeric_limits<T>::lowest();
	}

	T result = value[first];
	for (simd_size_type i = first + 1; i < basic_vec<T, Abi>::size; ++i) {
		if (mask_value[i] && value[i] > result) {
			result = value[i];
		}
	}
	return result;
}

template<size_t Bytes, class Abi>
constexpr bool all_of(const basic_mask<Bytes, Abi>& value) noexcept {
	for (simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) {
		if (!value[i]) {
			return false;
		}
	}
	return true;
}

template<size_t Bytes, class Abi>
constexpr bool any_of(const basic_mask<Bytes, Abi>& value) noexcept {
	for (simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) {
		if (value[i]) {
			return true;
		}
	}
	return false;
}

template<size_t Bytes, class Abi>
constexpr bool none_of(const basic_mask<Bytes, Abi>& value) noexcept {
	return !any_of(value);
}

template<size_t Bytes, class Abi>
constexpr simd_size_type reduce_count(const basic_mask<Bytes, Abi>& value) noexcept {
	simd_size_type result = 0;
	for (simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) {
		if (value[i]) {
			++result;
		}
	}
	return result;
}

template<size_t Bytes, class Abi>
constexpr simd_size_type reduce_min_index(const basic_mask<Bytes, Abi>& value) noexcept {
	for (simd_size_type i = 0; i < basic_mask<Bytes, Abi>::size; ++i) {
		if (value[i]) {
			return i;
		}
	}
	return 0;
}

template<size_t Bytes, class Abi>
constexpr simd_size_type reduce_max_index(const basic_mask<Bytes, Abi>& value) noexcept {
	for (simd_size_type i = basic_mask<Bytes, Abi>::size; i > 0; --i) {
		if (value[i - 1]) {
			return i - 1;
		}
	}
	return 0;
}

constexpr bool all_of(bool value) noexcept {
	return value;
}

constexpr bool any_of(bool value) noexcept {
	return value;
}

constexpr bool none_of(bool value) noexcept {
	return !value;
}

constexpr simd_size_type reduce_count(bool value) noexcept {
	return value ? 1 : 0;
}

constexpr simd_size_type reduce_min_index(bool) noexcept {
	return 0;
}

constexpr simd_size_type reduce_max_index(bool) noexcept {
	return 0;
}

} // namespace simd
} // namespace std
