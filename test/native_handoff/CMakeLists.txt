# Native handoff: all std-injecting wrappers in one TU. Guards against
# ODR/redefinition regressions when a toolchain ships (partial) native support
# and Forge is expected to stand aside.
forge_add_runtime_test(native_handoff_test native_handoff_test.cpp NativeHandoff)
