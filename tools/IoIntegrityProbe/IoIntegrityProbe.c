/* XP/i586 storage diagnostics. Only 'create' writes, and only CREATE_NEW. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define TOTAL (64UL * 1024 * 1024)
#define MAX_IO (256UL * 1024)
#define PAGE 4096UL
static const DWORD lengths[] = {512, 4096, 64512, 65536, 69632, 131072, 262144};
static const char *names[] = {"buffered", "direct", "overlapped", "scatter"};

static DWORD word_at(DWORD index) {
    DWORD x = index ^ 0xa53c9e17UL;
    x ^= x >> 16; x *= 0x7feb352dUL;
    x ^= x >> 15; x *= 0x846ca68bUL;
    return x ^ (x >> 16);
}
static void pattern(BYTE *p, DWORD offset, DWORD n) {
    DWORD i;
    for (i = 0; i < n; i += 4) {
        DWORD v = word_at((offset + i) / 4);
        memcpy(p + i, &v, 4);
    }
}
static int mismatch(const BYTE *a, const BYTE *b, DWORD n, DWORD offset) {
    DWORD i;
    for (i = 0; i < n; ++i) if (a[i] != b[i]) {
        printf("MISMATCH offset=0x%08lx expected=%02x actual=%02x\n",
               (unsigned long)(offset+i), a[i], b[i]);
        return 1;
    }
    return 0;
}
static HANDLE open_read(const char *path, int mode) {
    DWORD flags = mode ? FILE_FLAG_NO_BUFFERING : 0;
    if (mode >= 2) flags |= FILE_FLAG_OVERLAPPED;
    return CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE |
                       FILE_SHARE_DELETE, NULL, OPEN_EXISTING, flags, NULL);
}
static void alignment(HANDLE h) {
    typedef NTSTATUS (NTAPI *query_fn)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG,
                                      FILE_INFORMATION_CLASS);
    query_fn fn = (query_fn)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"),
                                                   "NtQueryInformationFile");
    IO_STATUS_BLOCK ios; ULONG mask = 0; NTSTATUS st;
    if (!fn) return;
    memset(&ios, 0, sizeof(ios));
    st = fn(h, &ios, &mask, sizeof(mask), FileAlignmentInformation);
    printf("FileAlignmentInformation: status=0x%08lx mask=0x%lx\n",
           (unsigned long)st, (unsigned long)mask);
}
/* All requests and buffers stay alive until completion, including pending I/O. */
static int read_at(HANDLE h, int mode, DWORD offset, DWORD n, BYTE *arena,
                   DWORD skew, BYTE *out) {
    DWORD got = 0, error = 0, i; BOOL ok;
    BYTE *buf = arena + skew;
    OVERLAPPED ov; LARGE_INTEGER pos;
    FILE_SEGMENT_ELEMENT segments[MAX_IO/PAGE + 1];
    memset(&ov, 0, sizeof(ov));
    memset(arena, 0xcc, 2*MAX_IO + PAGE);
    if (mode < 2) {
        pos.QuadPart = offset;
        ok = SetFilePointerEx(h, pos, NULL, FILE_BEGIN);
        if (ok) ok = ReadFile(h, buf, n, &got, NULL);
        if (!ok) error = GetLastError();
    } else {
        ov.Offset = offset;
        ov.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        if (!ov.hEvent) { error = GetLastError(); ok = FALSE; }
        else {
            if (mode == 3) {
                memset(segments, 0, sizeof(segments));
                for (i = 0; i < n/PAGE; ++i)
                    segments[i].Alignment = (ULONGLONG)(ULONG_PTR)(buf+2*i*PAGE);
                ok = ReadFileScatter(h, segments, n, NULL, &ov);
            } else ok = ReadFile(h, buf, n, &got, &ov);
            error = ok ? 0 : GetLastError();
            if (ok || error == ERROR_IO_PENDING) {
                ok = GetOverlappedResult(h, &ov, &got, TRUE);
                error = ok ? 0 : GetLastError();
            }
            CloseHandle(ov.hEvent);
        }
    }
    if (!ok || got != n) {
        printf("READ FAIL mode=%s offset=%lu length=%lu buffer_low16=0x%lx "
               "error=%lu bytes=%lu\n", names[mode], (unsigned long)offset,
               (unsigned long)n, (unsigned long)((ULONG_PTR)buf & 65535),
               (unsigned long)error, (unsigned long)got);
        return 1;
    }
    if (mode == 3) {
        for (i=0; i<n/PAGE; ++i) memcpy(out+i*PAGE, buf+2*i*PAGE, PAGE);
    } else memcpy(out, buf, n);
    return 0;
}
static int create_test(const char *path) {
    HANDLE h; BYTE *buf; DWORD offset=0, i=0, n, got; int result=1;
    h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                    FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("CREATE_NEW failed: %lu (existing files are never overwritten)\n",
               (unsigned long)GetLastError()); return 1;
    }
    if (GetFileType(h) != FILE_TYPE_DISK) {
        printf("Refusing a non-disk-file handle\n"); CloseHandle(h); return 1;
    }
    buf = VirtualAlloc(NULL, MAX_IO, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!buf) { printf("Allocation failed: %lu\n",(unsigned long)GetLastError()); goto done; }
    alignment(h);
    while (offset<TOTAL) {
        n=lengths[i++ % (sizeof(lengths)/sizeof(lengths[0]))];
        if(n>TOTAL-offset)n=TOTAL-offset;
        pattern(buf,offset,n); got=0;
        SetLastError(0);
        if(!WriteFile(h,buf,n,&got,NULL) || got!=n) {
            printf("WRITE FAIL offset=%lu length=%lu bytes=%lu error=%lu\n",
                   (unsigned long)offset,(unsigned long)n,(unsigned long)got,
                   (unsigned long)GetLastError()); goto free_buffer;
        }
        offset+=n;
    }
    if (!FlushFileBuffers(h)) {
        printf("FLUSH FAIL error=%lu\n",(unsigned long)GetLastError()); goto free_buffer;
    }
    printf("CREATE PASS bytes=%lu; file retained for independent verification\n",
           (unsigned long)offset); result=0;
free_buffer:
    VirtualFree(buf,0,MEM_RELEASE);
done:
    if(!CloseHandle(h)) { printf("CLOSE FAIL error=%lu\n",(unsigned long)GetLastError());result=1; }
    return result;
}
static int verify_test(const char *path, BYTE *arena, BYTE *ref, BYTE *out) {
    HANDLE h; LARGE_INTEGER size; DWORD offset,n,i=0; int mode,result=0;
    for(mode=0; mode<2; ++mode) {
        h=open_read(path,mode);
        if(h==INVALID_HANDLE_VALUE) { printf("OPEN FAIL %s error=%lu\n",names[mode],(unsigned long)GetLastError());return 1; }
        if(!GetFileSizeEx(h,&size) || size.QuadPart!=TOTAL) {
            printf("SIZE FAIL: expected %lu bytes\n",(unsigned long)TOTAL);CloseHandle(h);return 1;
        }
        if (mode == 1) alignment(h);
        for(offset=0; offset<TOTAL;offset+=n) {
            n=lengths[(i++ + 3) % (sizeof(lengths)/sizeof(lengths[0]))];
            if(n>TOTAL-offset)n=TOTAL-offset;
            pattern(ref,offset,n);
            if(read_at(h,mode,offset,n,arena,(i&1)*PAGE,out) || mismatch(ref,out,n,offset)) {result=1;break;}
        }
        CloseHandle(h);
        if(result)return result;
        printf("VERIFY PASS mode=%s bytes=%lu\n",names[mode],(unsigned long)TOTAL);
    }
    return 0;
}
static int compare_reads(const char *path, BYTE *arena, BYTE *ref, BYTE *out, BYTE *after) {
    static const DWORD offsets[]={0,PAGE,0,PAGE};
    static const DWORD sizes[]={PAGE,PAGE,65536,65536};
    HANDLE base,h; LARGE_INTEGER length; DWORD c,mode,skew,n,off; int result=0,fail;
    base=open_read(path,0);
    if(base==INVALID_HANDLE_VALUE) {printf("OPEN FAIL error=%lu\n",(unsigned long)GetLastError());return 1;}
    if(!GetFileSizeEx(base,&length)) {CloseHandle(base);return 1;}
    if(length.QuadPart<2*PAGE) {printf("Need at least 8192 bytes for header/backup-header tests\n");CloseHandle(base);return 1;}
    for(mode=1;mode<=3;++mode) {
        h=open_read(path,mode);
        if(h==INVALID_HANDLE_VALUE) {printf("OPEN FAIL %s error=%lu\n",names[mode],(unsigned long)GetLastError());result=1;continue;}
        if (mode == 1) alignment(h);
        for(c=0;c<4;++c) {
            off=offsets[c];n=sizes[c];
            if((LONGLONG)off+n>length.QuadPart)continue;
            for(skew=0;skew<=PAGE;skew+=PAGE) {
                printf("READ TEST mode=%s offset=%lu length=%lu buffer_skew=%lu\n",
                       names[mode],(unsigned long)off,(unsigned long)n,(unsigned long)skew);
                if(read_at(base,0,off,n,arena,0,ref)) {result=1;continue;}
                fail=read_at(h,mode,off,n,arena,skew,out);
                if(read_at(base,0,off,n,arena,0,after)) {result=1;continue;}
                if(memcmp(ref,after,n)) {printf("INDETERMINATE: buffered reference changed during test\n");result=1;continue;}
                if(fail) {result=1;continue;}
                if(mismatch(ref,out,n,off))result=1;
                else printf("READ PASS\n");
            }
        }
        CloseHandle(h);
    }
    CloseHandle(base);return result;
}
int main(int argc,char **argv) {
    BYTE *arena,*ref,*out,*after; SYSTEM_INFO info; int r;
    setvbuf(stdout,NULL,_IONBF,0);
    if(argc!=3 || (strcmp(argv[1],"create") && strcmp(argv[1],"verify") && strcmp(argv[1],"compare"))) {
        printf("IoIntegrityProbe v1 (XP/i586)\n"
               "  create NEW_FILE : create a 64 MiB known-pattern file, then verify\n"
               "  verify FILE     : read-only known-pattern verification\n"
               "  compare FILE    : read-only buffered/direct/async/scatter comparison\n");return 2;
    }
    /* Restrict the only writing mode to ordinary absolute drive-letter paths. */
    if(!strcmp(argv[1],"create") && (strlen(argv[2])<4 || argv[2][1]!=':' || argv[2][2]!='\\' || strchr(argv[2]+2,':') || ((argv[2][0]<'A' || argv[2][0]>'Z') && (argv[2][0]<'a' || argv[2][0]>'z')))) {
        printf("create requires an ordinary absolute path, e.g. C:\\io-test.bin\n");return 2;
    }
    GetSystemInfo(&info);
    if(info.dwPageSize!=PAGE) {printf("This build requires 4096-byte memory pages\n");return 2;}
    printf("IoIntegrityProbe v1 mode=%s path=%s\n",argv[1],argv[2]);
    arena=VirtualAlloc(NULL,2*MAX_IO+PAGE,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    ref=VirtualAlloc(NULL,MAX_IO,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    out=VirtualAlloc(NULL,MAX_IO,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    after=VirtualAlloc(NULL,MAX_IO,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!arena||!ref||!out||!after) {printf("Allocation failed\n");return 1;}
    if(!strcmp(argv[1],"compare"))r=compare_reads(argv[2],arena,ref,out,after);
    else {
        r=!strcmp(argv[1],"create") ? create_test(argv[2]) : 0;
        if(!r)r=verify_test(argv[2],arena,ref,out);
        if(!r)r=compare_reads(argv[2],arena,ref,out,after);
    }
    printf("RESULT: %s\n",r?"FAIL (see errors above)":"PASS");
    VirtualFree(arena,0,MEM_RELEASE);VirtualFree(ref,0,MEM_RELEASE);
    VirtualFree(out,0,MEM_RELEASE);VirtualFree(after,0,MEM_RELEASE);return r;
}
