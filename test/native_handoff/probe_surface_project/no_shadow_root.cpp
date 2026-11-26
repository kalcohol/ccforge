#if __has_include(<forge_native_execution.hpp>)
#error The Forge backport include root must be absent when no backport is needed.
#endif

#if defined(FORGE_TEST_EXPECT_NO_BASE_MDSPAN)
#if !defined(FORGE_NO_BASE_MDSPAN)
#error The unavailable base mdspan decision must reach the standard consumer.
#endif
#if !__has_include(<linalg>) || !__has_include(<mdspan>)
#error The unavailable-header fixtures must be on the consumer's include path.
#endif
#endif

int main() {}
