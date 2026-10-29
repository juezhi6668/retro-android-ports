/*
 * Copyright 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef BT_VENDOR_QCOM_H
#define BT_VENDOR_QCOM_H

#include <stdbool.h>
#include "bt_vendor_lib.h"

#ifndef FALSE
#define FALSE  0
#endif

#ifndef TRUE
#define TRUE   (!FALSE)
#endif

#define STREAM_TO_UINT16(u16, p) {u16 = ((uint16_t)(*(p)) + (((uint16_t)(*((p) + 1))) << 8)); (p) += 2;}
#define UINT16_TO_STREAM(p, u16) {*(p)++ = (uint8_t)(u16); *(p)++ = (uint8_t)((u16) >> 8);}
#define UINT32_TO_STREAM(p, u32) {*(p)++ = (uint8_t)(u32); *(p)++ = (uint8_t)((u32) >> 8); *(p)++ = (uint8_t)((u32) >> 16); *(p)++ = (uint8_t)((u32) >> 24);}

/* KitKat's bt_vendor_lib.h stops at BT_VND_OP_EPILOG: there is no A2DP
 * offload opcode, so the FM-over-UART and ANT opcodes the donor derived from
 * BT_VND_OP_A2DP_OFFLOAD_STOP cannot exist here. This port drives a single
 * HCI UART channel and no FM/ANT client.
 *
 * rome_soc_init() already ends with a HCI_RESET on the raw fd, so the epilog
 * has nothing left to send back through the stack's xmit_cb.
 */
#ifndef HW_NEED_END_WITH_HCI_RESET
#define HW_NEED_END_WITH_HCI_RESET FALSE
#endif

/* No bt_wake/wlan_bt_kill pins on the cherokee UART: RTS/CTS hardware flow
 * control carries the wakeup. */
#ifndef BT_WAKE_VIA_USERIAL_IOCTL
#define BT_WAKE_VIA_USERIAL_IOCTL FALSE
#endif

#define HCI_RESET  0x0C03
#define HCI_CMD_PREAMBLE_SIZE 3
#define HCI_EVT_CMD_CMPL_STATUS_RET_BYTE   5
#define HCI_EVT_CMD_CMPL_OPCODE        3
#define BT_PWR_CNTRL_DEVICE    "/dev/btpower"
#define BT_CMD_PWR_CTRL        0xbfad

struct bt_qcom_struct {
    bt_vendor_callbacks_t *cb;
    uint8_t bdaddr[6];
    int rfkill_id;
    bool enable_extldo;
};
extern struct bt_qcom_struct q;

#endif /* BT_VENDOR_QCOM_H */

