/*
 * irsc-load.c - load IPC Router Security Configuration (IRSC) rules from a
 * sec_config file, then close the socket to signal the IRSC completion.
 *
 * Why this exists (net/ipc_router/ipc_router_socket.c:351):
 *   if (port_ptr->type == CLIENT_PORT)
 *           wait_for_irsc_completion();
 * Every send from a userspace (CLIENT_PORT) socket blocks until an
 * IRSC_PORT is destroyed (ipc_router_core.c:3567 ->
 * signal_irsc_completion() -> complete_all()). The stock daemon that does
 * this is irsc_util: it configures each sec_config rule through
 * IPC_ROUTER_IOCTL_CONFIG_SEC_RULES (which arms the port as IRSC_PORT)
 * and exits. This port has no irsc_util, so *every* userspace QMI client
 * blocks in its first send forever (uninterruptible, SIGKILL-proof).
 *
 * Usage: irsc-load [sec_config_path]   (default /vendor/etc/sec_config)
 * File format: service:instance:gid[:gid...]  (decimal; 4294967295 = all)
 * Lines starting with '/' or '#' are comments.
 * Root (euid 0) is required by the kernel; root senders bypass the rules.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef AF_MSM_IPC
#define AF_MSM_IPC 27
#endif

#define IPC_ROUTER_IOCTL_MAGIC 0xC3

struct config_sec_rules_args {
	int num_group_info;
	uint32_t service_id;
	uint32_t instance_id;
	unsigned reserved;
	uint32_t group_id[0];
};

#define IPC_ROUTER_IOCTL_CONFIG_SEC_RULES \
	_IOR(IPC_ROUTER_IOCTL_MAGIC, 5, struct config_sec_rules_args)

#define MAX_GIDS 32

int main(int argc, char **argv)
{
	const char *path = argc > 1 ? argv[1] : "/vendor/etc/sec_config";
	uint8_t buf[sizeof(struct config_sec_rules_args) +
		    MAX_GIDS * sizeof(uint32_t)];
	struct config_sec_rules_args *args = (struct config_sec_rules_args *)buf;
	FILE *f;
	char line[1024];
	int fd;
	int ok = 0;
	int fail = 0;

	setvbuf(stdout, NULL, _IONBF, 0);

	fd = socket(AF_MSM_IPC, SOCK_DGRAM, 0);
	if (fd < 0) {
		printf("socket(AF_MSM_IPC) failed: %s (%d)\n", strerror(errno),
		       errno);
		return 1;
	}
	printf("irsc-load: file=%s fd=%d\n", path, fd);

	f = fopen(path, "r");
	if (!f)
		printf("warn: cannot open %s: %s\n", path, strerror(errno));

	while (f && fgets(line, sizeof(line), f)) {
		char *p = line;
		char *tok;
		unsigned long vals[2 + MAX_GIDS];
		int nv = 0;
		int i;

		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '\0' || *p == '\n' || *p == '/' || *p == '#')
			continue;

		tok = strtok(p, ": \t\r\n");
		while (tok && nv < 2 + MAX_GIDS) {
			vals[nv++] = strtoul(tok, NULL, 0);
			tok = strtok(NULL, ": \t\r\n");
		}
		if (nv < 3)
			continue;

		memset(buf, 0, sizeof(buf));
		args->service_id = (uint32_t)vals[0];
		args->instance_id = (uint32_t)vals[1];
		args->num_group_info = nv - 2;
		for (i = 0; i < args->num_group_info && i < MAX_GIDS; i++)
			args->group_id[i] = (uint32_t)vals[2 + i];

		if (ioctl(fd, IPC_ROUTER_IOCTL_CONFIG_SEC_RULES, args) < 0) {
			fail++;
			if (fail <= 5)
				printf("rule svc=0x%x inst=0x%x gids=%d failed: %s (%d)\n",
				       args->service_id, args->instance_id,
				       args->num_group_info, strerror(errno),
				       errno);
		} else {
			ok++;
		}
	}
	if (f)
		fclose(f);

	if (!ok) {
		/* Any non-EPERM result arms the IRSC port; ALL_SERVICE is
		 * rejected by the kernel (-EINVAL) which still arms it. */
		printf("no rules loaded; arming IRSC port with dummy rule\n");
		memset(buf, 0, sizeof(buf));
		args->service_id = 0xFFFFFFFF;
		args->instance_id = 0xFFFFFFFF;
		args->num_group_info = 1;
		args->group_id[0] = 3004;	/* AID_NET_RAW */
		if (ioctl(fd, IPC_ROUTER_IOCTL_CONFIG_SEC_RULES, args) < 0)
			printf("dummy rule ioctl: %s (%d)\n", strerror(errno),
			       errno);
		else
			ok++;
	}
	printf("rules ok=%d fail=%d\n", ok, fail);

	close(fd);
	printf("irsc port closed -> completion signaled\n");
	return 0;
}
