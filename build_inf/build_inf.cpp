/*++

Copyright (c) 2003 Alexander A. Telyatnikov (Alter)

Module Name:
    build_inf.cpp

Abstract:
    This is simple utility that constructs uniata2k.inf from
    uniata2k.in_ by insertion of device list, taken from bm_devs.h

Author:
    Alexander A. Telyatnikov (Alter)

Environment:
    kernel mode only

Notes:

    THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
    IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
    OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
    IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
    INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
    NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
    THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Revision History:

--*/

#include <windows.h>
#include <stdio.h>
#include "../driver/bm_devs.h"

#define DEVLIST_NAME     ";DEVLIST"
#define DEVNAMELIST_NAME ";DEVNAMELIST"

LONGLONG
get_image_size(
    HANDLE h)
{
    LONG offh = 0;
    LONG offl = 0;

    offl = SetFilePointer((HANDLE)h, 0, &offh, FILE_END);
    if(GetLastError())
        return -1;
    return (((LONGLONG)offh)<<32) | offl;
}

LONGLONG
set_file_pointer(
    HANDLE h,
    LONGLONG offs)
{
    LONG offh = (ULONG)(offs >> 32);
    LONG offl = (ULONG)offs;

    offl = SetFilePointer((HANDLE)h, offl, &offh, FILE_BEGIN);
    if(GetLastError())
        return -1;
    return (((LONGLONG)offh)<<32) | offl;
}

void main (int argc, char* argv[]) {
    HANDLE h;
    LONG l=0;
    PUCHAR buff=NULL;
    PUCHAR buff_j=NULL;
    PUCHAR buff_i=NULL;
    LONG tmp;
    LONG i=0, j=0;
    ULONG k;
    
    if(argc < 2)
        exit(-1);
    h = CreateFile(argv[1], GENERIC_READ | GENERIC_WRITE,
                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                   NULL,
                   OPEN_EXISTING,
                   FILE_ATTRIBUTE_NORMAL/* | FILE_FLAG_NO_BUFFERING*/,  NULL);
    if(h == ((HANDLE)-1)) {
        exit(-2);
    }
    l = (LONG)get_image_size(h);
    if(l < 1) {
        exit(-3);
    }
    if(set_file_pointer(h, 0) < 0) {
        exit(-3);
    }
    buff = (PUCHAR)GlobalAlloc(GMEM_DISCARDABLE, l+1);
    if(!ReadFile(h, buff, l, (PULONG)&tmp, NULL) ||
        tmp != l) {
        tmp = GetLastError();

        exit(-4);
    }
    CloseHandle(h);

    for(i=0, j=0; i<l; i++) {
        if(buff[i] == '\r')
            continue;
        buff[j] = buff[i];
        j++;
    }
    buff[j] = 0;
    buff_i = (PUCHAR)strstr((PCHAR)buff, DEVLIST_NAME);
    if(!buff_i) {
        exit(-5);
    }
/*    if(!WriteFile(h, buff, i, &tmp, NULL) ||
        tmp != l) {
        exit(-6);
    }*/
    buff_i[0]=0;
    printf("%s", buff);
    for(k=0; k<NUM_BUSMASTER_ADAPTERS; k++) {
        printf("%%PCI\\VEN_%s&DEV_%s.DeviceDesc%%=uniata_Inst, PCI\\VEN_%s&DEV_%s\n", 
               BusMasterAdapters[k].VendorId,
               BusMasterAdapters[k].DeviceId,
               BusMasterAdapters[k].VendorId,
               BusMasterAdapters[k].DeviceId
               );
    }
    printf("%s\n\r", DEVLIST_NAME);
    buff_i+=sizeof(DEVLIST_NAME);
    buff_j=buff_i;
    buff_i = (PUCHAR)strstr((PCHAR)buff_i, DEVNAMELIST_NAME);
    if(!buff_i) {
        exit(-6);
    }
    buff_i[0]=0;
    printf("%s", buff_j);
    for(k=0; k<NUM_BUSMASTER_ADAPTERS; k++) {
        printf("PCI\\VEN_%s&DEV_%s.DeviceDesc=\"%s\"\n", 
               BusMasterAdapters[k].VendorId,
               BusMasterAdapters[k].DeviceId,
               BusMasterAdapters[k].FullDevName
               );
    }
    printf("%s\n\r", DEVNAMELIST_NAME);
    buff_i+=sizeof(DEVNAMELIST_NAME);
    printf("%s", buff_i);

    exit(0);
}
