// ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
// no window system, frames leave through the post callback instead
#include "NativeSubWindow.h"

EGLNativeWindowType createSubWindow(FBNativeWindowType, EGLNativeDisplayType* display_out,
        int, int, int, int)
{
    *display_out = EGL_DEFAULT_DISPLAY;
    return (EGLNativeWindowType)0;
}

void destroySubWindow(EGLNativeDisplayType, EGLNativeWindowType) {}
