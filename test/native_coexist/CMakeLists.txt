# Native coexistence: all std-injecting wrappers in one TU. Guards against
# ODR/redefinition regressions when a toolchain ships (partial) native support
# and Forge is expected to stand aside.
forge_add_runtime_test(native_coexist_test native_coexist_test.cpp NativeCoexist)
