/* The guard must have no effect when force-included into a C translation unit. */
#if defined(__cplusplus)
#error "This regression must compile as C"
#endif
#if defined(FORGE_DETAIL_LANGUAGE)
#error "The Forge language guard leaked a helper macro into C"
#endif
int forge_c_fixture(void) { return 0; }
