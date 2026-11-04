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

Optional cost probe: configure a Release build with
-DFORGE_HIVE_BUILD_COST_PROBE=ON, build hive_cost_probe, then run it directly.
Usage: hive_cost_probe {list|hive} {int|double|ordinary64|align64}
                       [default|256|1024|4096|8192]
The optional block maximum applies only to hive; its minimum remains the
default minimum. Each process runs one configuration with 1,000,000 elements.
It prints a CSV header and one row. W1 bytes, peak, allocation calls, size,
and capacity include container construction and are captured after filling;
list capacity is its size. Allocator metadata is excluded. W1 and W2 times
and traversal min/median are total nanoseconds, not per-element values.
Each traversal has one warmup and 31 samples. W2 uses three seeded random
erase/reinsert rounds; the dense checksum is validated before W2.
The probe is not a CTest timing gate. Orchestrate five interleaved processes
per container/version, record batch load and apply the specification's noise
rules externally. Compile baseline and new headers with the same probe and
flags (specified primary toolchain: clang 19, libc++, -O2 -DNDEBUG).
