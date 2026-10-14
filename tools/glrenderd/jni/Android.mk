# ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
# GLR_SDK (kitkat's sdk project) and GLR_GEN (emugen's decoders) come from build.sh
LOCAL_PATH := $(call my-dir)
E := $(GLR_SDK)/emulator/opengl
G := $(GLR_GEN)
H := $(LOCAL_PATH)/..

include $(CLEAR_VARS)
LOCAL_MODULE := glrenderd
LOCAL_SRC_FILES := \
    $(H)/src/glrenderd.cpp \
    $(H)/src/UnixStream.cpp \
    $(H)/src/NativeSubWindow.cpp \
    $(H)/src/TcpStream.cpp \
    $(addprefix $(E)/host/libs/libOpenglRender/, ColorBuffer.cpp EGLDispatch.cpp FBConfig.cpp FrameBuffer.cpp \
        GL2Dispatch.cpp GLDispatch.cpp ReadBuffer.cpp RenderContext.cpp RenderControl.cpp \
        RenderServer.cpp RenderThread.cpp ThreadInfo.cpp WindowSurface.cpp) \
    $(E)/host/libs/GLESv1_dec/GLDecoder.cpp $(G)/GLESv1_dec/gl_dec.cpp $(G)/GLESv1_dec/gl_server_context.cpp \
    $(E)/host/libs/GLESv2_dec/GL2Decoder.cpp $(G)/GLESv2_dec/gl2_dec.cpp $(G)/GLESv2_dec/gl2_server_context.cpp \
    $(G)/renderControl_dec/renderControl_dec.cpp $(G)/renderControl_dec/renderControl_server_context.cpp \
    $(addprefix $(E)/shared/OpenglCodecCommon/, glUtils.cpp SocketStream.cpp TimeUtils.cpp) \
    $(addprefix $(E)/shared/OpenglOsUtils/, osDynLibrary.cpp osProcessUnix.cpp osThreadUnix.cpp)
LOCAL_C_INCLUDES := \
    $(H)/shim \
    $(E)/host/include $(E)/host/include/libOpenglRender $(E)/host/libs/libOpenglRender \
    $(E)/host/libs/GLESv1_dec $(E)/host/libs/GLESv2_dec $(E)/host/libs/renderControl_dec \
    $(G)/GLESv1_dec $(G)/GLESv2_dec $(G)/renderControl_dec \
    $(E)/shared/OpenglCodecCommon $(E)/shared/OpenglOsUtils $(E)/shared
LOCAL_CFLAGS := -include $(H)/shim/prelude.h -DWITH_GLES2 -DEMUGL_BUILD_DEBUG=0 \
    -Wno-error -Wno-unused-parameter -Wno-c++11-narrowing -Wno-format -fpermissive -funwind-tables
LOCAL_CPPFLAGS := -std=gnu++11 -funwind-tables
LOCAL_LDLIBS := -ldl -llog
include $(BUILD_EXECUTABLE)
