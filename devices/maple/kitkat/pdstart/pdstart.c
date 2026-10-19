/*
 * pdstart.c - one-shot SERVREG_NOTIF client for the modem's servreg
 * notifier (QMI service 0x42, vers 1, ins 180 -> router instance
 * 0xb401) on the maple KitKat port.
 *
 * Speaks the same legacy IPC Router userspace socket API as pdserve.c
 * (the server side): AF_MSM_IPC (27) SOCK_DGRAM, server lookup through
 * IPC_ROUTER_IOCTL_LOOKUP_SERVER = _IOWR(0xC3, 2, struct sockaddr_msm_ipc),
 * connect() by MSM_IPC_ADDR_ID, 7-byte QMI header + TLVs
 * (u8 cntl_flag | u16 txn | u16 msgid | u16 msglen; u8 type | u16 len |
 * value; top-level strings are bare bytes, no NUL).
 *
 * Why: the port has no service-notifier client that ever sends these
 * messages from userspace, and icnss only sends RESTART_PD on its own
 * recovery path. The modem acks the kernel's listener registration with
 * curr_state = UNINIT (0x7fffffff) once per boot and never starts the
 * wlan_pd. This tool re-registers as a listener (to read the raw result
 * code/state), queries the state, sends QMI_SERVREG_NOTIF_RESTART_PD_REQ
 * (0x24) for the domain, and then watches for STATE_UPDATED_IND (0x22)
 * for up to 10 s.
 *
 * Messages (drivers/soc/qcom/service-notifier-private.h):
 *  0x20 REGISTER_LISTENER req TLV 0x01 u8 enable, TLV 0x02 string name
 *       resp TLV 0x02 {u16 result, u16 error}, TLV 0x10 u32 curr_state
 *  0x21 QUERY_STATE       req TLV 0x01 string name
 *  0x24 RESTART_PD        req TLV 0x01 string name
 *       resp TLV 0x02 {u16 result, u16 error}
 * States: 0x7fffffff UNINIT, 0x1fffffff UP, 0x0fffffff DOWN.
 *
 * Usage: pdstart [name] [all|register|unregister|query|restart]
 *        name defaults to msm/modem/wlan_pd.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef AF_MSM_IPC
#define AF_MSM_IPC 27
#endif
#define MSM_IPC_ADDR_NAME 1
#define MSM_IPC_ADDR_ID   2

#define IPC_ROUTER_IOCTL_MAGIC 0xC3
#define IPC_ROUTER_IOCTL_LOOKUP_SERVER \
	_IOWR(IPC_ROUTER_IOCTL_MAGIC, 2, struct sockaddr_msm_ipc)

#define SERVREG_NOTIF_SERVICE 0x42
#define SERVREG_NOTIF_INSTANCE \
	(((180) & 0xFF) << 8 | ((1) & 0xFF))	/* ins 180, vers 1 */

#define QMI_CNTL_REQUEST  0x00
#define QMI_CNTL_RESPONSE 0x02

#define MSG_REGISTER_LISTENER 0x0020
#define MSG_QUERY_STATE       0x0021
#define MSG_STATE_UPDATED_IND 0x0022
#define MSG_STATE_UPDATED_ACK 0x0023
#define MSG_RESTART_PD        0x0024

struct msm_ipc_port_addr {
	unsigned int node_id;
	unsigned int port_id;
};

struct msm_ipc_port_name {
	unsigned int service;
	unsigned int instance;
};

struct msm_ipc_addr {
	unsigned char addrtype;
	union {
		struct msm_ipc_port_addr port_addr;
		struct msm_ipc_port_name port_name;
	} addr;
};

struct sockaddr_msm_ipc {
	unsigned short family;
	struct msm_ipc_addr address;
	unsigned char reserved;
};

struct msm_ipc_server_info {
	unsigned int node_id;
	unsigned int port_id;
	unsigned int service;
	unsigned int instance;
};

struct server_lookup_args {
	struct msm_ipc_port_name port_name;
	int num_entries_in_array;
	int num_entries_found;
	unsigned int lookup_mask;
	struct msm_ipc_server_info srv_info[8];
};

static void put_u16(uint8_t *b, int o, uint16_t v)
{
	b[o] = v & 0xFF;
	b[o + 1] = (v >> 8) & 0xFF;
}

static uint16_t get_u16(const uint8_t *b, int o)
{
	return (uint16_t)(b[o] | (b[o + 1] << 8));
}

static uint32_t get_u32(const uint8_t *b, int o)
{
	return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) |
	       ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}

static int qmi_begin(uint8_t *b, uint16_t txn, uint16_t msgid)
{
	b[0] = QMI_CNTL_REQUEST;
	put_u16(b, 1, txn);
	put_u16(b, 3, msgid);
	put_u16(b, 5, 0);
	return 7;
}

static int tlv_u8(uint8_t *b, int pos, uint8_t type, uint8_t val)
{
	b[pos] = type;
	put_u16(b, pos + 1, 1);
	b[pos + 3] = val;
	return pos + 4;
}

static int tlv_str(uint8_t *b, int pos, uint8_t type, const char *s)
{
	int n = (int)strlen(s);

	b[pos] = type;
	put_u16(b, pos + 1, (uint16_t)n);
	memcpy(b + pos + 3, s, n);
	return pos + 3 + n;
}

static void qmi_finish(uint8_t *b, int len)
{
	put_u16(b, 5, (uint16_t)(len - 7));
}

static void dump_resp(const char *tag, const uint8_t *b, int n)
{
	int pos = 7;
	int i;

	if (n < 7) {
		printf("[%s] short datagram (%d bytes)\n", tag, n);
		return;
	}
	printf("[%s] %d bytes cntl=0x%02x txn=%u msgid=0x%04x msglen=%u\n",
	       tag, n, b[0], get_u16(b, 1), get_u16(b, 3), get_u16(b, 5));
	while (pos + 3 <= n) {
		uint8_t type = b[pos];
		uint16_t len = get_u16(b, pos + 1);

		printf("  TLV 0x%02x len=%u data=", type, len);
		if (pos + 3 + len > n) {
			printf("<truncated>\n");
			break;
		}
		for (i = 0; i < len && i < 40; i++)
			printf("%02x", b[pos + 3 + i]);
		if (len > 40)
			printf("...");
		if (type == 0x02 && len >= 4)
			printf("  {result=%u error=%u}", get_u16(b, pos + 3),
			       get_u16(b, pos + 5));
		if (type == 0x10 && len == 4) {
			uint32_t st = get_u32(b, pos + 3);

			printf("  curr_state=0x%08x (%s)", st,
			       st == 0x7fffffff ? "UNINIT" :
			       st == 0x1fffffff ? "UP" :
			       st == 0x0fffffff ? "DOWN" : "?");
		}
		printf("\n");
		pos += 3 + len;
	}
}

static int do_msg(int fd, const char *name, uint16_t txn, uint16_t msgid,
		  int mode)
{
	uint8_t buf[256];
	uint8_t rbuf[512];
	int len;
	int rc;
	struct pollfd pfd;

	len = qmi_begin(buf, txn, msgid);
	if (msgid == MSG_REGISTER_LISTENER)
		len = tlv_u8(buf, len, 0x01, mode ? 0x01 : 0x00);
	if (msgid == MSG_REGISTER_LISTENER)
		len = tlv_str(buf, len, 0x02, name);
	else
		len = tlv_str(buf, len, 0x01, name);
	qmi_finish(buf, len);

	printf("\n==> msgid 0x%04x txn %u (%d bytes)%s\n", msgid, txn, len,
	       msgid == MSG_REGISTER_LISTENER ?
	       (mode ? " register" : " unregister") : "");

	rc = (int)send(fd, buf, len, 0);
	if (rc < 0) {
		printf("send failed: %s (%d)\n", strerror(errno), errno);
		return -1;
	}
	printf("sent %d bytes\n", rc);

	pfd.fd = fd;
	pfd.events = POLLIN;
	rc = poll(&pfd, 1, 3000);
	if (rc <= 0) {
		printf("no response within 3 s (poll=%d errno=%d)\n", rc,
		       errno);
		return -1;
	}
	rc = (int)recv(fd, rbuf, sizeof(rbuf), 0);
	if (rc < 0) {
		printf("recv failed: %s (%d)\n", strerror(errno), errno);
		return -1;
	}
	dump_resp("resp", rbuf, rc);
	return 0;
}

int main(int argc, char **argv)
{
	const char *name = "msm/modem/wlan_pd";
	const char *mode = "all";
	struct server_lookup_args args;
	struct sockaddr_msm_ipc sa;
	int fd;
	int rc;
	int found = 0;

	setvbuf(stdout, NULL, _IONBF, 0);

	if (argc > 1)
		name = argv[1];
	if (argc > 2)
		mode = argv[2];
	printf("pdstart: name=%s mode=%s\n", name, mode);

	fd = socket(AF_MSM_IPC, SOCK_DGRAM, 0);
	if (fd < 0) {
		printf("socket(AF_MSM_IPC) failed: %s (%d)\n", strerror(errno),
		       errno);
		return 1;
	}
	printf("socket fd=%d\n", fd);

	/* Find the modem's servreg notifier (service 0x42 / ins 180). */
	memset(&args, 0, sizeof(args));
	args.port_name.service = SERVREG_NOTIF_SERVICE;
	args.port_name.instance = SERVREG_NOTIF_INSTANCE;
	args.num_entries_in_array = 8;
	args.lookup_mask = 0xFFFFFFFF;
	rc = ioctl(fd, IPC_ROUTER_IOCTL_LOOKUP_SERVER, &args);
	printf("lookup ioctl rc=%d errno=%d found=%d\n", rc, errno,
	       args.num_entries_found);
	if (args.num_entries_found > 0) {
		int i;

		for (i = 0; i < args.num_entries_found && i < 8; i++)
			printf("  srv[%d] node=%u port=%u service=0x%x instance=0x%x\n",
			       i, args.srv_info[i].node_id,
			       args.srv_info[i].port_id,
			       args.srv_info[i].service,
			       args.srv_info[i].instance);
		found = 1;
	}
	if (!found) {
		printf("servreg notifier not found (expected node 0 / instance 0xb401)\n");
		close(fd);
		return 1;
	}

	memset(&sa, 0, sizeof(sa));
	sa.family = AF_MSM_IPC;
	sa.address.addrtype = MSM_IPC_ADDR_ID;
	sa.address.addr.port_addr.node_id = args.srv_info[0].node_id;
	sa.address.addr.port_addr.port_id = args.srv_info[0].port_id;
	rc = connect(fd, (struct sockaddr *)&sa, sizeof(sa));
	printf("connect rc=%d errno=%d (node=%u port=%u)\n", rc, errno,
	       sa.address.addr.port_addr.node_id,
	       sa.address.addr.port_addr.port_id);
	if (rc < 0) {
		close(fd);
		return 1;
	}

	if (!strcmp(mode, "register") || !strcmp(mode, "all"))
		do_msg(fd, name, 0x1001, MSG_REGISTER_LISTENER, 1);
	if (!strcmp(mode, "query") || !strcmp(mode, "all"))
		do_msg(fd, name, 0x1002, MSG_QUERY_STATE, 0);
	if (!strcmp(mode, "restart") || !strcmp(mode, "all"))
		do_msg(fd, name, 0x1003, MSG_RESTART_PD, 0);

	if (!strcmp(mode, "all") || !strcmp(mode, "restart")) {
		/* Watch for STATE_UPDATED_IND for 10 s. */
		int waited = 0;

		printf("\nwatching for state indications for 10 s...\n");
		while (waited < 10000) {
			struct pollfd pfd;
			uint8_t rbuf[512];

			pfd.fd = fd;
			pfd.events = POLLIN;
			rc = poll(&pfd, 1, 500);
			if (rc > 0) {
				rc = (int)recv(fd, rbuf, sizeof(rbuf), 0);
				if (rc > 0)
					dump_resp("ind", rbuf, rc);
			}
			waited += 500;
		}
	}

	if (!strcmp(mode, "unregister") || !strcmp(mode, "all"))
		do_msg(fd, name, 0x1004, MSG_REGISTER_LISTENER, 0);

	close(fd);
	printf("\ndone\n");
	return 0;
}
