# `std::unique_resource` Implementation Review Report (v3-r0)

**Date**: 2026-03-20  
**Reviewer**: Code Review  
**Scope**: `backport/cpp26/unique_resource.hpp` and associated tests  
**Standard Reference**: P0052R15 (C++26 `std::unique_resource`)

---

## Executive Summary

This review evaluates the `std::unique_resource` backport implementation from the perspective of Standard Library quality requirements (exception safety, constexpr support, ABI stability, and standard conformance).

**Overall Rating**: **B+** (Production-ready with minor limitations)

The implementation demonstrates **excellent exception safety** with correct strong exception guarantee implementation and copy-fallback logic. However, it **lacks constexpr support** which is required by C++23/26 standards, and has **missing edge-case tests** (self-move, self-swap).

---

## 1. Exception Safety Analysis

### 1.1 Constructor Exception Safety (Lines 50-62, 273-281)

**Implementation**:
```cpp
template<class RR, class DD>
    requires (is_constructible_v<resource_storage, RR> &&
              is_constructible_v<D, DD> &&
              (is_nothrow_constructible_v<resource_storage, RR> ||
               is_constructible_v<resource_storage, RR&>) &&
              (is_nothrow_constructible_v<D, DD> ||
               is_constructible_v<D, DD&>))
```

**Assessment**: 
- ✅ Constraints follow P0052R15 requirements
- ✅ Copy-fallback logic implemented correctly (Line 196)
- ✅ Exception during construction properly cleans up resource (Lines 220-225)

**Test Coverage**:
- `test_unique_resource_exception.cpp` Lines 92-103: Verifies resource cleanup on deleter construction failure

**Status**: **COMPLIANT**

### 1.2 Move Constructor Exception Safety (Lines 67-72, 248-271)

**Critical Code Path**:
```cpp
unique_resource(unique_resource&& other) noexcept(
    is_nothrow_move_constructible_v<resource_storage> &&
    is_nothrow_move_constructible_v<D>)
    : resource_(construct_resource_from_other(other))
    , deleter_(construct_deleter_from_other(other, resource_))
    , execute_on_reset_(std::exchange(other.execute_on_reset_, false)) {}
```

**Issue Identified** (Minor):
In `construct_deleter_from_other` (Lines 257-271), when deleter move construction throws, the implementation invokes the deleter on **potentially moved-from resource**:

```cpp
catch (...) {
    if (other.execute_on_reset_) {
        std::invoke(other.deleter_, resource_value(resource_));  // resource may be moved-from
        other.release();
    }
    throw;
}
```

**Analysis**: This is technically allowed by the standard (resource is in a valid but unspecified state), but the semantic subtlety should be documented.

**Test Coverage**:
- `test_unique_resource_wording.cpp` Lines 167-181: Verifies copy-fallback for deleter

**Status**: **COMPLIANT** (with documentation recommendation)

### 1.3 Move Assignment Exception Safety (Lines 74-105)

**Strong Exception Safety Implementation**:
```cpp
if constexpr (is_nothrow_move_assignable_v<R> || !is_copy_assignable_v<R>) {
    if constexpr (is_nothrow_move_assignable_v<D> || !is_copy_assignable_v<D>) {
        resource_ = std::move(other.resource_);
        deleter_ = std::move(other.deleter_);
    } else {
        deleter_ = other.deleter_;      // copy deleter first (may throw)
        resource_ = std::move(other.resource_);
    }
} else {
    // ... symmetric logic
}
```

**Assessment**: 
- ✅ Correctly implements P0052R15 strong exception guarantee
- ✅ Copy-assignment of potentially-throwing members happens before move-assignment of non-throwing members
- ✅ If copy throws, object remains in original state

**Test Gap**: No test specifically verifies the "copy deleter first, then move resource" ordering behavior.

**Status**: **COMPLIANT** (add test recommended)

### 1.4 `reset(RR&&)` Exception Safety (Lines 118-138)

**Implementation**:
```cpp
template<class RR>
void reset(RR&& r) {
    reset();  // noexcept cleanup of old value
    
    try {
        if constexpr (is_nothrow_assignable_v<resource_storage&, RR>) {
            resource_ = std::forward<RR>(r);
        } else {
            resource_ = std::as_const(r);  // copy from lvalue
        }
        execute_on_reset_ = true;
    } catch (...) {
        std::invoke(deleter_, r);  // cleanup new resource on failure
        throw;
    }
}
```

**Assessment**:
- ✅ Correctly implements P0052R15 semantics: deleter called on new resource if assignment throws
- ✅ Uses copy-from-lvalue fallback for potentially-throwing assignments

**Test Coverage**:
- `test_unique_resource_exception.cpp` Lines 164-178: Verifies cleanup of new resource on assignment failure

**Status**: **COMPLIANT**

### 1.5 Destructor (Lines 107-109)

```cpp
~unique_resource() noexcept {
    reset();
}
```

**Assessment**: ✅ Correctly marked `noexcept`, delegates to `noexcept reset()`.

**Status**: **COMPLIANT**

---

## 2. Constexpr Support

**Status**: ❌ **NOT IMPLEMENTED**

**Standard Requirement**: C++23 P0052R15 requires `unique_resource` operations to be `constexpr` (excluding operations involving virtual functions or I/O).

**Impact**:
```cpp
// This will NOT compile with current implementation:
constexpr auto make_resource() {
    return std::unique_resource(42, [](int) {});  // Error: not constexpr
}
```

**Required Changes**:
Add `constexpr` to:
- All constructors
- `get()`, `get_deleter()`
- `reset()`, `release()`
- Move operations
- `swap()`

**Severity**: **MEDIUM** - Not a runtime bug, but limits compile-time usage scenarios.

---

## 3. Noexcept Specifications

### 3.1 Conditional Noexcept (Lines 152-156)

```cpp
template<class T = R>
    requires (is_pointer_v<T> && !is_void_v<remove_pointer_t<T>>)
add_lvalue_reference_t<remove_pointer_t<T>> operator*() const 
    noexcept(noexcept(*declval<const T&>())) {
    return *get();
}
```

**Assessment**: ✅ Correct - noexcept depends on underlying type's dereference operation.

### 3.2 Swap Noexcept (Lines 164-171)

```cpp
void swap(unique_resource& other)
    noexcept(is_nothrow_swappable_v<resource_storage> && is_nothrow_swappable_v<D>)
```

**Assessment**: ✅ Correct - uses C++17 `is_nothrow_swappable_v`.

### 3.3 Move Operations

All move operations have appropriate conditional noexcept specifications based on member type properties.

**Status**: **COMPLIANT**

---

## 4. SFINAE and Constraints

### 4.1 Constructor Constraints (Lines 50-56)

Uses C++20 `requires` clauses:
```cpp
requires (is_constructible_v<resource_storage, RR> &&
          is_constructible_v<D, DD> &&
          (is_nothrow_constructible_v<resource_storage, RR> ||
           is_constructible_v<resource_storage, RR&>) &&
          (is_nothrow_constructible_v<D, DD> ||
           is_constructible_v<D, DD&>))
```

**Analysis**: The constraints are **stricter than P0052R15 minimum requirements** (which only require `is_constructible_v`). This stricter constraint is intentional to enable strong exception safety via copy-fallback.

**Status**: **ACCEPTABLE** (stricter constraints are allowed and beneficial)

### 4.2 Pointer Operator Constraints (Lines 152-162)

```cpp
template<class T = R>
    requires (is_pointer_v<T> && !is_void_v<remove_pointer_t<T>>)
```

**Assessment**: ✅ Correctly restricts `operator*` and `operator->` to pointer types.

### 4.3 Friend Swap Constraint (Lines 294-301)

```cpp
requires (is_swappable_v<conditional_t<is_reference_v<R>, reference_wrapper<remove_reference_t<R>>, R>> &&
          is_swappable_v<D>)
```

**Assessment**: ✅ Correctly constrains ADL-discovered swap.

**Status**: **COMPLIANT**

---

## 5. ABI Stability

### 5.1 Memory Layout (Lines 289-291)

```cpp
resource_storage resource_;
D deleter_;
bool execute_on_reset_;
```

**Analysis**:
- **Order**: Large-to-small (generally optimal)
- **Padding**: `bool` member may cause 3-7 bytes padding depending on `D` alignment
- **Standard Layout**: Not guaranteed (contains `reference_wrapper` conditional member)

**Recommendation**: Consider `[[no_unique_address]]` (C++20) on `deleter_` if stateless, but this would be an ABI break from current implementation.

### 5.2 Inline Static Members

```cpp
inline static constexpr integral_constant<simd_size_type, simd_size<T, Abi>::value> size{};
```

(Note: This is from `basic_vec`, but similar patterns apply)

**Status**: **ACCEPTABLE** for header-only library

---

## 6. Standard Conformance (P0052R15)

### 6.1 API Completeness

| Feature | Status | Notes |
|---------|--------|-------|
| Constructors | ✅ | Including copy-fallback logic |
| Move operations | ✅ | Strong exception guarantee |
| `reset()` / `reset(RR&&)` | ✅ | Correct semantics |
| `release()` | ✅ | noexcept |
| `get()` / `get_deleter()` | ✅ | const-only overloads |
| `operator*` / `operator->` | ✅ | Pointer types only |
| `swap` | ✅ | Member and non-member |
| CTAD | ✅ | Line 304 |
| `make_unique_resource_checked` | ✅ | Lines 306-314 |

### 6.2 Deviations from Standard

1. **Constexpr**: Not implemented (required by C++23)
2. **Constraints**: Stricter than minimum (acceptable)
3. **Feature macro**: Not defined (`__cpp_lib_unique_resource`) - intentional per project policy

---

## 7. Test Coverage Analysis

### 7.1 Well-Covered Scenarios ✅

- Basic lifecycle (construct, destruct, reset, release)
- Move semantics (move-construct, move-assign)
- Exception safety paths (construction failure, move failure, reset failure)
- Copy-fallback behavior (move-may-throw → copy)
- Reference resource types (`unique_resource<T&, D>`)
- Pointer dereference operators
- ADL swap
- CTAD deduction

### 7.2 Missing Test Scenarios ❌

**CRITICAL GAPS**:

1. **Self-move assignment**:
   ```cpp
   resource = std::move(resource);  // Behavior unspecified but should not crash
   ```

2. **Self-swap**:
   ```cpp
   resource.swap(resource);  // Should be well-defined
   ```

3. **Non-noexcept deleters**:
   ```cpp
   struct throwing_deleter {
       void operator()(T) { throw std::runtime_error("cleanup failed"); }
   };
   ```

4. **Complex types**:
   - Types with throwing move/copy constructors
   - Types with non-trivial destructors

5. **Move assignment edge cases**:
   - Verify "copy deleter first" ordering when deleter may throw

---

## 8. Code Quality

### 8.1 Strengths

- **Clear naming**: `construct_resource_from_input`, `resource_value`, etc.
- **Type safety**: Proper use of `conditional_t`, `decay_t`, `remove_reference_t`
- **Documentation**: Key paths have explanatory comments
- **Modern C++**: Uses C++20 `requires`, `is_nothrow_*` traits

### 8.2 Areas for Improvement

1. **Code duplication**: 
   - `construct_resource` vs `construct_resource_from_input` have overlapping logic
   - Could be refactored with better abstraction

2. **Comments**:
   - Complex branching in move assignment (4-way `if constexpr`) needs inline documentation
   - Copy-fallback rationale should be documented

3. **Consistency**:
   - Mixed tab and space indentation (minor style issue)

---

## 9. Key Findings Summary

| Finding | Severity | Status |
|---------|----------|--------|
| Missing constexpr support | **MEDIUM** | Not implemented |
| Exception safety implementation | **NONE** | Excellent |
| Missing self-move/self-swap tests | **LOW** | Test gap |
| Missing non-noexcept deleter tests | **LOW** | Test gap |
| Move-assignment ordering not tested | **LOW** | Test gap |
| Bool member padding | **INFO** | Acceptable |
| Stricter than standard constraints | **INFO** | Acceptable |

---

## 10. Recommendations

### 10.1 Required Before Production (Hard Requirements)

None. The implementation is production-ready for runtime usage.

### 10.2 Recommended Improvements

1. **Add constexpr support** (Priority: HIGH)
   - Required for C++23 conformance
   - Enable compile-time usage scenarios

2. **Add missing tests** (Priority: MEDIUM)
   - Self-move assignment
   - Self-swap
   - Non-noexcept deleter behavior
   - Move-assignment ordering verification

3. **Documentation** (Priority: LOW)
   - Document copy-fallback behavior
   - Document why constraints are stricter than standard
   - Add note about moved-from resource in deleter construction failure path

4. **Code cleanup** (Priority: LOW)
   - Normalize indentation (spaces vs tabs)
   - Refactor duplicate construction logic

---

## 11. Conclusion

The `std::unique_resource` implementation is of **high quality** and suitable for production use. The exception safety implementation is exemplary, correctly handling all edge cases specified in P0052R15.

The primary limitation is the **lack of constexpr support**, which prevents compile-time usage but does not affect runtime correctness.

**Production Readiness**: **APPROVED** with recommendation to address constexpr support in future iteration.

---

## Appendix: Test Command

```bash
# Run all unique_resource tests
ctest --test-dir build -R unique_resource --output-on-failure

# Expected: 7/7 tests passing
# - unique_resource_runtime
# - unique_resource_checked
# - unique_resource_exception
# - unique_resource_wording
# - unique_resource_api_compile
# - unique_resource_checked_noexcept_compile
# - unique_resource_include_compile
```
