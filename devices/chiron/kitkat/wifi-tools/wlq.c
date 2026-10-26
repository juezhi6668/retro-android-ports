#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <grp.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <linux/msm_ipc.h>

/* wlq — raw QMI client against the modem's WLFW service (0x45:1).
 *
 * The ipc-router resolves MSM_IPC_ADDR_NAME destinations itself, so no node or
 * port discovery is needed here; that also separates a routing failure (-ENODEV)
 * from an IRSC security-rule rejection (-EPERM), which is the failure mode the
 * in-kernel icnss client can never hit because it is not permission-checked.
 *
 * Only read-only requests are sent by default: CAP (0x24) tells whether the
 * modem still holds the image, MAC_ADDR (0x33) only answers once the WLAN chip
 * firmware is actually alive. */

#define SVC 0x45
#define INS 1

static unsigned char pkt[8192];
static unsigned char tlvbuf[8192];
static struct sockaddr_msm_ipc dst;

static void put16(int off, unsigned v) { pkt[off] = v & 0xff; pkt[off+1] = (v >> 8) & 0xff; }

/* QMI TLV writers into an arbitrary buffer at cursor *o. A TLV is
 * [u8 type][u16 len][value]; an optional field is a 1-byte OPT_FLAG(=1) TLV
 * immediately followed by the value TLV of the same type. */
static void tlv_u8(unsigned char* b, int* o, int type, unsigned v) {
    b[(*o)++] = type; b[(*o)++] = 1; b[(*o)++] = 0; b[(*o)++] = v & 0xff;
}
static void tlv_u32(unsigned char* b, int* o, int type, unsigned v) {
    b[(*o)++] = type; b[(*o)++] = 4; b[(*o)++] = 0;
    b[(*o)++] = v & 0xff; b[(*o)++] = (v >> 8) & 0xff;
    b[(*o)++] = (v >> 16) & 0xff; b[(*o)++] = (v >> 24) & 0xff;
}
static void tlv_data(unsigned char* b, int* o, int type, const unsigned char* d, int dl) {
    int l = 2 + dl;                       /* u16 length prefix + payload */
    b[(*o)++] = type; b[(*o)++] = l & 0xff; b[(*o)++] = (l >> 8) & 0xff;
    b[(*o)++] = dl & 0xff; b[(*o)++] = (dl >> 8) & 0xff;
    memcpy(b + *o, d, dl); *o += dl;
}

/* legacy ipc-router QMI header is 7 bytes: u8 cntl | u16 txn | u16 msg_id |
 * u16 msg_len (6-byte framing makes the modem drop the request silently). */
#define HD 7

static int build(unsigned txn, unsigned msg_id, const unsigned char* tlvs, int tlen) {
    pkt[0] = 0x00;
    put16(1, txn);
    put16(3, msg_id);
    put16(5, tlen);
    if (tlen) memcpy(pkt + HD, tlvs, tlen);
    return HD + tlen;
}

static void send_req(int fd, unsigned txn, unsigned msg_id,
                     const unsigned char* tlvs, int tlen) {
    int n = build(txn, msg_id, tlvs, tlen);
    if (send(fd, pkt, n, 0) < 0)
        printf(">> id=0x%02x send: %s\n", msg_id, strerror(errno));
    else
        printf(">> id=0x%02x txn=%u len=%d ok\n", msg_id, txn, tlen);
}

static void listen_pkt(int fd, int ms, const char* tag) {
    struct timeval tv = { .tv_sec = ms / 1000, .tv_usec = (ms % 1000) * 1000 };
    fd_set r;
    FD_ZERO(&r); FD_SET(fd, &r);
    int s = select(fd + 1, &r, NULL, NULL, &tv);
    if (s <= 0) { printf("   %s: nothing in %d ms\n", tag, ms); return; }
    int n = recv(fd, pkt, sizeof pkt, 0);
    if (n < HD) { printf("   %s: recv %d (%s)\n", tag, n, strerror(errno)); return; }
    printf("   %s: cf=%02x txn=%u id=0x%02x len=%u |", tag, pkt[0],
           pkt[1] | (pkt[2] << 8), pkt[3] | (pkt[4] << 8), pkt[5] | (pkt[6] << 8));
    int lim = n < HD + 120 ? n : HD + 120;
    for (int i = HD; i < lim; i++) printf(" %02x", pkt[i]);
    printf("\n");
}

/* On msm8998 legacy ipc-router v1, every user-space CLIENT_PORT sendmsg() calls
 * wait_for_irsc_completion() first; that completion is signaled only when an
 * IRSC_PORT is removed. Normally the vendor irsc_util opens an IRSC port, loads
 * sec_config, and exits, which fires it. Here irsc_util is a 64-bit vendor blob
 * that cannot link against KitKat's 32-bit liblog, so the completion never
 * signals and every client (cnss-daemon included) stalls forever.
 *
 * This replays irsc_util's job from our own 32-bit static binary: ioctl
 * IPC_ROUTER_IOCTL_CONFIG_SEC_RULES makes the kernel flip the port to IRSC_PORT
 * (only -EPERM, i.e. non-root, avoids it), and close() then runs the IRSC_PORT
 * release path -> signal_irsc_completion() -> complete_all, unblocking all
 * current and future client sends for the rest of the boot. */
static int irsc_load(const char* path) {
    int fd = socket(AF_MSM_IPC, SOCK_DGRAM, 0);
    if (fd < 0) { printf("irsc socket: %s\n", strerror(errno)); return 1; }

    FILE* f = fopen(path, "r");
    if (!f) { printf("irsc open %s: %s\n", path, strerror(errno)); close(fd); return 1; }

    char line[512];
    int loaded = 0;
    while (fgets(line, sizeof line, f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '/' || *p == '#' || *p == '\n' || *p == '\r' || *p == 0)
            continue;

        gid_t gids[32];
        char* tok = strtok(p, ": \r\n\t");
        if (!tok) continue;
        uint32_t svc = (uint32_t)strtoul(tok, NULL, 10);
        tok = strtok(NULL, ": \r\n\t");
        if (!tok) continue;
        uint32_t ins = (uint32_t)strtoul(tok, NULL, 10);
        int ng = 0;
        while (ng < 32 && (tok = strtok(NULL, ": \r\n\t")) != NULL)
            gids[ng++] = (gid_t)strtoul(tok, NULL, 10);
        if (ng <= 0) continue;

        unsigned char buf[sizeof(struct config_sec_rules_args) + 32 * sizeof(gid_t)];
        struct config_sec_rules_args* a = (struct config_sec_rules_args*)buf;
        memset(buf, 0, sizeof buf);
        a->num_group_info = ng;
        a->service_id = svc;
        a->instance_id = ins;
        a->reserved = 0;
        for (int i = 0; i < ng; i++) a->group_id[i] = gids[i];

        if (ioctl(fd, IPC_ROUTER_IOCTL_CONFIG_SEC_RULES, buf) < 0)
            printf("irsc svc=%u ins=%u: %s\n", svc, ins, strerror(errno));
        else
            loaded++;
    }
    fclose(f);
    printf("irsc: %d rules loaded, closing IRSC port -> signal_irsc_completion\n", loaded);
    close(fd);
    return 0;
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    if (argc > 1 && !strcmp(argv[1], "irsc"))
        return irsc_load(argc > 2 ? argv[2] : "/system/vendor/etc/sec_config");


    /* msm_ipc_check_send_permissions() short-circuits to allowed when
     * current_euid==root, so as adb shell (uid 0) our sends are never rejected
     * on credentials — the only gate is the IRSC completion (see irsc_load).
     * The group drop stays for the non-root case and is otherwise inert. */
    {
        const char* gs = getenv("WLFW_GID");
        gid_t grp = gs ? (gid_t)atoi(gs) : 1000;
        gid_t list[1] = { grp };
        if (setgroups(1, list) && getuid() == 0)
            printf("setgroups(%u): %s\n", grp, strerror(errno));
        if (setegid(grp))
            printf("setegid(%u): %s\n", grp, strerror(errno));
        printf("egid=%u\n", getegid());
    }

    struct {
        struct msm_ipc_port_name port_name;
        int num_entries_in_array;
        int num_entries_found;
        uint32_t lookup_mask;
        struct msm_ipc_server_info srv[8];
    } arg;
    memset(&arg, 0, sizeof arg);
    arg.port_name.service = SVC;
    arg.port_name.instance = INS;
    arg.num_entries_in_array = 8;
    /* the router matches with (server_instance & lookup_mask) == wanted, so a
     * zero mask only ever finds instance 0 */
    arg.lookup_mask = 0xFFFFFFFF;

    int fd = socket(AF_MSM_IPC, SOCK_DGRAM, 0);
    if (fd < 0) { printf("socket: %s\n", strerror(errno)); return 1; }

    if (ioctl(fd, IPC_ROUTER_IOCTL_LOOKUP_SERVER, &arg) < 0)
        printf("LOOKUP_SERVER 0x%x:%u: %s\n", SVC, INS, strerror(errno));
    else
        printf("LOOKUP: service 0x%x ins %u -> %d entries\n", SVC, INS, arg.num_entries_found);
    for (int i = 0; i < arg.num_entries_found && i < 8; i++)
        printf("   node=%u port=%u svc=0x%x ins=%u\n", arg.srv[i].node_id,
               arg.srv[i].port_id, arg.srv[i].service, arg.srv[i].instance);

    memset(&dst, 0, sizeof dst);
    dst.family = AF_MSM_IPC;
    if (arg.num_entries_found <= 0) {
        printf("WLFW server not present\n");
        return 1;
    }
    /* connect() only accepts MSM_IPC_ADDR_ID, and a connected client port is
     * what makes the modem's replies find their way back. */
    dst.address.addrtype = MSM_IPC_ADDR_ID;
    dst.address.addr.port_addr.node_id = arg.srv[0].node_id;
    dst.address.addr.port_addr.port_id = arg.srv[0].port_id;

    if (connect(fd, (struct sockaddr*)&dst, sizeof dst) < 0)
        printf("connect node=%u port=%u: %s\n", arg.srv[0].node_id,
               arg.srv[0].port_id, strerror(errno));
    else
        printf("connected to node=%u port=%u\n", arg.srv[0].node_id,
               arg.srv[0].port_id);

    /* drain the discovery packet every new client port receives */
    listen_pkt(fd, 500, "discovery");

    unsigned char t[64];
    int n;

    if (argc > 1 && !strcmp(argv[1], "bdf")) {
        /* This is the step the vendor cnss-daemon would do but cannot on this
         * KitKat tree (it stalls at pm_init/DMS because there is no
         * vendor.qcom.PeripheralManager). Send QMI_WLFW_BDF_DOWNLOAD_REQ (0x0025)
         * ourselves, in <=6144-byte segments with a monotonically increasing
         * seg_id and end=1 on the last, then wait for FW_READY_ind. */
        const char* path = argc > 2 ? argv[2] : "/vendor/firmware/bdf_d5.bin";
        FILE* f = fopen(path, "rb");
        if (!f) { printf("open %s: %s\n", path, strerror(errno)); return 1; }
        static unsigned char img[65536];
        int total = fread(img, 1, sizeof img, f);
        fclose(f);
        printf("bdf %s = %d bytes\n", path, total);
        int txn = 10, seg = 0;
        for (int off = 0; off < total; off += 6144, seg++) {
            int dlen = total - off; if (dlen > 6144) dlen = 6144;
            int end = (off + dlen >= total) ? 1 : 0;
            int o = 0;
            tlv_u8(tlvbuf, &o, 0x01, 1);                        /* image_type=BDF */
            tlv_u8(tlvbuf, &o, 0x10, 1); tlv_u32(tlvbuf, &o, 0x10, 0);  /* file_id */
            tlv_u8(tlvbuf, &o, 0x11, 1); tlv_u32(tlvbuf, &o, 0x11, total); /* total_size */
            tlv_u8(tlvbuf, &o, 0x12, 1); tlv_u32(tlvbuf, &o, 0x12, seg);  /* seg_id */
            tlv_u8(tlvbuf, &o, 0x13, 1); tlv_data(tlvbuf, &o, 0x13, img + off, dlen);
            tlv_u8(tlvbuf, &o, 0x14, 1); tlv_u8(tlvbuf, &o, 0x14, end);   /* end */
            send_req(fd, txn++, 0x0025, tlvbuf, o);
            listen_pkt(fd, 4000, "bdf-resp");
        }
    } else if (argc > 1 && !strcmp(argv[1], "reg")) {
        /* second-client ind_register: does the modem replay FW_READY to a
         * client that registers after the chip is already up? */
        n = 0;
        t[n++]=1; t[n++]=0; t[n++]=1; t[n++]=1;
        t[n++]=4; t[n++]=0; t[n++]=1; t[n++]=1;
        t[n++]=5; t[n++]=0; t[n++]=1; t[n++]=1;
        t[n++]=6; t[n++]=0; t[n++]=4; t[n++]=1; t[n++]=0; t[n++]=0; t[n++]=0;
        send_req(fd, 1, 0x20, t, n);
        listen_pkt(fd, 3000, "reg-resp");
    } else {
        send_req(fd, 1, 0x24, NULL, 0);            /* CAP, no TLVs */
        listen_pkt(fd, 3000, "cap-resp");

        send_req(fd, 2, 0x33, NULL, 0);            /* MAC_ADDR: chip-FW liveness */
        listen_pkt(fd, 3000, "mac-resp");
    }

    if (argc > 1 && !strcmp(argv[1], "hostcap")) {
        n = 0;
        t[n++]=1; t[n++]=0; t[n++]=4; t[n++]=1; t[n++]=0; t[n++]=0; t[n++]=0;
        t[n++]=5; t[n++]=0; t[n++]=1; t[n++]=1;
        send_req(fd, 3, 0x34, t, n);               /* HOST_CAP */
        listen_pkt(fd, 3000, "hostcap-resp");
    }

    printf("listening for indications 20s...\n");
    for (int i = 0; i < 10; i++)
        listen_pkt(fd, 2000, "ind");
    return 0;
}
