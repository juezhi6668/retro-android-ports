// ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
// force-included everywhere. the 2012 renderer thinks linux means x11, we have no windows at all
#pragma once
#define _RENDER_API_PLATFORM_TYPES_H
typedef void* FBNativeWindowType;
#define EMUGL_LIBNAME(name) "lib" name ".so"

#ifdef __cplusplus
#include <string.h>
// alignment-safe read for the generated decoders, see unalign.py
template <typename T> static inline T Unpack(const void* p)
{
    T v;
    memcpy(&v, p, sizeof(T));
    return v;
}
#endif
