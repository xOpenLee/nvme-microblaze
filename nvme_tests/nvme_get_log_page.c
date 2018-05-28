/**
 * Copyright (c) 2015-2016, Micron Technology, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *   3. Neither the name of the copyright holder nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 * @brief Invoke NVMe get log page command.
 */

#include <stdio.h>
#include "nvme_tests.h"
#include "../unvme/unvme_mem.h"
#include "../unvme/unvme_nvme.h"
#include "../unvme/unvme_log.h"

static mem_device_t* memdev;
static nvme_device_t* nvmedev;
static mem_dma_t* adminsq;
static mem_dma_t* admincq;

/**
 * NVMe setup.
 */
static void nvme_setup(int pci, int aqsize, u64 mem_base_pci, void *mem_base_mb, size_t mem_size)
{
    memdev = mem_create(NULL, pci, mem_base_pci, mem_base_mb, mem_size);
    if (!memdev) errx(1, "vfio_create");

    nvmedev = nvme_create(NULL);
    if (!nvmedev) errx(1, "nvme_create");

    adminsq = mem_dma_alloc(memdev, aqsize * sizeof(nvme_sq_entry_t), 1);
    if (!adminsq) errx(1, "vfio_dma_alloc");
    admincq = mem_dma_alloc(memdev, aqsize * sizeof(nvme_cq_entry_t), 1);
    if (!admincq) errx(1, "vfio_dma_alloc");

    if (!nvme_adminq_setup(nvmedev, aqsize, adminsq->buf, adminsq->addr,
                                            admincq->buf, admincq->addr)) {
        errx(1, "nvme_setup_adminq");
    }
}

/**
 * NVMe cleanup.
 */
static void nvme_cleanup()
{
    mem_dma_free(adminsq);
    mem_dma_free(admincq);
    nvme_delete(nvmedev);
    mem_delete(memdev);
}

/**
 * Print log page error info.
 */
void print_error_info(void* buf)
{
    nvme_log_page_error_t* err = buf;

    printf("Log Page Error Information\n");
    printf("==========================\n");
    printf("Error Count              : %lld\n", err->count);
    printf("Submission Queue ID      : %d\n", err->sqid);
    printf("Command ID               : %d\n", err->cid);
    printf("Status Field             : %#x\n", err->sf);
    printf("Parameter Error Location : byte=%d bit=%d\n", err->byte, err->bit);
    printf("LBA                      : %#llx\n", err->lba);
    printf("Namespace                : %ld\n", err->ns);
    printf("Vendor Specific          : %#x\n", err->vspec);
}

/**
 * Print 128-bit value as hex into designated string dest.
 */
char* hex128(char* dest, u64 val[2])
{
    if (val[0]) sprintf(dest, "%#llx%08llx", val[0], val[1]);
    else sprintf(dest, "%lld", val[1]);
    return dest;
}

/**
 * Print log page smart health info. - something funny might be happening to these numbers, probably need endianness swapping
 */
void print_smart_health(void* buf)
{
    char s[64];
    nvme_log_page_health_t* smh = buf;

    printf("Log Page SMART / Health Information\n");
    printf("===================================\n");
    printf("Critical Warning              : %#x\n", smh->warn);
    printf("Temperature (Kelvin)          : %d\n", smh->temp);
    printf("Available Spare (%%)           : %d\n", smh->avspare);
    printf("Available Spare Threshold (%%) : %d\n", smh->avsparethresh);
    printf("Percentage Used               : %d\n", smh->used);
    printf("# of Data Units Read          : %s\n", hex128(s, smh->dur));
    printf("# of Data Units Written       : %s\n", hex128(s, smh->duw));
    printf("# of Host Read Commands       : %s\n", hex128(s, smh->hrc));
    printf("# of Host Write Commands      : %s\n", hex128(s, smh->hwc));
    printf("Controller Busy Time (mins)   : %s\n", hex128(s, smh->cbt));
    printf("# of Power Cycles             : %s\n", hex128(s, smh->pcycles));
    printf("# of Power On Hours           : %s\n", hex128(s, smh->phours));
    printf("# of Unsafe Shutdowns         : %s\n", hex128(s, smh->unsafeshut));
    printf("# of Media Errors             : %s\n", hex128(s, smh->merrors));
    printf("# of Error Log Entries        : %s\n", hex128(s, smh->errlogs));
}

/**
 * Print log page firmware slot info.
 */
void print_firmware_slot(void* buf)
{
    nvme_log_page_fw_t* fw = buf;

    printf("Log Page Firmware Slot Information\n");
    printf("==================================\n");
    int i;
    for (i = 0; i < 7; i++) {
        printf("Firmware Revision Slot %d : %.8s %s\n",
               i + 1, (char*)&fw->fr[i], fw->afi == (i + 1) ? "(active)" : "");
    }
}

/**
 * Main program.
 */
int nvme_get_log_page(int pci, int lid, int nsid, u64 mem_base_pci, void *mem_base_mb, size_t mem_size)
{
	printf("\r\n%s test starting...\r\n\n", __func__);

	// LOG_PAGE_ID 1 = error information
	// LOG_PAGE_ID 2 = SMART / Health information
	// LOG_PAGE_ID 3 = firmware slot information

    if (lid < 1 || lid > 3) {
    	printf("Invalid Log ID\r\n");
        return 1;
    }

    nvme_setup(pci, 8, mem_base_pci, mem_base_mb, mem_size);
    mem_dma_t* dma = mem_dma_alloc(memdev, 8192, 0);
    if (!dma) errx(1, "vfio_dma_alloc");

    int numd = dma->size / sizeof(u32) - 1;
    u64 prp1 = dma->addr;
    u64 prp2 = dma->addr + 4096;
    int err = nvme_acmd_get_log_page(nvmedev, nsid, lid, numd, prp1, prp2);
    if (err) errx(1, "nvme_acmd_get_log_page");

    switch (lid) {
        case 1: print_error_info(dma->buf); break;
        case 2: print_smart_health(dma->buf); break;
        case 3: print_firmware_slot(dma->buf); break;
    }

    nvme_cleanup();

    printf("\r\n%s test complete\r\n\n", __func__);
    return 0;
}

