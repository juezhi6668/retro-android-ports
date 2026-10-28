#include <assert.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int mock_socket(int domain, int type, int protocol);
static int mock_bind(int fd, const struct sockaddr *addr, socklen_t len);
static int mock_ioctl(int fd, unsigned long request, ...);
static ssize_t mock_sendto(int fd, const void *buf, size_t len, int flags,
                          const struct sockaddr *addr, socklen_t addrlen);
static ssize_t mock_write(int fd, const void *buf, size_t len);
static ssize_t mock_recvfrom(int fd, void *buf, size_t len, int flags,
                             struct sockaddr *addr, socklen_t *addrlen);
#define socket mock_socket
#define bind mock_bind
#define ioctl mock_ioctl
#define sendto mock_sendto
#define recvfrom mock_recvfrom
#define write mock_write
#define main qmxd_main
#include "qmxd.c"
#undef main
#undef socket
#undef bind
#undef ioctl
#undef sendto
#undef recvfrom
#undef write

static int modem_peers[256], peer_count, sent_count, write_mode, write_count;
static int send_error, recv_error;
static uint32_t sent_node, sent_port;
static uint8_t sent_data[4096];
static size_t sent_len;

static int mock_socket(int domain, int type, int protocol)
{
    int pair[2];
    assert(domain == AF_MSM_IPC && type == SOCK_DGRAM && protocol == 0);
    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) == 0);
    assert(peer_count < 256);
    modem_peers[peer_count++] = pair[1];
    return pair[0];
}

static int mock_bind(int fd, const struct sockaddr *addr, socklen_t len)
{
    (void)fd;
    assert(addr->sa_family == AF_MSM_IPC);
    assert(len == sizeof(struct sockaddr_msm_ipc));
    return 0;
}

static int mock_ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    struct server_lookup_args *a;
    (void)fd;
    assert(request == IPC_ROUTER_IOCTL_LOOKUP_SERVER);
    va_start(ap, request);
    a = va_arg(ap, struct server_lookup_args *);
    va_end(ap);
    a->num_entries_found = 0;
    if (a->port_name.service == 9) {
        a->num_entries_found = 1;
        a->srv_info[0].node_id = 0;
        a->srv_info[0].port_id = 0x38;
        a->srv_info[0].instance = 2;
    }
    return 0;
}

static ssize_t mock_sendto(int fd, const void *buf, size_t len, int flags,
                          const struct sockaddr *addr, socklen_t addrlen)
{
    const struct sockaddr_msm_ipc *sa = (const void *)addr;
    assert(fcntl(fd, F_GETFL) & O_NONBLOCK);
    assert(flags & MSG_DONTWAIT);
    if (send_error) { errno = send_error; return -1; }
    assert(addrlen == sizeof *sa && len <= sizeof sent_data);
    sent_node = sa->address.addr.port_addr.node_id;
    sent_port = sa->address.addr.port_addr.port_id;
    sent_len = len;
    memcpy(sent_data, buf, len);
    sent_count++;
    return (ssize_t)len;
}

static ssize_t mock_write(int fd, const void *buf, size_t len)
{
    int step = write_count++;
    if (write_mode == 1 && step == 0) return write(fd, buf, len > 3 ? 3 : len);
    if (write_mode == 1 && step == 1) { errno = EAGAIN; return -1; }
    if (write_mode == 2 && step == 0) { errno = EINTR; return -1; }
    return write(fd, buf, len);
}

static ssize_t mock_recvfrom(int fd, void *buf, size_t len, int flags,
                             struct sockaddr *addr, socklen_t *addrlen)
{
    if (recv_error) { errno = recv_error; return -1; }
    return recvfrom(fd, buf, len, flags, addr, addrlen);
}

static uint32_t u32(const uint8_t *p)
{
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static void put32(uint8_t *p, uint32_t v)
{
    memcpy(p, &v, 4);
}

int main(void)
{
    int pair[2], other[2], i;
    uint8_t hdr[MSG_HDR] = {0}, body[CTL_BODY] = {0}, reply[MAX_FRAME], frame[MAX_FRAME];
    uint8_t request[] = {0, 1, 0, 3, 0, 4, 0, 0x11, 1, 0, 1};
    uint8_t response[] = {2, 1, 0, 3, 0, 7, 0, 2, 4, 0, 0, 0, 0, 0};
    struct client *c = &clients[0];
    signal(SIGPIPE, SIG_IGN);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, other) == 0);
    assert(fcntl(pair[0], F_SETFL, O_NONBLOCK) == 0);
    c->used = 1;
    c->cid = 0x1234;
    c->fd = pair[0];
    clients[1].used = 1;
    clients[1].cid = 0x5678;
    clients[1].fd = other[0];
    put32(hdr, 1);
    put32(hdr + 4, c->cid);
    put32(hdr + 8, 0x108);
    put32(hdr + 20, 7);
    put32(body, 9);
    handle_ctl(c, hdr, body, sizeof body);
    assert(read(pair[1], reply, sizeof reply) == 896);
    assert(u32(reply + 16) == 0x108 && u32(reply + 28) == 7);
    assert(u32(reply + 20) == 0 && reply[40] == 1);
    assert(sc[0].used && sc[0].svc == 9 && sc[0].owner == c->cid);
    assert(sc[0].conn_id == 7 && sc[0].port == 0x38);
    put32(hdr, 0);
    put32(hdr + 24, 9);
    hdr[28] = 1;
    handle_qmi(c, hdr, request, sizeof request);
    assert(sent_count == 1 && sent_node == 0 && sent_port == 0x38);
    assert(sent_len == sizeof request && !memcmp(sent_data, request, sizeof request));
    handle_qmi(&clients[1], hdr, request, sizeof request);
    handle_qmi(c, hdr, request, 3);
    assert(sent_count == 1);
    assert(write(modem_peers[0], response, sizeof response) == sizeof response);
    modem_read(sc[0].mfd);
    assert(read(pair[1], reply, sizeof reply) == 40 + sizeof response);
    assert(u32(reply + 28) == 7 && u32(reply + 32) == 9);
    assert(reply[36] == 1 && reply[37] == 0x80);
    assert(!memcmp(reply + 40, response, sizeof response));
    assert(recv(other[1], reply, sizeof reply, MSG_DONTWAIT) == -1 && errno == EAGAIN);
    recv_error = EAGAIN;
    modem_read(sc[0].mfd);
    assert(!c->failed);
    recv_error = EMSGSIZE;
    modem_read(sc[0].mfd);
    assert(c->failed);
    recv_error = 0;
    c->failed = 0;
    send_error = EAGAIN;
    handle_qmi(c, hdr, request, sizeof request);
    assert(c->failed && sent_count == 1);
    send_error = 0;
    c->failed = 0;
    put32(hdr, 2);
    body[4] = 1;
    handle_ctl(&clients[1], hdr, body, sizeof body);
    assert(read(other[1], reply, sizeof reply) == 896 && sc[0].used);
    assert((int32_t)u32(reply + 20) == -1);
    handle_ctl(c, hdr, body, sizeof body);
    assert(read(pair[1], reply, sizeof reply) == 896 && !sc[0].used);
    handle_ctl(c, hdr, body, 2);
    assert(read(pair[1], reply, sizeof reply) == 896);
    assert((int32_t)u32(reply + 20) == -1);
    put32(hdr, 1);
    put32(body, 99);
    handle_ctl(c, hdr, body, sizeof body);
    assert(read(pair[1], reply, sizeof reply) == 896);
    assert((int32_t)u32(reply + 20) == -1 && !sc[0].used);
    put32(body, 9);
    for (i = 0; i < MAXSC; i++) {
        handle_ctl(c, hdr, body, sizeof body);
        assert(read(pair[1], reply, sizeof reply) == 896);
        assert(u32(reply + 20) == 0 && reply[40] == i + 1 && sc[i].used);
    }
    handle_ctl(c, hdr, body, sizeof body);
    assert(read(pair[1], reply, sizeof reply) == 896 && (int32_t)u32(reply + 20) == -1);
    sc_release_owner(clients[1].cid);
    assert(sc[0].used);
    sc_release_owner(c->cid);
    for (i = 0; i < MAXSC; i++) assert(!sc[i].used);
    put32(hdr, 10);
    handle_ctl(c, hdr, body, sizeof body);
    assert(read(pair[1], reply, sizeof reply) == 896 && (int32_t)u32(reply + 20) == -1);
    write_mode = 1;
    write_count = 0;
    assert(cli_send(c, hdr, body, sizeof body) == 0 && c->outlen == 893);
    assert(read(pair[1], reply, 3) == 3);
    client_flush(c);
    assert(!c->outlen && read(pair[1], reply + 3, sizeof reply - 3) == 893);
    assert(u32(reply) == 896 && !memcmp(reply + 40, body, sizeof body));
    write_mode = 2;
    write_count = 0;
    assert(cli_send(c, hdr, body, sizeof body) == 0 && !c->outlen);
    assert(read(pair[1], reply, sizeof reply) == 896);
    write_mode = 0;
    put32(frame, 896);
    put32(frame + 4, c->cid);
    put32(hdr, 4);
    memcpy(frame + 8, hdr, sizeof hdr);
    memcpy(frame + 40, body, sizeof body);
    assert(write(pair[1], frame, 5) == 5 && client_read(c) == 0 && c->inlen == 5);
    assert(write(pair[1], frame + 5, 891) == 891 && client_read(c) == 0 && !c->inlen);
    assert(read(pair[1], reply, sizeof reply) == 896 && u32(reply + 20) == 0);
    put32(frame + 8, 0x100);
    handle_frame(c, frame, 896);
    assert(read(pair[1], reply, sizeof reply) == 896 && (int32_t)u32(reply + 20) == -1);
    put32(frame + 4, c->cid + 1);
    handle_frame(c, frame, 896);
    assert(c->failed);
    c->failed = 0;
    c->outlen = OUT_CAP;
    assert(cli_send(c, hdr, body, sizeof body) == -1 && c->failed);
    c->outlen = 0;
    c->failed = 0;
    close(pair[1]);
    assert(cli_send(c, hdr, body, sizeof body) == -1 && c->failed);
    for (i = 0; i < peer_count; i++) close(modem_peers[i]);
    close(pair[0]); close(other[0]); close(other[1]);
    puts("PASS: routing, txn 0x108, isolation, 128 clients, framing, partial writes, EINTR/EAGAIN, overflow and disconnect");
    return 0;
}
