/* Read-only Windows 2000/XP resource-conflict diagnostic. */
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static unsigned errors;

static void failure(const char *where, CONFIGRET cr)
{
    printf("    %s: CONFIGRET=0x%08lx\n", where, cr);
    ++errors;
}

static int contains(const char *s, const char *part)
{
    size_t n = strlen(part);
    for (; *s; ++s) {
        size_t i;
        for (i = 0; i < n && s[i] &&
             toupper((unsigned char)s[i]) == toupper((unsigned char)part[i]); ++i) {}
        if (i == n) return 1;
    }
    return n == 0;
}

static void property(DEVINST dev, ULONG which, char *out, ULONG size)
{
    ULONG type = 0, len = size;
    CONFIGRET cr = CM_Get_DevNode_Registry_PropertyA(dev, which, &type, out, &len, 0);
    if (cr != CR_SUCCESS || type != REG_SZ) out[0] = 0;
    out[size - 1] = 0;
}

static void conflict_query(DEVINST dev, RESOURCEID type, const void *data, ULONG size)
{
    CONFLICT_LIST list = 0;
    ULONG count = 0, i;
    CONFIGRET cr = CM_Query_Resource_Conflict_List(&list, dev, type, data, size, 0, NULL);
    if (cr != CR_SUCCESS) {
        failure("CM_Query_Resource_Conflict_List", cr);
        return;
    }
    cr = CM_Get_Resource_Conflict_Count(list, &count);
    if (cr != CR_SUCCESS) {
        failure("CM_Get_Resource_Conflict_Count", cr);
        CM_Free_Resource_Conflict_Handle(list);
        return;
    }
    printf("    Conflicts reported: %lu\n", count);
    for (i = 0; i < count; ++i) {
        CONFLICT_DETAILS_A details;
        char id[MAX_DEVICE_ID_LEN];
        memset(&details, 0, sizeof(details));
        details.CD_ulSize = sizeof(details);
        details.CD_ulMask = CM_CDMASK_DEVINST | CM_CDMASK_FLAGS | CM_CDMASK_DESCRIPTION;
        details.CD_dnDevInst = (DEVINST)-1;
        cr = CM_Get_Resource_Conflict_DetailsA(list, i, &details);
        if (cr != CR_SUCCESS) {
            failure("CM_Get_Resource_Conflict_Details", cr);
            continue;
        }
        details.CD_szDescription[MAX_PATH - 1] = 0;
        printf("      [%lu] flags=0x%08lx%s%s%s description=%s\n", i,
               details.CD_ulFlags,
               (details.CD_ulFlags & CM_CDFLAGS_DRIVER) ? " LEGACY_DRIVER" : "",
               (details.CD_ulFlags & CM_CDFLAGS_ROOT_OWNED) ? " ROOT/HAL" : "",
               (details.CD_ulFlags & CM_CDFLAGS_RESERVED) ? " RESERVED/UNKNOWN" : "",
               details.CD_szDescription);
        if (details.CD_dnDevInst != (DEVINST)-1 &&
            (details.CD_ulMask & CM_CDMASK_DEVINST)) {
            cr = CM_Get_Device_IDA(details.CD_dnDevInst, id, sizeof(id), 0);
            if (cr == CR_SUCCESS) printf("          instance=%s\n", id);
            else failure("CM_Get_Device_ID (conflict owner)", cr);
        }
    }
    CM_Free_Resource_Conflict_Handle(list);
}

static void resource(DEVINST dev, RES_DES rd, RESOURCEID type, int allocated)
{
    ULONG size = 0, i;
    unsigned char *data;
    CONFIGRET cr = CM_Get_Res_Des_Data_Size(&size, rd, 0);
    if (cr != CR_SUCCESS) { failure("CM_Get_Res_Des_Data_Size", cr); return; }
    if (!size || size > 1024 * 1024) {
        printf("    Invalid descriptor size: %lu\n", size);
        ++errors;
        return;
    }
    data = malloc(size);
    if (!data) { puts("    Out of memory"); ++errors; return; }
    cr = CM_Get_Res_Des_Data(rd, data, size, 0);
    if (cr != CR_SUCCESS) {
        failure("CM_Get_Res_Des_Data", cr);
        free(data);
        return;
    }
    printf("    Resource type=%lu bytes=%lu\n", type, size);
    if (allocated) {
        if (type == ResType_IO && size >= sizeof(IO_DES)) {
            const IO_DES *d = (const IO_DES *)data;
            printf("      I/O %llx-%llx flags=0x%08lx\n",
                   (unsigned long long)d->IOD_Alloc_Base,
                   (unsigned long long)d->IOD_Alloc_End, d->IOD_DesFlags);
        } else if (type == ResType_Mem && size >= sizeof(MEM_DES)) {
            const MEM_DES *d = (const MEM_DES *)data;
            printf("      Memory %llx-%llx flags=0x%08lx\n",
                   (unsigned long long)d->MD_Alloc_Base,
                   (unsigned long long)d->MD_Alloc_End, d->MD_Flags);
        } else if (type == ResType_IRQ && size >= sizeof(IRQ_DES)) {
            const IRQ_DES *d = (const IRQ_DES *)data;
            printf("      IRQ %lu flags=0x%08lx\n", d->IRQD_Alloc_Num, d->IRQD_Flags);
        } else if (type == ResType_DMA && size >= sizeof(DMA_DES)) {
            const DMA_DES *d = (const DMA_DES *)data;
            printf("      DMA %lu flags=0x%08lx\n", d->DD_Alloc_Chan, d->DD_Flags);
        }
    }
    /* Preserve descriptors verbatim, including alternative requirements. */
    for (i = 0; i < size; ++i) {
        if (!(i % 16)) printf("      %04lx:", i);
        printf(" %02x", data[i]);
        if ((i % 16) == 15 || i + 1 == size) putchar('\n');
    }
    if (allocated && (type == ResType_IO || type == ResType_Mem ||
                      type == ResType_IRQ || type == ResType_DMA))
        conflict_query(dev, type, data, size);
    free(data);
}

static void configurations(DEVINST dev, ULONG flags, const char *name, int allocated)
{
    LOG_CONF lc, next_lc;
    unsigned index = 0;
    CONFIGRET cr = CM_Get_First_Log_Conf(&lc, dev, flags);
    if (cr == CR_NO_MORE_LOG_CONF) { printf("  %s: absent\n", name); return; }
    if (cr != CR_SUCCESS) { failure(name, cr); return; }
    for (;;) {
        RES_DES rd = 0, next_rd;
        RESOURCEID type;
        printf("  %s[%u]%s\n", name, index++,
               allocated ? " (query current conflicts)" : " (requirements only)");
        cr = CM_Get_Next_Res_Des(&next_rd, lc, ResType_All, &type, 0);
        while (cr == CR_SUCCESS) {
            if (rd) CM_Free_Res_Des_Handle(rd);
            rd = next_rd;
            resource(dev, rd, type, allocated);
            cr = CM_Get_Next_Res_Des(&next_rd, rd, ResType_All, &type, 0);
        }
        if (rd) CM_Free_Res_Des_Handle(rd);
        if (cr != CR_NO_MORE_RES_DES) failure("CM_Get_Next_Res_Des", cr);
        cr = CM_Get_Next_Log_Conf(&next_lc, lc, 0);
        CM_Free_Log_Conf_Handle(lc); /* Frees the handle, NOT the configuration. */
        if (cr == CR_NO_MORE_LOG_CONF) break;
        if (cr != CR_SUCCESS) { failure("CM_Get_Next_Log_Conf", cr); break; }
        lc = next_lc;
    }
}

int main(int argc, char **argv)
{
    HDEVINFO devices;
    SP_DEVINFO_DATA info;
    DWORD index;
    unsigned found = 0;
    if (argc > 2 || (argc == 2 && (!strcmp(argv[1], "--help") || !strcmp(argv[1], "/?")))) {
        puts("PnpResourceProbe [instance-ID substring]\n"
             "Default: present Code 12 devices and UniATA devices.\n"
             "Read-only. Run as administrator on 32-bit Windows XP.\n"
             "Example: PnpResourceProbe > pnp-resources.txt");
        return argc > 2 ? 2 : 0;
    }
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("PnpResourceProbe 1 - read-only, x86 Windows 2000/XP\n"
         "Conflicts are a current query, not a recording of the failed boot allocation.\n"
         "An empty result does not exclude conflicts between devices using one driver.");
    devices = SetupDiGetClassDevsA(NULL, NULL, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (devices == INVALID_HANDLE_VALUE) {
        printf("SetupDiGetClassDevs failed: %lu\n", GetLastError());
        return 1;
    }
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    for (index = 0; SetupDiEnumDeviceInfo(devices, index, &info); ++index) {
        char id[MAX_DEVICE_ID_LEN], desc[512], service[256];
        ULONG status = 0, problem = 0;
        CONFIGRET cr = CM_Get_Device_IDA(info.DevInst, id, sizeof(id), 0);
        CONFIGRET status_cr;
        if (cr != CR_SUCCESS) { failure("CM_Get_Device_ID", cr); continue; }
        property(info.DevInst, CM_DRP_SERVICE, service, sizeof(service));
        status_cr = CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0);
        if (argc == 2 ? !contains(id, argv[1]) :
            (strcmpi(service, "UniATA") && !contains(id, "PCI\\VEN_1078&DEV_0102") &&
             !(status_cr == CR_SUCCESS && problem == 12))) continue;
        ++found;
        property(info.DevInst, CM_DRP_DEVICEDESC, desc, sizeof(desc));
        printf("\nDEVICE %s\n  description=%s\n  service=%s\n", id, desc, service);
        if (status_cr == CR_SUCCESS)
            printf("  status=0x%08lx problem=%lu%s\n", status, problem,
                   (status & DN_HAS_PROBLEM) ? " (DN_HAS_PROBLEM)" : "");
        else failure("CM_Get_DevNode_Status", status_cr);
        configurations(info.DevInst, ALLOC_LOG_CONF, "ALLOC", 1);
        configurations(info.DevInst, BOOT_LOG_CONF, "BOOT", 1);
        configurations(info.DevInst, FORCED_LOG_CONF, "FORCED", 1);
        configurations(info.DevInst, BASIC_LOG_CONF, "BASIC", 0);
        configurations(info.DevInst, FILTERED_LOG_CONF, "FILTERED", 0);
    }
    if (GetLastError() != ERROR_NO_MORE_ITEMS) {
        printf("SetupDiEnumDeviceInfo failed: %lu\n", GetLastError());
        ++errors;
    }
    SetupDiDestroyDeviceInfoList(devices);
    printf("\nResult: %u selected device(s), %u diagnostic error(s).\n", found, errors);
    if (!found) puts("No matching present device; try an instance-ID substring.");
    return errors ? 1 : (found ? 0 : 2);
}
