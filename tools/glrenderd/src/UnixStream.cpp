// ported from pdx203-jellybean by @chardidathing, https://github.com/chardidathing/pdx203-jellybean
// replaces the emulator's UnixStream, which used libcutils and a path under /tmp.
// an abstract socket crosses the chroot with no bind mounts, kitkat's guest libs dial the same name
#include "UnixStream.h"
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

static const char kSocketName[] = "glrenderd";

static socklen_t make_addr(struct sockaddr_un* addr)
{
    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    // leading nul is what makes it abstract
    memcpy(addr->sun_path + 1, kSocketName, sizeof(kSocketName) - 1);
    return offsetof(struct sockaddr_un, sun_path) + 1 + sizeof(kSocketName) - 1;
}

UnixStream::UnixStream(size_t bufSize) : SocketStream(bufSize) {}

UnixStream::UnixStream(int sock, size_t bufSize) : SocketStream(sock, bufSize) {}

int UnixStream::listen(char addrstr[MAX_ADDRSTR_LEN])
{
    struct sockaddr_un addr;
    socklen_t len = make_addr(&addr);
    m_sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_sock < 0)
        return int(ERR_INVALID_SOCKET);
    if (::bind(m_sock, (struct sockaddr*)&addr, len) < 0 || ::listen(m_sock, 16) < 0) {
        ::close(m_sock);
        m_sock = -1;
        return int(ERR_INVALID_SOCKET);
    }
    snprintf(addrstr, MAX_ADDRSTR_LEN, "@%s", kSocketName);
    return 0;
}

SocketStream* UnixStream::accept()
{
    int clientSock;
    do {
        clientSock = ::accept(m_sock, nullptr, nullptr);
    } while (clientSock < 0 && errno == EINTR);
    if (clientSock < 0)
        return nullptr;
    // MAPLE: before any command traffic - the 208 KB kernel default is smaller
    // than one UI frame's command stream (see glrSetSocketBuffers in
    // SocketStream.cpp). Set on the accepting side so the guest can run ahead.
    glrSetSocketBuffers(clientSock);
    return new UnixStream(clientSock, m_bufsize);
}

int UnixStream::connect(const char*)
{
    struct sockaddr_un addr;
    socklen_t len = make_addr(&addr);
    m_sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_sock < 0)
        return -1;
    if (::connect(m_sock, (struct sockaddr*)&addr, len) < 0) {
        ::close(m_sock);
        m_sock = -1;
        return -1;
    }
    return 0;
}
