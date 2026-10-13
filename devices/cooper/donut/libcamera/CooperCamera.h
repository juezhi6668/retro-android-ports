/*
 * CooperCamera.h — minimal V4L2 camera HAL for Samsung Galaxy S5830 on Donut.
 * Mirrors the AOSP CameraHardwareStub interface so CameraService links
 * unchanged; adds a best-effort /dev/video0 (YUYV) capture path with
 * fallback to a generated test pattern. Permissive setParameters: accepts
 * any reasonable preview/picture size so the Donut Camera app never gets
 * -EINVAL (the stub rejects everything except 213x350).
 */
#ifndef ANDROID_HARDWARE_COOPER_CAMERA_H
#define ANDROID_HARDWARE_COOPER_CAMERA_H

#include <ui/CameraHardwareInterface.h>
#include <utils/MemoryBase.h>
#include <utils/MemoryHeapBase.h>
#include <utils/threads.h>
#include <utils/Vector.h>

namespace android {

class CooperCamera : public CameraHardwareInterface {
public:
    virtual sp<IMemoryHeap> getPreviewHeap() const;
    virtual sp<IMemoryHeap> getRawHeap() const;

    virtual status_t    startPreview(preview_callback cb, void* user);
    virtual void        stopPreview();
    virtual bool        previewEnabled();

    virtual status_t    startRecording(recording_callback cb, void* user);
    virtual void        stopRecording();
    virtual bool        recordingEnabled();
    virtual void        releaseRecordingFrame(const sp<IMemory>& mem);

    virtual status_t    autoFocus(autofocus_callback, void *user);
    virtual status_t    takePicture(shutter_callback,
                                    raw_callback,
                                    jpeg_callback,
                                    void* user);
    virtual status_t    cancelPicture(bool cancel_shutter,
                                      bool cancel_raw,
                                      bool cancel_jpeg);
    virtual status_t    dump(int fd, const Vector<String16>& args) const;
    virtual status_t    setParameters(const CameraParameters& params);
    virtual CameraParameters  getParameters() const;
    virtual void release();

    static sp<CameraHardwareInterface> createInstance();

private:
                        CooperCamera();
    virtual             ~CooperCamera();
    void                initDefaultParameters();
    void                initHeapLocked();
    int                 previewThread();
    int                 autoFocusThread();
    int                 pictureThread();
    static int          beginAutoFocusThread(void *cookie);
    static int          beginPictureThread(void *cookie);
    bool                grabV4l2Frame(uint8_t *dst, int size);
    void                fillTestPattern(uint8_t *dst, int w, int h);
    bool                encodeJpeg(const uint8_t *yuyv, int w, int h,
                                   int quality,
                                   uint8_t **out, size_t *outSize);

    static wp<CameraHardwareInterface> singleton;
    static const int kBufferCount = 4;

    class PreviewThread : public Thread {
        CooperCamera* mHardware;
    public:
        PreviewThread(CooperCamera* hw)
            : Thread(false), mHardware(hw) { }
        virtual void onFirstRef() {
            run("CooperPreviewThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            mHardware->previewThread();
            return true;
        }
    };

    mutable Mutex       mLock;
    CameraParameters    mParameters;
    sp<MemoryHeapBase>  mPreviewHeap;
    sp<MemoryHeapBase>  mRawHeap;
    sp<MemoryBase>      mBuffers[kBufferCount];
    int                 mPreviewFrameSize;
    int                 mCurrentPreviewFrame;
    bool                mPreviewRunning;

    preview_callback    mPreviewCallback;
    void*               mPreviewCallbackCookie;
    sp<PreviewThread>   mPreviewThread;

    autofocus_callback  mAutoFocusCallback;
    void*               mAutoFocusCallbackCookie;

    shutter_callback    mShutterCallback;
    raw_callback        mRawPictureCallback;
    jpeg_callback       mJpegPictureCallback;
    void*               mPictureCallbackCookie;

    int                 mV4l2Fd;
    int                 mFrameCounter;
};

}; // namespace android

#endif
