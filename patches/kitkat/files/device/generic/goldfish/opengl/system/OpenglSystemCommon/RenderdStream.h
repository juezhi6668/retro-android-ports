// ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
#ifndef __RENDERD_STREAM_H
#define __RENDERD_STREAM_H

#include "SocketStream.h"

// talks to glrenderd, the adreno renderer running in the lineage chroot,
// over an abstract unix socket instead of the emulator's qemu pipe
class RenderdStream : public SocketStream {
public:
    explicit RenderdStream(size_t bufsize = 10000) : SocketStream(bufsize) {}
    virtual int listen(unsigned short port) { return -1; }
    virtual SocketStream *accept() { return NULL; }
    virtual int connect(unsigned short port);
    int connect() { return connect(0); }

    // both are already virtual on SocketStream, so overriding them does not
    // change the vtable (`flush()`/`alloc()` are inline and the prebuilt encoder
    // libraries hard-code member offsets; the vtable must not move)
    virtual const unsigned char *readFully(void *buf, size_t len);
    virtual int writeFully(const void *buf, size_t len);

    // whether this process should use glrenderd at all, decided once. the egl loader in
    // frameworks/native makes the same call so gralloc and egl never disagree
    static bool available();

private:
    // reconnect the socket in place when the renderer died or went silent. The
    // connection object must survive: the guest EGL layer holds pointers to the
    // encoders built on it, so rebuilding the connection crashes those callers.
    bool reconnectIfDead();
};

#endif
