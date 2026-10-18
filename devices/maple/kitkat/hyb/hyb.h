/* libhyb: loads newer-Android libraries (ARM 32-bit) into a KitKat process.
 * On maple the newer side is the LineageOS 22.1 (Android 15) vendor closure staged under
 * HYB_ROOT: the real Adreno 540 driver, the QTI gralloc1, and their dependencies.
 * Their libc/libm/libdl/liblog/libcutils/libz imports resolve to KitKat's libraries, completed
 * by hooks (hooks.c); everything else is loaded by libhyb itself from the root. */
#ifndef HYB_H
#define HYB_H
#ifdef __cplusplus
extern "C" {
#endif

#ifndef HYB_ROOT
/* root of the newer-Android files: <root>/vendor/lib, <root>/system/lib */
#define HYB_ROOT "/system/a15"
#endif

void hyb_set_root(const char *root);
void *hyb_dlopen(const char *name, int flags);
void *hyb_dlsym(void *handle, const char *symbol);
int hyb_dlclose(void *handle);
const char *hyb_dlerror(void);

#ifdef __cplusplus
}
#endif
#endif
