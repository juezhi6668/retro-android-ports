// ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
// the bits of cutils atomics smartptr uses, on compiler builtins. return the old value like cutils
#pragma once
#include <stdint.h>

static inline int32_t android_atomic_inc(volatile int32_t* addr) { return __sync_fetch_and_add(addr, 1); }
static inline int32_t android_atomic_dec(volatile int32_t* addr) { return __sync_fetch_and_sub(addr, 1); }
static inline int32_t android_atomic_add(int32_t value, volatile int32_t* addr) { return __sync_fetch_and_add(addr, value); }
