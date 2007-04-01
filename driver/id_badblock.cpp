/*++

Copyright (C) 2006 VorontSOFT

Module Name:
    id_badblock.cpp

Abstract:
    This is the artificial badblock simulation part of the
    miniport driver for ATA/ATAPI IDE controllers with Busmaster DMA support

Author:
    Nikolai Vorontsov (NickViz)

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
    2006/08/03 Initial implementation.
    2006/08/06 Added registry work.
    2007/03/27 Added device serial to registry value name instead if LUN.

--*/

#include "stdafx.h"

#define MAX_BADBLOCKS_ITEMS 512



//SBadBlockRange    arrBadBlocks[MAX_BADBLOCKS_ITEMS];
//ULONG            nBadBlocks = 0;

LIST_ENTRY BBList;
BOOLEAN BBListInited = FALSE;

typedef struct _SBadBlockList {
    LIST_ENTRY       List;
    PHW_LU_EXTENSION LunExt;
    WCHAR            SerNumStr[64];
    SBadBlockRange*  arrBadBlocks;
    ULONG            nBadBlocks;
} SBadBlockList, *PSBadBlockList;

// RtlQueryRegistryValues callback function
static NTSTATUS __stdcall
BadBlockQueryRoutine(
    IN PWSTR ValueName,
    IN ULONG ValueType,
    IN PVOID ValueData,
    IN ULONG ValueLength,
    IN PVOID Context,
    IN PVOID EntryContext
    )
{
    // The ValueType should be REG_SZ
    // The ValueData is UNICODE string of the following format:
    //  "badblocks_start_from_lba  badblocks_end_at_lba"

    PHW_LU_EXTENSION LunExt = (PHW_LU_EXTENSION)EntryContext;

    KdPrint(( "BadBlockQueryRoutine: S/N:%S\n type %#x, len %#x\n", ValueName, ValueType, ValueLength));
    KdPrint(( "  LunExt %#x\n", LunExt));

    if((ValueType == REG_BINARY)  &&            // STRING
        ValueLength &&            // At least "0 0 0"
        !(ValueLength % sizeof(SBadBlockRange)))    // There is free space for the record
    {
        LunExt->nBadBlocks = 0;
        if(LunExt->arrBadBlocks) {
            ExFreePool(LunExt->arrBadBlocks);
        }
        LunExt->arrBadBlocks = (SBadBlockRange*)ExAllocatePool(NonPagedPool, ValueLength);
        if(!LunExt->arrBadBlocks)
            return 0;
        RtlCopyMemory(LunExt->arrBadBlocks, ValueData, ValueLength);
        LunExt->nBadBlocks = ValueLength/sizeof(SBadBlockRange);
    }

    return 0;
}


void
InitBadBlocks(
    IN PHW_LU_EXTENSION LunExt
    )
{
    RTL_QUERY_REGISTRY_TABLE QueryTable[2];    // Main record and zero filled end of array marker
    WCHAR DevSerial[64];
    ULONG Length;
    PLIST_ENTRY      link;
    PSBadBlockList   cur;
    // Read from the registry necessary badblock pairs and fill in arrBadBlocks array
    // HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\UniATA\Parameters\BadBlocks

    if(!LunExt) {
        // init
        if(!BBListInited) {
            InitializeListHead(&BBList);
            BBListInited = TRUE;
        }

        QueryTable[0].QueryRoutine    = BadBlockQueryRoutine;
        QueryTable[0].Flags           = RTL_QUERY_REGISTRY_REQUIRED;
        QueryTable[0].Name            = NULL;   // If Name is NULL, the QueryRoutine function 
                                                //  specified for this table entry is called 
                                                //  for all values associated with the current 
                                                //  registry key. 
        QueryTable[0].EntryContext    = NULL;
        QueryTable[0].DefaultType     = REG_NONE;
        QueryTable[0].DefaultData     = 0;
        QueryTable[0].DefaultLength   = 0;

        RtlZeroMemory(QueryTable + 1, sizeof RTL_QUERY_REGISTRY_TABLE);    // EOF

        NTSTATUS status = RtlQueryRegistryValues(RTL_REGISTRY_SERVICES,
                                                L"UniATA\\Parameters\\BadBlocks",
                                                QueryTable, 0, 0);

        KdPrint(( "InitBadBlocks returned: %#x\n", status));
    } else {

        Length = EncodeVendorStr(DevSerial, LunExt->IdentifyData.SerialNumber, sizeof(LunExt->IdentifyData.SerialNumber));

        KdPrint(( "LunExt %#x\n", LunExt));
        KdPrint(( "S/N:%S\n", DevSerial));

        LunExt->nBadBlocks = 0;
        LunExt->arrBadBlocks = NULL;

        link = BBList.Flink;
        while(link != &BBList) {
            cur = CONTAINING_RECORD( link, SBadBlockList, List);
            link = link->Flink;
            if(cur->LunExt == LunExt) {
                cur->LunExt->nBadBlocks   = 0;
                cur->LunExt->arrBadBlocks = NULL;
                cur->LunExt = NULL;
            }
            if(!wcscmp(cur->SerNumStr, DevSerial)) {
                cur->LunExt->nBadBlocks   = 0;
                cur->LunExt->arrBadBlocks = NULL;
                cur->LunExt = NULL;
            }
        }

        if(!(LunExt->DeviceFlags & DFLAGS_ATAPI_DEVICE)) {

            link = BBList.Flink;
            while(link != &BBList) {
                cur = CONTAINING_RECORD( link, SBadBlockList, List);
                link = link->Flink;
                if(!wcscmp(cur->SerNumStr, DevSerial)) {
                    KdPrint(( "found BB:List\n"));
                    cur->LunExt = LunExt;
                    LunExt->arrBadBlocks = cur->arrBadBlocks;
                    LunExt->nBadBlocks   = cur->nBadBlocks;
                    return;
                }
            }
        }
    }
    return;
}


bool
CheckIfBadBlock(
    IN PHW_LU_EXTENSION LunExt,
    IN UCHAR command,
    IN ULONGLONG lba,
    IN USHORT count
    )
{
    if (LunExt->nBadBlocks == 0)
        return false;

    if (command == IDE_COMMAND_READ                ||
        command == IDE_COMMAND_READ_MULTIPLE    ||
        command == IDE_COMMAND_READ_DMA            ||
        command == IDE_COMMAND_READ_DMA_Q        ||
        command == IDE_COMMAND_WRITE            ||
        command == IDE_COMMAND_WRITE_MULTIPLE    ||
        command == IDE_COMMAND_WRITE_DMA        ||
        command == IDE_COMMAND_WRITE_DMA_Q        ||
        command == IDE_COMMAND_FLUSH_CACHE        ||
        command == IDE_COMMAND_VERIFY)
    {
        for (ULONG i = 0; i < nBadBlocks; i++)
        {
            if (lba + count > LunExt->arrBadBlocks[i].m_lbaStart  &&  
                        lba < LunExt->arrBadBlocks[i].m_lbaEnd)
                return true;
        }
    } // if

    return false;
//    return lba + count > 77000000  &&  lba < 80000000;
}
