// Check the full Forge include surface, not just the standard target.
#if !__has_include(<forge/unique_resource.hpp>)
#error The Forge extension include root must be available.
#endif

#if __has_include(<forge/linalg.hpp>)
#error The removed Forge linalg header must not be available.
#endif

int main() {}
