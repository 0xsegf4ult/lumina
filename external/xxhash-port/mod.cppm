module;

#define XXH_STATIC_LINKING_ONLY
#define XXH_IMPLEMENTATION
#include <xxhash.h>

export module xxhash;

export namespace xxhash
{
	using ::XXH3_64bits;
}


