#pragma once

#if defined(__SANITIZE_THREAD__)
#define FORGE_TEST_STATIC_OPSTATE_TSAN 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define FORGE_TEST_STATIC_OPSTATE_TSAN 1
#endif
#endif
