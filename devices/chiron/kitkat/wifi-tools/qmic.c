#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <linux/msm_ipc.h>

/* qmic — manual wlFW (0x45) QMI client to poke the firmware past the
 * capability-then-silence stall. QMI SDU: [cf(1)][txn(1)][msg_id(2LE)][len(2LE)][TLVs]
 * TLV: [type(1)][len(2LE)][value]. */

static unsigned char pkt[2048];
static int fd;

static void put16(int off, unsigned v) { pkt[off] = v & 0xff; pkt[off+1] = (v >> 8) & 0xff; }

static int build(unsigned txn, unsigned msg_id, const unsigned char* tlvs, int tlen) {
    pkt[0] = 0x00;             /* control flags: request */
    pkt[1] = txn & 0xff;
    put16(2, msg_id);
    put16(4, tlen);
    if (tlen) memcpy(pkt + 6, tlvs, tlen);
    return 6 + tlen;
}

static void send_pkt(int n, const char* name) {
    struct sockaddr_msm_ipc to;
    memset(&to, 0, sizeof to);
    /* filled by caller via global */
    extern struct sockaddr_msm_ipc g_dst;
    to = g_dst;
    if (sendto(fd, pkt, n, 0, (struct sockaddr*)&to, sizeof to) < 0)
        printf("send %s: %s\n", name, strerror(errno));
    else
        printf(">> %s (txn=%u id=0x%x len=%d)\n", name, pkt[1], pkt[2] | (pkt[3] << 8), pkt[4] | (pkt[5] << 8));
}

static int recv_pkt(int wait_ms) {
    struct timeval tv = { .tv_sec = wait_ms / 1000, .tv_usec = (wait_ms % 1000) * 1000 };
    fd_set r;
    FD_ZERO(&r); FD_SET(fd, &r);
    if (select(fd + 1, &r, NULL, NULL, &tv) <= 0) return -1;
    struct sockaddr_msm_ipc from;
    socklen_t fl = sizeof from;
    int n = recvfrom(fd, pkt, sizeof pkt, 0, (struct sockaddr*)&from, &fl);
    if (n < 6) return -1;
    unsigned id = pkt[2] | (pkt[3] << 8);
    printf("<< cf=%02x txn=%02x id=0x%02x len=%u:", pkt[0], pkt[1], id, pkt[4] | (pkt[5] << 8));
    for (int i = 6; i < n && i < 6 + 64; i++) printf(" %02x", pkt[i]);
    printf("\n");
    return id;
}

struct sockaddr_msm_ipc g_dst;

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    fd = socket(AF_MSM_IPC, SOCK_DGRAM, 0);
    if (fd < 0) { printf("socket: %s\n", strerror(errno)); return 1; }

    unsigned node = argc > 1 ? strtoul(argv[1], 0, 0) : 0;
    unsigned port = argc > 2 ? strtoul(argv[2], 0, 0) : 0x60;
    printf("wlFW at node=%u port=%u\n", node, port);
    if (!port) { printf("wlFW not found\n"); return 1; }

    memset(&g_dst, 0, sizeof g_dst);
    g_dst.family = AF_MSM_IPC;
    g_dst.address.addrtype = MSM_IPC_ADDR_ID;
    g_dst.address.addr.port_addr.node_id = node;
    g_dst.address.addr.port_addr.port_id = port;

    unsigned char t[128];
    int n;

    /* ind_register: TLV1 fw_ready=1, TLV4 msa_ready=1, TLV5 pin_connect=1, TLV6 client_id=1 */
    n = 0;
    t[n++]=1; t[n++]=0; t[n++]=1; t[n++]=1;               /* TLV1 */
    t[n++]=4; t[n++]=0; t[n++]=1; t[n++]=1;               /* TLV4 */
    t[n++]=5; t[n++]=0; t[n++]=1; t[n++]=1;               /* TLV5 */
    t[n++]=6; t[n++]=0; t[n++]=4; t[n++]=1; t[n++]=0; t[n++]=0; t[n++]=0; /* TLV6 u32=1 */
    send_pkt(build(1, 0x20, t, n), "IND_REGISTER");
    recv_pkt(3000);

    /* cap req: no TLVs */
    send_pkt(build(2, 0x24, NULL, 0), "CAP");
    recv_pkt(3000);

    /* ini: TLV1 enablefwlog=1 */
    n = 0; t[n++]=1; t[n++]=0; t[n++]=1; t[n++]=1;
    send_pkt(build(3, 0x2f, t, n), "INI(fwlog=1)");
    recv_pkt(3000);

    /* msa_ready req: no TLVs */
    send_pkt(build(4, 0x2e, NULL, 0), "MSA_READY");
    recv_pkt(3000);

    /* wlan_mode: TLV1 mode=0 MISSION */
    n = 0; t[n++]=1; t[n++]=0; t[n++]=4; t[n++]=0; t[n++]=0; t[n++]=0; t[n++]=0;
    send_pkt(build(5, 0x22, t, n), "WLAN_MODE(MISSION)");
    recv_pkt(3000);

    /* host_cap: TLV1 num_clients(u32)=1, TLV5 bdf_support(u8)=1, TLV11 cal_done(u8)=0 */
    n = 0;
    t[n++]=1; t[n++]=0; t[n++]=4; t[n++]=1; t[n++]=0; t[n++]=0; t[n++]=0;   /* num_clients=1 */
    t[n++]=5; t[n++]=0; t[n++]=1; t[n++]=1;                                  /* bdf_support=1 */
    t[n++]=11; t[n++]=0; t[n++]=1; t[n++]=0;                                 /* cal_done=0 */
    send_pkt(build(6, 0x34, t, n), "HOST_CAP");
    recv_pkt(3000);

    printf("listening for indications 15s...\n");
    for (int i = 0; i < 8; i++) {
        int id = recv_pkt(2000);
        if (id == 0x21) { printf("*** FW_READY_IND ***\n"); break; }
        if (id < 0) continue;
    }
    printf("done\n");
    return 0;
}
