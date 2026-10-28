/* QMUX header: u32 type/link/txn/sys_err/qmi_err/conn/service, u8 client/flags, u16 padding. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <time.h>
#ifdef ANDROID_INIT
#include <cutils/properties.h>
#endif
#ifdef HAVE_ANDROID_LOG
#include <android/log.h>
#define LOG(...) __android_log_print(ANDROID_LOG_ERROR, "QMXD", __VA_ARGS__)
#else
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#endif

#ifndef AF_MSM_IPC
#define AF_MSM_IPC 27
#endif
#define MSM_IPC_ADDR_NAME 1
#define MSM_IPC_ADDR_ID 2

struct msm_ipc_port_addr { uint32_t node_id, port_id; };
struct msm_ipc_port_name { uint32_t service, instance; };
struct msm_ipc_addr {
    unsigned char addrtype;
    union { struct msm_ipc_port_addr port_addr; struct msm_ipc_port_name port_name; } addr;
};
struct sockaddr_msm_ipc {
    unsigned short family;
    struct msm_ipc_addr address;
    unsigned char reserved;
};
struct msm_ipc_server_info { uint32_t node_id, port_id, service, instance; };
struct server_lookup_args {
    struct msm_ipc_port_name port_name;
    int num_entries_in_array;
    int num_entries_found;
    uint32_t lookup_mask;
    struct msm_ipc_server_info srv_info[16];
};
#define IPC_ROUTER_IOCTL_LOOKUP_SERVER _IOWR(0xC3, 2, struct sockaddr_msm_ipc)
#define QDIR "/dev/socket/qmux_radio"
#define CONN_PATH QDIR "/qmux_connect_socket"
#define MSG_HDR 32
#define CTL_BODY 856
#define MAX_FRAME (8 + MSG_HDR + 4096)
#define MODEM_NODE 0
#define MAXC 8
#define MAXSC 128
#define OUT_CAP (16 * MAX_FRAME)

static int verbose;

static void hexdump(const char *tag, const uint8_t *p, int n)
{
    int i;
    if (!verbose) return;
    fprintf(stderr, "%s (%d):", tag, n);
    for (i = 0; i < n; i++) fprintf(stderr, " %02x", p[i]);
    fprintf(stderr, "\n");
}

static int svc_lookup(int fd, uint32_t svc, uint32_t *node, uint32_t *port, uint32_t *inst)
{
    struct server_lookup_args a;
    int i;
    memset(&a, 0, sizeof a);
    a.port_name.service = svc;
    a.num_entries_in_array = 16;
    if (ioctl(fd, IPC_ROUTER_IOCTL_LOOKUP_SERVER, &a) < 0) return -1;
    for (i = 0; i < a.num_entries_found && i < 16; i++) {
        if (a.srv_info[i].node_id != MODEM_NODE) continue;
        *node = a.srv_info[i].node_id;
        *port = a.srv_info[i].port_id;
        *inst = a.srv_info[i].instance;
        return 0;
    }
    return -1;
}

static int modem_sock(uint32_t instance)
{
    int fd = socket(AF_MSM_IPC, SOCK_DGRAM, 0);
    struct sockaddr_msm_ipc sa;
    if (fd < 0) { perror("AF_MSM_IPC socket"); return -1; }
    if (fd >= FD_SETSIZE || fcntl(fd, F_SETFL, O_NONBLOCK) < 0) { close(fd); return -1; }
    memset(&sa, 0, sizeof sa);
    sa.family = AF_MSM_IPC;
    sa.address.addrtype = MSM_IPC_ADDR_NAME;
    sa.address.addr.port_name.service = 0x200 + instance;
    sa.address.addr.port_name.instance = getpid() + instance;
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        perror("msm_ipc bind");
        close(fd);
        return -1;
    }
    return fd;
}

static int modem_send(int fd, uint32_t node, uint32_t port, const uint8_t *b, int n)
{
    struct sockaddr_msm_ipc sa;
    memset(&sa, 0, sizeof sa);
    sa.family = AF_MSM_IPC;
    sa.address.addrtype = MSM_IPC_ADDR_ID;
    sa.address.addr.port_addr.node_id = node;
    sa.address.addr.port_addr.port_id = port;
    if (sendto(fd, b, n, MSG_DONTWAIT, (struct sockaddr *)&sa, sizeof sa) != n) {
        fprintf(stderr, "sendto node=%u port=0x%x: %s\n", node, port, strerror(errno));
        return -1;
    }
    return 0;
}

struct service_client {
    int used;
    uint8_t svc;
    uint8_t client_id;
    int mfd;
    uint32_t owner;
    uint32_t conn_id;
    uint32_t inst;
    uint32_t node;
    uint32_t port;
    uint32_t svc_inst;
};

struct client {
    int fd;
    uint32_t cid;
    int used;
    uint8_t in[MAX_FRAME];
    int inlen;
    uint8_t out[OUT_CAP];
    size_t outlen;
    int failed;
};

static struct client clients[MAXC];
static struct service_client sc[MAXSC];
static uint32_t nsc_inst = 1;

static struct client *client_by_cid(uint32_t cid)
{
    int i;
    for (i = 0; i < MAXC; i++)
        if (clients[i].used && clients[i].cid == cid) return &clients[i];
    return NULL;
}

static struct service_client *sc_find(uint32_t owner, uint32_t conn_id,
                                      uint32_t svc, uint8_t cid)
{
    int i;
    for (i = 0; i < MAXSC; i++)
        if (sc[i].used && sc[i].owner == owner && sc[i].conn_id == conn_id &&
            sc[i].svc == svc && sc[i].client_id == cid) return &sc[i];
    return NULL;
}

static void sc_release_owner(uint32_t owner)
{
    int i;
    for (i = 0; i < MAXSC; i++)
        if (sc[i].used && sc[i].owner == owner) {
            close(sc[i].mfd);
            sc[i].used = 0;
        }
}

static struct service_client *sc_by_fd(int fd)
{
    int i;
    for (i = 0; i < MAXSC; i++)
        if (sc[i].used && sc[i].mfd == fd) return &sc[i];
    return NULL;
}

static void client_flush(struct client *c)
{
    while (c->outlen && !c->failed) {
        ssize_t n = write(c->fd, c->out, c->outlen);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (n <= 0) { c->failed = 1; return; }
        c->outlen -= n;
        memmove(c->out, c->out + n, c->outlen);
    }
}

static int cli_send(struct client *c, const uint8_t *hdr, const uint8_t *body, int blen)
{
    uint32_t total = 8 + MSG_HDR + blen;
    uint8_t *buf;
    if (c->failed) return -1;
    if (blen < 0 || blen > 4096 || c->outlen + total > sizeof c->out) {
        c->failed = 1;
        return -1;
    }
    buf = c->out + c->outlen;
    memcpy(buf, &total, 4);
    memcpy(buf + 4, &c->cid, 4);
    memcpy(buf + 8, hdr, MSG_HDR);
    if (blen) memcpy(buf + 8 + MSG_HDR, body, blen);
    hexdump("TX", buf, total);
    c->outlen += total;
    client_flush(c);
    return c->failed ? -1 : 0;
}

static int cli_send_ctl(struct client *c, uint32_t type, uint32_t txn,
                        uint32_t conn_id, int32_t result,
                        const uint8_t *body, int blen)
{
    uint8_t hdr[MSG_HDR];
    memset(hdr, 0, sizeof hdr);
    memcpy(hdr, &type, 4);
    memcpy(hdr + 4, &c->cid, 4);
    memcpy(hdr + 8, &txn, 4);
    memcpy(hdr + 12, &result, 4);
    memcpy(hdr + 20, &conn_id, 4);
    return cli_send(c, hdr, body, blen);
}

static void handle_ctl(struct client *c, const uint8_t *hdr, const uint8_t *body, int blen)
{
    uint32_t type, txn, conn_id, service;
    uint8_t reply[CTL_BODY];
    struct service_client *s;
    int i;
    memcpy(&type, hdr, 4);
    memcpy(&txn, hdr + 8, 4);
    memcpy(&conn_id, hdr + 20, 4);
    memset(reply, 0, sizeof reply);
    if (verbose) fprintf(stderr, "CTL type=%u txn=%u conn=%u blen=%d\n", type, txn, conn_id, blen);
    switch (type) {
    case 1:
        if (blen < 4) goto ctl_error;
        memcpy(&service, body, 4);
        if (service > 255) goto ctl_error;
        for (i = 0; i < MAXSC && sc[i].used; i++) ;
        if (i == MAXSC) goto ctl_error;
        s = &sc[i];
        memset(s, 0, sizeof *s);
        s->owner = c->cid;
        s->conn_id = conn_id;
        s->svc = (uint8_t)service;
        s->client_id = (uint8_t)(i + 1);
        s->inst = nsc_inst++;
        s->mfd = modem_sock(s->inst);
        if (s->mfd < 0) goto ctl_error;
        if (svc_lookup(s->mfd, s->svc, &s->node, &s->port, &s->svc_inst) < 0) {
            LOG("alloc svc=%u: service not registered\n", service);
            close(s->mfd);
            goto ctl_error;
        }
        s->used = 1;
        reply[0] = s->client_id;
        LOG("alloc svc=%u -> client_id=%u node=%u port=0x%x inst=0x%x\n",
            service, s->client_id, s->node, s->port, s->svc_inst);
        break;
    case 2:
        if (blen < 5) goto ctl_error;
        memcpy(&service, body, 4);
        s = sc_find(c->cid, conn_id, service, body[4]);
        if (!s) goto ctl_error;
        close(s->mfd);
        s->used = 0;
        break;
    case 4:
        break;
    case 5:
        sc_release_owner(c->cid);
        break;
    default:
        LOG("unsupported ctl type %u\n", type);
        goto ctl_error;
    }
    cli_send_ctl(c, type, txn, conn_id, 0, reply, CTL_BODY);
    return;
ctl_error:
    cli_send_ctl(c, type, txn, conn_id, -1, reply, CTL_BODY);
}

static void handle_qmi(struct client *c, const uint8_t *hdr, const uint8_t *body, int blen)
{
    uint8_t cid = hdr[28];
    uint32_t conn_id, svc;
    struct service_client *s;
    memcpy(&conn_id, hdr + 20, 4);
    memcpy(&svc, hdr + 24, 4);
    s = sc_find(c->cid, conn_id, svc, cid);
    if (!s) {
        LOG("QMI for unknown client id %u (svc %u)\n", cid, svc);
        return;
    }
    if (blen < 7 || blen != 7 + body[5] + ((int)body[6] << 8)) {
        LOG("invalid QMI length %d\n", blen);
        return;
    }
    if (verbose) fprintf(stderr, "QMI req svc=%u cid=%u len=%d\n", svc, cid, blen);
    hexdump("QMI req", body, blen);
    if (modem_send(s->mfd, s->node, s->port, body, blen) < 0) c->failed = 1;
}

static void handle_frame(struct client *c, const uint8_t *frame, int len)
{
    const uint8_t *hdr, *body;
    uint32_t type, link, header_link;
    if (len < 8 + MSG_HDR) { c->failed = 1; return; }
    hdr = frame + 8;
    body = hdr + MSG_HDR;
    memcpy(&link, frame + 4, 4);
    memcpy(&header_link, hdr + 4, 4);
    if (link != c->cid || header_link != c->cid) { c->failed = 1; return; }
    memcpy(&type, hdr, 4);
    if (type == 0) handle_qmi(c, hdr, body, len - 8 - MSG_HDR);
    else handle_ctl(c, hdr, body, len - 8 - MSG_HDR);
}

static int client_read(struct client *c)
{
    while (!c->failed) {
        uint32_t total = 8;
        ssize_t n;
        if (c->inlen >= 8) {
            memcpy(&total, c->in, 4);
            if (total < 8 + MSG_HDR || total > MAX_FRAME) return -1;
            if (c->inlen >= (int)total) {
                handle_frame(c, c->in, total);
                c->inlen -= total;
                memmove(c->in, c->in + total, c->inlen);
                continue;
            }
        }
        n = read(c->fd, c->in + c->inlen, sizeof c->in - c->inlen);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
        if (n == 0) return -1;
        c->inlen += n;
    }
    return -1;
}

static void modem_read(int fd)
{
    uint8_t buf[4096], hdr[MSG_HDR];
    struct service_client *s = sc_by_fd(fd);
    struct sockaddr_msm_ipc sa;
    socklen_t sl = sizeof sa;
    int n;
    struct client *c;
    uint32_t svc;
    if (!s) return;
    c = client_by_cid(s->owner);
    if (!c) return;
    memset(&sa, 0, sizeof sa);
    n = recvfrom(fd, buf, sizeof buf, MSG_DONTWAIT, (struct sockaddr *)&sa, &sl);
    if (n < 0) {
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("modem recvfrom");
            c->failed = 1;
        }
        return;
    }
    if (n < 7 || n != 7 + buf[5] + ((int)buf[6] << 8)) return;
    if (verbose) fprintf(stderr, "MODEM RX svc=%u cid=%u\n", s->svc, s->client_id);
    hexdump("MODEM RX", buf, n);
    svc = s->svc;
    memset(hdr, 0, sizeof hdr);
    memcpy(hdr + 4, &c->cid, 4);
    memcpy(hdr + 20, &s->conn_id, 4);
    memcpy(hdr + 24, &svc, 4);
    hdr[28] = s->client_id;
    hdr[29] = 0x80;
    cli_send(c, hdr, buf, n);
}

int main(int argc, char **argv)
{
    int lfd, i, maxfd;
    struct sockaddr_un sa;
    uint32_t next_cid = 1;
    setvbuf(stderr, NULL, _IONBF, 0);
    for (i = 1; i < argc; i++)
        if (!strcmp(argv[i], "-v")) verbose = 1;
    signal(SIGPIPE, SIG_IGN);
    mkdir(QDIR, 0770);
    if (chown(QDIR, 0, 1001) < 0 || chmod(QDIR, 0770) < 0) {
        LOG("qmx dir %s: errno=%d\n", QDIR, errno);
        perror(QDIR);
        return 1;
    }
    lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lfd < 0) { LOG("socket: errno=%d\n", errno); perror("socket"); return 1; }
    memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, CONN_PATH, sizeof sa.sun_path - 1);
    unlink(CONN_PATH);
    if (bind(lfd, (struct sockaddr *)&sa, sizeof sa) < 0 ||
        chown(CONN_PATH, 0, 1001) < 0 || chmod(CONN_PATH, 0660) < 0 ||
        listen(lfd, MAXC) < 0) {
        LOG("bind %s: errno=%d\n", CONN_PATH, errno);
        perror(CONN_PATH);
        return 1;
    }
    fprintf(stderr, "qmxd listening on %s\n", CONN_PATH);
    LOG("listening on %s\n", CONN_PATH);
#ifdef ANDROID_INIT
    if (property_set("sys.msm8998.qmux.ready", "1") < 0) {
        LOG("property_set ready failed\n");
        return 1;
    }
#endif
    for (;;) {
        fd_set rfds, wfds;
        FD_ZERO(&rfds);
        FD_ZERO(&wfds);
        FD_SET(lfd, &rfds);
        maxfd = lfd;
        for (i = 0; i < MAXC; i++) {
            if (!clients[i].used) continue;
            FD_SET(clients[i].fd, &rfds);
            if (clients[i].outlen) FD_SET(clients[i].fd, &wfds);
            if (clients[i].fd > maxfd) maxfd = clients[i].fd;
        }
        for (i = 0; i < MAXSC; i++) {
            if (!sc[i].used) continue;
            FD_SET(sc[i].mfd, &rfds);
            if (sc[i].mfd > maxfd) maxfd = sc[i].mfd;
        }
        if (select(maxfd + 1, &rfds, &wfds, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            perror("select");
            return 1;
        }
        /* Consume old modem fds before client commands can release/reuse them. */
        for (i = 0; i < MAXSC; i++)
            if (sc[i].used && FD_ISSET(sc[i].mfd, &rfds)) modem_read(sc[i].mfd);
        for (i = 0; i < MAXC; i++) {
            struct client *c = &clients[i];
            if (!c->used) continue;
            if (FD_ISSET(c->fd, &wfds)) client_flush(c);
            if (!c->failed && FD_ISSET(c->fd, &rfds) && client_read(c) < 0) c->failed = 1;
            if (c->failed) {
                LOG("client fd=%d cid=%u closed\n", c->fd, c->cid);
                fprintf(stderr, "client fd=%d closed\n", c->fd);
                sc_release_owner(c->cid);
                close(c->fd);
                c->used = 0;
            }
        }
        if (FD_ISSET(lfd, &rfds)) {
            int fd = accept(lfd, NULL, NULL), slot;
            if (fd < 0) continue;
            for (slot = 0; slot < MAXC && clients[slot].used; slot++) ;
            if (slot == MAXC || fd >= FD_SETSIZE ||
                fcntl(fd, F_SETFL, O_NONBLOCK) < 0) { close(fd); continue; }
            memset(&clients[slot], 0, sizeof clients[slot]);
            clients[slot].used = 1;
            clients[slot].fd = fd;
            clients[slot].cid = next_cid++;
            memcpy(clients[slot].out, &clients[slot].cid, 4);
            clients[slot].outlen = 4;
            client_flush(&clients[slot]);
            LOG("accept fd=%d cid=%u\n", fd, clients[slot].cid);
            fprintf(stderr, "accept fd=%d cid=%u\n", fd, clients[slot].cid);
        }
    }
    return 0;
}
