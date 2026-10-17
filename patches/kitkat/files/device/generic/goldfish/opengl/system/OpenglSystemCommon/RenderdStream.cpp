// ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
#include "RenderdStream.h"
#include <cutils/log.h>
#include <cutils/sockets.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static const char kSocketName[] = "glrenderd";
// shipped only when the chroot was staged, without it there's nothing to wait for
static const char kRenderdBinary[] = "/system/glr/root/glrenderd";
// glrenderd starts in post-fs-data, surfaceflinger can still beat it to the
// socket. 300 tries = the same 30 s window the egl loader waits; maple cold
// boots have needed the entire window (poplar uses 50).
static const int kWaitTries = 300;
static const useconds_t kWaitStepUs = 100000;

int RenderdStream::connect(unsigned short port)
{
    m_sock = socket_local_client(kSocketName, ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
    if (valid()) {
        // POPLAR: never wait forever for a reply - see SocketStream.cpp
        glrGuestSetReplyTimeout(m_sock);
        glrSetSocketBuffers(m_sock);
        return 0;
    }
    return -1;
}

// Reconnect the socket in place. The stream object - and therefore every
// encoder built on it - stays alive; only m_sock is replaced. Freeing the
// connection instead (the first version of this fix) left the guest EGL layer
// with dangling encoder pointers and crashed the app in setActiveTextureUnit.
bool RenderdStream::reconnectIfDead()
{
    if (valid()) {
        return true;
    }

    // the caller may be in a hot loop; try at most once a second
    struct timeval now;
    gettimeofday(&now, NULL);
    static __thread long long s_lastTryMs;
    long long nowMs = (long long)now.tv_sec * 1000 + now.tv_usec / 1000;
    if (s_lastTryMs && nowMs - s_lastTryMs < 1000) {
        return false;
    }
    s_lastTryMs = nowMs;

    int fd = socket_local_client(kSocketName, ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
    if (fd < 0) {
        return false;
    }

    m_sock = fd;
    glrGuestSetReplyTimeout(m_sock);
    glrSetSocketBuffers(m_sock);

    // MAPLE: the renderer reads one handshake word per connection before it
    // starts decoding (RenderServer::Main). The first connection gets it from
    // HostConnection::get(); a reconnect must send it here, or the renderer
    // would read the first GL command's bytes as clientFlags and fail the
    // connection - or exit, if the EXIT bit pattern happened to match.
    unsigned int clientFlags = 0;
    if (::send(m_sock, &clientFlags, sizeof(clientFlags), MSG_NOSIGNAL)
            != (ssize_t)sizeof(clientFlags)) {
        ::close(m_sock);
        m_sock = -1;
        return false;
    }

    glrGuestStreamRevive();
    ALOGE("RenderdStream: reconnected to the renderer\n");
    return true;
}

const unsigned char *RenderdStream::readFully(void *buf, size_t len)
{
    if (!reconnectIfDead()) {
        return NULL;
    }
    return SocketStream::readFully(buf, len);
}

int RenderdStream::writeFully(const void *buf, size_t len)
{
    if (!reconnectIfDead()) {
        // drop it, report success (see SocketStream::writeFully)
        return 0;
    }
    return SocketStream::writeFully(buf, len);
}

static bool s_available;
static pthread_once_t s_availableOnce = PTHREAD_ONCE_INIT;

static void probe()
{
    if (access(kRenderdBinary, F_OK))
        return;
    for (int i = 0; i < kWaitTries; i++) {
        int fd = socket_local_client(kSocketName, ANDROID_SOCKET_NAMESPACE_ABSTRACT, SOCK_STREAM);
        if (fd >= 0) {
            close(fd);
            s_available = true;
            return;
        }
        usleep(kWaitStepUs);
    }
}

bool RenderdStream::available()
{
    pthread_once(&s_availableOnce, probe);
    return s_available;
}
