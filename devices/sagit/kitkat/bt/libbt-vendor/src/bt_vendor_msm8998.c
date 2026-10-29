/*
 * KitKat bluedroid vendor library for the Qualcomm WCN3990 ("cherokee")
 * Bluetooth controller on msm8998 (Xiaomi Mi 6 / sagit, Mi MIX 2 / chiron).
 *
 * Derived from hardware/qcom/bt libbt-vendor (LineageOS 17.1, Apache-2.0):
 * the patchram sequence lives in hw_rome.c and the UART plumbing in
 * hci_uart.c; this file replaces the Android-10 bt_vendor_qcom.c glue, whose
 * cherokee path only connected to a vendor "bt_sock" HCI filter that KitKat
 * has no equivalent of.
 *
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Copyright 2012 The Android Open Source Project.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License. You may
 * obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

/******************************************************************************
 *
 *  Filename:      bt_vendor_msm8998.c
 *
 *  Description:   Vendor interface for the cherokee UART controller:
 *                      POWER_CTRL   /dev/btpower ioctl
 *                      USERIAL_OPEN /dev/ttyHS0 + patchram, before the fd is
 *                                     handed to the stack
 *                      FW_CFG       reports what USERIAL_OPEN achieved
 *
 ******************************************************************************/

#define LOG_TAG "bt_vendor"

#include "bt_vendor_qcom.h"
#include "hci_uart.h"
#include "hw_rome.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <utils/Log.h>
/******************************************************************************
**  Variables
******************************************************************************/

struct bt_qcom_struct q;

/* Controller state as of the last BT_VND_OP_USERIAL_OPEN. KitKat's
 * userial_open() starts the H4 receive thread as soon as we hand it the fd,
 * so all EDL traffic has to have finished by then; FW_CFG only reports the
 * outcome of that call. */
static bool soc_initialized = FALSE;

static const tUSERIAL_CFG userial_init_cfg = {
    (USERIAL_DATABITS_8 | USERIAL_PARITY_NONE | USERIAL_STOPBITS_1),
    USERIAL_BAUD_115200
};

/******************************************************************************
**  Functions
******************************************************************************/

/* Copy BD address to little-endian byte order, which is how the NVM tag and
 * the EDL write-address command want it. */
static void le2bd(unsigned char *src, unsigned char *dst)
{
    int i;

    for (i = 0; i < 6; i++)
        dst[i] = src[5 - i];
}

/*
 * Find the rfkill class entry that belongs to Bluetooth.
 *
 * On cherokee the power itself goes through /dev/btpower, so this is only
 * used to read the <rfkill>/device/extldo flag that decides whether
 * rome_download_tlv_file() has to disable the controller's internal LDO --
 * and to record the blocked state, which is the first thing to look at when
 * the UART answers nothing at all.
 */
static void bt_find_rfkill(void)
{
    char type_path[64];
    char state_path[64];
    char ldo_path[64];
    char value[16];
    int id;

    q.rfkill_id = -1;
    q.enable_extldo = FALSE;

    for (id = 0; id < 16 && q.rfkill_id < 0; id++) {
        char type[16];
        int fd;
        int size;

        snprintf(type_path, sizeof(type_path),
                 "/sys/class/rfkill/rfkill%d/type", id);
        fd = open(type_path, O_RDONLY);
        if (fd < 0)
            continue;

        size = read(fd, type, sizeof(type) - 1);
        close(fd);

        if (size > 0 && !memcmp(type, "bluetooth", 9)) {
            q.rfkill_id = id;
            snprintf(state_path, sizeof(state_path),
                     "/sys/class/rfkill/rfkill%d/state", id);
            break;
        }
    }

    if (q.rfkill_id < 0) {
        ALOGI("%s: no bluetooth rfkill device", __FUNCTION__);
        return;
    }

    ALOGI("%s: rfkill id %d", __FUNCTION__, q.rfkill_id);

    {
        int fd = open(state_path, O_RDONLY);

        if (fd >= 0) {
            int size = read(fd, value, sizeof(value) - 1);

            close(fd);
            if (size > 0) {
                value[size] = '\0';
                ALOGI("%s: rfkill state %s", __FUNCTION__, value);
            }
        }
    }

    snprintf(ldo_path, sizeof(ldo_path),
             "/sys/class/rfkill/rfkill%d/device/extldo", q.rfkill_id);
    {
        int fd = open(ldo_path, O_RDONLY);

        if (fd >= 0) {
            int size = read(fd, value, sizeof(value) - 1);

            close(fd);
            if (size > 0) {
                value[size] = '\0';
                if (!memcmp(value, "true", 4)) {
                    ALOGI("%s: external LDO configured", __FUNCTION__);
                    q.enable_extldo = TRUE;
                }
            }
        }
    }
}

/*
 * Power the controller through the btpower driver.
 *
 * WCN3990 has no kernel HCI transport (CONFIG_BT_HCIUART is not set) and no
 * rfkill-driven power path either; btpower is the only thing that brings the
 * chip out of reset, and it is the same node the WLAN cnss-daemon uses.
 */
static int bt_powerup(int enable)
{
    int fd, ret;

    ALOGI("%s: %s", __FUNCTION__, enable ? "on" : "off");

    fd = open(BT_PWR_CNTRL_DEVICE, O_RDWR | O_NONBLOCK);
    if (fd < 0) {
        ALOGE("%s: open %s failed: %s(%d)", __FUNCTION__, BT_PWR_CNTRL_DEVICE,
              strerror(errno), errno);
        return -1;
    }

    ret = ioctl(fd, BT_CMD_PWR_CTRL, (unsigned long)enable);
    if (ret < 0) {
        ALOGE("%s: ioctl BT_CMD_PWR_CTRL(%d) failed: %s(%d)", __FUNCTION__,
              enable, strerror(errno), errno);
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

/*****************************************************************************
**
**   BLUETOOTH VENDOR INTERFACE LIBRARY FUNCTIONS
**
*****************************************************************************/

static int init(const bt_vendor_callbacks_t *p_cb, unsigned char *local_bdaddr)
{
    ALOGI("bt-vendor : init");

    if (p_cb == NULL) {
        ALOGE("init failed with no user callbacks!");
        return -1;
    }

    q.cb = (bt_vendor_callbacks_t *) p_cb;
    soc_initialized = FALSE;

    userial_vendor_init();
    bt_find_rfkill();

    if (local_bdaddr != NULL) {
        le2bd(local_bdaddr, q.bdaddr);
        ALOGI("BD Address from stack: %.02x:%.02x:%.02x:%.02x:%.02x:%.02x",
              q.bdaddr[5], q.bdaddr[4], q.bdaddr[3],
              q.bdaddr[2], q.bdaddr[1], q.bdaddr[0]);
    }

    return 0;
}

/** Requested operations */
static int op(bt_vendor_opcode_t opcode, void *param)
{
    int retval = 0;

    ALOGI("bt-vendor : op for %d", opcode);

    switch (opcode) {
        case BT_VND_OP_POWER_CTRL:
            {
                int nState = *(int *) param;

                ALOGI("bt-vendor : BT_VND_OP_POWER_CTRL: %s",
                      (nState == BT_VND_PWR_ON) ? "on" : "off");

                retval = bt_powerup(nState == BT_VND_PWR_ON ? 1 : 0);
                if (retval == 0 && nState == BT_VND_PWR_OFF)
                    soc_initialized = FALSE;
            }
            break;

        case BT_VND_OP_FW_CFG:
            {
                /* Everything the controller needs was already done while the
                 * fd was still ours; see USERIAL_OPEN below. */
                if (q.cb)
                    q.cb->fwcfg_cb(soc_initialized ?
                                   BT_VND_OP_RESULT_SUCCESS :
                                   BT_VND_OP_RESULT_FAIL);
            }
            break;

        case BT_VND_OP_SCO_CFG:
            {
                if (q.cb)
                    q.cb->scocfg_cb(BT_VND_OP_RESULT_SUCCESS);
            }
            break;

        case BT_VND_OP_USERIAL_OPEN:
            {
                int (*fd_array)[] = (int (*)[]) param;
                int fd, idx;

                fd = userial_vendor_open((tUSERIAL_CFG *)&userial_init_cfg);
                if (fd < 0) {
                    ALOGE("%s: could not open %s", __FUNCTION__,
                          BT_HS_UART_DEVICE);
                    retval = -1;
                    break;
                }

                /* msm_serial_hs keeps its clock gated unless the port is
                 * claimed through the PM ioctl. */
                if (userial_clock_operation(fd, USERIAL_OP_CLK_ON) < 0)
                    ALOGE("%s: UART clock on failed", __FUNCTION__);

                /* EDL bring-up: read the ROM version, jump to 3 Mbaud,
                 * download rampatch + NVM TLVs, HCI reset. It has to happen
                 * here, not in FW_CFG, because userial_open() starts the H4
                 * receive thread the moment this call returns and that thread
                 * would eat the patchram vendor events. */
                if (rome_soc_init(fd, (char *)q.bdaddr) < 0) {
                    ALOGE("%s: cherokee firmware download failed", __FUNCTION__);
                    userial_clock_operation(fd, USERIAL_OP_CLK_OFF);
                    userial_vendor_close();
                    soc_initialized = FALSE;
                    retval = -1;
                    break;
                }

                soc_initialized = TRUE;
                for (idx = 0; idx < CH_MAX; idx++)
                    (*fd_array)[idx] = fd;

                /* KitKat's userial_open() rejects anything but exactly 1. */
                retval = 1;
            }
            break;

        case BT_VND_OP_USERIAL_CLOSE:
            {
                userial_vendor_close();
                soc_initialized = FALSE;
            }
            break;

        case BT_VND_OP_GET_LPM_IDLE_TIMEOUT:
            {
                uint32_t *timeout_ms = (uint32_t *) param;
                *timeout_ms = 1000;
            }
            break;

        case BT_VND_OP_LPM_SET_MODE:
            {
                /* No bt_wake/wlan_bt_kill GPIO wiring on this port: the UART
                 * stays clocked and hardware flow control parks the host
                 * instead. Answer the request so bluedroid does not stall. */
                if (q.cb)
                    q.cb->lpm_cb(BT_VND_OP_RESULT_SUCCESS);
            }
            break;

        case BT_VND_OP_LPM_WAKE_SET_STATE:
            break;

        case BT_VND_OP_EPILOG:
            {
                /* rome_soc_init() already finished with a HCI_RESET. */
                if (q.cb)
                    q.cb->epilog_cb(BT_VND_OP_RESULT_SUCCESS);
            }
            break;

        default:
            ALOGE("unknown operation %d", opcode);
            break;
    }

    return retval;
}

/** Closes the interface */
static void cleanup(void)
{
    ALOGI("cleanup");

    userial_vendor_close();
    q.cb = NULL;
    soc_initialized = FALSE;
}

// Entry point of DLib
const bt_vendor_interface_t BLUETOOTH_VENDOR_LIB_INTERFACE = {
    sizeof(bt_vendor_interface_t),
    init,
    op,
    cleanup
};
