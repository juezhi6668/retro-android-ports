/*
 *
 *  Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *  Not a Contribution.
 *
 *  Copyright 2012 The Android Open Source Project
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you
 *  may not use this file except in compliance with the License. You may
 *  obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 *  implied. See the License for the specific language governing
 *  permissions and limitations under the License.
 *
 */

/******************************************************************************
 *
 *  Filename:      hw_rome.c
 *
 *  Description:   Contains controller-specific functions, like
 *                      firmware patch download
 *                      low power mode operations
 *
 ******************************************************************************/
#ifdef __cplusplus
extern "C" {
#endif

#define LOG_TAG "bt_vendor"

#include "bt_hci_bdroid.h"
#include "bt_vendor_qcom.h"
#include "hci_uart.h"
#include "hw_rome.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <cutils/properties.h>
#include <utils/Log.h>

#ifdef __cplusplus
}
#endif

#define RESERVED(p)  if(p) ALOGI( "%s: reserved param", __FUNCTION__);

int read_vs_hci_event(int fd, unsigned char* buf, int size);

/******************************************************************************
**  Variables
******************************************************************************/
FILE *file;
unsigned char *phdr_buffer;
unsigned char *pdata_buffer = NULL;
patch_info rampatch_patch_info;
int chipset_ver = 0;
unsigned char gTlv_type;
unsigned char gTlv_dwndCfg;
static unsigned int wipower_flag = 0;
static unsigned int wipower_handoff_ready = 0;
char *rampatch_file_path = NULL;
char *nvm_file_path = NULL;
char *fw_su_info = NULL;
unsigned short fw_su_offset =0;
unsigned char wait_vsc_evt = TRUE;
bool patch_dnld_pending = FALSE;
int dnld_fd = -1;

/* Cherokee (WCN3990) patchram file names, resolved from the ROM build the
 * chip reports -- see rome_set_cherokee_fw_paths(). */
static char cherokee_rampatch_path[ROME_CHEROKEE_FW_MAX];
static char cherokee_nvm_path[ROME_CHEROKEE_FW_MAX];

/******************************************************************************
**  Extern variables
******************************************************************************/

/*****************************************************************************
**   Functions
*****************************************************************************/
int do_write(int fd, unsigned char *buf,int len)
{
    int ret = 0;
    int write_offset = 0;
    int write_len = len;
    do {
        ret = write(fd,buf+write_offset,write_len);
        if (ret < 0)
        {
            ALOGE("%s, write failed ret = %d err = %s",__func__,ret,strerror(errno));
            return -1;
        } else if (ret == 0) {
            ALOGE("%s, write failed with ret 0 err = %s",__func__,strerror(errno));
            return 0;
        } else {
            if (ret < write_len) {
                ALOGD("%s, Write pending,do write ret = %d err = %s",__func__,ret,
                       strerror(errno));
                write_len = write_len - ret;
                write_offset = ret;
            } else {
                ALOGV("Write successful");
                break;
            }
        }
    } while(1);
    return len;
}

int get_vs_hci_event(unsigned char *rsp)
{
    int err = 0;
    unsigned char paramlen = 0;
    unsigned char EMBEDDED_MODE_CHECK = 0x02;
    FILE *btversionfile = 0;
    unsigned int soc_id = 0;
    unsigned int productid = 0;
    unsigned short patchversion = 0;
    char build_label[255];
    int build_lbl_len;
    unsigned short buildversion = 0;

    if( (rsp[EVENTCODE_OFFSET] == VSEVENT_CODE) || (rsp[EVENTCODE_OFFSET] == EVT_CMD_COMPLETE))
        ALOGI("%s: Received HCI-Vendor Specific event", __FUNCTION__);
    else {
        ALOGI("%s: Failed to receive HCI-Vendor Specific event", __FUNCTION__);
        err = -EIO;
        goto failed;
    }

    ALOGI("%s: Parameter Length: 0x%x", __FUNCTION__, paramlen = rsp[EVT_PLEN]);
    ALOGI("%s: Command response: 0x%x", __FUNCTION__, rsp[CMD_RSP_OFFSET]);
    ALOGI("%s: Response type   : 0x%x", __FUNCTION__, rsp[RSP_TYPE_OFFSET]);

    /* Check the status of the operation */
    switch ( rsp[CMD_RSP_OFFSET] )
    {
        case EDL_CMD_REQ_RES_EVT:
        ALOGI("%s: Command Request Response", __FUNCTION__);
        switch(rsp[RSP_TYPE_OFFSET])
        {
            case EDL_PATCH_VER_RES_EVT:
            case EDL_APP_VER_RES_EVT:
                productid = (unsigned int)(rsp[PATCH_PROD_ID_OFFSET +3] << 24 |
                                    rsp[PATCH_PROD_ID_OFFSET+2] << 16 |
                                    rsp[PATCH_PROD_ID_OFFSET+1] << 8 |
                                    rsp[PATCH_PROD_ID_OFFSET]  );
                ALOGI("\t Current Product ID\t\t: 0x%08x", productid);

                /* Patch Version indicates FW patch version */
                patchversion = (unsigned short)(rsp[PATCH_PATCH_VER_OFFSET + 1] << 8 |
                                       rsp[PATCH_PATCH_VER_OFFSET] );
                ALOGI("\t Current Patch Version\t\t: 0x%04x", patchversion);

                /* ROM Build Version indicates ROM build version like 1.0/1.1/2.0 */
                buildversion = (int)(rsp[PATCH_ROM_BUILD_VER_OFFSET + 1] << 8 |
                                       rsp[PATCH_ROM_BUILD_VER_OFFSET] );
                ALOGI("\t Current ROM Build Version\t: 0x%04x", buildversion);

                /* In case rome 1.0/1.1, there is no SOC ID version available */
                if (paramlen - 10)
                {
                    soc_id = (unsigned int)(rsp[PATCH_SOC_VER_OFFSET +3] << 24 |
                                     rsp[PATCH_SOC_VER_OFFSET+2] << 16 |
                                     rsp[PATCH_SOC_VER_OFFSET+1] << 8 |
                                     rsp[PATCH_SOC_VER_OFFSET]  );
                    ALOGI("\t Current SOC Version\t\t: 0x%08x", soc_id);
                }

                /* Rome Chipset Version can be decided by Patch version and SOC version,
                Upper 2 bytes will be used for Patch version and Lower 2 bytes will be
                used for SOC as combination for BT host driver */
                chipset_ver = (buildversion << 16) |(soc_id & 0x0000ffff);

                break;
            case EDL_TVL_DNLD_RES_EVT:
            case EDL_CMD_EXE_STATUS_EVT:
                switch (err = rsp[CMD_STATUS_OFFSET])
                    {
                    case HCI_CMD_SUCCESS:
                        ALOGI("%s: Download Packet successfully!", __FUNCTION__);
                        break;
                    case PATCH_LEN_ERROR:
                        ALOGI("%s: Invalid patch length argument passed for EDL PATCH "
                        "SET REQ cmd", __FUNCTION__);
                        break;
                    case PATCH_VER_ERROR:
                        ALOGI("%s: Invalid patch version argument passed for EDL PATCH "
                        "SET REQ cmd", __FUNCTION__);
                        break;
                    case PATCH_CRC_ERROR:
                        ALOGI("%s: CRC check of patch failed!!!", __FUNCTION__);
                        break;
                    case PATCH_NOT_FOUND:
                        ALOGI("%s: Invalid patch data!!!", __FUNCTION__);
                        break;
                    case TLV_TYPE_ERROR:
                        ALOGI("%s: TLV Type Error !!!", __FUNCTION__);
                        break;
                    default:
                        ALOGI("%s: Undefined error (0x%x)", __FUNCTION__, err);
                        break;
                    }
            break;
            case HCI_VS_GET_BUILD_VER_EVT:
                build_lbl_len = rsp[5];
                memcpy (build_label, &rsp[6], build_lbl_len);
                *(build_label+build_lbl_len) = '\0';

                ALOGI("BT SoC FW SU Build info: %s, %d", build_label, build_lbl_len);
            break;
        }
        break;

        case NVM_ACCESS_CODE:
            ALOGI("%s: NVM Access Code!!!", __FUNCTION__);
            err = HCI_CMD_SUCCESS;
            break;
        case EDL_SET_BAUDRATE_RSP_EVT:
            /* Rome 1.1 has bug with the response, so it should ignore it. */
            if (rsp[BAUDRATE_RSP_STATUS_OFFSET] != BAUDRATE_CHANGE_SUCCESS)
            {
                ALOGE("%s: Set Baudrate request failed - 0x%x", __FUNCTION__,
                    rsp[CMD_STATUS_OFFSET]);
                err = -1;
            }
            break;
       case EDL_WIP_QUERY_CHARGING_STATUS_EVT:
            /* Query charging command has below return values
            0 - in embedded mode not charging
            1 - in embedded mode and charging
            2 - hadofff completed and in normal mode
            3 - no wipower supported on mtp. so irrepective of charging
            handoff command has to be sent if return values are 0 or 1.
            These change include logic to enable generic BT turn on sequence.*/
            if (rsp[4] < EMBEDDED_MODE_CHECK)
            {
               ALOGI("%s: WiPower Charging in Embedded Mode!!!", __FUNCTION__);
               wipower_handoff_ready = rsp[4];
               wipower_flag = 1;
            }
            break;
        case EDL_WIP_START_HANDOFF_TO_HOST_EVENT:
            /*TODO: rsp code 00 mean no charging
            this is going to change in FW soon*/
            if (rsp[4] == NON_WIPOWER_MODE)
            {
               ALOGE("%s: WiPower Charging hand off not ready!!!", __FUNCTION__);
            }
            break;
        case HCI_VS_GET_ADDON_FEATURES_EVENT:
            if ((rsp[4] & ADDON_FEATURES_EVT_WIPOWER_MASK))
            {
               ALOGD("%s: WiPower feature supported!!", __FUNCTION__);
               property_set("persist.bluetooth.a4wp", "true");
            }
            break;
        case HCI_VS_STRAY_EVT:
            /* WAR to handle stray Power Apply EVT during patch download */
            ALOGD("%s: Stray HCI VS EVENT", __FUNCTION__);
            if (patch_dnld_pending && dnld_fd != -1)
            {
                unsigned char rsp[HCI_MAX_EVENT_SIZE];
                memset(rsp, 0x00, HCI_MAX_EVENT_SIZE);
                read_vs_hci_event(dnld_fd, rsp, HCI_MAX_EVENT_SIZE);
            }
            else
            {
                ALOGE("%s: Not a valid status!!!", __FUNCTION__);
                err = -1;
            }
            break;
        default:
            ALOGE("%s: Not a valid status!!!", __FUNCTION__);
            err = -1;
            break;
    }

failed:
    return err;
}


/*
 * Read an VS HCI event from the given file descriptor.
 */
int read_vs_hci_event(int fd, unsigned char* buf, int size)
{
    int remain, r;
    int count = 0, i;

    if (size <= 0) {
        ALOGE("Invalid size arguement!");
        return -1;
    }

    ALOGI("%s: Wait for HCI-Vendor Specfic Event from SOC", __FUNCTION__);

    /* The first byte identifies the packet type. For HCI event packets, it
     * should be 0x04, so we read until we get to the 0x04. */
    /* It will keep reading until find 0x04 byte */
    while (1) {
            r = uart_read(fd, buf, 1);
            if (r <= 0)
                    return -1;
            if (buf[0] == 0x04)
                    break;
    }
    count++;

    /* The next two bytes are the event code and parameter total length. */
    while (count < 3) {
            r = uart_read(fd, buf + count, 3 - count);
            if ((r <= 0) || (buf[1] != 0xFF )) {
                ALOGE("It is not VS event !! ret: %d, EVT: %d", r, buf[1]);
                return -1;
            }
            count += r;
    }

    /* Now we read the parameters. */
    if (buf[2] < (size - 3))
            remain = buf[2];
    else
            remain = size - 3;

    while ((count - 3) < remain) {
            r = uart_read(fd, buf + count, remain - (count - 3));
            if (r <= 0)
                    return -1;
            count += r;
    }

     /* Check if the set patch command is successful or not */
    if(get_vs_hci_event(buf) != HCI_CMD_SUCCESS)
        return -1;

    return count;
}

/*
 * For Hand-Off related Wipower commands, Command complete arrives first and
 * the followd with VS event
 *
 */
int hci_send_wipower_vs_cmd(int fd, unsigned char *cmd, unsigned char *rsp, int size)
{
    int ret = 0;
    int err = 0;

    /* Send the HCI command packet to UART for transmission */
    ret = do_write(fd, cmd, size);
    if (ret != size) {
        ALOGE("%s: WP Send failed with ret value: %d", __FUNCTION__, ret);
        goto failed;
    }

    /* Wait for command complete event */
    err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
    if ( err < 0) {
        ALOGE("%s: Failed to charging status cmd on Controller", __FUNCTION__);
        goto failed;
    }

    ALOGI("%s: WP Received HCI command complete Event from SOC", __FUNCTION__);
failed:
    return ret;
}


int hci_send_vs_cmd(int fd, unsigned char *cmd, unsigned char *rsp, int size)
{
    int ret = 0;

    /* Send the HCI command packet to UART for transmission */
    ret = do_write(fd, cmd, size);
    if (ret != size) {
        ALOGE("%s: Send failed with ret value: %d", __FUNCTION__, ret);
        goto failed;
    }

    if (wait_vsc_evt) {
        /* Check for response from the Controller */
        if (read_vs_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE) < 0) {
           ret = -ETIMEDOUT;
           ALOGI("%s: Failed to get HCI-VS Event from SOC", __FUNCTION__);
           goto failed;
        }
        ALOGI("%s: Received HCI-Vendor Specific Event from SOC", __FUNCTION__);
    }

failed:
    return ret;
}

void frame_hci_cmd_pkt(
    unsigned char *cmd,
    int edl_cmd, unsigned int p_base_addr,
    int segtNo, int size
    )
{
    int offset = 0;
    hci_command_hdr *cmd_hdr;

    memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

    cmd_hdr = (void *) (cmd + 1);

    cmd[0]      = HCI_COMMAND_PKT;
    cmd_hdr->opcode = cmd_opcode_pack(HCI_VENDOR_CMD_OGF, HCI_PATCH_CMD_OCF);
    cmd_hdr->plen   = size;
    cmd[4]      = edl_cmd;

    switch (edl_cmd)
    {
        case EDL_PATCH_SET_REQ_CMD:
            /* Copy the patch header info as CMD params */
            memcpy(&cmd[5], phdr_buffer, PATCH_HDR_LEN);
            ALOGD("%s: Sending EDL_PATCH_SET_REQ_CMD", __FUNCTION__);
            ALOGD("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x",
                segtNo, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4]);
            break;
        case EDL_PATCH_DLD_REQ_CMD:
            offset = ((segtNo - 1) * MAX_DATA_PER_SEGMENT);
            p_base_addr += offset;
            cmd_hdr->plen   = (size + 6);
            cmd[5]  = (size + 4);
            cmd[6]  = EXTRACT_BYTE(p_base_addr, 0);
            cmd[7]  = EXTRACT_BYTE(p_base_addr, 1);
            cmd[8]  = EXTRACT_BYTE(p_base_addr, 2);
            cmd[9]  = EXTRACT_BYTE(p_base_addr, 3);
            memcpy(&cmd[10], (pdata_buffer + offset), size);

            ALOGD("%s: Sending EDL_PATCH_DLD_REQ_CMD: size: %d bytes",
                __FUNCTION__, size);
            ALOGD("HCI-CMD %d:\t0x%x\t0x%x\t0x%x\t0x%x\t0x%x\t0x%x\t0x%x\t"
                "0x%x\t0x%x\t0x%x\t\n", segtNo, cmd[0], cmd[1], cmd[2],
                cmd[3], cmd[4], cmd[5], cmd[6], cmd[7], cmd[8], cmd[9]);
            break;
        case EDL_PATCH_ATCH_REQ_CMD:
            ALOGD("%s: Sending EDL_PATCH_ATTACH_REQ_CMD", __FUNCTION__);
            ALOGD("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x",
            segtNo, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4]);
            break;
        case EDL_PATCH_RST_REQ_CMD:
            ALOGD("%s: Sending EDL_PATCH_RESET_REQ_CMD", __FUNCTION__);
            ALOGD("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x",
            segtNo, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4]);
            break;
        case EDL_PATCH_VER_REQ_CMD:
            ALOGD("%s: Sending EDL_PATCH_VER_REQ_CMD", __FUNCTION__);
            ALOGD("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x",
            segtNo, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4]);
            break;
        case EDL_PATCH_TLV_REQ_CMD:
            ALOGD("%s: Sending EDL_PATCH_TLV_REQ_CMD", __FUNCTION__);
            /* Parameter Total Length */
            cmd[3] = size +2;

            /* TLV Segment Length */
            cmd[5] = size;
            ALOGD("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x \t0x%x",
            segtNo, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4], cmd[5]);
            offset = (segtNo * MAX_SIZE_PER_TLV_SEGMENT);
            memcpy(&cmd[6], (pdata_buffer + offset), size);
            break;
        case EDL_GET_BUILD_INFO:
            ALOGD("%s: Sending EDL_GET_BUILD_INFO", __FUNCTION__);
            ALOGD("HCI-CMD %d:\t0x%x \t0x%x \t0x%x \t0x%x \t0x%x",
                segtNo, cmd[0], cmd[1], cmd[2], cmd[3], cmd[4]);
            break;
        default:
            ALOGE("%s: Unknown EDL CMD !!!", __FUNCTION__);
    }
}

void rome_extract_patch_header_info(unsigned char *buf)
{
    int index;

    /* Extract patch id */
    for (index = 0; index < 4; index++)
        rampatch_patch_info.patch_id |=
            (LSH(buf[index + P_ID_OFFSET], (index * 8)));

    /* Extract (ROM and BUILD) version information */
    for (index = 0; index < 2; index++)
        rampatch_patch_info.patch_ver.rom_version |=
            (LSH(buf[index + P_ROME_VER_OFFSET], (index * 8)));

    for (index = 0; index < 2; index++)
        rampatch_patch_info.patch_ver.build_version |=
            (LSH(buf[index + P_BUILD_VER_OFFSET], (index * 8)));

    /* Extract patch base and entry addresses */
    for (index = 0; index < 4; index++)
        rampatch_patch_info.patch_base_addr |=
            (LSH(buf[index + P_BASE_ADDR_OFFSET], (index * 8)));

    /* Patch BASE & ENTRY addresses are same */
    rampatch_patch_info.patch_entry_addr = rampatch_patch_info.patch_base_addr;

    /* Extract total length of the patch payload */
    for (index = 0; index < 4; index++)
        rampatch_patch_info.patch_length |=
            (LSH(buf[index + P_LEN_OFFSET], (index * 8)));

    /* Extract the CRC checksum of the patch payload */
    for (index = 0; index < 4; index++)
        rampatch_patch_info.patch_crc |=
            (LSH(buf[index + P_CRC_OFFSET], (index * 8)));

    /* Extract patch control value */
    for (index = 0; index < 4; index++)
        rampatch_patch_info.patch_ctrl |=
            (LSH(buf[index + P_CONTROL_OFFSET], (index * 8)));

    ALOGI("PATCH_ID\t : 0x%x", rampatch_patch_info.patch_id);
    ALOGI("ROM_VERSION\t : 0x%x", rampatch_patch_info.patch_ver.rom_version);
    ALOGI("BUILD_VERSION\t : 0x%x", rampatch_patch_info.patch_ver.build_version);
    ALOGI("PATCH_LENGTH\t : 0x%x", rampatch_patch_info.patch_length);
    ALOGI("PATCH_CRC\t : 0x%x", rampatch_patch_info.patch_crc);
    ALOGI("PATCH_CONTROL\t : 0x%x\n", rampatch_patch_info.patch_ctrl);
    ALOGI("PATCH_BASE_ADDR\t : 0x%x\n", rampatch_patch_info.patch_base_addr);

}

int rome_edl_set_patch_request(int fd)
{
    int size, err;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

    /* Frame the HCI CMD to be sent to the Controller */
    frame_hci_cmd_pkt(cmd, EDL_PATCH_SET_REQ_CMD, 0,
        -1, PATCH_HDR_LEN + 1);

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + cmd[PLEN]);

    /* Send HCI Command packet to Controller */
    err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
    if ( err != size) {
        ALOGE("Failed to set the patch info to the Controller!");
        goto error;
    }

    err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
    if ( err < 0) {
        ALOGE("%s: Failed to set patch info on Controller", __FUNCTION__);
        goto error;
    }
    ALOGI("%s: Successfully set patch info on the Controller", __FUNCTION__);
error:
    return err;
}

int rome_edl_patch_download_request(int fd)
{
    int no_of_patch_segment;
    int index = 1, err = 0, size = 0;
    unsigned int p_base_addr;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

    no_of_patch_segment = (rampatch_patch_info.patch_length /
        MAX_DATA_PER_SEGMENT);
    ALOGI("%s: %d patch segments to be d'loaded from patch base addr: 0x%x",
        __FUNCTION__, no_of_patch_segment,
    rampatch_patch_info.patch_base_addr);

    /* Initialize the patch base address from the one read from bin file */
    p_base_addr = rampatch_patch_info.patch_base_addr;

    /*
    * Depending upon size of the patch payload, download the patches in
    * segments with a max. size of 239 bytes
    */
    for (index = 1; index <= no_of_patch_segment; index++) {

        ALOGI("%s: Downloading patch segment: %d", __FUNCTION__, index);

        /* Frame the HCI CMD PKT to be sent to Controller*/
        frame_hci_cmd_pkt(cmd, EDL_PATCH_DLD_REQ_CMD, p_base_addr,
        index, MAX_DATA_PER_SEGMENT);

        /* Total length of the packet to be sent to the Controller */
        size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + cmd[PLEN]);

        /* Initialize the RSP packet everytime to 0 */
        memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

        /* Send HCI Command packet to Controller */
        err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
        if ( err != size) {
            ALOGE("Failed to send the patch payload to the Controller!");
            goto error;
        }

        /* Read Command Complete Event */
        err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
        if ( err < 0) {
            ALOGE("%s: Failed to downlaod patch segment: %d!",
            __FUNCTION__, index);
            goto error;
        }
        ALOGI("%s: Successfully downloaded patch segment: %d",
        __FUNCTION__, index);
    }

    /* Check if any pending patch data to be sent */
    size = (rampatch_patch_info.patch_length < MAX_DATA_PER_SEGMENT) ?
        rampatch_patch_info.patch_length :
        (rampatch_patch_info.patch_length  % MAX_DATA_PER_SEGMENT);

    if (size)
    {
        /* Frame the HCI CMD PKT to be sent to Controller*/
        frame_hci_cmd_pkt(cmd, EDL_PATCH_DLD_REQ_CMD, p_base_addr, index, size);

        /* Initialize the RSP packet everytime to 0 */
        memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

        /* Total length of the packet to be sent to the Controller */
        size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + cmd[PLEN]);

        /* Send HCI Command packet to Controller */
        err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
        if ( err != size) {
            ALOGE("Failed to send the patch payload to the Controller!");
            goto error;
        }

        /* Read Command Complete Event */
        err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
        if ( err < 0) {
            ALOGE("%s: Failed to downlaod patch segment: %d!",
                __FUNCTION__, index);
            goto error;
        }

        ALOGI("%s: Successfully downloaded patch segment: %d",
        __FUNCTION__, index);
    }

error:
    return err;
}

static int rome_download_rampatch(int fd)
{
    int c, tmp, size, index, ret = -1;

    ALOGI("%s: ", __FUNCTION__);

    /* Get handle to the RAMPATCH binary file */
    ALOGI("%s: Getting handle to the RAMPATCH binary file from %s", __FUNCTION__, ROME_FW_PATH);
    file = fopen(ROME_FW_PATH, "r");
    if (file == NULL) {
        ALOGE("%s: Failed to get handle to the RAMPATCH bin file!",
        __FUNCTION__);
        return -ENFILE;
    }

    /* Allocate memory for the patch headder info */
    ALOGI("%s: Allocating memory for the patch header", __FUNCTION__);
    phdr_buffer = (unsigned char *) malloc(PATCH_HDR_LEN + 1);
    if (phdr_buffer == NULL) {
        ALOGE("%s: Failed to allocate memory for patch header",
        __FUNCTION__);
        goto phdr_alloc_failed;
    }
    for (index = 0; index < PATCH_HDR_LEN + 1; index++)
        phdr_buffer[index] = 0x0;

    /* Read 28 bytes of patch header information */
    ALOGI("%s: Reading patch header info", __FUNCTION__);
    index = 0;
    do {
        c = fgetc (file);
        phdr_buffer[index++] = (unsigned char)c;
    } while (index != PATCH_HDR_LEN);

    /* Save the patch header info into local structure */
    ALOGI("%s: Saving patch hdr. info", __FUNCTION__);
    rome_extract_patch_header_info((unsigned char *)phdr_buffer);

    /* Set the patch header info onto the Controller */
    ret = rome_edl_set_patch_request(fd);
    if (ret < 0) {
        ALOGE("%s: Error setting the patchheader info!", __FUNCTION__);
        goto pdata_alloc_failed;
    }

    /* Allocate memory for the patch payload */
    ALOGI("%s: Allocating memory for patch payload", __FUNCTION__);
    size = rampatch_patch_info.patch_length;
    pdata_buffer = (unsigned char *) malloc(size+1);
    if (pdata_buffer == NULL) {
        ALOGE("%s: Failed to allocate memory for patch payload",
            __FUNCTION__);
        goto pdata_alloc_failed;
    }
    for (index = 0; index < size+1; index++)
        pdata_buffer[index] = 0x0;

    /* Read the patch data from Rampatch binary image */
    ALOGI("%s: Reading patch payload from RAMPATCH file", __FUNCTION__);
    index = 0;
    do {
        c = fgetc (file);
        pdata_buffer[index++] = (unsigned char)c;
    } while (c != EOF);

    /* Downloading patches in segments to controller */
    ret = rome_edl_patch_download_request(fd);
    if (ret < 0) {
        ALOGE("%s: Error downloading patch segments!", __FUNCTION__);
        goto cleanup;
    }
cleanup:
    free(pdata_buffer);
pdata_alloc_failed:
    free(phdr_buffer);
phdr_alloc_failed:
    fclose(file);
error:
    return ret;
}

int rome_attach_rampatch(int fd)
{
    int size, err;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

    /* Frame the HCI CMD to be sent to the Controller */
    frame_hci_cmd_pkt(cmd, EDL_PATCH_ATCH_REQ_CMD, 0,
        -1, EDL_PATCH_CMD_LEN);

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + cmd[PLEN]);

    /* Send HCI Command packet to Controller */
    err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
    if ( err != size) {
        ALOGE("Failed to attach the patch payload to the Controller!");
        goto error;
    }

    /* Read Command Complete Event */
    err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
    if ( err < 0) {
        ALOGE("%s: Failed to attach the patch segment(s)", __FUNCTION__);
        goto error;
    }
error:
    return err;
}

int rome_rampatch_reset(int fd)
{
    int size, err = 0, flags;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    struct timespec tm = { 0, 100*1000*1000 }; /* 100 ms */

    /* Frame the HCI CMD to be sent to the Controller */
    frame_hci_cmd_pkt(cmd, EDL_PATCH_RST_REQ_CMD, 0,
                                        -1, EDL_PATCH_CMD_LEN);

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + EDL_PATCH_CMD_LEN);

    /* Send HCI Command packet to Controller */
    err = do_write(fd, cmd, size);
    if (err != size) {
        ALOGE("%s: Send failed with ret value: %d", __FUNCTION__, err);
        goto error;
    }

    /*
    * Controller doesn't sends any response for the patch reset
    * command. HOST has to wait for 100ms before proceeding.
    */
    nanosleep(&tm, NULL);

error:
    return err;
}

static int bdaddr_is_set(const uint8_t *addr)
{
    int i;

    for (i = 0; i < 6; i++)
        if (addr[i])
            return TRUE;

    return FALSE;
}

/* WCN3990 has no Rome SU number: the EDL version command answers with a ROM
 * build in the 0x01xx/0x02xx range, and its patchram files are named after the
 * two low nibbles of that build (0x0201 -> crbtfw21.tlv + crnv21.bin). Resolve
 * the pair that is actually installed rather than hard-coding the package, and
 * take the rampatch and its NVM from the same SoC prefix.
 *
 * Returns 0 with rampatch_file_path/nvm_file_path filled, or -1 when nothing
 * matches the build the chip reported. */
int rome_set_cherokee_fw_paths(void)
{
    static const char *prefixes[] = {ROME_CHEROKEE_FW_PREFIXES};
    unsigned int build = ((unsigned int)chipset_ver >> 16) & 0xffff;
    unsigned int digits = (((build >> 8) & 0xf) << 4) | (build & 0xf);
    size_t i;

    for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        snprintf(cherokee_rampatch_path, sizeof(cherokee_rampatch_path),
                 ROME_CHEROKEE_FW_DIR "%sbtfw%02x.tlv", prefixes[i], digits);
        snprintf(cherokee_nvm_path, sizeof(cherokee_nvm_path),
                 ROME_CHEROKEE_FW_DIR "%snv%02x.bin", prefixes[i], digits);

        if (access(cherokee_rampatch_path, R_OK) == 0) {
            rampatch_file_path = cherokee_rampatch_path;
            nvm_file_path = cherokee_nvm_path;
            ALOGI("%s: ROM build 0x%04x -> %s", __FUNCTION__, build,
                  cherokee_rampatch_path);
            return 0;
        }
    }

    ALOGE("%s: no patchram for ROM build 0x%04x under %s", __FUNCTION__,
          build, ROME_CHEROKEE_FW_DIR);
    return -1;
}

int rome_get_tlv_file(char *file_path)
{
    FILE * pFile;
    long fileSize;
    int readSize, err = 0, total_segment, remain_size, nvm_length, nvm_index, i;
    unsigned short nvm_tag_len;
    tlv_patch_info *ptlv_header;
    tlv_nvm_hdr *nvm_ptr;
    unsigned char data_buf[PRINT_BUF_SIZE]={0,};
    unsigned char *nvm_byte_ptr;

    ALOGI("File Open (%s)", file_path);
    pFile = fopen ( file_path , "r" );
    if (pFile==NULL) {;
        ALOGE("%s File Open Fail", file_path);
        return -1;
    }

    /* Get File Size */
    fseek (pFile , 0 , SEEK_END);
    fileSize = ftell (pFile);
    rewind (pFile);

    pdata_buffer = (unsigned char*) malloc (sizeof(char)*fileSize);
    if (pdata_buffer == NULL) {
        ALOGE("Allocated Memory failed");
        fclose (pFile);
        return -1;
    }

    /* Copy file into allocated buffer */
    readSize = fread (pdata_buffer,1,fileSize,pFile);

    /* File Close */
    fclose (pFile);

    if (readSize != fileSize) {
        ALOGE("Read file size(%d) not matched with actual file size (%ld bytes)",readSize,fileSize);
        return -1;
    }

    ptlv_header = (tlv_patch_info *) pdata_buffer;

    /* To handle different event between rampatch and NVM */
    gTlv_type = ptlv_header->tlv_type;
    gTlv_dwndCfg = ptlv_header->tlv.patch.dwnd_cfg;

    if(ptlv_header->tlv_type == TLV_TYPE_PATCH){
        ALOGI("====================================================");
        ALOGI("TLV Type\t\t\t : 0x%x", ptlv_header->tlv_type);
        ALOGI("Length\t\t\t : %d bytes", (ptlv_header->tlv_length1) |
                                                    (ptlv_header->tlv_length2 << 8) |
                                                    (ptlv_header->tlv_length3 << 16));
        ALOGI("Total Length\t\t\t : %d bytes", ptlv_header->tlv.patch.tlv_data_len);
        ALOGI("Patch Data Length\t\t\t : %d bytes",ptlv_header->tlv.patch.tlv_patch_data_len);
        ALOGI("Signing Format Version\t : 0x%x", ptlv_header->tlv.patch.sign_ver);
        ALOGI("Signature Algorithm\t\t : 0x%x", ptlv_header->tlv.patch.sign_algorithm);
        ALOGI("Event Handling\t\t\t : 0x%x", ptlv_header->tlv.patch.dwnd_cfg);
        ALOGI("Reserved\t\t\t : 0x%x", ptlv_header->tlv.patch.reserved1);
        ALOGI("Product ID\t\t\t : 0x%04x\n", ptlv_header->tlv.patch.prod_id);
        ALOGI("Rom Build Version\t\t : 0x%04x\n", ptlv_header->tlv.patch.build_ver);
        ALOGI("Patch Version\t\t : 0x%04x\n", ptlv_header->tlv.patch.patch_ver);
        ALOGI("Reserved\t\t\t : 0x%x\n", ptlv_header->tlv.patch.reserved2);
        ALOGI("Patch Entry Address\t\t : 0x%x\n", (ptlv_header->tlv.patch.patch_entry_addr));
        ALOGI("====================================================");

    } else {
        /* Anything that is not the patch file is an NVM. Note that a cherokee
         * crnv*.bin reports the outer type 4, not TLV_TYPE_NVM (2) -- under
         * the old header test this branch never ran here, which silently
         * dropped both the BD Address fixup below and the tag dump. */
        nvm_length = (pdata_buffer[5]) |
                     (pdata_buffer[6] << 8) |
                     (pdata_buffer[7] << 16);
        nvm_byte_ptr = pdata_buffer + ROME_NVM_TAG_AREA_OFFSET;
        nvm_index = 0;

        ALOGI("====================================================");
        ALOGI("TLV Type\t\t\t : 0x%x, NVM tags %d bytes",
              ptlv_header->tlv_type, nvm_length);

        if(nvm_length <= 0)
            return readSize;

       while (nvm_index + (int) sizeof(tlv_nvm_hdr) <= nvm_length)
       {
            nvm_ptr = (tlv_nvm_hdr *) nvm_byte_ptr;
            nvm_tag_len = nvm_ptr->tag_len;

            /* Increase nvm_index to NVM data */
            nvm_index+=sizeof(tlv_nvm_hdr);
            nvm_byte_ptr+=sizeof(tlv_nvm_hdr);

            if (nvm_index + nvm_tag_len > nvm_length) {
                ALOGE("%s: tag %d claims %d bytes, only %d left -- stop parsing",
                      __FUNCTION__, nvm_ptr->tag_id, nvm_tag_len, nvm_length - nvm_index);
                break;
            }

            /* Write BD Address */
            if(nvm_ptr->tag_id == TAG_NUM_2){
                /* sagit/chiron keep no Bluetooth address in /persist and the
                 * bootloader passes no androidboot.btmacaddr, so the stack may
                 * hand us an all-zero address. Overwriting the tag with zeros
                 * would leave the controller with an invalid BD_ADDR and no
                 * way to be discovered -- keep the address shipped in the NVM
                 * file instead. */
                if (bdaddr_is_set(q.bdaddr)) {
                    memcpy(nvm_byte_ptr, q.bdaddr, 6);
                    ALOGI("BD Address from stack: %.02x:%.02x:%.02x:%.02x:%.02x:%.02x",
                        *nvm_byte_ptr, *(nvm_byte_ptr+1), *(nvm_byte_ptr+2),
                        *(nvm_byte_ptr+3), *(nvm_byte_ptr+4), *(nvm_byte_ptr+5));
                } else {
                    ALOGI("Stack BD Address is unset, keeping NVM default: "
                        "%.02x:%.02x:%.02x:%.02x:%.02x:%.02x",
                        *nvm_byte_ptr, *(nvm_byte_ptr+1), *(nvm_byte_ptr+2),
                        *(nvm_byte_ptr+3), *(nvm_byte_ptr+4), *(nvm_byte_ptr+5));
                }
            } else if (nvm_ptr->tag_id == EDL_TAG_ID_HCI && nvm_tag_len > 2) {
                /* The controller re-programmes its own UART from byte 2 of
                 * this tag when the patched firmware boots on the HCI Reset.
                 * crnv21.bin asks for 0x11 (3.2 Mbps) while rome_set_baudrate_req()
                 * puts the link at 3 Mbps and leaves the local port there, so
                 * the chip re-tunes out from under us at the reset and answers
                 * nothing at any rate afterwards -- which is exactly where the
                 * bring-up stops. Pin the tag to the rate we hold the port at;
                 * rome_tlv_check_data() in the vendor kernel does the same for
                 * its own oper_speed. */
                if (nvm_byte_ptr[2] != (unsigned char) BAUDRATE_3000000) {
                    ALOGI("NVM HCI tag wants baudrate 0x%02x, pinning to 3 Mbps (0x%02x)",
                          nvm_byte_ptr[2], (unsigned char) BAUDRATE_3000000);
                    nvm_byte_ptr[2] = (unsigned char) BAUDRATE_3000000;
                }

                /* Byte 0 bit 7 is what the vendor kernel's own comment calls
                 * "enabling software inband sleep onto controller side". With it
                 * set the chip parks its UART as soon as the host goes quiet and
                 * then has to wake it in band before delivering anything: on the
                 * run of 2026-10-29 the adapter reached ON at 41.2 s and from
                 * 42.886 s the port produced 0xfd (HCI_IBS_WAKE_IND) every 10 ms
                 * for the rest of the enable, 2445 of them. KitKat's h4 read
                 * thread does not implement HCI_IBS -- it logs "Unknown HCI
                 * message type drop this byte 0xfd" and never answers the 0xfc
                 * wake ack -- so every event behind that handshake stays unsent,
                 * the next command (Read_Local_OOB_Data 0x200b) times out twice,
                 * BTLD kills the Bluetooth process and the chip is left running
                 * the patch with no host. There is no bt_wake GPIO on this port
                 * either, so keep the transport permanently awake. */
                if (nvm_byte_ptr[0] & HCI_IBS_ENABLE_MASK) {
                    ALOGI("NVM HCI tag byte0 0x%02x has in-band sleep, clearing to 0x%02x",
                          nvm_byte_ptr[0],
                          nvm_byte_ptr[0] & (unsigned char) ~HCI_IBS_ENABLE_MASK);
                    nvm_byte_ptr[0] &= (unsigned char) ~HCI_IBS_ENABLE_MASK;
                }
            } else if (nvm_ptr->tag_id == EDL_TAG_ID_DEEP_SLEEP && nvm_tag_len > 0) {
                /* One level further than the IBS bit: this mask lets the
                 * controller drop its sleepable clocks outright, and coming out
                 * of that state is signalled on bt_wake, which this port does
                 * not route. crnv21.bin ships 0x01 and the vendor kernel ORs
                 * the same bit in for its own hci_qca. */
                if (nvm_byte_ptr[0] & HCI_DEEP_SLEEP_ENABLE_MASK) {
                    ALOGI("NVM deep-sleep tag 0x%02x, disabling", nvm_byte_ptr[0]);
                    nvm_byte_ptr[0] &= (unsigned char) ~HCI_DEEP_SLEEP_ENABLE_MASK;
                }
            }

            /* Only the tags that decide whether the chip still talks to us:
             * a full dump is 40 tags and the KitKat kmsg ring holds far less. */
            if (nvm_ptr->tag_id == TAG_NUM_2 || nvm_ptr->tag_id == EDL_TAG_ID_HCI ||
                nvm_ptr->tag_id == EDL_TAG_ID_DEEP_SLEEP) {
                for(i = 0;(i<nvm_tag_len && (i*3 + 2) <PRINT_BUF_SIZE);i++)
                    snprintf((char *) data_buf, PRINT_BUF_SIZE, "%s%.02x ", (char *)data_buf, *(nvm_byte_ptr + i));
                ALOGI("TAG %d (%d bytes) : %s", nvm_ptr->tag_id, nvm_tag_len, (char *) data_buf);
                /* Clear buffer */
                memset(data_buf, 0x0, PRINT_BUF_SIZE);
            }

            /* increased by tag_len */
            nvm_index+=nvm_tag_len;
            nvm_byte_ptr +=nvm_tag_len;
        }

        ALOGI("Parsed %d NVM tags", nvm_index);
        ALOGI("====================================================");
    }

    return readSize;
}

int rome_tlv_dnld_segment(int fd, int index, int seg_size, unsigned char wait_cc_evt)
{
    int size=0, err = -1;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

    ALOGI("%s: Downloading TLV Patch segment no.%d, size:%d", __FUNCTION__, index, seg_size);

    /* Frame the HCI CMD PKT to be sent to Controller*/
    frame_hci_cmd_pkt(cmd, EDL_PATCH_TLV_REQ_CMD, 0, index, seg_size);

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + cmd[PLEN]);

    /* Initialize the RSP packet everytime to 0 */
    memset(rsp, 0x0, HCI_MAX_EVENT_SIZE);

    /* Send HCI Command packet to Controller */
    err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
    if ( err != size) {
        ALOGE("Failed to send the patch payload to the Controller! 0x%x", err);
        return err;
    }

    if(wait_cc_evt) {
        err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
        if ( err < 0) {
            ALOGE("%s: Failed to downlaod patch segment: %d!",  __FUNCTION__, index);
            return err;
        }
    }

    ALOGI("%s: Successfully downloaded patch segment: %d", __FUNCTION__, index);
    return err;
}

int rome_tlv_dnld_req(int fd, int tlv_size)
{
    int  total_segment, remain_size, i, err = -1;
    unsigned char wait_cc_evt = TRUE;

    total_segment = tlv_size/MAX_SIZE_PER_TLV_SEGMENT;
    remain_size = (tlv_size < MAX_SIZE_PER_TLV_SEGMENT)?\
        tlv_size: (tlv_size%MAX_SIZE_PER_TLV_SEGMENT);

    ALOGI("%s: TLV size: %d, Total Seg num: %d, remain size: %d",
        __FUNCTION__,tlv_size, total_segment, remain_size);

    if (gTlv_type == TLV_TYPE_PATCH) {
       /* Prior to Rome version 3.2(including inital few rampatch release of Rome 3.2), the event
        * handling mechanism is ROME_SKIP_EVT_NONE. After few release of rampatch for Rome 3.2, the
        * mechamism is changed to ROME_SKIP_EVT_VSE_CC. Rest of the mechanism is not used for now
        */
       switch(gTlv_dwndCfg)
       {
           case ROME_SKIP_EVT_NONE:
              wait_vsc_evt = TRUE;
              wait_cc_evt = TRUE;
              ALOGI("Event handling type: ROME_SKIP_EVT_NONE");
              break;
           case ROME_SKIP_EVT_VSE_CC:
              wait_vsc_evt = FALSE;
              wait_cc_evt = FALSE;
              ALOGI("Event handling type: ROME_SKIP_EVT_VSE_CC");
              break;
           /* Not handled for now */
           case ROME_SKIP_EVT_VSE:
           case ROME_SKIP_EVT_CC:
           default:
              ALOGE("Unsupported Event handling: %d", gTlv_dwndCfg);
              break;
       }
    } else {
        wait_vsc_evt = TRUE;
        wait_cc_evt = TRUE;
    }

    for(i=0;i<total_segment ;i++){
        if ((i+1) == total_segment) {
             /* The event scheme is carried by the rampatch header itself
              * (dwnd_cfg); the Rome SU comparison the donor used here never
              * matches a cherokee build, which reports 0x01xx/0x02xx.
              */
             if ((gTlv_type == TLV_TYPE_PATCH) && (gTlv_dwndCfg == ROME_SKIP_EVT_NONE)) {
               /* If the Rome version is from 1.1 to 3.1 (and WCN3990 with
                * crbtfw11)
                * 1. No CCE for the last command segment but all other segment
                * 2. All the command segments get VSE including the last one
                */
                wait_cc_evt = !remain_size ? FALSE: TRUE;
             } else if ((gTlv_type == TLV_TYPE_PATCH) && (gTlv_dwndCfg == ROME_SKIP_EVT_VSE_CC)) {
                /* If the Rome version is 3.2 (and WCN3990 with crbtfw20/21)
                 * 1. None of the command segments receive CCE
                 * 2. No command segments receive VSE except the last one
                 */
                 wait_vsc_evt = !remain_size ? TRUE: FALSE;
             }
        }

        patch_dnld_pending = TRUE;
        if((err = rome_tlv_dnld_segment(fd, i, MAX_SIZE_PER_TLV_SEGMENT, wait_cc_evt )) < 0)
            goto error;
        patch_dnld_pending = FALSE;
    }

    if ((gTlv_type == TLV_TYPE_PATCH) && (gTlv_dwndCfg == ROME_SKIP_EVT_NONE)) {
       /* If the Rome version is from 1.1 to 3.1 (and WCN3990 with crbtfw11)
        * 1. No CCE for the last command segment but all other segment
        * 2. All the command segments get VSE including the last one
        */
        wait_cc_evt = remain_size ? FALSE: TRUE;
    } else if ((gTlv_type == TLV_TYPE_PATCH) && (gTlv_dwndCfg == ROME_SKIP_EVT_VSE_CC)) {
        /* If the Rome version is 3.2 (and WCN3990 with crbtfw20/21)
         * 1. None of the command segments receive CCE
         * 2. No command segments receive VSE except the last one
         */
        wait_vsc_evt = remain_size ? TRUE: FALSE;
    }
    patch_dnld_pending = TRUE;
    if(remain_size) err =rome_tlv_dnld_segment(fd, i, remain_size, wait_cc_evt);
    patch_dnld_pending = FALSE;
error:
    if(patch_dnld_pending) patch_dnld_pending = FALSE;
    return err;
}

int rome_download_tlv_file(int fd)
{
    int tlv_size, err = -1;

    /* Rampatch TLV file Downloading */
    pdata_buffer = NULL;
    if((tlv_size = rome_get_tlv_file(rampatch_file_path)) < 0)
        goto error;

    if((err =rome_tlv_dnld_req(fd, tlv_size)) <0 )
        goto error;

    if (pdata_buffer != NULL){
        free (pdata_buffer);
        pdata_buffer = NULL;
    }

    /* The chip applies the patch before it will accept the NVM; qca_uart_setup_rome()
     * in drivers/bluetooth/btqca.c puts the same gap between the two. */
    usleep(10000);

nvm_download:
    if(!nvm_file_path) {
        ALOGI("%s: nvm file is not available", __FUNCTION__);
        err = 0; // in case of nvm/rampatch is not available
        goto error;
    }

   /* NVM TLV file Downloading */
    if((tlv_size = rome_get_tlv_file(nvm_file_path)) <= 0)
        goto error;

    if((err =rome_tlv_dnld_req(fd, tlv_size)) <0 )
        goto error;

error:
    if (pdata_buffer != NULL)
        free (pdata_buffer);

    return err;
}

int rome_1_0_nvm_tag_dnld(int fd)
{
    int i, size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

#if (NVM_VERSION >= ROME_1_0_100019)
    unsigned char cmds[MAX_TAG_CMD][HCI_MAX_CMD_SIZE] =
    {
        /* Tag 2 */ /* BD Address */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     9,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     2,
            /* Tag Len */      6,
            /* Tag Value */   0x77,0x78,0x23,0x01,0x56,0x22
         },
        /* Tag 6 */ /* Bluetooth Support Features */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     11,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     6,
            /* Tag Len */      8,
            /* Tag Value */   0xFF,0xFE,0x8B,0xFE,0xD8,0x3F,0x5B,0x8B
         },
        /* Tag 17 */ /* HCI Transport Layer Setting */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     11,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     17,
            /* Tag Len */      8,
            /* Tag Value */   0x82,0x01,0x0E,0x08,0x04,0x32,0x0A,0x00
         },
        /* Tag 35 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     58,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     35,
            /* Tag Len */      55,
            /* Tag Value */   0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x58, 0x59,
                                      0x0E, 0x0E, 0x16, 0x16, 0x16, 0x1E, 0x26, 0x5F, 0x2F, 0x5F,
                                      0x0E, 0x0E, 0x16, 0x16, 0x16, 0x1E, 0x26, 0x5F, 0x2F, 0x5F,
                                      0x0C, 0x18, 0x14, 0x24, 0x40, 0x4C, 0x70, 0x80, 0x80, 0x80,
                                      0x0C, 0x18, 0x14, 0x24, 0x40, 0x4C, 0x70, 0x80, 0x80, 0x80,
                                      0x1B, 0x14, 0x01, 0x04, 0x48
         },
        /* Tag 36 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     15,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     36,
            /* Tag Len */      12,
            /* Tag Value */   0x0F,0x00,0x03,0x03,0x03,0x03,0x00,0x00,0x03,0x03,0x04,0x00
         },
        /* Tag 39 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     7,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     39,
            /* Tag Len */      4,
            /* Tag Value */   0x12,0x00,0x00,0x00
         },
        /* Tag 41 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     91,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     41,
            /* Tag Len */      88,
            /* Tag Value */   0x15, 0x00, 0x00, 0x00, 0xF6, 0x02, 0x00, 0x00, 0x76, 0x00,
                                      0x1E, 0x00, 0x29, 0x02, 0x1F, 0x00, 0x61, 0x00, 0x1A, 0x00,
                                      0x76, 0x00, 0x1E, 0x00, 0x7D, 0x00, 0x40, 0x00, 0x91, 0x00,
                                      0x06, 0x00, 0x92, 0x00, 0x03, 0x00, 0xA6, 0x01, 0x50, 0x00,
                                      0xAA, 0x01, 0x15, 0x00, 0xAB, 0x01, 0x0A, 0x00, 0xAC, 0x01,
                                      0x00, 0x00, 0xB0, 0x01, 0xC5, 0x00, 0xB3, 0x01, 0x03, 0x00,
                                      0xB4, 0x01, 0x13, 0x00, 0xB5, 0x01, 0x0C, 0x00, 0xC5, 0x01,
                                      0x0D, 0x00, 0xC6, 0x01, 0x10, 0x00, 0xCA, 0x01, 0x2B, 0x00,
                                      0xCB, 0x01, 0x5F, 0x00, 0xCC, 0x01, 0x48, 0x00
         },
        /* Tag 42 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     63,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     42,
            /* Tag Len */      60,
            /* Tag Value */   0xD7, 0xC0, 0x00, 0x00, 0x8F, 0x5C, 0x02, 0x00, 0x80, 0x47,
                                      0x60, 0x0C, 0x70, 0x4C, 0x00, 0x00, 0x00, 0x01, 0x1F, 0x01,
                                      0x42, 0x01, 0x69, 0x01, 0x95, 0x01, 0xC7, 0x01, 0xFE, 0x01,
                                      0x3D, 0x02, 0x83, 0x02, 0xD1, 0x02, 0x29, 0x03, 0x00, 0x0A,
                                      0x10, 0x00, 0x1F, 0x00, 0x3F, 0x00, 0x7F, 0x00, 0xFD, 0x00,
                                      0xF9, 0x01, 0xF1, 0x03, 0xDE, 0x07, 0x00, 0x00, 0x9A, 0x01
         },
        /* Tag 84 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     153,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     84,
            /* Tag Len */      150,
            /* Tag Value */   0x7C, 0x6A, 0x59, 0x47, 0x19, 0x36, 0x35, 0x25, 0x25, 0x28,
                                      0x2C, 0x2B, 0x2B, 0x28, 0x2C, 0x28, 0x29, 0x28, 0x29, 0x28,
                                      0x29, 0x29, 0x2C, 0x29, 0x2C, 0x29, 0x2C, 0x28, 0x29, 0x28,
                                      0x29, 0x28, 0x29, 0x2A, 0x00, 0x00, 0x2C, 0x2A, 0x2C, 0x18,
                                      0x98, 0x98, 0x98, 0x98, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E, 0x1E,
                                      0x1E, 0x13, 0x1E, 0x1E, 0x1E, 0x1E, 0x13, 0x13, 0x11, 0x13,
                                      0x1E, 0x1E, 0x13, 0x12, 0x12, 0x12, 0x11, 0x12, 0x1F, 0x12,
                                      0x12, 0x12, 0x10, 0x0C, 0x18, 0x0D, 0x01, 0x01, 0x01, 0x01,
                                      0x01, 0x01, 0x01, 0x0C, 0x01, 0x01, 0x01, 0x01, 0x0D, 0x0D,
                                      0x0E, 0x0D, 0x01, 0x01, 0x0D, 0x0D, 0x0D, 0x0D, 0x0F, 0x0D,
                                      0x10, 0x0D, 0x0D, 0x0D, 0x0D, 0x10, 0x05, 0x10, 0x03, 0x00,
                                      0x7E, 0x7B, 0x7B, 0x72, 0x71, 0x50, 0x50, 0x50, 0x00, 0x40,
                                      0x60, 0x60, 0x30, 0x08, 0x02, 0x0F, 0x00, 0x01, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00, 0x08, 0x16, 0x16, 0x08, 0x08, 0x00,
                                      0x00, 0x00, 0x1E, 0x34, 0x2B, 0x1B, 0x23, 0x2B, 0x15, 0x0D
         },
        /* Tag 85 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     119,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     85,
            /* Tag Len */      116,
            /* Tag Value */   0x03, 0x00, 0x38, 0x00, 0x45, 0x77, 0x00, 0xE8, 0x00, 0x59,
                                      0x01, 0xCA, 0x01, 0x3B, 0x02, 0xAC, 0x02, 0x1D, 0x03, 0x8E,
                                      0x03, 0x00, 0x89, 0x01, 0x0E, 0x02, 0x5C, 0x02, 0xD7, 0x02,
                                      0xF8, 0x08, 0x01, 0x00, 0x1F, 0x00, 0x0A, 0x02, 0x55, 0x02,
                                      0x00, 0x35, 0x00, 0x00, 0x00, 0x00, 0x2A, 0xD7, 0x00, 0x00,
                                      0x00, 0x1E, 0xDE, 0x00, 0x00, 0x00, 0x14, 0x0F, 0x0A, 0x0F,
                                      0x0A, 0x0C, 0x0C, 0x0C, 0x0C, 0x04, 0x04, 0x04, 0x0C, 0x0C,
                                      0x0C, 0x0C, 0x06, 0x06, 0x00, 0x02, 0x02, 0x02, 0x02, 0x02,
                                      0x01, 0x00, 0x02, 0x02, 0x02, 0x02, 0x01, 0x00, 0x00, 0x00,
                                      0x06, 0x0F, 0x14, 0x05, 0x47, 0xCF, 0x77, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0xAC, 0x7C, 0xFF, 0x40, 0x00, 0x00, 0x00,
                                      0x12, 0x04, 0x04, 0x01, 0x04, 0x03
         },
        {TAG_END}
    };
#elif (NVM_VERSION == ROME_1_0_6002)
    unsigned char cmds[MAX_TAG_CMD][HCI_MAX_CMD_SIZE] =
    {
        /* Tag 2 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     9,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     2,
            /* Tag Len */      6,
            /* Tag Value */   0x77,0x78,0x23,0x01,0x56,0x22 /* BD Address */
         },
        /* Tag 6 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     11,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     6,
            /* Tag Len */      8,
            /* Tag Value */   0xFF,0xFE,0x8B,0xFE,0xD8,0x3F,0x5B,0x8B
         },
        /* Tag 17 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     11,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     17,
            /* Tag Len */      8,
            /* Tag Value */   0x82,0x01,0x0E,0x08,0x04,0x32,0x0A,0x00
         },
        /* Tag 36 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     15,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     36,
            /* Tag Len */      12,
            /* Tag Value */   0x0F,0x00,0x03,0x03,0x03,0x03,0x00,0x00,0x03,0x03,0x04,0x00
         },

        /* Tag 39 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     7,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     39,
            /* Tag Len */      4,
            /* Tag Value */   0x12,0x00,0x00,0x00
         },

        /* Tag 41 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     199,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     41,
            /* Tag Len */      196,
            /* Tag Value */   0x30,0x00,0x00,0x00,0xD5,0x00,0x0E,0x00,0xD6,0x00,0x0E,0x00,
                                      0xD7,0x00,0x16,0x00,0xD8,0x00,0x16,0x00,0xD9,0x00,0x16,0x00,
                                      0xDA,0x00,0x1E,0x00,0xDB,0x00,0x26,0x00,0xDC,0x00,0x5F,0x00,
                                      0xDD,0x00,0x2F,0x00,0xDE,0x00,0x5F,0x00,0xE0,0x00,0x0E,0x00,
                                      0xE1,0x00,0x0E,0x00,0xE2,0x00,0x16,0x00,0xE3,0x00,0x16,0x00,
                                      0xE4,0x00,0x16,0x00,0xE5,0x00,0x1E,0x00,0xE6,0x00,0x26,0x00,
                                      0xE7,0x00,0x5F,0x00,0xE8,0x00,0x2F,0x00,0xE9,0x00,0x5F,0x00,
                                      0xEC,0x00,0x0C,0x00,0xED,0x00,0x08,0x00,0xEE,0x00,0x14,0x00,
                                      0xEF,0x00,0x24,0x00,0xF0,0x00,0x40,0x00,0xF1,0x00,0x4C,0x00,
                                      0xF2,0x00,0x70,0x00,0xF3,0x00,0x80,0x00,0xF4,0x00,0x80,0x00,
                                      0xF5,0x00,0x80,0x00,0xF8,0x00,0x0C,0x00,0xF9,0x00,0x18,0x00,
                                      0xFA,0x00,0x14,0x00,0xFB,0x00,0x24,0x00,0xFC,0x00,0x40,0x00,
                                      0xFD,0x00,0x4C,0x00,0xFE,0x00,0x70,0x00,0xFF,0x00,0x80,0x00,
                                      0x00,0x01,0x80,0x00,0x01,0x01,0x80,0x00,0x04,0x01,0x1B,0x00,
                                      0x05,0x01,0x14,0x00,0x06,0x01,0x01,0x00,0x07,0x01,0x04,0x00,
                                      0x08,0x01,0x00,0x00,0x09,0x01,0x00,0x00,0x0A,0x01,0x03,0x00,
                                      0x0B,0x01,0x03,0x00
         },

        /* Tag 44 */
        {  /* Packet Type */HCI_COMMAND_PKT,
            /* Opcode */       0x0b,0xfc,
            /* Total Len */     44,
            /* NVM CMD */    NVM_ACCESS_SET,
            /* Tag Num */     44,
            /* Tag Len */      41,
            /* Tag Value */   0x6F,0x0A,0x00,0x00,0x00,0x00,0x00,0x50,0xFF,0x10,0x02,0x02,
                                      0x01,0x00,0x14,0x01,0x06,0x28,0xA0,0x62,0x03,0x64,0x01,0x01,
                                      0x0A,0x00,0x00,0x00,0x00,0x00,0x00,0xA0,0xFF,0x10,0x02,0x01,
                                      0x00,0x14,0x01,0x02,0x03
         },
        {TAG_END}
    };
#endif

    ALOGI("%s: Start sending NVM Tags (ver: 0x%x)", __FUNCTION__, (unsigned int) NVM_VERSION);

    for (i=0; (i < MAX_TAG_CMD) && (cmds[i][0] != TAG_END); i++)
    {
        /* Write BD Address */
        if(cmds[i][TAG_NUM_OFFSET] == TAG_NUM_2){
            memcpy(&cmds[i][TAG_BDADDR_OFFSET], q.bdaddr, 6);
            ALOGI("BD Address: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x",
                cmds[i][TAG_BDADDR_OFFSET ], cmds[i][TAG_BDADDR_OFFSET + 1],
                cmds[i][TAG_BDADDR_OFFSET + 2], cmds[i][TAG_BDADDR_OFFSET + 3],
                cmds[i][TAG_BDADDR_OFFSET + 4], cmds[i][TAG_BDADDR_OFFSET + 5]);
        }
        size = cmds[i][3] + HCI_COMMAND_HDR_SIZE + 1;
        /* Send HCI Command packet to Controller */
        err = hci_send_vs_cmd(fd, (unsigned char *)&cmds[i][0], rsp, size);
        if ( err != size) {
            ALOGE("Failed to attach the patch payload to the Controller!");
            goto error;
        }

        /* Read Command Complete Event - This is extra routine for ROME 1.0. From ROM 2.0, it should be removed. */
        err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
        if ( err < 0) {
            ALOGE("%s: Failed to get patch version(s)", __FUNCTION__);
            goto error;
        }
    }

error:
    return err;
}



int rome_patch_ver_req(int fd)
{
    int size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

    /* Frame the HCI CMD to be sent to the Controller */
    frame_hci_cmd_pkt(cmd, EDL_PATCH_VER_REQ_CMD, 0,
    -1, EDL_PATCH_CMD_LEN);

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + EDL_PATCH_CMD_LEN);

    /* Send HCI Command packet to Controller */
    err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
    if ( err != size) {
        ALOGE("Failed to attach the patch payload to the Controller!");
        goto error;
    }

    /* Read Command Complete Event - This is extra routine for ROME 1.0. From ROM 2.0, it should be removed. */
    err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
    if ( err < 0) {
        ALOGE("%s: Failed to get patch version(s)", __FUNCTION__);
        goto error;
    }
error:
    return err;

}

int rome_get_build_info_req(int fd)
{
    int size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];

    /* Frame the HCI CMD to be sent to the Controller */
    frame_hci_cmd_pkt(cmd, EDL_GET_BUILD_INFO, 0,
    -1, EDL_PATCH_CMD_LEN);

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + EDL_PATCH_CMD_LEN);

    /* Send HCI Command packet to Controller */
    err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
    if ( err != size) {
        ALOGE("Failed to send get build info cmd to the SoC!");
        goto error;
    }

    err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
    if ( err < 0) {
        ALOGE("%s: Failed to get build info", __FUNCTION__);
        goto error;
    }
error:
    return err;

}


int rome_set_baudrate_req(int fd)
{
    int size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];
    hci_command_hdr *cmd_hdr;
    int flags;

    memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

    cmd_hdr = (void *) (cmd + 1);
    cmd[0]  = HCI_COMMAND_PKT;
    cmd_hdr->opcode = cmd_opcode_pack(HCI_VENDOR_CMD_OGF, EDL_SET_BAUDRATE_CMD_OCF);
    cmd_hdr->plen     = VSC_SET_BAUDRATE_REQ_LEN;
    cmd[4]  = BAUDRATE_3000000;

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + VSC_SET_BAUDRATE_REQ_LEN);

    /* Flow off during baudrate change */
    if ((err = userial_vendor_ioctl(USERIAL_OP_FLOW_OFF , &flags)) < 0)
    {
      ALOGE("%s: HW Flow-off error: 0x%x\n", __FUNCTION__, err);
      goto error;
    }

    /* Send the HCI command packet to UART for transmission */
    err = do_write(fd, cmd, size);
    if (err != size) {
        ALOGE("%s: Send failed with ret value: %d", __FUNCTION__, err);
        goto error;
    }

    /* The controller re-programmes its own UART when it takes this command,
     * which is not instantaneous: the in-kernel QCA driver waits
     * BAUDRATE_SETTLE_TIMEOUT_MS before it talks at the new rate (see
     * qca_set_baudrate() in drivers/bluetooth/hci_qca.c). The reply is
     * buffered by msm_serial_hs while we wait, so read_vs_hci_event() below
     * still finds it. */
    usleep(300000);

    /* Change Local UART baudrate to high speed UART */
    userial_vendor_set_baud(USERIAL_BAUD_3M);

    /* Flow on after changing local uart baudrate */
    if ((err = userial_vendor_ioctl(USERIAL_OP_FLOW_ON , &flags)) < 0)
    {
        ALOGE("%s: HW Flow-on error: 0x%x \n", __FUNCTION__, err);
        return err;
    }

    /* Check for response from the Controller */
    if ((err =read_vs_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE)) < 0) {
            ALOGE("%s: Failed to get HCI-VS Event from SOC", __FUNCTION__);
            goto error;
    }

    ALOGI("%s: Received HCI-Vendor Specific Event from SOC", __FUNCTION__);

    /* Wait for command complete event */
    err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
    if ( err < 0) {
        ALOGE("%s: Failed to set patch info on Controller", __FUNCTION__);
        goto error;
    }

error:
    return err;

}


int rome_hci_reset_req(int fd)
{
    int size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];
    hci_command_hdr *cmd_hdr;
    int flags;

    ALOGI("%s: HCI RESET ", __FUNCTION__);

    memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

    cmd_hdr = (void *) (cmd + 1);
    cmd[0]  = HCI_COMMAND_PKT;
    cmd_hdr->opcode = HCI_RESET;
    cmd_hdr->plen   = 0;

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE);

    /* Flow off during baudrate change */
    if ((err = userial_vendor_ioctl(USERIAL_OP_FLOW_OFF , &flags)) < 0)
    {
      ALOGE("%s: HW Flow-off error: 0x%x\n", __FUNCTION__, err);
      goto error;
    }

    /* Send the HCI command packet to UART for transmission */
    ALOGI("%s: HCI CMD: 0x%x 0x%x 0x%x 0x%x\n", __FUNCTION__, cmd[0], cmd[1], cmd[2], cmd[3]);
    err = do_write(fd, cmd, size);
    if (err != size) {
        ALOGE("%s: Send failed with ret value: %d", __FUNCTION__, err);
        goto error;
    }

    /* Change Local UART baudrate to high speed UART */
    userial_vendor_set_baud(USERIAL_BAUD_3M);

    /* Flow on after changing local uart baudrate */
    if ((err = userial_vendor_ioctl(USERIAL_OP_FLOW_ON , &flags)) < 0)
    {
        ALOGE("%s: HW Flow-on error: 0x%x \n", __FUNCTION__, err);
        return err;
    }

    /* Wait for command complete event */
    err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
    if ( err < 0) {
        ALOGE("%s: Failed to set patch info on Controller", __FUNCTION__);
        goto error;
    }

error:
    return err;

}


/* The framework gives the bring-up about 7.5 s between the build-info read and
 * the teardown of a failed enable (measured 2026-10-28: HCI RESET at 37.947 s,
 * bt_powerup("off") at 45.524 s). Closing the UART in that teardown makes every
 * later read return EBADF, so the tail has to reach its conclusion inside the
 * window. */
#define HCI_PROBE_MS         500
/* Poll slice. The Command Complete is searched for in everything received so far
 * after every read, so the slice only sets how often we look, not how long we
 * are prepared to wait. */
#define HCI_RESET_POLL_MS    200

/* Budget for the "is the patch already running" question that rome_soc_init()
 * asks before anything else. bluedroid kills the enable 3 s after FW_CFG starts
 * (PRELOAD_START_TIMEOUT_MS, with PRELOAD_MAX_RETRY_ATTEMPTS 0 in KitKat's
 * main/bte_main.c), so this pre-check has to stay far below that. A patched
 * controller answered in 7.5 ms (measured 2026-10-29), so the pre-check costs a
 * cold boot 200 ms of draining plus one window and still leaves 2.5 s for the
 * download, which took 1.42 s (39.591 s open -> 41.014 s reset done). */
#define HCI_WARM_PROBE_MS    200

/* Draining the controller's stale queue: how long the line has to stay silent
 * before it is considered empty, how long one listen lasts, and how many listens
 * bound the work in case the chip never stops talking. */
#define HCI_BACKLOG_POLL_MS  10
#define HCI_BACKLOG_QUIET_MS 20
#define HCI_BACKLOG_MAX_READS 64

/* Print the raw bytes the controller sent back. */
static void rome_log_rx(const char *tag, const unsigned char *rsp, int len)
{
    char line[3 * 96 + 1];
    int used = 0;
    int i;

    for (i = 0; i < len && used < (int)sizeof(line) - 4; i++)
        used += snprintf(line + used, sizeof(line) - used, "%02x ", rsp[i]);

    line[used] = '\0';
    ALOGI("%s: rx %d byte(s): [%s]", tag, len, line);
}

/* HCI Reset answered with a Command Complete carrying success. The reply is
 * 04 0e 04 01 03 0c 00; scan for it at any offset because a controller coming
 * out of reset can precede it with garbage. */
static bool rome_rsp_is_reset_cc(const unsigned char *rsp, int len)
{
    int i;

    for (i = 0; i + 7 <= len; i++) {
        if (rsp[i]     == HCI_EVENT_PKT &&
            rsp[i + 1] == EVT_CMD_COMPLETE &&
            rsp[i + 2] >= 4 &&
            rsp[i + 4] == (HCI_RESET & 0xff) &&
            rsp[i + 5] == (HCI_RESET >> 8) &&
            rsp[i + 6] == HCI_EV_SUCCESS)
            return TRUE;
    }

    return FALSE;
}

/* Write HCI Reset on the wire. */
static int rome_hci_reset_write(int fd)
{
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    hci_command_hdr *cmd_hdr;
    int size;
    int r;

    memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

    cmd_hdr = (void *) (cmd + 1);
    cmd[0]  = HCI_COMMAND_PKT;
    cmd_hdr->opcode = HCI_RESET;
    cmd_hdr->plen   = 0;

    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE);

    r = do_write(fd, cmd, size);
    if (r != size) {
        ALOGE("%s: Send failed with ret value: %d", __FUNCTION__, r);
        return -1;
    }

    return 0;
}

/*
 * Report what the UART driver itself thinks about the two handshake lines that
 * matter here. GPIO 48 is the host RTS output and it is the controller's wake
 * input; GPIO 47 is the controller's CTS output and, with CRTSCTS set by
 * userial_vendor_open(), it gates what the kernel is willing to put on the wire
 * at all. If either side is not where the bring-up assumes it is, every command
 * below is silently swallowed by the kernel rather than sent.
 */
static void rome_log_port_state(const char *tag, int fd)
{
    struct termios t;
    unsigned int m = 0;

    if (ioctl(fd, TIOCMGET, &m) < 0) {
        ALOGE("%s: TIOCMGET failed: %s(%d)", tag, strerror(errno), errno);
        return;
    }

    if (tcgetattr(fd, &t) < 0) {
        ALOGE("%s: tcgetattr failed: %s(%d)", tag, strerror(errno), errno);
        return;
    }

    ALOGI("%s: RTS=%d CTS=%d DTR=%d DSR=%d c_cflag=0x%lx %s", tag,
          (m & TIOCM_RTS) ? 1 : 0, (m & TIOCM_CTS) ? 1 : 0,
          (m & TIOCM_DTR) ? 1 : 0, (m & TIOCM_DSR) ? 1 : 0,
          (unsigned long) t.c_cflag,
          (t.c_cflag & CRTSCTS) ? "hw-flow" : "sw/no-flow");
}

/*
 * Listen for ms and log whatever arrives. Nothing is flushed: a flush between
 * attempts is what threw away late bytes while the tail was being measured.
 *
 * Returns the byte count, or -1 if the fd went away underneath us (bluedroid
 * closes the UART when the enable times out; sweeping on against a closed fd is
 * what produced the "-1 baud" lines).
 */
static int rome_rx_collect(int fd, unsigned char *rsp, int cap, int ms, const char *tag)
{
    struct pollfd pfd;
    int len = 0;
    int waited;
    int r;

    pfd.fd = fd;
    pfd.events = POLLIN;

    for (waited = 0; waited < ms && len < cap; waited += HCI_RESET_POLL_MS) {
        pfd.revents = 0;
        r = poll(&pfd, 1, HCI_RESET_POLL_MS);
        if (r < 0)
            break;
        if (r == 0)
            continue;

        r = read(fd, rsp + len, cap - len);
        if (r <= 0) {
            ALOGE("%s: read failed: %s(%d)", tag, strerror(errno), errno);
            rome_log_rx(tag, rsp, len);
            return -1;
        }
        len += r;

        if (rome_rsp_is_reset_cc(rsp, len))
            break;
    }

    rome_log_rx(tag, rsp, len);

    return len;
}

static int rome_reset_answered(int attempt)
{
    ALOGI("%s: HCI Reset answered on attempt %d at %d baud", __FUNCTION__,
          attempt, userial_vendor_get_baud());

    return 0;
}

/*
 * Ask the ROM the question that was answered in 1 ms just before the reset, so
 * that "no bytes" after the reset can be told apart from "no bytes because the
 * controller is no longer in ROM mode". This is a raw write plus the same
 * collector used for the reset; the vendor helpers are not used here because
 * they add a second, 3 s long, wait for a Command Complete and that pushes the
 * tail past the point where bluedroid closes the UART.
 */
static int rome_edl_build_info_ask(int fd, unsigned char *rsp, int cap, int no)
{
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    char tag[32];
    int size;

    frame_hci_cmd_pkt(cmd, EDL_GET_BUILD_INFO, 0, -1, EDL_PATCH_CMD_LEN);

    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + EDL_PATCH_CMD_LEN);
    if (do_write(fd, cmd, size) != size) {
        ALOGE("%s: build-info probe did not go out", __FUNCTION__);
        return -1;
    }

    snprintf(tag, sizeof(tag), "EDL build-info #%d", no);

    return rome_rx_collect(fd, rsp, cap, HCI_PROBE_MS, tag);
}

/* HCI Reset followed by one probe window. Returns 1 if the Command Complete is
 * in the bytes just received, otherwise the byte count, or -1. */
static int rome_reset_ask(int fd, unsigned char *rsp, int cap, int no)
{
    char tag[32];
    int r;

    if (rome_hci_reset_write(fd) < 0)
        return -1;

    snprintf(tag, sizeof(tag), "reset #%d at 3M", no);

    r = rome_rx_collect(fd, rsp, cap, HCI_PROBE_MS, tag);
    if (r < 0)
        return r;

    return rome_rsp_is_reset_cc(rsp, r) ? 1 : r;
}

/*
 * Final step of the cherokee bring-up: reset the controller so it runs the patch
 * that was just downloaded.
 *
 * The run of 2026-10-29 (PID 5277) reopens the whole question:
 *
 *   before reset : RTS=1 CTS=1 DTR=1 DSR=1 c_cflag=0x80001cbd hw-flow
 *   reset #1     : rx 4 byte(s) [c0 20 f8 fe]   (2.5 s window)
 *   reset #2     : rx 0 byte(s)                 (1 s window, 250 ms later)
 *   EDL probe    : rx 2 byte(s) [fd fd]
 *
 * So after two resets the controller is still in ROM mode and still replies to
 * EDL -- but with 2 bytes where the same command had produced a complete 39-byte
 * vendor event 3.5 s earlier. Everything received since the first reset is a
 * handful of stray bytes, and stray bytes are not a resampled frame: a stream
 * read at the wrong rate still yields about as many bytes as were sent, not a
 * twentieth of them.
 *
 * Interleave instead of blocking: EDL, reset, EDL, reset, EDL. That separates
 * "the reset broke the receive path" from "the receive path was already broken
 * before the reset" -- every run so far sent the reset first and only asked EDL
 * afterwards, so it cannot tell those apart. Each step logs its own raw bytes.
 */
int rome_hci_reset(int fd)
{
    unsigned char reset[96];
    unsigned char edl[96];
    int step;
    int r;

    ALOGI("%s: HCI RESET", __FUNCTION__);

    rome_log_port_state("before reset", fd);

    userial_vendor_set_baud(USERIAL_BAUD_3M);

    for (step = 1; step <= 3; step++) {
        r = rome_edl_build_info_ask(fd, edl, (int)sizeof(edl), step);
        if (r < 0)
            return -1;

        r = rome_reset_ask(fd, reset, (int)sizeof(reset), step);
        if (r < 0)
            return -1;
        if (r == 1)
            return rome_reset_answered(step);
    }

    ALOGE("%s: HCI Reset got no Command Complete", __FUNCTION__);
    return -1;
}

/*
 * Throw away everything the controller had queued.
 *
 * The run of 2026-10-29 shows why this is needed: the enable that started on a
 * patched chip read 96 bytes back for its very first reset -- the Command
 * Complete followed by the stale ACL and event traffic the chip had accumulated
 * while no host was reading -- and then read 96 more bytes of that same backlog
 * for the reset that was supposed to confirm it. The confirming reply was behind
 * the buffer, the probe concluded "no patch" and the EDL download it fell through
 * to can never succeed on a patched chip, so the enable died.
 *
 * Drain until the line is quiet, so that what comes back after the next command
 * can only be the answer to it.
 */
static void rome_drain_backlog(int fd)
{
    unsigned char junk[256];
    struct pollfd pfd;
    int quiet_ms = 0;
    int spins = 0;
    int r;

    pfd.fd = fd;
    pfd.events = POLLIN;

    while (quiet_ms < HCI_BACKLOG_QUIET_MS && spins++ < HCI_BACKLOG_MAX_READS) {
        pfd.revents = 0;

        r = poll(&pfd, 1, HCI_BACKLOG_POLL_MS);
        if (r < 0)
            return;
        if (r == 0) {
            quiet_ms += HCI_BACKLOG_POLL_MS;
            continue;
        }
        if (read(fd, junk, sizeof(junk)) <= 0)
            return;

        quiet_ms = 0;
    }
}

/*
 * Decide whether this enable starts on a controller that already runs its patch.
 *
 * The EDL ROM protocol rome_patch_ver_req() speaks is only answered while the chip
 * runs its boot ROM. On sagit/chiron a bt_power "off" that lasts less than about a
 * second does not clear the controller -- measured on 2026-10-29: the chip was off
 * for 0.79 s between two enables and still ran its patch, while a 2.35 s gap had
 * put it back into ROM mode -- so such an enable answers no EDL command at all and
 * rome_patch_ver_req() waits out its full 3 s HCI_RESPONSE_TIMEOUT_MS. That is not
 * a cosmetic delay: 3 s is exactly bluedroid's preload watchdog, so the watchdog
 * fires (main/bte_main.c, PRELOAD_MAX_RETRY_ATTEMPTS 0 -> BT_EVT_HARDWARE_INIT_FAIL)
 * before the HAL is done and the enable is torn down even when the chip turns out
 * to be healthy. That is what the user sees as "it turns on once or twice, then
 * never again until a reboot".
 *
 * Ask instead the question only a patched controller can answer, and ask it first:
 * a controller running its patch replies to HCI Reset with a Command Complete at
 * 3 Mbps within milliseconds (measured 7.5 ms). Only if nothing answers go down to
 * 115200 and talk to the boot ROM.
 */
static int rome_chip_already_patched(int fd)
{
    unsigned char rsp[512];
    int r;

    userial_vendor_set_baud(USERIAL_BAUD_3M);

    if (rome_hci_reset_write(fd) < 0)
        return -1;

    /* Disturb the chip into emptying its queue, wait for that to stop arriving,
     * and only then take the answer we actually intend to read. */
    rome_drain_backlog(fd);

    if (rome_hci_reset_write(fd) < 0)
        return -1;

    r = rome_rx_collect(fd, rsp, (int)sizeof(rsp), HCI_WARM_PROBE_MS, "warm reset");
    if (r < 0)
        return -1;

    if (rome_rsp_is_reset_cc(rsp, r)) {
        rome_log_port_state("after warm reset", fd);
        ALOGI("%s: the patch is still running, skipping the download", __FUNCTION__);
        return rome_reset_answered(1);
    }

    /* Either a controller in ROM mode, which the caller has time left to download
     * into, or no controller at all. */
    userial_vendor_set_baud(USERIAL_BAUD_115200);

    return -1;
}

int rome_wipower_current_charging_status_req(int fd)
{
    int size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];
    hci_command_hdr *cmd_hdr;
    int flags;

    memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

    cmd_hdr = (void *) (cmd + 1);
    cmd[0]  = HCI_COMMAND_PKT;
    cmd_hdr->opcode = cmd_opcode_pack(HCI_VENDOR_CMD_OGF, EDL_WIPOWER_VS_CMD_OCF);
    cmd_hdr->plen     = EDL_WIP_QUERY_CHARGING_STATUS_LEN;
    cmd[4]  = EDL_WIP_QUERY_CHARGING_STATUS_CMD;

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + EDL_WIP_QUERY_CHARGING_STATUS_LEN);

    ALOGD("%s: Sending EDL_WIP_QUERY_CHARGING_STATUS_CMD", __FUNCTION__);
    ALOGD("HCI-CMD: \t0x%x \t0x%x \t0x%x \t0x%x \t0x%x", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4]);

    err = hci_send_wipower_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
    if ( err != size) {
        ALOGE("Failed to send EDL_WIP_QUERY_CHARGING_STATUS_CMD command!");
        goto error;
    }

    /* Check for response from the Controller */
    if (read_vs_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE) < 0) {
        err = -ETIMEDOUT;
        ALOGI("%s: WP Failed to get HCI-VS Event from SOC", __FUNCTION__);
        goto error;
    }

    /* Read Command Complete Event - This is extra routine for ROME 1.0. From ROM 2.0, it should be removed. */
    if (rsp[4] >= NON_WIPOWER_MODE) {
        err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
        if (err < 0) {
            ALOGE("%s: Failed to get charging status", __FUNCTION__);
            goto error;
        }
    }

error:
    return err;
}

int addon_feature_req(int fd)
{
    int size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];
    hci_command_hdr *cmd_hdr;
    int flags;

    memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

    cmd_hdr = (void *) (cmd + 1);
    cmd[0]  = HCI_COMMAND_PKT;
    cmd_hdr->opcode = cmd_opcode_pack(HCI_VENDOR_CMD_OGF, HCI_VS_GET_ADDON_FEATURES_SUPPORT);
    cmd_hdr->plen     = 0x00;

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE);

    ALOGD("%s: Sending HCI_VS_GET_ADDON_FEATURES_SUPPORT", __FUNCTION__);
    ALOGD("HCI-CMD: \t0x%x \t0x%x \t0x%x \t0x%x", cmd[0], cmd[1], cmd[2], cmd[3]);
    err = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
    if ( err != size) {
        ALOGE("Failed to send HCI_VS_GET_ADDON_FEATURES_SUPPORT command!");
        goto error;
    }

    err = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
    if (err < 0) {
        ALOGE("%s: Failed to get feature request", __FUNCTION__);
        goto error;
    }
error:
    return err;
}


int check_embedded_mode(int fd) {
    int err = 0;

    wipower_flag = 0;
    /* Get current wipower charging status */
    if ((err = rome_wipower_current_charging_status_req(fd)) < 0)
    {
        ALOGI("%s: Wipower status req failed (0x%x)", __FUNCTION__, err);
    }
    usleep(500);

    ALOGE("%s: wipower_flag: %d", __FUNCTION__, wipower_flag);

    return wipower_flag;
}

int rome_get_addon_feature_list(int fd) {
    int err = 0;

    /* Get addon features that are supported by FW */
    if ((err = addon_feature_req(fd)) < 0)
    {
        ALOGE("%s: failed (0x%x)", __FUNCTION__, err);
    }
    return err;
}

int rome_wipower_forward_handoff_req(int fd)
{
    int size, err = 0;
    unsigned char cmd[HCI_MAX_CMD_SIZE];
    unsigned char rsp[HCI_MAX_EVENT_SIZE];
    hci_command_hdr *cmd_hdr;
    int flags;

    memset(cmd, 0x0, HCI_MAX_CMD_SIZE);

    cmd_hdr = (void *) (cmd + 1);
    cmd[0]  = HCI_COMMAND_PKT;
    cmd_hdr->opcode = cmd_opcode_pack(HCI_VENDOR_CMD_OGF, EDL_WIPOWER_VS_CMD_OCF);
    cmd_hdr->plen     = EDL_WIP_START_HANDOFF_TO_HOST_LEN;
    cmd[4]  = EDL_WIP_START_HANDOFF_TO_HOST_CMD;

    /* Total length of the packet to be sent to the Controller */
    size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + EDL_WIP_START_HANDOFF_TO_HOST_LEN);

    ALOGD("%s: Sending EDL_WIP_START_HANDOFF_TO_HOST_CMD", __FUNCTION__);
    ALOGD("HCI-CMD: \t0x%x \t0x%x \t0x%x \t0x%x \t0x%x", cmd[0], cmd[1], cmd[2], cmd[3], cmd[4]);
    err = hci_send_wipower_vs_cmd(fd, (unsigned char *)cmd, rsp, size);
    if ( err != size) {
        ALOGE("Failed to send EDL_WIP_START_HANDOFF_TO_HOST_CMD command!");
        goto error;
    }

    /* Check for response from the Controller */
    if (read_vs_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE) < 0) {
        err = -ETIMEDOUT;
        ALOGI("%s: WP Failed to get HCI-VS Event from SOC", __FUNCTION__);
        goto error;
    }

error:
    return err;
}


void enable_controller_log (int fd, unsigned char wait_for_evt)
{
   int ret = 0;
   /* VS command to enable controller logging to the HOST. By default it is disabled */
   unsigned char cmd[6] = {0x01, 0x17, 0xFC, 0x02, 0x00, 0x00};
   unsigned char rsp[HCI_MAX_EVENT_SIZE];
   char value[PROPERTY_VALUE_MAX] = {'\0'};

   property_get("persist.service.bdroid.soclog", value, "false");

   // value at cmd[5]: 1 - to enable, 0 - to disable
   ret = (strcmp(value, "true") == 0) ? cmd[5] = 0x01: 0;
   ALOGI("%s: %d", __func__, ret);
   /* Ignore vsc evt if wait_for_evt is true */
   if (wait_for_evt) wait_vsc_evt = FALSE;

   ret = hci_send_vs_cmd(fd, (unsigned char *)cmd, rsp, 6);
   if (ret != 6) {
       ALOGE("%s: command failed", __func__);
   }
   /*Ignore hci_event if wait_for_evt is true*/
   if (wait_for_evt)
       goto end;
   ret = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
   if (ret < 0) {
       ALOGE("%s: Failed to get CC for enable SoC log", __FUNCTION__);
   }
end:
   wait_vsc_evt = TRUE;
   return;
}

static int disable_internal_ldo(int fd)
{
    int ret = 0;
    if (q.enable_extldo) {
        unsigned char cmd[5] = {0x01, 0x0C, 0xFC, 0x01, 0x32};
        unsigned char rsp[HCI_MAX_EVENT_SIZE];

        ALOGI(" %s ", __FUNCTION__);
        ret = do_write(fd, cmd, 5);
        if (ret != 5) {
            ALOGE("%s: Send failed with ret value: %d", __FUNCTION__, ret);
            ret = -1;
        } else {
            /* Wait for command complete event */
            ret = read_hci_event(fd, rsp, HCI_MAX_EVENT_SIZE);
            if ( ret < 0) {
                ALOGE("%s: Failed to get response from controller", __FUNCTION__);
            }
        }
    }
    return ret;
}

int rome_soc_init(int fd, char *bdaddr)
{
    int err = -1, size = 0;
    dnld_fd = fd;
    ALOGI(" %s ", __FUNCTION__);
    RESERVED(bdaddr);

    /* If wipower charging is going on in embedded mode then start hand off req */
    if (wipower_flag == WIPOWER_IN_EMBEDDED_MODE && wipower_handoff_ready != NON_WIPOWER_MODE)
    {
        wipower_flag = 0;
        wipower_handoff_ready = 0;
        if ((err = rome_wipower_forward_handoff_req(fd)) < 0)
        {
            ALOGI("%s: Wipower handoff failed (0x%x)", __FUNCTION__, err);
        }
    }

    /* A controller that still runs the patch from the previous enable answers no
     * EDL command, and waiting for one uses up all the time bluedroid allows for
     * the preload. Ask the patched chip first; it answers in milliseconds. */
    err = rome_chip_already_patched(fd);
    if (err >= 0)
        goto done;

    /* Get Rome version information */
    if((err = rome_patch_ver_req(fd)) <0){
        ALOGI("%s: Fail to get Rome Version (0x%x)", __FUNCTION__, err);
        goto error;
    }

    ALOGI("%s: Chipset Version (0x%08x)", __FUNCTION__, chipset_ver);

    switch (chipset_ver){
        case ROME_VER_1_0:
            {
                /* Set and Download the RAMPATCH */
                ALOGI("%s: Setting Patch Header & Downloading Patches", __FUNCTION__);
                err = rome_download_rampatch(fd);
                if (err < 0) {
                    ALOGE("%s: DOWNLOAD RAMPATCH failed!", __FUNCTION__);
                    goto error;
                }
                ALOGI("%s: DOWNLOAD RAMPTACH complete", __FUNCTION__);

                /* Attach the RAMPATCH */
                ALOGI("%s: Attaching the patches", __FUNCTION__);
                err = rome_attach_rampatch(fd);
                if (err < 0) {
                    ALOGE("%s: ATTACH RAMPATCH failed!", __FUNCTION__);
                    goto error;
                }
                ALOGI("%s: ATTACH RAMPTACH complete", __FUNCTION__);

                /* Send Reset */
                size = (HCI_CMD_IND + HCI_COMMAND_HDR_SIZE + EDL_PATCH_CMD_LEN);
                err = rome_rampatch_reset(fd);
                if ( err < 0 ) {
                    ALOGE("Failed to RESET after RAMPATCH upgrade!");
                    goto error;
                }

                /* NVM download */
                ALOGI("%s: Downloading NVM", __FUNCTION__);
                err = rome_1_0_nvm_tag_dnld(fd);
                if ( err <0 ) {
                    ALOGE("Downloading NVM Failed !!");
                    goto error;
                }

                /* Change baud rate 115.2 kbps to 3Mbps*/
                err = rome_hci_reset_req(fd);
                if (err < 0) {
                    ALOGE("HCI Reset Failed !!");
                    goto error;
                }

                ALOGI("HCI Reset is done\n");
            }
            break;
        case ROME_VER_1_1:
            rampatch_file_path = ROME_RAMPATCH_TLV_PATH;
            nvm_file_path = ROME_NVM_TLV_PATH;
            goto download;
        case ROME_VER_1_3:
            rampatch_file_path = ROME_RAMPATCH_TLV_1_0_3_PATH;
            nvm_file_path = ROME_NVM_TLV_1_0_3_PATH;
            goto download;
        case ROME_VER_2_1:
            rampatch_file_path = ROME_RAMPATCH_TLV_2_0_1_PATH;
            nvm_file_path = ROME_NVM_TLV_2_0_1_PATH;
            goto download;
        case ROME_VER_3_0:
            rampatch_file_path = ROME_RAMPATCH_TLV_3_0_0_PATH;
            nvm_file_path = ROME_NVM_TLV_3_0_0_PATH;
            fw_su_info = ROME_3_1_FW_SU;
            fw_su_offset = ROME_3_1_FW_SW_OFFSET;
            goto download;
        case ROME_VER_3_2:
            rampatch_file_path = ROME_RAMPATCH_TLV_3_0_2_PATH;
            nvm_file_path = ROME_NVM_TLV_3_0_2_PATH;
            fw_su_info = ROME_3_2_FW_SU;
            fw_su_offset =  ROME_3_2_FW_SW_OFFSET;

download:
            /* Change baud rate 115.2 kbps to 3Mbps*/
            err = rome_set_baudrate_req(fd);
            if (err < 0) {
                ALOGE("%s: Baud rate change failed!", __FUNCTION__);
                goto error;
            }
            ALOGI("%s: Baud rate changed successfully ", __FUNCTION__);
            /* Donwload TLV files (rampatch, NVM) */
            err = rome_download_tlv_file(fd);
            if (err < 0) {
                ALOGE("%s: Download TLV file failed!", __FUNCTION__);
                goto error;
            }
            ALOGI("%s: Download TLV file successfully ", __FUNCTION__);

            /* Get SU FM label information */
            if((err = rome_get_build_info_req(fd)) <0){
                ALOGI("%s: Fail to get Rome FW SU Build info (0x%x)", __FUNCTION__, err);
                //Ignore the failure of ROME FW SU label information
                err = 0;
            }

            /* Disable internal LDO to use external LDO instead*/
            err = disable_internal_ldo(fd);

            /* Send HCI Reset */
            err = rome_hci_reset(fd);
            if ( err <0 ) {
                ALOGE("HCI Reset Failed !!");
                goto error;
            }

            ALOGI("HCI Reset is done\n");

            break;
        default:
            /* WCN3990 (cherokee) reports a 0x01xx/0x02xx ROM build, which
             * matches none of the Rome SU enums above. Resolve its
             * crbtfw/crnv pair from the build version and run the standard
             * TLV flow; only if nothing is installed is the SoC unknown. */
            if (rome_set_cherokee_fw_paths() == 0)
                goto download;
            ALOGE("%s: Detected unknown SoC version: 0x%08x", __FUNCTION__, chipset_ver);
            err = -1;
            break;
    }

done:
error:
    dnld_fd = -1;
    return err;
}
