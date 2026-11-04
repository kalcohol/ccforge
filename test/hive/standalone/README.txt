Standalone Hive backport harness
================================

Configure this directory directly; it is not included by the root project.
Point FORGE_HIVE_GTEST_SOURCE_DIR at an existing pinned GoogleTest checkout.
Nothing is downloaded or installed. The standard defaults to C++23 and can be
set to 26 on a compiler and standard library supporting that mode. The
standard library must supply C++23 facilities used by Hive, such as
std::from_range. Only the Hive wrapper and implementation are staged in the
build tree. A native <hive> header or __cpp_lib_hive macro is a conflict,
including partial support; the harness refuses to inject in that case.

Configure runs paired positive and expected-rejection compile checks as static
libraries, with logs in the build tree. A failure aborts configuration. Build
the default targets before running CTest; CTest runs the runtime GoogleTests
and multi-translation-unit executable, each with a timeout. No CTest test
invokes a build. These checks are not registered as synthetic passing tests.

Deferred: test_hive_version_feature.cpp requires a shared backport <version>,
which has not been imported. The multi-TU test checks the macro after <hive>,
not the separate version-only requirement.

Mutation.cmake builds the independent model against normal and size-mutated
Hive-only overlays with the same configured toolchain. CTest requires the
normal model to pass and the mutant to fail through a model mismatch, not a
crash or compilation failure. No original implementation file is changed.
The supplied legacy verify_hive_mutation.cmake remains only as provenance;
this harness does not invoke its direct compiler path.
