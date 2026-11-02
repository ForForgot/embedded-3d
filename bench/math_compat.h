/* The frozen legacy sources call sqrt/sin/cos/tan/fmin/fmax without including
 * <math.h>: GCC <= 13 only warns, GCC 14+ rejects it.  This header is pulled in
 * with -include when building the benchmark so the legacy snapshot can stay
 * byte identical to the original commit.  It is NOT part of the library. */
#include <math.h>

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wimplicit-function-declaration"
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif
