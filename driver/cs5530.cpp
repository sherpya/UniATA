/* CS5530 initialization/timing adapted from Linux drivers/ata/pata_cs5530.c
 * (Alan Cox, Red Hat; based on Mark Lord's cs5530 driver), GPL-2.0-only.
 * Selected by the controller-quirk table for PCI 1078:0102.
 */
#include "stdafx.h"
#include "cs5530.h"

static BOOLEAN CsPciRead(PHW_DEVICE_EXTENSION ext, ULONG slot, ULONG offset,
                         PVOID buffer, ULONG length)
{
    // The old offset wrapper can report success without copying data after
    // a short HAL read. Require the complete prefix before using any bytes.
    UCHAR config[256];
    if (offset > sizeof(config) || length > sizeof(config)-offset)
        return FALSE;
    if (HalGetBusData(PCIConfiguration, ext->SystemIoBusNumber, slot,
                     config, offset+length) != offset+length)
        return FALSE;
    RtlCopyMemory(buffer, config+offset, length);
    return TRUE;
}

static BOOLEAN CsPciWrite(PHW_DEVICE_EXTENSION ext, ULONG slot, ULONG offset,
                          PVOID buffer, ULONG length)
{
    return ScsiPortSetBusDataByOffset(ext, PCIConfiguration,
        ext->SystemIoBusNumber, slot, buffer, offset, length) == length;
}

BOOLEAN UniataCs5530Init(PHW_DEVICE_EXTENSION ext)
{
    if (ext->DevID != ATA_CYRIX_5530 || !ext->BusMaster)
        return FALSE;

    /* The companion is function 0 of this controller. PCI master is on
     * the same bus. Validate both identities before touching either one.
     */
    ULONG legacy = ext->slotNumber & 0x1f;
    ULONG master = 0xffffffff, id;
    if (!CsPciRead(ext, legacy, 0, &id, 4) || id != 0x01001078)
        return FALSE;
    for (ULONG device = 0; device < 32; ++device) {
        if (CsPciRead(ext, device, 0, &id, 4) && id == 0x00011078) {
            master = device;
            break;
        }
    }
    if (master == 0xffffffff)
        return FALSE;

    USHORT command, newCommand, trap = 0x5006, gotCommand;
    UCHAR newCache = 4, gotCache;
    UCHAR after[4], wanted[4] = {0x1e, 0x14, 0x00, 0xc1};
    if (!CsPciRead(ext, legacy, 4, &command, 2))
        return FALSE;

    // Establish a valid cache-line size before requesting MWI; preserve
    // all existing PCI Command bits. Linux treats MWI support as optional.
    newCommand = command | 0x0014; // bus master + memory write invalidate
    if (!CsPciWrite(ext, legacy, 0x0c, &newCache, 1) ||
        !CsPciWrite(ext, legacy, 4, &newCommand, 2) ||
        !CsPciWrite(ext, legacy, 0xd0, &trap, 2))
        return FALSE;
    for (ULONG i = 0; i < 4; ++i) {
        if (!CsPciWrite(ext, master, 0x40+i, &wanted[i], 1))
            return FALSE;
    }
    // D0 is a firmware trap command, not a persistent status register.
    if (!CsPciRead(ext, legacy, 4, &gotCommand, 2) ||
        !CsPciRead(ext, legacy, 0x0c, &gotCache, 1) ||
        !CsPciRead(ext, master, 0x40, after, 4))
        return FALSE;
    if (!(gotCommand & 4) || gotCache != 4)
        return FALSE;
    for (ULONG i = 0; i < 4; ++i) {
        if (after[i] != wanted[i]) return FALSE;
    }
    // UniATA's existing last_cdev handling now restores the shared
    // UDMA/MWDMA bit when switching between devices on the same channel.
    ext->HwFlags |= UNIATA_CHAN_TIMINGS;
    return TRUE;
}

VOID UniataCs5530Timing(PHW_DEVICE_EXTENSION ext, ULONG device,
                        ULONG logicalChannel, ULONG pioMode, ULONG mode)
{
    static const ULONG pio[2][5] = {
        {0x00009172, 0x00012171, 0x00020080, 0x00032010, 0x00040010},
        {0xd1329172, 0x71212171, 0x30200080, 0x20102010, 0x00100010}
    };
    static const ULONG udma[3] = {0x00921250, 0x00911140, 0x00911030};
    static const ULONG mwdma[3] = {0x00077771, 0x00012121, 0x00002020};
    ULONG channel = ext->Channel + logicalChannel;
    if (ext->DevID != ATA_CYRIX_5530 || channel > 1 || device > 1 ||
        logicalChannel >= ext->NumberChannels || pioMode > 4 ||
        !ext->BaseIoAddressBM_0.Addr || ext->BaseIoAddressBM_0.MemIo)
        return;

    // A compatibility-mode secondary extension holds BAR4+8, not BAR4.
    // Normalize once, then use separate PIO (+20) and DMA (+24) registers.
    IORES io = ext->BaseIoAddressBM_0;
    io.Addr &= ~((ULONGIO_PTR)0x0f);
    ULONG base = 0x20 + channel*0x10;
    PHW_CHANNEL chan = &ext->chan[logicalChannel];
    ULONGIO_PTR port = (ULONGIO_PTR)&io;
    ULONG tuning = AtapiReadPortEx4(chan, port, base+4);
    ULONG format = (tuning >> 31) & 1;
    ULONG pioValue = pio[format][pioMode];
    AtapiWritePortEx4(chan, port, base+device*8, pioValue);

    ULONG timing;
    if (mode >= ATA_UDMA0 && mode <= ATA_UDMA2)
        timing = udma[mode-ATA_UDMA0];
    else if (mode >= ATA_WDMA0 && mode <= ATA_WDMA2)
        timing = mwdma[mode-ATA_WDMA0];
    else
        return;
    timing |= tuning & 0x80000000;
    if (device) {
        tuning = (tuning & ~0x00100000UL) | (timing & 0x00100000);
        AtapiWritePortEx4(chan, port, base+4, tuning);
    }
    AtapiWritePortEx4(chan, port, base+device*8+4, timing);
    UCHAR status = AtapiReadPortEx1(chan, port, channel*8+2);
    // Preserve read/write capability bits without acknowledging W1C IRQ/error.
    status = (status & ~0x06) | (1 << (5+device));
    AtapiWritePortEx1(chan, port, channel*8+2, status);
    KdPrint2(("AtaSetTransferMode: CS5530 timings ch=%lu dev=%lu mode=%#lx pio=%#lx dma=%#lx\n",
        channel, device, mode, pioValue, timing));
}
