// No Forge include is needed: the language contract travels with the target.
#ifdef FORGE_DETAIL_LANGUAGE
#error "The Forge language guard leaked a helper macro"
#endif
int main() { return 0; }
