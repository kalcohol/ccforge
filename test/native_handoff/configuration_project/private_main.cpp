// The implementation library's private Forge dependency must not promote or
// check this executable's language mode. No Forge headers are included here.
#if defined(_MSVC_LANG)
static_assert(_MSVC_LANG == 202002L);
#else
static_assert(__cplusplus == 202002L);
#endif

int fixture_value();
int main() { return fixture_value(); }
