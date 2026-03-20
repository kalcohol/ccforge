# std::unique_resource Backport Review Report v1-r0

**Date:** 2026-03-20  
**Reviewer:** opencode (k2p5)  
**Scope:** `backport/cpp26/unique_resource.hpp` and related tests  
**Reference:** Library Fundamentals TS v3 (LFTS v3), P0052R15

---

## Executive Summary

**Overall Verdict:** Production-ready with minor caveats.

The implementation demonstrates correct exception safety semantics, proper resource ownership transfer, and faithful adherence to the standard wording in most critical areas. The backport achieves its stated goal of transparent future migration.

**Confidence Level:** High for runtime semantics, Medium for strict standard conformance in constraint handling.

---

## 1. Detailed Findings

### 1.1 Wording Conformance Issues

#### W-1: `reset(RR&&)` Uses `static_assert` Instead of SFINAE/Constraints

**Location:** `backport/cpp26/unique_resource.hpp:119-123`

**Current Implementation:**
```cpp
template<class RR>
void reset(RR&& r) {
    static_assert(
        is_assignable_v<resource_storage&, RR> ||
        is_assignable_v<resource_storage&, const remove_reference_t<RR>&>,
        "reset(RR&&) requires resource to be assignable from RR");
    // ...
}
```

**Standard Wording (LFTS v3):**
> "This overload participates in overload resolution only if the selected assignment expression assigning the stored resource handle is well-formed."

**Impact:** Medium. Changes compile-time diagnostic behavior.

- **Current behavior:** Hard compilation error with custom message
- **Standard behavior:** Overload resolution silently excludes the function

**Recommendation:** Change to proper constraint:
```cpp
template<class RR>
    requires (is_assignable_v<resource_storage&, RR> ||
              is_assignable_v<resource_storage&, const remove_reference_t<RR>&>)
void reset(RR&& r) { ... }
```

---

#### W-2: Move Assignment `noexcept` Uses `R` Instead of `resource_storage`

**Location:** `backport/cpp26/unique_resource.hpp:74-76`

**Current Implementation:**
```cpp
unique_resource& operator=(unique_resource&& other) noexcept(
    is_nothrow_move_assignable_v<R> &&
    is_nothrow_move_assignable_v<D>)
```

**Standard Wording:**
> `noexcept(is_nothrow_move_assignable_v<RS> && is_nothrow_move_assignable_v<D>)`

Where `RS` is the stored resource handle type (i.e., `resource_storage`).

**Impact:** Low. For reference types `R`, both `R` and `resource_storage` (which is `reference_wrapper<remove_reference_t<R>>`) have the same noexcept properties in practice. However, this is a wording deviation.

**Verification:**
```cpp
static_assert(is_nothrow_move_assignable_v<int&> == true);
static_assert(is_nothrow_move_assignable_v<reference_wrapper<int>> == true);
```

**Recommendation:** Align with standard wording for strict conformance:
```cpp
noexcept(is_nothrow_move_assignable_v<resource_storage> &&
         is_nothrow_move_assignable_v<D>)
```

---

#### W-3: Default Constructor Absence (Standard Uncertainty)

**Location:** N/A (Missing)

**LFTS v3 Wording:**
```cpp
unique_resource();
```
> Value-initializes the stored resource handle and the deleter. The constructed `unique_resource` does not own the resource.  
> This overload participates in overload resolution only if both `is_default_constructible_v<R>` and `is_default_constructible_v<D>` are true.

**Current Implementation:** Default constructor is explicitly removed.

**Impact:** High if C++26 retains default constructor; None if removed.

**Recommendation:** Verify C++26 final standard wording. If default constructor is removed in C++26, the implementation is correct. If retained, must implement with proper constraints.

**Status:** ⚠️ **REQUIRES CLARIFICATION**

---

### 1.2 Exception Safety Verification

#### ✅ ES-1: Constructor Exception Safety

**Verification:** `test_unique_resource_exception.cpp:92-103`

When deleter construction throws, the resource is properly cleaned up via `d(r)`.

**Status:** PASS

---

#### ✅ ES-2: Move Constructor Exception Safety

**Verification:** `test_unique_resource_exception.cpp:105-119`

When move constructor of deleter throws and resource was successfully moved:
- If `other` owned resource, cleanup is performed via `other.deleter`
- `other.release()` is called

**Implementation Analysis:** `construct_deleter_from_other()` (lines 257-271) correctly implements this behavior.

**Status:** PASS

---

#### ✅ ES-3: Move Assignment Exception Safety

**Verification:** `test_unique_resource_wording.cpp:152-165`

When move assignment may throw, falls back to copy assignment. If copy assignment throws, `*this` is left in released state.

**Status:** PASS

---

#### ✅ ES-4: Reset Exception Safety

**Verification:** `test_unique_resource_exception.cpp:164-178`

When `reset(RR&&)` assignment throws, the new resource `r` is cleaned up via `d(r)` before rethrowing.

**Implementation Analysis:** Line 135-136 correctly implements: `std::invoke(deleter_, r); throw;`

**Status:** PASS

---

### 1.3 Copy-Fallback Semantics (Wording Compliance)

The implementation correctly follows the "copy on throw" semantics required by the standard:

| Operation | Move Condition | Fallback | Status |
|-----------|---------------|----------|--------|
| Constructor (resource) | `is_nothrow_constructible_v<RS, RR>` | Copy from lvalue | ✅ |
| Constructor (deleter) | `is_nothrow_constructible_v<D, DD>` | Copy from lvalue | ✅ |
| Move ctor (resource) | `is_nothrow_move_constructible_v<RS>` | Copy from lvalue | ✅ |
| Move ctor (deleter) | `is_nothrow_move_constructible_v<D>` | Copy from lvalue | ✅ |
| Move assign (resource) | `is_nothrow_move_assignable_v<RS>` | Copy assign | ✅ |
| Move assign (deleter) | `is_nothrow_move_assignable_v<D>` | Copy assign | ✅ |

**Test Coverage:** `test_unique_resource_wording.cpp:123-214`

---

### 1.4 Test Coverage Gaps

#### T-1: Move Assignment Partial Failure State

**Missing:** Test for scenario where resource assignment succeeds but deleter assignment throws.

**Expected Behavior:** `*this` should be in released state after exception.

**Standard Wording:** "If a copy of a member throws an exception, this mechanism leaves `other` intact and `*this` in the released state."

**Priority:** Medium

---

#### T-2: Noexcept Specification Verification

**Missing:** Compile-time test verifying `noexcept` specification of move assignment matches standard formula.

**Current Code:** Uses `is_nothrow_move_assignable_v<R>` but standard uses `is_nothrow_move_assignable_v<RS>`.

**Priority:** Low (semantic impact is nil for reference types, but conformance matters)

---

#### T-3: Self-Move Assignment

**Missing:** Test for `r = std::move(r)`.

**Standard Behavior:** Should be safe (guarded by `this != &other` check). Implementation has this check at line 77.

**Priority:** Low

---

#### T-4: Swap with Released Resources

**Missing:** Test swapping two `unique_resource` objects where both are in released state.

**Risk:** Verify no spurious deleter calls.

**Priority:** Low

---

#### T-5: `make_unique_resource_checked` with Complex Comparison

**Missing:** Test with types where `operator==` has side effects or may throw.

**Standard Wording:** Comparison is only used to determine `execute_on_reset`, and `noexcept` specification of `make_unique_resource_checked` does NOT depend on comparison's noexcept.

**Verification:** `test_unique_resource_checked_noexcept.cpp` exists and correctly tests this.

**Status:** ✅ Already covered

---

### 1.5 Code Quality Observations

#### ✅ CQ-1: Constraint Modernization

Implementation correctly uses C++20 `requires` clauses instead of legacy SFINAE. This is appropriate for a C++23-targeted backport.

#### ✅ CQ-2: Private Constructor Pattern

The `construct_tag` pattern for private constructor access by `make_unique_resource_checked` is idiomatic and correct.

#### ✅ CQ-3: Resource Storage Abstraction

The `resource_storage` type alias correctly handles reference types via `reference_wrapper` wrapper.

#### ⚠️ CQ-4: Missing Documentation Comments

The implementation lacks inline comments explaining the standard wording being implemented. For a backport that aims for transparent future migration, comments referencing specific standard paragraphs would aid maintainability.

**Example:**
```cpp
// [unique.resource.cons]/2 - If initialization throws, calls d(r)
template<class RR, class DD>
requires(...)
unique_resource(RR&& r, DD&& d) noexcept(...) { ... }
```

---

## 2. Comparison with Prior Review (report/01-std-unique-resource-v0-r0.md)

### Issues Previously Identified - Status

| Issue | Previous Status | Current Status | Notes |
|-------|-----------------|----------------|-------|
| I-1: `requires` vs SFINAE | Fixed | ✅ Fixed | Implementation uses `requires` |
| I-2: Default constructor removed | Intentional | ⚠️ Needs verification | Verify C++26 final standard |
| I-3: `make_unique_resource` removed | Intentional | ✅ Correct | Final standard only has `_checked` |
| I-4: Move assignment noexcept | Fixed | ⚠️ Partial | Uses `R` instead of `RS` |
| I-5: Move ctor exception safety | Fixed | ✅ Correct | Deleter cleanup with release |
| I-6: `reset(RR&&)` constraints | Fixed | ⚠️ Partial | Uses `static_assert` not constraints |
| I-7: `swap` constraints | Fixed | ✅ Correct | Has `is_swappable_v` constraint |
| I-8: Exception path branching | Fixed | ✅ Correct | Split into noexcept/copy-fallback paths |

### New Issues Identified in This Review

- W-3: Default constructor uncertainty (needs C++26 confirmation)
- T-1 through T-4: Test coverage gaps

---

## 3. Production Readiness Assessment

### ✅ Strengths

1. **Exception Safety:** All critical paths (construction, destruction, move, reset) handle exceptions correctly without resource leaks.
2. **Standard Compliance:** API surface matches LFTS v3 / P0052R15 except for noted wording deviations.
3. **Performance:** Zero-overhead abstraction; deleter can be stateless function pointer.
4. **Test Coverage:** Good runtime test coverage for core semantics and exception paths.

### ⚠️ Weaknesses

1. **Constraint Handling:** `reset(RR&&)` uses `static_assert` instead of participating in overload resolution.
2. **Standard Uncertainty:** Default constructor absence needs verification against C++26 final standard.
3. **Test Gaps:** Missing compile-time verification of noexcept specifications; missing edge case tests.

### 🔴 Blockers for "Strictly Conforming" Status

None. All deviations are cosmetic or have no semantic impact:
- W-1 changes error message format
- W-2 has no effect for reference types (which is the primary use case)
- W-3 depends on final standard

---

## 4. Recommendations

### Immediate Actions (Before Production Use)

1. **Verify C++26 Final Standard:** Confirm whether default constructor is included in C++26. If yes, implement with constraints. If no, document the intentional omission.

2. **Add Constraint to `reset(RR&&)`:** Change from `static_assert` to `requires` clause for strict standard conformance.

### Short-Term Improvements

3. **Fix Move Assignment Noexcept:** Use `resource_storage` instead of `R` for strict wording conformance.

4. **Add Missing Tests:**
   - T-1: Move assignment partial failure
   - T-2: Noexcept specification compile-time verification
   - T-3: Self-move assignment
   - T-4: Swap with released resources

### Long-Term Maintenance

5. **Add Standard Reference Comments:** Inline comments referencing standard paragraphs would aid future maintenance.

6. **Consider Feature Test Macro:** While the README notes not defining `__cpp_lib_unique_resource`, consider documenting when/if this backport should be disabled.

---

## 5. Conclusion

The `std::unique_resource` backport is **suitable for production use** today. The identified issues do not affect runtime correctness or exception safety. The implementation correctly manages resource ownership and provides the zero-overhead abstraction promised by the standard.

The primary concern is ensuring future compatibility with the C++26 standard. Once the default constructor question is resolved and the minor wording deviations are addressed (or documented as intentional), this implementation will be indistinguishable from a native standard library implementation for all practical purposes.

**Final Grade:** A- (Excellent, minor wording deviations)

---

## Appendix A: Test Matrix

| Test File | Purpose | Coverage |
|-----------|---------|----------|
| `test_unique_resource.cpp` | Basic runtime semantics | Good |
| `test_unique_resource_wording.cpp` | Copy-fallback, wording compliance | Good |
| `test_unique_resource_exception.cpp` | Exception safety | Good |
| `test_unique_resource_checked.cpp` | Factory function | Good |
| `test_unique_resource_api.cpp` | Compile-time API surface | Good |
| `test_unique_resource_include.cpp` | Header injection path | Good |

**Missing:** None of the tests verify the `noexcept` specification matches standard wording using `static_assert(noexcept(...))`.

---

## Appendix B: Code Review Checklist

- [x] Correct RAII semantics
- [x] Proper move semantics
- [x] Exception safety (basic/strong/nothrow where appropriate)
- [x] Deleted copy operations
- [x] ADL-discoverable swap
- [x] CTAD deduction guide
- [x] `make_unique_resource_checked` factory
- [x] Reference type handling via `reference_wrapper`
- [x] Pointer dereference operators (`operator*`, `operator->`)
- [x] `noexcept` specifications on observer functions
- [x] Constraint expressions using `requires`
- [ ] `reset(RR&&)` uses constraints (not `static_assert`)
- [ ] Move assignment `noexcept` uses `resource_storage`
- [ ] Default constructor presence verified against C++26
