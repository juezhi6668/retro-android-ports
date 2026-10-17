/*
 * pdserve.c - minimal SERVREG_LOC (QMI service 0x40) provider for the
 * maple KitKat port, using the legacy IPC Router userspace socket API
 * (AF_MSM_IPC = 27, SOCK_DGRAM).
 *
 * The kernel's service-locator (drivers/soc/qcom/service-locator.c) blocks
 * on the arrival of a SERVREG_LOC server (service 0x40, vers 1, ins 1 ->
 * instance 0x0101) before it hands any client (icnss, audio_pdr, slimbus,
 * ath10k) its protection-domain list. On stock this server is the 64-bit
 * pd-mapper daemon; this port has a 32-bit-only userspace, so pdserve
 * serves the same wire protocol from the .jsn domain tables on the modem
 * partition (transcribed below).
 *
 * Wire format (all little-endian, matches lib/qmi_encdec.c):
 *   header: u8 cntl_flag | u16 txn_id | u16 msg_id | u16 msg_len
 *   TLV:    u8 type | u16 len | value...
 * Nested strings are [u8 len][bytes] without NUL; top-level strings are
 * bare bytes without NUL.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>

#ifndef AF_MSM_IPC
#define AF_MSM_IPC 27
#endif
#define MSM_IPC_ADDR_NAME 1
#define MSM_IPC_ADDR_ID 2

struct msm_ipc_port_addr {
	uint32_t node_id;
	uint32_t port_id;
};

struct msm_ipc_port_name {
	uint32_t service;
	uint32_t instance;
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

#define SERVREG_LOC_SERVICE	0x40u
#define SERVREG_LOC_INSTANCE	0x0101u	/* BUILD_INSTANCE_ID(1, 1) */

#define QMI_CNTL_REQUEST	0x00
#define QMI_CNTL_RESPONSE	0x02

#define QMI_MSG_INDICATION_REGISTER	0x0020
#define QMI_MSG_GET_DOMAIN_LIST		0x0021

struct dom_entry {
	const char *name;
	uint32_t instance_id;
	uint8_t service_data_valid;
	uint32_t service_data;
};

/*
 * Domain tables, transcribed from the modem partition dot-jsn files:
 *   adspr.jsn:   msm/adsp/root_pd,  instance 74,  provides tms/servreg
 *   adspua.jsn:  msm/adsp/audio_pd, instance 74,  provides avs/audio,
 *                tms/servreg
 *   modemr.jsn:  msm/modem/root_pd,  instance 180, provides tms/servreg,
 *                tms/pdr_enabled
 *   modemuw.jsn: msm/modem/wlan_pd,  instance 180, provides kernel/elf_loader,
 *                tms/servreg, wlan/fw
 *
 * A lookup returns every domain whose sr_service list contains a matching
 * "provider/service" entry, exactly like the stock pd-mapper.
 */
static const struct dom_entry dom_adsp_root[] = {
	{ "msm/adsp/root_pd", 74, 0, 0 },
};
static const struct dom_entry dom_adsp_audio[] = {
	{ "msm/adsp/audio_pd", 74, 0, 0 },
};
static const struct dom_entry dom_modem_root[] = {
	{ "msm/modem/root_pd", 180, 0, 0 },
};
static const struct dom_entry dom_modem_wlan[] = {
	{ "msm/modem/wlan_pd", 180, 0, 0 },
};

struct svc_map {
	const char *service;
	const struct dom_entry *doms;
	int count;
};

static const struct svc_map svc_table[] = {
	{ "tms/servreg", NULL, 0 },	/* filled in specially: 4 domains */
	{ "avs/audio", dom_adsp_audio,
	  sizeof(dom_adsp_audio) / sizeof(dom_adsp_audio[0]) },
	{ "tms/pdr_enabled", dom_modem_root,
	  sizeof(dom_modem_root) / sizeof(dom_modem_root[0]) },
{ "tms/pddump_disabled", dom_modem_root,
	  sizeof(dom_modem_root) / sizeof(dom_modem_root[0]) },
	{ "kernel/elf_loader", dom_modem_wlan,
	  sizeof(dom_modem_wlan) / sizeof(dom_modem_wlan[0]) },
	{ "wlan/fw", dom_modem_wlan,
	  sizeof(dom_modem_wlan) / sizeof(dom_modem_wlan[0]) },
};

static const struct dom_entry *lookup_svc(const char *svc, int *count)
{
	/* tms/servreg is provided by every root_pd + child_pd in the tables. */
	static const struct dom_entry tms_servreg[] = {
		{ "msm/adsp/root_pd", 74, 0, 0 },
		{ "msm/adsp/audio_pd", 74, 0, 0 },
		{ "msm/modem/root_pd", 180, 0, 0 },
		{ "msm/modem/wlan_pd", 180, 0, 0 },
	};

	if (!strcmp(svc, "tms/servreg")) {
		*count = sizeof(tms_servreg) / sizeof(tms_servreg[0]);
		return tms_servreg;
	}
	unsigned i;
	for (i = 0; i < sizeof(svc_table) / sizeof(svc_table[0]); i++) {
		if (!strcmp(svc, svc_table[i].service)) {
			*count = svc_table[i].count;
			return svc_table[i].doms;
		}
	}
	*count = 0;
	return NULL;
}

static void put_u16(uint8_t *p, uint16_t v)
{
	memcpy(p, &v, 2);
}

static void put_u32(uint8_t *p, uint32_t v)
{
	memcpy(p, &v, 4);
}

static uint16_t get_u16(const uint8_t *p)
{
	uint16_t v;

	memcpy(&v, p, 2);
	return v;
}

static uint32_t get_u32(const uint8_t *p)
{
	uint32_t v;

	memcpy(&v, p, 4);
	return v;
}

static int tlv_put(uint8_t *buf, int pos, uint8_t type, const void *val,
		   uint16_t len)
{
	buf[pos] = type;
	put_u16(buf + pos + 1, len);
	if (len)
		memcpy(buf + pos + 3, val, len);
	return pos + 3 + len;
}

static void serve_ring(int fd, const uint8_t *req, ssize_t reqlen,
		       const struct sockaddr_msm_ipc *from, socklen_t fromlen)
{
	uint8_t resp[512];
	static uint8_t body[400];
	char svc[65];
	uint32_t offset = 0;
	uint16_t txn, msgid, msglen;
	int p, end, bodylen, i, n;
	const struct dom_entry *doms = NULL;

	if (reqlen < 7)
		return;

	txn = get_u16(req + 1);
	msgid = get_u16(req + 3);
	msglen = get_u16(req + 5);

	memset(svc, 0, sizeof(svc));
	end = 7 + msglen;
	if (end > reqlen)
		end = reqlen;

	for (p = 7; p + 3 <= end;) {
		uint8_t type = req[p];
		uint16_t l = get_u16(req + p + 1);

		p += 3;
		if (p + l > end)
			break;
		if (type == 0x01) {	/* service_name */
			int nn = l < 64 ? l : 64;

			memcpy(svc, req + p, nn);
			svc[nn] = 0;
		} else if (type == 0x10 && l >= 4) {	/* domain_offset */
			offset = get_u32(req + p);
		}
		p += l;
	}

	printf("req: cntl=%02x txn=%04x msg=%04x len=%u service='%s' offset=%u\n",
	       req[0], txn, msgid, msglen, svc, offset);
	fflush(stdout);

	if (req[0] != QMI_CNTL_REQUEST)
		return;

	doms = lookup_svc(svc, &n);

	/* TLV 0x02: qmi_response_type_v01 { result, error } */
	{
		uint8_t r[4];

		put_u16(r, 0);
		put_u16(r + 2, 0);
		bodylen = tlv_put(body, 0, 0x02, r, 4);
	}

	if (msgid == QMI_MSG_GET_DOMAIN_LIST) {
		uint8_t v[2];
		uint8_t list[256];
		int lp = 1;	/* u8 count first */

		put_u16(v, n);
		bodylen = tlv_put(body, bodylen, 0x10, v, 2);	/* total */
		put_u16(v, 1);
		bodylen = tlv_put(body, bodylen, 0x11, v, 2);	/* db_rev */

		for (i = 0; i < n; i++) {
			const char *nm = doms[i].name;
			int nl = strlen(nm);

			list[lp++] = nl;
			memcpy(list + lp, nm, nl);
			lp += nl;
			put_u32(list + lp, doms[i].instance_id);
			lp += 4;
			list[lp++] = doms[i].service_data_valid;
			put_u32(list + lp, doms[i].service_data);
			lp += 4;
		}
		list[0] = n;
		bodylen = tlv_put(body, bodylen, 0x12, list, lp);
	}

	/* header */
	resp[0] = QMI_CNTL_RESPONSE;
	put_u16(resp + 1, txn);
	put_u16(resp + 3, msgid);
	put_u16(resp + 5, (uint16_t)bodylen);
	memcpy(resp + 7, body, bodylen);

	if (sendto(fd, resp, 7 + bodylen, 0, (const struct sockaddr *)from,
		   fromlen) < 0) {
		perror("sendto");
		return;
	}
	printf("resp: msg=%04x service='%s' total=%d body=%d\n", msgid, svc, n,
	       bodylen);
	fflush(stdout);
}

int main(void)
{
	struct sockaddr_msm_ipc addr;
	int fd;

	signal(SIGPIPE, SIG_IGN);

	fd = socket(AF_MSM_IPC, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket(AF_MSM_IPC)");
		return 1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.family = AF_MSM_IPC;
	addr.address.addrtype = MSM_IPC_ADDR_NAME;
	addr.address.addr.port_name.service = SERVREG_LOC_SERVICE;
	addr.address.addr.port_name.instance = SERVREG_LOC_INSTANCE;

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind(servreg_loc)");
		return 1;
	}

	printf("pdserve: bound servreg_loc 0x%x/%04x\n",
	       SERVREG_LOC_SERVICE, SERVREG_LOC_INSTANCE);
	fflush(stdout);

	for (;;) {
		uint8_t req[4096];
		struct sockaddr_msm_ipc from;
		socklen_t fromlen = sizeof(from);
		ssize_t n;
	(void)n;

		n = recvfrom(fd, req, sizeof(req), 0,
			     (struct sockaddr *)&from, &fromlen);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			perror("recvfrom");
			break;
		}
		if (fromlen < sizeof(from))
			memset((char *)&from + fromlen, 0,
			       sizeof(from) - fromlen);

		serve_ring(fd, req, n, &from, fromlen);
	}

	return 1;
}
