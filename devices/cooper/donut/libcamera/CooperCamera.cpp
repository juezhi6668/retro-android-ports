/*
 * CooperCamera.cpp — minimal V4L2 camera HAL for cooper (GT-S5830) on Donut.
 *
 * Strategy (safe-first):
 *  - Try /dev/video0 as V4L2 YUYV capture on construction. Any failure
 *    (missing node, no capture caps, fmt/set failure) -> mV4l2Fd = -1 and
 *    the HAL behaves like the AOSP stub (animated test pattern preview,
 *    JPEG-encoded snapshot of the current frame). CameraService and the
 *    Camera app never see an error because of missing hardware.
 *  - setParameters is permissive (any sane preview/picture size, any of the
 *    yuv422-family preview formats the Donut app may request). The stub is
 *    strict (176x144 + 213x350 only) and makes the app's setParameters fail.
 *  - takePicture encodes the current preview frame to JPEG with libjpeg, so
 *    the saved picture matches what preview showed (any size), instead of a
 *    canned blob.
 *  - startRecording returns UNKNOWN_ERROR like the stub (video TODO).
 */
#define LOG_TAG "CooperCamera"
#include <utils/Log.h>

#include "CooperCamera.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/videodev2.h>

extern "C" {
#include "jpeglib.h"
#include "jerror.h"
}

namespace android {

wp<CameraHardwareInterface> CooperCamera::singleton;

CooperCamera::CooperCamera()
    : mPreviewHeap(0),
      mRawHeap(0),
      mPreviewFrameSize(0),
      mCurrentPreviewFrame(0),
      mPreviewRunning(false),
      mPreviewCallback(0),
      mPreviewCallbackCookie(0),
      mAutoFocusCallback(0),
      mAutoFocusCallbackCookie(0),
      mShutterCallback(0),
      mRawPictureCallback(0),
      mJpegPictureCallback(0),
      mPictureCallbackCookie(0),
      mV4l2Fd(-1),
      mFrameCounter(0)
{
    // Best-effort V4L2 open. The cooper 2.6.35 kernel's sensor path is
    // Samsung-specific; if it exposes V4L2 we use it, otherwise fallback.
    int fd = ::open("/dev/video0", O_RDWR);
    if (fd >= 0) {
        struct v4l2_capability cap;
        memset(&cap, 0, sizeof(cap));
        if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0 &&
            (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
            if (cap.capabilities & V4L2_CAP_READWRITE) {
                mV4l2Fd = fd;
                LOGI("CooperCamera: using /dev/video0 (%.32s)", cap.card);
            } else {
                LOGW("CooperCamera: /dev/video0 lacks READWRITE, fallback");
                ::close(fd);
            }
        } else {
            LOGW("CooperCamera: /dev/video0 not a capture device, fallback");
            ::close(fd);
        }
    } else {
        LOGI("CooperCamera: no /dev/video0, test-pattern fallback");
    }
    initDefaultParameters();
}

CooperCamera::~CooperCamera()
{
    stopPreview();
    if (mV4l2Fd >= 0) {
        ::close(mV4l2Fd);
        mV4l2Fd = -1;
    }
    singleton.clear();
}

void CooperCamera::initDefaultParameters()
{
    CameraParameters p;
    // HVGA-ish default; app overrides via setParameters immediately.
    p.setPreviewSize(320, 240);
    p.setPreviewFrameRate(15);
    p.setPreviewFormat("yuv422sp");
    p.setPictureSize(1024, 768);
    p.setPictureFormat("jpeg");
    // setParameters is permissive so this cannot fail, but keep the check.
    if (setParameters(p) != NO_ERROR) {
        LOGE("CooperCamera: default parameters rejected?!");
    }
}

void CooperCamera::initHeapLocked()
{
    int picture_width, picture_height;
    mParameters.getPictureSize(&picture_width, &picture_height);
    if (picture_width <= 0 || picture_height <= 0) {
        picture_width = 1024; picture_height = 768;
    }
    mRawHeap = new MemoryHeapBase(picture_width * 2 * picture_height);

    int preview_width, preview_height;
    mParameters.getPreviewSize(&preview_width, &preview_height);
    if (preview_width <= 0 || preview_height <= 0) {
        preview_width = 320; preview_height = 240;
    }
    LOGD("CooperCamera: preview %dx%d picture %dx%d %s",
         preview_width, preview_height, picture_width, picture_height,
         mV4l2Fd >= 0 ? "v4l2" : "pattern");

    int how_big = preview_width * preview_height * 2;
    if (how_big == mPreviewFrameSize && mPreviewHeap != 0)
        return;
    mPreviewFrameSize = how_big;
    mPreviewHeap = new MemoryHeapBase(mPreviewFrameSize * kBufferCount);
    for (int i = 0; i < kBufferCount; i++) {
        mBuffers[i] = new MemoryBase(mPreviewHeap, i * mPreviewFrameSize,
                                     mPreviewFrameSize);
    }
}

sp<IMemoryHeap> CooperCamera::getPreviewHeap() const
{
    return mPreviewHeap;
}

sp<IMemoryHeap> CooperCamera::getRawHeap() const
{
    return mRawHeap;
}

bool CooperCamera::grabV4l2Frame(uint8_t *dst, int size)
{
    if (mV4l2Fd < 0 || dst == NULL || size <= 0)
        return false;
    int preview_w, preview_h;
    {
        // mLock already held by caller (previewThread); read params directly
        // to avoid re-locking (Mutex is non-recursive in Donut).
        mParameters.getPreviewSize(&preview_w, &preview_h);
    }
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = preview_w;
    fmt.fmt.pix.height = preview_h;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_ANY;
    if (ioctl(mV4l2Fd, VIDIOC_S_FMT, &fmt) < 0)
        return false;
    // Single read() of one interleaved YUYV frame (2 bytes/px).
    int want = preview_w * preview_h * 2;
    if (want != size)
        return false;
    ssize_t got = ::read(mV4l2Fd, dst, want);
    return got == want;
}

void CooperCamera::fillTestPattern(uint8_t *dst, int w, int h)
{
    // Animated checkerboard in YUYV (Y0 U Y1 V per 2px), same layout the
    // stub's FakeCamera emits, so SurfaceFlinger shows the same thing the
    // stub would. Phase shifts with mFrameCounter for motion.
    int phase = mFrameCounter & 63;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x += 2) {
            int c0 = (((x + phase) / 16) + ((y / 16))) & 1;
            int c1 = (((x + 1 + phase) / 16) + ((y / 16))) & 1;
            uint8_t y0 = c0 ? 235 : 16;
            uint8_t y1 = c1 ? 235 : 16;
            uint8_t *p = dst + (y * w + x) * 2;
            p[0] = y0; p[1] = 128; p[2] = y1; p[3] = 128;
        }
    }
}

int CooperCamera::previewThread()
{
    mLock.lock();
    int previewFrameRate = mParameters.getPreviewFrameRate();
    if (previewFrameRate <= 0 || previewFrameRate > 30)
        previewFrameRate = 15;
    int pw, ph;
    mParameters.getPreviewSize(&pw, &ph);
    ssize_t offset = mCurrentPreviewFrame * mPreviewFrameSize;
    sp<MemoryHeapBase> heap = mPreviewHeap;
    sp<MemoryBase> buffer = mBuffers[mCurrentPreviewFrame];
    int v4l2 = mV4l2Fd;
    mLock.unlock();

    if (buffer != 0 && heap != 0) {
        int delay = (int)(1000000.0f / float(previewFrameRate));
        uint8_t *frame = ((uint8_t *)heap->base()) + offset;
        bool real = false;
        if (v4l2 >= 0)
            real = grabV4l2Frame(frame, mPreviewFrameSize);
        if (!real)
            fillTestPattern(frame, pw, ph);
        mFrameCounter++;
        if (mPreviewCallback)
            mPreviewCallback(buffer, mPreviewCallbackCookie);
        mLock.lock();
        mCurrentPreviewFrame = (mCurrentPreviewFrame + 1) % kBufferCount;
        mLock.unlock();
        usleep(delay);
    }
    return NO_ERROR;
}

status_t CooperCamera::startPreview(preview_callback cb, void* user)
{
    Mutex::Autolock lock(mLock);
    if (mPreviewThread != 0)
        return INVALID_OPERATION;
    mPreviewCallback = cb;
    mPreviewCallbackCookie = user;
    mPreviewThread = new PreviewThread(this);
    mPreviewRunning = true;
    return NO_ERROR;
}

void CooperCamera::stopPreview()
{
    sp<PreviewThread> previewThread;
    {
        Mutex::Autolock lock(mLock);
        previewThread = mPreviewThread;
    }
    if (previewThread != 0)
        previewThread->requestExitAndWait();
    Mutex::Autolock lock(mLock);
    mPreviewThread.clear();
    mPreviewRunning = false;
}

bool CooperCamera::previewEnabled()
{
    return mPreviewThread != 0;
}

status_t CooperCamera::startRecording(recording_callback, void*)
{
    // Video path TODO (needs encoder + timed callbacks); photo-only v1.
    return UNKNOWN_ERROR;
}

void CooperCamera::stopRecording()
{
}

bool CooperCamera::recordingEnabled()
{
    return false;
}

void CooperCamera::releaseRecordingFrame(const sp<IMemory>&)
{
}

int CooperCamera::beginAutoFocusThread(void *cookie)
{
    CooperCamera *c = (CooperCamera *)cookie;
    return c->autoFocusThread();
}

int CooperCamera::autoFocusThread()
{
    // No AF hardware path on Donut cooper; report success immediately so the
    // Camera app proceeds to takePicture (same as stub).
    if (mAutoFocusCallback != NULL) {
        mAutoFocusCallback(true, mAutoFocusCallbackCookie);
        mAutoFocusCallback = NULL;
        return NO_ERROR;
    }
    return UNKNOWN_ERROR;
}

status_t CooperCamera::autoFocus(autofocus_callback af_cb, void *user)
{
    Mutex::Autolock lock(mLock);
    if (mAutoFocusCallback != NULL)
        return mAutoFocusCallback == af_cb ? NO_ERROR : INVALID_OPERATION;
    mAutoFocusCallback = af_cb;
    mAutoFocusCallbackCookie = user;
    if (createThread(beginAutoFocusThread, this) == false)
        return UNKNOWN_ERROR;
    return NO_ERROR;
}

int CooperCamera::beginPictureThread(void *cookie)
{
    CooperCamera *c = (CooperCamera *)cookie;
    return c->pictureThread();
}

// YUYV -> RGB24 row conversion for the JPEG encoder.
static void yuyv_to_rgb24(const uint8_t *yuyv, uint8_t *rgb, int w, int h)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x += 2) {
            const uint8_t *p = yuyv + (y * w + x) * 2;
            int y0 = p[0], u = p[1] - 128, y1 = p[2], v = p[3] - 128;
            int r, g, b;
#define CLAMP(v) ((v) < 0 ? 0 : ((v) > 255 ? 255 : (v)))
            r = CLAMP((int)(y0 + 1.402 * v));
            g = CLAMP((int)(y0 - 0.344 * u - 0.714 * v));
            b = CLAMP((int)(y0 + 1.772 * u));
            uint8_t *o = rgb + (y * w + x) * 3;
            o[0] = r; o[1] = g; o[2] = b;
            r = CLAMP((int)(y1 + 1.402 * v));
            g = CLAMP((int)(y1 - 0.344 * u - 0.714 * v));
            b = CLAMP((int)(y1 + 1.772 * u));
            o[3] = r; o[4] = g; o[5] = b;
#undef CLAMP
        }
    }
}

struct MemDest {
    struct jpeg_destination_mgr pub;
    uint8_t *buf;
    size_t bufsize;
    size_t datacount;
};

static void mem_init_dest(j_compress_ptr cinfo)
{
    MemDest *d = (MemDest *)cinfo->dest;
    d->datacount = 0;
}

static boolean mem_empty_output(j_compress_ptr cinfo)
{
    MemDest *d = (MemDest *)cinfo->dest;
    // Grow buffer exponentially.
    size_t nsize = d->bufsize ? d->bufsize * 2 : 65536;
    uint8_t *nbuf = (uint8_t *)realloc(d->buf, nsize);
    if (!nbuf)
        return FALSE;
    d->buf = nbuf;
    d->pub.next_output_byte = nbuf + d->datacount;
    d->pub.free_in_buffer = nsize - d->datacount;
    d->bufsize = nsize;
    return TRUE;
}

static void mem_term_dest(j_compress_ptr cinfo)
{
    MemDest *d = (MemDest *)cinfo->dest;
    d->datacount = d->bufsize - d->pub.free_in_buffer;
}

bool CooperCamera::encodeJpeg(const uint8_t *yuyv, int w, int h, int quality,
                              uint8_t **out, size_t *outSize)
{
    if (!yuyv || w <= 0 || h <= 0 || !out || !outSize)
        return false;
    uint8_t *rgb = (uint8_t *)malloc(w * h * 3);
    if (!rgb)
        return false;
    yuyv_to_rgb24(yuyv, rgb, w, h);

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    MemDest dest;
    memset(&dest, 0, sizeof(dest));
    dest.bufsize = w * h;  // initial guess
    dest.buf = (uint8_t *)malloc(dest.bufsize);
    if (!dest.buf) {
        free(rgb);
        jpeg_destroy_compress(&cinfo);
        return false;
    }
    dest.pub.init_destination = mem_init_dest;
    dest.pub.empty_output_buffer = mem_empty_output;
    dest.pub.term_destination = mem_term_dest;
    dest.pub.next_output_byte = dest.buf;
    dest.pub.free_in_buffer = dest.bufsize;
    cinfo.dest = (struct jpeg_destination_mgr *)&dest;

    cinfo.image_width = w;
    cinfo.image_height = h;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality <= 0 ? 75 : quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);
    JSAMPROW row[1];
    while (cinfo.next_scanline < cinfo.image_height) {
        row[0] = rgb + cinfo.next_scanline * w * 3;
        jpeg_write_scanlines(&cinfo, row, 1);
    }
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    free(rgb);
    *out = dest.buf;
    *outSize = dest.datacount;
    return dest.datacount > 0;
}

int CooperCamera::pictureThread()
{
    if (mShutterCallback)
        mShutterCallback(mPictureCallbackCookie);

    // Snapshot the latest preview buffer for raw + jpeg.
    int pw, ph;
    uint8_t *snap = NULL;
    {
        Mutex::Autolock lock(mLock);
        mParameters.getPreviewSize(&pw, &ph);
        int sz = pw * ph * 2;
        snap = (uint8_t *)malloc(sz);
        if (snap && mPreviewHeap != 0) {
            ssize_t off = ((mCurrentPreviewFrame + kBufferCount - 1)
                           % kBufferCount) * mPreviewFrameSize;
            if (off + sz <= (ssize_t)(mPreviewFrameSize * kBufferCount))
                memcpy(snap, ((uint8_t *)mPreviewHeap->base()) + off, sz);
            else {
                free(snap);
                snap = NULL;
            }
        } else if (snap) {
            free(snap);
            snap = NULL;
        }
    }
    // Fallback pattern if no preview ran yet.
    if (!snap) {
        Mutex::Autolock lock(mLock);
        mParameters.getPreviewSize(&pw, &ph);
        snap = (uint8_t *)malloc(pw * ph * 2);
        if (snap)
            fillTestPattern(snap, pw, ph);
    }

    if (mRawPictureCallback && snap) {
        int rawSize = pw * ph * 2;
        sp<MemoryHeapBase> heap = new MemoryHeapBase(rawSize);
        memcpy(heap->base(), snap, rawSize);
        sp<MemoryBase> mem = new MemoryBase(heap, 0, rawSize);
        mRawPictureCallback(mem, mPictureCallbackCookie);
    }

    if (mJpegPictureCallback && snap) {
        uint8_t *jpg = NULL;
        size_t jpgSize = 0;
        if (encodeJpeg(snap, pw, ph, 75, &jpg, &jpgSize) && jpg) {
            sp<MemoryHeapBase> heap = new MemoryHeapBase(jpgSize);
            memcpy(heap->base(), jpg, jpgSize);
            sp<MemoryBase> mem = new MemoryBase(heap, 0, jpgSize);
            mJpegPictureCallback(mem, mPictureCallbackCookie);
            free(jpg);
        } else {
            LOGE("CooperCamera: jpeg encode failed");
        }
    }
    if (snap)
        free(snap);
    return NO_ERROR;
}

status_t CooperCamera::takePicture(shutter_callback shutter_cb,
                                   raw_callback raw_cb,
                                   jpeg_callback jpeg_cb,
                                   void* user)
{
    stopPreview();
    mShutterCallback = shutter_cb;
    mRawPictureCallback = raw_cb;
    mJpegPictureCallback = jpeg_cb;
    mPictureCallbackCookie = user;
    if (createThread(beginPictureThread, this) == false)
        return UNKNOWN_ERROR;
    return NO_ERROR;
}

status_t CooperCamera::cancelPicture(bool cancel_shutter,
                                     bool cancel_raw,
                                     bool cancel_jpeg)
{
    if (cancel_shutter) mShutterCallback = NULL;
    if (cancel_raw) mRawPictureCallback = NULL;
    if (cancel_jpeg) mJpegPictureCallback = NULL;
    return NO_ERROR;
}

status_t CooperCamera::dump(int fd, const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    result.append("CooperCamera (cooper V4L2 w/ pattern fallback)\n");
    mParameters.dump(fd, args);
    snprintf(buffer, SIZE, " preview frame(%d), size(%d), v4l2(%s)\n",
             mCurrentPreviewFrame, mPreviewFrameSize,
             mV4l2Fd >= 0 ? "open" : "fallback");
    result.append(buffer);
    write(fd, result.string(), result.size());
    return NO_ERROR;
}

status_t CooperCamera::setParameters(const CameraParameters& params)
{
    Mutex::Autolock lock(mLock);

    // Permissive: clamp to sane ranges, normalize format. Never fail the
    // app for asking a size the sensor will scale to anyway.
    int pw, ph, fw, fh;
    params.getPreviewSize(&pw, &ph);
    params.getPictureSize(&fw, &fh);
    if (pw <= 0 || ph <= 0) { pw = 320; ph = 240; }
    if (fw <= 0 || fh <= 0) { fw = 1024; fh = 768; }
    if (pw > 640) { ph = ph * 640 / pw; pw = 640; }
    if (fw > 2048 || fh > 1536) { fw = 2048; fh = 1536; }

    int fps = params.getPreviewFrameRate();
    if (fps <= 0 || fps > 30)
        fps = 15;

    const char *pfmt = params.getPreviewFormat();
    const char *useFmt = "yuv422sp";
    if (pfmt && (!strcmp(pfmt, "yuv422sp") || !strcmp(pfmt, "yuv422") ||
                 !strcmp(pfmt, "yuv420sp") || !strcmp(pfmt, "rgb565")))
        useFmt = pfmt;

    const char *jfmt = params.getPictureFormat();
    const char *useJ = "jpeg";
    if (jfmt && !strcmp(jfmt, "yuv422"))
        useJ = jfmt;  // raw path; jpeg path still encodes jpeg

    mParameters.setPreviewSize(pw, ph);
    mParameters.setPreviewFrameRate(fps);
    mParameters.setPreviewFormat(useFmt);
    mParameters.setPictureSize(fw, fh);
    mParameters.setPictureFormat(useJ);

    initHeapLocked();
    return NO_ERROR;
}

CameraParameters CooperCamera::getParameters() const
{
    Mutex::Autolock lock(mLock);
    return mParameters;
}

void CooperCamera::release()
{
}

sp<CameraHardwareInterface> CooperCamera::createInstance()
{
    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0)
            return hardware;
    }
    sp<CameraHardwareInterface> hardware(new CooperCamera());
    singleton = hardware;
    return hardware;
}

extern "C" sp<CameraHardwareInterface> openCameraHardware()
{
    return CooperCamera::createInstance();
}

}; // namespace android
