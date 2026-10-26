#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/android/binder.h>

/* PING_TRANSACTION = B_PACK_CHARS('P','I','N','G') */
#define PING_CODE 0x50494e47

static void dump_br(uint32_t* r, size_t words) {
    for (size_t i = 0; i < words; ) {
        uint32_t c = r[i++];
        switch (c) {
        case BR_ERROR: printf("BR_ERROR %u\n", r[i++]); break;
        case BR_OK: printf("BR_OK\n"); break;
        case BR_TRANSACTION_SEC_CTX: /* fallthrough */
        case BR_TRANSACTION: case BR_REPLY: {
            struct binder_transaction_data* tr = (struct binder_transaction_data*)&r[i];
            const char* nm = (c == BR_REPLY) ? "BR_REPLY" : ((c == BR_TRANSACTION) ? "BR_TRANSACTION" : "BR_TRANSACTION_SEC_CTX");
            printf("%s code=%u flags=%u data_size=%u\n", nm, tr->code, tr->flags, (unsigned)tr->data_size);
            if (c == BR_REPLY && tr->data_size >= 4) {
                int32_t status = *(int32_t*)tr->data.ptr.buffer;
                printf("  reply status=%d\n", status);
            }
            i += sizeof(*tr) / 4;
            break;
        }
        case BR_ACQUIRE_RESULT: printf("BR_ACQUIRE_RESULT %u\n", r[i++]); break;
        case BR_DEAD_REPLY: printf("BR_DEAD_REPLY (no target)\n"); break;
        case BR_FROZEN_REPLY: printf("BR_FROZEN_REPLY\n"); break;
        case BR_TRANSACTION_COMPLETE: printf("BR_TRANSACTION_COMPLETE\n"); break;
        case BR_INCREFS: case BR_ACQUIRE: case BR_RELEASE: case BR_DECREFS: {
            struct binder_ptr_cookie* pc = (struct binder_ptr_cookie*)&r[i];
            const char* nm = c==BR_INCREFS?"BR_INCREFS":c==BR_ACQUIRE?"BR_ACQUIRE":c==BR_RELEASE?"BR_RELEASE":"BR_DECREFS";
            printf("%s ptr=%u\n", nm, (unsigned)pc->ptr);
            i += sizeof(*pc) / 4;
            break;
        }
        case BR_ATTEMPT_ACQUIRE: {
            struct binder_ptr_cookie* pc = (struct binder_ptr_cookie*)&r[i];
            printf("BR_ATTEMPT_ACQUIRE ptr=%u\n", (unsigned)pc->ptr);
            i += sizeof(*pc) / 4 + 1;
            break;
        }
        case BR_SPAWN_LOOPER: printf("BR_SPAWN_LOOPER\n"); break;
        case BR_DEAD_BINDER: printf("BR_DEAD_BINDER\n"); i++; break;
        case BR_CLEAR_DEATH_NOTIFICATION_DONE: printf("BR_CLEAR_DEATH_NOTIFICATION_DONE\n"); i++; break;
        case BR_FAILED_REPLY: printf("BR_FAILED_REPLY (transaction rejected)\n"); break;
        default: printf("BR_? 0x%x\n", c); return;
        }
    }
}

int main(int argc, char** argv) {
    const char* dev = argc > 1 ? argv[1] : "/dev/vndbinder";
    int fd = open(dev, O_RDWR);
    if (fd < 0) { printf("open %s failed: %s\n", dev, strerror(errno)); return 1; }
    void* m = mmap(NULL, 128*1024, PROT_READ, MAP_PRIVATE, fd, 0);
    if (m == MAP_FAILED) { printf("mmap failed: %s\n", strerror(errno)); return 1; }
    struct binder_version { int32_t protocol_version; } v = {0};
    ioctl(fd, BINDER_VERSION, &v);
    printf("binder protocol version: %d\n", v.protocol_version);

    uint32_t w[128];
    struct binder_transaction_data tr;
    memset(&tr, 0, sizeof tr);
    tr.target.handle = 0;
    tr.code = PING_CODE;
    tr.flags = 0; /* synchronous, want reply */
    w[0] = BC_TRANSACTION;
    memcpy(&w[1], &tr, sizeof tr);
    struct binder_write_read bwr;
    memset(&bwr, 0, sizeof bwr);
    bwr.write_size = 4 + sizeof tr;
    bwr.write_buffer = (uintptr_t)w;
    uint32_t r[512];
    bwr.read_size = sizeof r;
    bwr.read_buffer = (uintptr_t)r;
    if (ioctl(fd, BINDER_WRITE_READ, &bwr) < 0) {
        printf("BINDER_WRITE_READ failed: %s\n", strerror(errno));
        return 1;
    }
    printf("read %u bytes hex:", (unsigned)bwr.read_consumed);
    for (size_t i = 0; i < bwr.read_consumed / 4 && i < 16; i++) printf(" %08x", r[i]);
    printf("\n");
    dump_br(r, bwr.read_consumed / 4);
    return 0;
}
