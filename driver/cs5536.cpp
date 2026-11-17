/* CS5536 PATA timings based on Linux drivers/ata/pata_cs5536.c
 * (Martin K. Petersen, Bartlomiej Zolnierkiewicz), GPL-2.0-only.
 * PCI 1022:209a, one channel. Use the firmware's PCI/MSR virtualization;
 * all timing writes are aligned DWORDs, never the generic AMD byte path.
 */
#include "stdafx.h"
#include "controller_quirks.h"
#include "cs5536.h"

static BOOLEAN CsRead(PHW_DEVICE_EXTENSION e, ULONG offset, ULONG* value)
{
    ULONG config[64];
    if((offset & 3) || offset > 252) return FALSE;
    if(HalGetBusData(PCIConfiguration, e->SystemIoBusNumber, e->slotNumber,
                    config, offset + 4) != offset + 4) return FALSE;
    *value = config[offset / 4];
    return TRUE;
}

static BOOLEAN CsWrite(PHW_DEVICE_EXTENSION e, ULONG offset, ULONG value)
{
    ULONG got;
    if(ScsiPortSetBusDataByOffset(e, PCIConfiguration, e->SystemIoBusNumber,
        e->slotNumber, &value, offset, 4) != 4) return FALSE;
    return CsRead(e, offset, &got) && got == value;
}

BOOLEAN UniataCs5536Init(PHW_DEVICE_EXTENSION e)
{
    ULONG id, cfg;
    if(e->DevID != ATA_AMD5536 || e->Channel != 0 || e->NumberChannels != 1 ||
       !CsRead(e, 0, &id) || id != ATA_AMD5536 || !CsRead(e, 0x40, &cfg) ||
       !(cfg & 2)) return FALSE; // respect firmware-disabled IDE
    // Linux uses either cable-detect bit to allow ATA66/100. Do not override
    // an absent 80-wire indication or the caller's lower transfer-mode cap.
    ULONG maximum = (cfg & 0x00030000) ? ATA_UDMA5 : ATA_UDMA2;
    e->MaxTransferMode = min(e->MaxTransferMode, maximum);
    e->chan[0].MaxTransferMode = min(e->chan[0].MaxTransferMode, maximum);
    KdPrint2(("[UniATA] CS5536 PCI timing path: cfg=%#lx max=%#lx\n", cfg, maximum));
    return TRUE;
}

BOOLEAN UniataCs5536SetMode(PHW_DEVICE_EXTENSION e, ULONG device,
                           ULONG channel, ULONG pioMode, ULONG mode)
{
    static const UCHAR pio[] = {0x98, 0x55, 0x32, 0x21, 0x20};
    static const UCHAR addr[] = {2, 1, 0, 0, 0};
    static const UCHAR cmd[] = {0x99, 0x92, 0x90, 0x22, 0x20};
    static const UCHAR udma[] = {0xc2, 0xc1, 0xc0, 0xc4, 0xc5, 0xc6};
    static const UCHAR mwdma[] = {0x67, 0x21, 0x20};
    ULONG cfg, oldDtc, oldCast, oldEtc;
    if(e->DevID != ATA_AMD5536 || e->Channel != 0 || channel != 0 ||
       e->NumberChannels != 1 || device > 1 || pioMode > 4) return FALSE;
    BOOLEAN isUdma = mode >= ATA_UDMA0 && mode <= ATA_UDMA5;
    BOOLEAN isMw = mode >= ATA_WDMA0 && mode <= ATA_WDMA2;
    if(!isUdma && !isMw && mode != ATA_PIO0 + pioMode) return FALSE;
    if(!CsRead(e, 0x40, &cfg) || !(cfg & 2) ||
       !CsRead(e, 0x48, &oldDtc) || !CsRead(e, 0x4c, &oldCast) ||
       !CsRead(e, 0x50, &oldEtc)) return FALSE;
    if(isUdma && mode > ATA_UDMA2 && !(cfg & 0x00030000)) return FALSE;

    PHW_LU_EXTENSION lun = e->chan[0].lun[device];
    PHW_LU_EXTENSION peer = e->chan[0].lun[device ^ 1];
    if(!lun) return FALSE;
    ULONG commandMode = pioMode;
    // An unconfigured peer starts at PIO0: never speed shared command cycles
    // up until both devices have selected their PIO timing.
    if(peer && (peer->DeviceFlags & DFLAGS_DEVICE_PRESENT))
        commandMode = min(commandMode, min(peer->PataPioMode, 4UL));
    ULONG shift = device ? 16 : 24;
    ULONG addressShift = device ? 4 : 6;
    ULONG dtc = (oldDtc & ~(0xffUL << shift)) |
                ((ULONG)(isMw ? mwdma[mode - ATA_WDMA0] : pio[pioMode]) << shift);
    ULONG cast = (oldCast & ~((3UL << addressShift) | 0xff000000UL)) |
                 ((ULONG)addr[pioMode] << addressShift) | ((ULONG)cmd[commandMode] << 24);
    ULONG etc = oldEtc & ~(0xc0UL << shift);
    if(isUdma) etc = (oldEtc & ~(0xffUL << shift)) | ((ULONG)udma[mode - ATA_UDMA0] << shift);

    // Validate controller programming before telling the disk to use DMA.
    // On rejection restore its previous timing before trying a lower mode.
    if(!CsWrite(e, 0x48, dtc) || !CsWrite(e, 0x4c, cast) || !CsWrite(e, 0x50, etc) ||
       !AtaSetTransferMode(e, device, channel, lun, mode)) {
        CsWrite(e, 0x48, oldDtc);
        CsWrite(e, 0x4c, oldCast);
        CsWrite(e, 0x50, oldEtc);
        return FALSE;
    }
    lun->PataPioMode = pioMode;
    KdPrint2(("[UniATA] CS5536 dev=%lu mode=%#lx DTC=%#lx CAST=%#lx ETC=%#lx\n",
        device, mode, dtc, cast, etc));
    return TRUE;
}
