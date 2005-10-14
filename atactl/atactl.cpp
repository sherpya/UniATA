#include <windows.h>
#include <WinIoCtl.h>
#include <stdio.h>
//#include <ntdddisk.h>
#include <ntddscsi.h>
#include "..\driver\atapi.h"
#include "..\driver\bm_devs.h"
#include "..\driver\uata_ctl.h"
#include "..\driver\tools.h"
#include "..\driver\uniata_ver.h"

#define DEFAULT_REMOVAL_LOCK_TIMEOUT 20

void print_help() {
    printf("Usage:\n"
           "  atactl -<switches> c|s<controller id>:b<bus id>:d<device id>[:l<lun>]\n"
           "Switches:\n"
           "  l         list devices on SCSI bus(es)\n"
//           "  f         find new devices on SCSI bus(es)\n"
//           "  h         hide device from SCSI bus\n"
           "  m [MODE]  set i/o mode for device or revert to default\n"
           "            available MODEs are PIO, PIO0-PIO5, DMA, WDMA0-WDMA2,\n"
           "              UDMA33/44/66/100/133, UDMA0-UDMA5\n"
//           "  d [XXX]   lock SCSI bus for device removal for XXX seconds or\n"
//           "            for %d seconds if no lock timeout specified.\n"
//           "            can be used with -h, -m or standalone.\n"
           "  r         reset device\n"
           "Examples:\n"
           "  atactl -l\n"
           "    will list all scsi buses and all connected devices\n"
           "  atactl -m udma0 s2:b1:d1\n"
           "    will switch device at Scsi2, bus 1, taget_id 1 to UDMA0 mode"
//           "  atactl -h -r 30 c1:b0:d0:l0 \n"
//           "    will hide Master (d0:l0) device on secondary (c1:b0) IDE channel"
//           "    and lock i/o on this channel for 30 seconds to ensure safity"
//           "    of removal process"
           "",
           DEFAULT_REMOVAL_LOCK_TIMEOUT
           );
    exit(0);
}

#define CMD_ATA_LIST  0x01
#define CMD_ATA_FIND  0x02
#define CMD_ATA_HIDE  0x03
#define CMD_ATA_MODE  0x04
#define CMD_ATA_RESET 0x05

HANDLE
ata_open_dev(
    char* Name
    )
{
    ULONG i;
    HANDLE h;

    for(i=0; i<4; i++) {
        h = CreateFile(Name,
                       READ_CONTROL | GENERIC_READ | GENERIC_WRITE ,
                       ((i & 1) ? 0 : FILE_SHARE_READ) | ((i & 2) ? 0 : FILE_SHARE_WRITE),
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
        if(h && (h != ((HANDLE)(-1))) ) {
            return h;
        }
    }

    for(i=0; i<4; i++) {
        h = CreateFile(Name,
                       GENERIC_READ | GENERIC_WRITE ,
                       ((i & 1) ? 0 : FILE_SHARE_READ) | ((i & 2) ? 0 : FILE_SHARE_WRITE),
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
        if(h && (h != ((HANDLE)(-1))) ) {
            return h;
        }
    }

    for(i=0; i<4; i++) {
        h = CreateFile(Name,
                          GENERIC_READ,
                          ((i & 1) ? 0 : FILE_SHARE_READ) | ((i & 2) ? 0 : FILE_SHARE_WRITE),
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
        if(h && (h != ((HANDLE)(-1))) ) {
            return h;
        }
    }

    for(i=0; i<4; i++) {
        h = CreateFile(Name,
                       READ_CONTROL,
                       ((i & 1) ? 0 : FILE_SHARE_READ) | ((i & 2) ? 0 : FILE_SHARE_WRITE),
                       NULL,
                       OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL,
                       NULL);
        if(h && (h != ((HANDLE)(-1))) ) {
            return h;
        }
    }

    return NULL;
} // end ata_open_dev()

void
ata_close_dev(
    HANDLE h
    )
{
    CloseHandle(h);
}

int
ata_send_ioctl(
    HANDLE h,
    PSCSI_ADDRESS addr,
    PCHAR  Signature,
    ULONG  Ioctl,
    PVOID  inBuffer,
    ULONG  inBufferLength,
    PVOID  outBuffer,
    ULONG  outBufferLength,
    PULONG returned
    )
{
    ULONG status;
    PUNIATA_CTL AtaCtl;
    ULONG data_len = max(inBufferLength, outBufferLength);
    ULONG len;

    if(addr) {
        len = data_len + offsetof(UNIATA_CTL, RawData);
    } else {
        len = data_len + sizeof(AtaCtl->hdr);
    }
    AtaCtl = (PUNIATA_CTL)GlobalAlloc(GMEM_FIXED, len);
    AtaCtl->hdr.HeaderLength = sizeof(SRB_IO_CONTROL);
    if(addr) {
        AtaCtl->hdr.Length = data_len + offsetof(UNIATA_CTL, RawData) - sizeof(AtaCtl->hdr);
    } else {
        AtaCtl->hdr.Length = data_len;
    }

    memcpy(&AtaCtl->hdr.Signature, Signature, 8);

    AtaCtl->hdr.Timeout = 10000;
    AtaCtl->hdr.ControlCode = Ioctl;
    
    if(addr) {
        AtaCtl->addr = *addr;
        AtaCtl->addr.Length = sizeof(AtaCtl->addr);
    }

    if(inBuffer && inBufferLength) {
        if(addr) {
            memcpy(&AtaCtl->RawData, inBuffer, inBufferLength);
        } else {
            memcpy(&AtaCtl->addr, inBuffer, inBufferLength);
        }
    }

    status = DeviceIoControl(h,
                             IOCTL_SCSI_MINIPORT,
                             AtaCtl,
                             len,
                             AtaCtl,
                             len,
                             returned,
                             FALSE);

    if(outBuffer && outBufferLength) {
        if(addr) {
            memcpy(outBuffer, &AtaCtl->RawData, outBufferLength);
        } else {
            memcpy(outBuffer, &AtaCtl->addr, outBufferLength);
        }
    }
    GlobalFree(AtaCtl);

    if(!status) {
        status = GetLastError();
        return FALSE;
    }
    return TRUE;
} // end ata_send_ioctl()

IO_SCSI_CAPABILITIES g_capabilities;
UCHAR g_inquiry_buffer[2048];

int
ata_cur_mode_from_ident(
    PIDENTIFY_DATA ident
    )
{
    if(ident->SataEnable && ident->SataEnable != 0xffff) {
        return ATA_SA150;
    }

    if (ident->UdmaModesValid) {
        if (ident->UltraDMAActive & 0x40)
            return ATA_UDMA0+6;
        if (ident->UltraDMAActive & 0x20)
            return ATA_UDMA0+5;
        if (ident->UltraDMAActive & 0x10)
            return ATA_UDMA0+4;
        if (ident->UltraDMAActive & 0x08)
            return ATA_UDMA0+3;
        if (ident->UltraDMAActive & 0x04)
            return ATA_UDMA0+2;
        if (ident->UltraDMAActive & 0x02)
            return ATA_UDMA0+1;
        if (ident->UltraDMAActive & 0x01)
            return ATA_UDMA0+0;
    }

    if (ident->MultiWordDMAActive & 0x04)
        return ATA_WDMA0+2;
    if (ident->MultiWordDMAActive & 0x02)
        return ATA_WDMA0+1;
    if (ident->MultiWordDMAActive & 0x01)
        return ATA_WDMA0+0;

    if (ident->SingleWordDMAActive & 0x04)
        return ATA_SDMA0+2;
    if (ident->SingleWordDMAActive & 0x02)
        return ATA_SDMA0+1;
    if (ident->SingleWordDMAActive & 0x01)
        return ATA_SDMA0+0;

    if (ident->PioTimingsValid) {
        if (ident->AdvancedPIOModes & AdvancedPIOModes_5)
            return ATA_PIO0+5;
        if (ident->AdvancedPIOModes & AdvancedPIOModes_4)
            return ATA_PIO0+4;
        if (ident->AdvancedPIOModes & AdvancedPIOModes_3) 
            return ATA_PIO0+3;
    }        
    if (ident->PioCycleTimingMode == 2)
        return ATA_PIO0+2;
    if (ident->PioCycleTimingMode == 1)
        return ATA_PIO0+1;
    if (ident->PioCycleTimingMode == 0)
        return ATA_PIO0+0;

    return ATA_PIO;
} // end ata_cur_mode_from_ident()

void
ata_mode_to_str(
    char* str,
    int mode
    )
{
    if(mode >= ATA_SA150) {
        sprintf(str, "SATA");
    } else
    if(mode >= ATA_UDMA0) {
        sprintf(str, "UDMA%d", mode-ATA_UDMA0);
    } else
    if(mode >= ATA_WDMA0) {
        sprintf(str, "WDMA%d", mode-ATA_WDMA0);
    } else
    if(mode >= ATA_SDMA0) {
        sprintf(str, "SDMA%d", mode-ATA_SDMA0);
    } else
    if(mode >= ATA_PIO0) {
        sprintf(str, "PIO%d", mode-ATA_PIO0);
    } else
    if(mode == ATA_PIO_NRDY) {
        sprintf(str, "PIO nRDY");
    } else
    {
        sprintf(str, "PIO");
    }
} // end ata_mode_to_str()

#define check_atamode_str(str, mode) \
   (!_stricmp(str, "UDMA##mode") || \
    !_stricmp(str, "UDMA-##mode") || \
    !_stricmp(str, "ATA-##mode") || \
    !_stricmp(str, "ATA##mode"))

int
ata_str_to_mode(
    char* str
    )
{
    int mode;
    int len;

    if(!_stricmp(str, "SATA"))
        return ATA_SA150;

    if(check_atamode_str(str, 16))
        return ATA_UDMA0;
    if(check_atamode_str(str, 25))
        return ATA_UDMA1;
    if(check_atamode_str(str, 33))
        return ATA_UDMA2;
    if(check_atamode_str(str, 44))
        return ATA_UDMA3;
    if(check_atamode_str(str, 66))
        return ATA_UDMA4;
    if(check_atamode_str(str, 100))
        return ATA_UDMA5;
    if(check_atamode_str(str, 122))
        return ATA_UDMA6;

    len = strlen(str);

    if(len >= 4 && !_memicmp(str, "UDMA", 4)) {
        if(len == 4)
            return ATA_UDMA0;
        if(len > 5)
            return -1;
        mode = str[4] - '0';
        if(mode < 0 || mode > 7)
            return -1;
        return ATA_UDMA0+mode;
    }
    if(len >= 4 && !_memicmp(str, "WDMA", 4)) {
        if(len == 4)
            return ATA_WDMA0;
        if(len > 5)
            return -1;
        mode = str[4] - '0';
        if(mode < 0 || mode > 2)
            return -1;
        return ATA_WDMA0+mode;
    }
    if(len >= 4 && !_memicmp(str, "SDMA", 4)) {
        if(len == 4)
            return ATA_SDMA0;
        if(len > 5)
            return -1;
        mode = str[4] - '0';
        if(mode < 0 || mode > 2)
            return -1;
        return ATA_SDMA0+mode;
    }
    if(len == 4 && !_memicmp(str, "DMA", 4)) {
        return ATA_SDMA0;
    }
    if(len >= 3 && !_memicmp(str, "PIO", 3)) {
        if(len == 3)
            return ATA_PIO;
        if(len > 4)
            return -1;
        mode = str[3] - '0';
        if(mode < 0 || mode > 5)
            return -1;
        return ATA_PIO0+mode;
    }

    return -1;
} // end ata_str_to_mode()

int
ata_check_unit(
    HANDLE h,    // handle to ScsiXXX:
    int dev_id
    )
{
    ULONG status;
    ULONG returned;

    PSCSI_ADAPTER_BUS_INFO  adapterInfo;
    PSCSI_INQUIRY_DATA inquiryData;
    SCSI_ADDRESS addr;
    ULONG i;
    int l_dev_id;
    GETTRANSFERMODE IoMode;
    PSENDCMDOUTPARAMS pout;
    PIDENTIFY_DATA   ident;
    char buff[sizeof(SENDCMDOUTPARAMS)+sizeof(IDENTIFY_DATA)];
    char mode_str[12];
    ULONG bus_id = (dev_id >> 24) & 0xff;

    if(dev_id != -1) {
        dev_id &= 0x00ffffff;
    }

    pout = (PSENDCMDOUTPARAMS)buff;
    ident = (PIDENTIFY_DATA)&(pout->bBuffer);

    status = DeviceIoControl(h,
                             IOCTL_SCSI_GET_INQUIRY_DATA,
                             NULL,
                             0,
                             g_inquiry_buffer,
                             sizeof(g_inquiry_buffer),
                             &returned,
                             FALSE);

    if(!status) {
        return FALSE;
    }

    adapterInfo = (PSCSI_ADAPTER_BUS_INFO)g_inquiry_buffer;
    for (i = 0; i < adapterInfo->NumberOfBuses; i++) {
        inquiryData = (PSCSI_INQUIRY_DATA) (g_inquiry_buffer +
            adapterInfo->BusData[i].InquiryDataOffset);
        while (adapterInfo->BusData[i].InquiryDataOffset) {
            /*
            if(dev_id/adapterInfo->BusData[i].NumberOfLogicalUnits ==
               inquiryData->TargetId &&
               dev_id%adapterInfo->BusData[i].NumberOfLogicalUnits ==
               inquiryData->Lun) {
                printf(" %d   %d  %3d    %s    %.28s ",
                i,
                inquiryData->TargetId,
                inquiryData->Lun,
                (inquiryData->DeviceClaimed) ? "Y" : "N",
                &inquiryData->InquiryData[8]);
            }*/
            l_dev_id = (i << 16) | ((ULONG)(inquiryData->TargetId) << 8) | inquiryData->Lun;
            
            if(l_dev_id == dev_id || dev_id == -1) {

                printf("  b%d:d%d:l%d    %.28s ",
                i,
                inquiryData->TargetId,
                inquiryData->Lun,
                /*(inquiryData->DeviceClaimed) ? "Y" : "N",*/
                &inquiryData->InquiryData[8]);

                /*
                for (j = 0; j < 8; j++) {
                    printf("%02X ", inquiryData->InquiryData[j]);
                }
                */

                addr.PortNumber = -1;
                addr.PathId   = inquiryData->PathId;
                addr.TargetId = inquiryData->TargetId;
                addr.Lun      = inquiryData->Lun;
                status = ata_send_ioctl(h, &addr, "-UNIATA-",
                                        IOCTL_SCSI_MINIPORT_UNIATA_GET_MODE,
                                        NULL, 0,
                                        &IoMode, sizeof(IoMode),
                                        &returned);
                if(status) {
                    ata_mode_to_str(mode_str, max(IoMode.CurrentMode, IoMode.MaxMode));
                    printf(" %.12s ", mode_str);
                } else {
                    SENDCMDINPARAMS pin;

                    memset(&pin, 0, sizeof(pin));
                    memset(buff, 0, sizeof(buff));
                    pin.irDriveRegs.bCommandReg = ID_CMD;
                    pin.bDriveNumber = inquiryData->TargetId;

                    status = ata_send_ioctl(h, NULL, "SCSIDISK",
                                            IOCTL_SCSI_MINIPORT_IDENTIFY,
                                            &pin, sizeof(pin),
                                            buff, sizeof(buff),
                                            &returned);
                    if(status) {
                        ata_mode_to_str(mode_str, ata_cur_mode_from_ident(ident));
                        printf(" %.12s ", mode_str);
                    }
                }
                printf("\n");
            }
            if (inquiryData->NextInquiryDataOffset == 0) {
                break;
            }
            
            inquiryData = (PSCSI_INQUIRY_DATA) (g_inquiry_buffer +
                inquiryData->NextInquiryDataOffset);
        }
    }

    return FALSE;
} // end ata_check_unit()

int
ata_check_controller(
    HANDLE h,    // handle to ScsiXXX:
    PIO_SCSI_CAPABILITIES capabilities
    )
{
    ULONG status;
    ULONG returned;

    status = DeviceIoControl(h,
                             IOCTL_SCSI_GET_CAPABILITIES,
                             NULL,
                             0,
                             capabilities,
                             sizeof(IO_SCSI_CAPABILITIES),
                             &returned,
                             FALSE);
    return status;
}

BOOLEAN
ata_list(
    int bus_id,
    int dev_id
    )
{
    char dev_name[64];
    HANDLE h;

    if(bus_id == -1) {
        for(bus_id=0; TRUE; bus_id++) {
            if(!ata_list(bus_id, dev_id))
                break;
        }
        return TRUE;
    }
    sprintf(dev_name, "\\\\.\\Scsi%d:", bus_id);
    h = ata_open_dev(dev_name);
    if(!h)
        return FALSE;
    if(dev_id == -1) {
        printf("Scsi%d:\n", bus_id);
        ata_check_controller(h, &g_capabilities);
        ata_check_unit(h, -1);
        ata_close_dev(h);
        return TRUE;
    }
    ata_check_unit(h, dev_id | (bus_id << 24));
    ata_close_dev(h);
    return TRUE;
} // end ata_list()

BOOLEAN
ata_mode(
    int bus_id,
    int dev_id,
    int mode
    )
{
    char dev_name[64];
    HANDLE h;
    SETTRANSFERMODE IoMode;
    ULONG status;
    ULONG returned;
    SCSI_ADDRESS addr;

    if(dev_id == -1) {
        return FALSE;
    }
    sprintf(dev_name, "\\\\.\\Scsi%d:", bus_id);
    h = ata_open_dev(dev_name);
    if(!h)
        return FALSE;
    addr.PortNumber = bus_id;
    addr.PathId   = (UCHAR)(dev_id >> 16);
    addr.TargetId = (UCHAR)(dev_id >> 8);
    addr.Lun      = (UCHAR)(dev_id);

    IoMode.MaxMode = mode;
    IoMode.ApplyImmediately = FALSE;
//    IoMode.ApplyImmediately = TRUE;
    IoMode.OrigMode = mode;

    status = ata_send_ioctl(h, &addr, "-UNIATA-",
                            IOCTL_SCSI_MINIPORT_UNIATA_SET_MAX_MODE,
                            &IoMode, sizeof(IoMode),
                            NULL, 0,
                            &returned);
    ata_close_dev(h);
    return TRUE;
} // end ata_list()

BOOLEAN
ata_reset(
    int bus_id,
    int dev_id,
    int mode
    )
{
    char dev_name[64];
    HANDLE h;
    ULONG status;
    ULONG returned;
    SCSI_ADDRESS addr;

    if(dev_id == -1) {
        return FALSE;
    }
    sprintf(dev_name, "\\\\.\\Scsi%d:", bus_id);
    h = ata_open_dev(dev_name);
    if(!h)
        return FALSE;
    addr.PortNumber = bus_id;
    addr.PathId   = (UCHAR)(dev_id >> 16);
    addr.TargetId = (UCHAR)(dev_id >> 8);
    addr.Lun      = (UCHAR)(dev_id);

    status = ata_send_ioctl(h, &addr, "-UNIATA-",
                            IOCTL_SCSI_MINIPORT_UNIATA_RESET_DEVICE,
                            NULL, 0,
                            NULL, 0,
                            &returned);
    ata_close_dev(h);
    return TRUE;
} // end ata_reset()

int
ata_num_to_x_dev(
    char a
    )
{
    if(a >= '0' && a <= '9')
        return a-'0';
    return -1;
}

void
main (
    int argc,
    char* argv[]
    )
{
    ULONG Flags = 0;
    int i, j;
    char a;
    int bus_id = -1;
    int dev_id = -1;
    int cmd = 0;
    int lock = -1;
    int b_dev=-1, d_dev=-1, l_dev=0;
    int mode=-1;

    printf("Console ATA control utility for Windows NT/2000/XP with UniATA installed\n"
           "Version 0." UNIATA_VER_STR ", Copyright (c) Alexander A. Telyatnikov, 2003-2005\n"
           "Home site: http://alter.org.ua\n");

    for(i=1; i<argc; i++) {
        if(!argv[i])
            continue;
        if((a = argv[i][0]) != '-') {
            for(j=0; a = argv[i][j]; j++) {
                switch(a) {
                case 's' :
                case 'c' :
                    j++;
                    bus_id = ata_num_to_x_dev(argv[i][j]);
                    break;
                case 'b' :
                    j++;
                    b_dev = ata_num_to_x_dev(argv[i][j]);
                    break;
                case 'd' :
                    j++;
                    d_dev = ata_num_to_x_dev(argv[i][j]);
                    break;
                case 'l' :
                    j++;
                    l_dev = ata_num_to_x_dev(argv[i][j]);
                    break;
                case ':' :
                    break;
                default:
                    print_help();
                }
            }
            continue;
        }
        j=1;
        while(argv[i] && (a = argv[i][j]) && (a != ' ') && (a != '\t')) {
            switch(a) {
            case 'l' :
                if(cmd || lock>0) {
                    print_help();
                }
                cmd = CMD_ATA_LIST;
                break;
            case 'f' :
                if(cmd || lock>0) {
                    print_help();
                }
                cmd = CMD_ATA_FIND;
                break;
            case 'h' :
                if(cmd) {
                    print_help();
                }
                cmd = CMD_ATA_HIDE;
                break;
            case 'm' :
                if(cmd) {
                    print_help();
                }
                cmd = CMD_ATA_MODE;
                i++;
                if(!argv[i]) {
                    print_help();
                }
                mode = ata_str_to_mode(argv[i]);
                if(mode == -1) {
                    print_help();
                }
                j = strlen(argv[i])-1;
                break;
            case 'r' :
                if(cmd) {
                    print_help();
                }
                cmd = CMD_ATA_RESET;
                break;
/*
            case 'r' :
                if(cmd && cmd != CMD_ATA_MODE && cmd != CMD_ATA_HIDE) {
                    print_help();
                }
                lock = DEFAULT_REMOVAL_LOCK_TIMEOUT;
                break;
*/
            case '?' :
            default:
                print_help();
            }
            j++;
        }
    }

    if(d_dev != -1 && b_dev != -1) {
        dev_id = (b_dev << 16) | (d_dev << 8) | l_dev;
    }
    if(cmd == CMD_ATA_LIST) {
        ata_list(bus_id, dev_id);
    } else
    if(cmd == CMD_ATA_MODE) {
        ata_mode(bus_id, dev_id, mode);
    } else
    if(cmd == CMD_ATA_RESET) {
        ata_reset(bus_id, dev_id, mode);
    } else {
        print_help();
    }
}

