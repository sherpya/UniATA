#include <windows.h>
#include <stdio.h>

#define DEFAULT_REMOVAL_LOCK_TIMEOUT 20

void print_help() {
    printf("Usage:\n"
           "  atactl -<switches> [s<bus id>[:d<device id>]]\n"
           "Switches:\n"
           "  l         list devices on SCSI bus(es)\n"
           "  f         find new devices on SCSI bus(es)\n"
           "  h         hide device from SCSI bus\n"
           "  m [MODE]  set i/o mode for device or revert to default\n"
           "            available MODEs are PIO, PIO0-PIO5, DMA, WDMA0-WDMA2, UDMA33/66/100/133\n"
           "  r [XXX]   lock SCSI bus for device removal for XXX seconds or\n"
           "            for %d seconds if no lock timeout specified.\n"
           "            can be used with -h, -m or standalone.\n"
           "Examples:\n"
           "  atactl -l\n"
           "    will list all scsi buses and all connected devices"
           "  atactl -h -r 30 s1:d0 \n"
           "    will hide Master (d0) device on secondary (s1) IDE channel"
           "    and lock i/o on this channel for 30 seconds to ensure safity"
           "    of removal precess"
           "",
           DEFAULT_REMOVAL_LOCK_TIMEOUT
           );
    exit(0);
}

#define CMD_ATA_LIST  0x01
#define CMD_ATA_FIND  0x02
#define CMD_ATA_HIDE  0x03
#define CMD_ATA_MODE  0x04

void main (int argc, char* argv[]) {
    ULONG Flags = 0;
    int i, j;
    char a;
    int bus_id = -1;
    int dev_id = -1;
    int cmd = 0;
    int lock = -1;

    printf("Console ATA control utility for Windows NT/2000 with UniATA installed\n"
           "Copyright (c) Alexander A. Telyatnikov, 2003\n");

    for(i=1; i<argc; i++) {
        if(!argv[i])
            continue;
        if((a = argv[i][0]) != '-') {
            switch(s) {
            case 's' :
                break;
            case 'd' :
                break;
            default:
                print_help();
            }
            continue;
        }
        j=1;
        while((a = argv[i][j]) && (a != ' ') && (a != '\t')) {
            switch(a) {
            case 'l' :
                i++;
                if(cmd || lock>0) {
                    print_help();
                }
                cmd = CMD_ATA_LIST;
                break;
            case 'f' :
                i++;
                if(cmd || lock>0) {
                    print_help();
                }
                cmd = CMD_ATA_FIND;
                break;
            case 'h' :
                i++;
                if(cmd) {
                    print_help();
                }
                cmd = CMD_ATA_HIDE;
                break;
            case 'm' :
                i++;
                if(cmd) {
                    print_help();
                }
                cmd = CMD_ATA_MODE;
                break;
            case 'r' :
                i++;
                if(cmd && cmd != CMD_ATA_MODE && cmd != CMD_ATA_HIDE) {
                    print_help();
                }
                lock = DEFAULT_REMOVAL_LOCK_TIMEOUT;
                break;

            case '?' :
            default:
                print_help();
            }
            j++;
        }
    }

}
