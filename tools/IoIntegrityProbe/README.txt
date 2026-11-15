IoIntegrityProbe v1 -- Windows XP / i586

Purpose
Distinguish rejected I/O requests (especially Win32 error87) from wrong file
contents. This utility does not install or modify any driver. Run it with
the storage driver under investigation; QEMU is not physical-hardware validation.

Build
  make
Cross build: i686-w64-mingw32-gcc, -O2 -s, i586, no SSE/MMX, PE subsystem5.1.

On the target Windows machine, from a command prompt in the executable's directory:

  IoIntegrityProbe.exe compare C:\WINDOWS\system32\CatRoot2\edb.chk > read-test.txt 2>&1
  IoIntegrityProbe.exe create C:\uniata-test.bin > write-test.txt 2>&1

compare only reads. It compares buffered reads with unbuffered synchronous,
overlapped and scatter reads. It tests offsets0/4096 and sizes4096/65536
where the file is large enough, using buffers at allocation base and base+4096.
Scatter buffers occupy nonadjacent virtual pages (physical layout is OS-owned).
The buffered reference is checked again after each read; a changing reference
is INDETERMINATE, not evidence of disk corruption. Avoid interpreting an actively
changing database as a stable reference. FileAlignmentInformation is printed.

create requires an absolute drive-letter path, creates one NEW 64 MiB file,
and refuses an existing path. It uses NO_BUFFERING and WRITE_THROUGH, then
FlushFileBuffers. Request sizes:512,4096,64512,65536,69632,131072,262144 bytes.
These include the CS5530's 63 KiB cap and larger requests requiring splitting.
After closing the write handle, every byte is checked against the known pattern
using buffered and unbuffered reads, then the read-comparison matrix is run.
The test file is retained, even on failure. CREATE_NEW failure on rerunning
the same filename is expected; preserve the first file for Linux verification.
Only temporary diagnostic data is written; existing databases are never repaired.

  IoIntegrityProbe.exe verify C:\uniata-test.bin > verify-test.txt 2>&1

verify does not write the file and repeats known-pattern checks and read tests.
Exit codes:0 PASS,1 failed/inconclusive operation,2 usage/unsupported page size.
FAIL alone is not proof of corruption: inspect the Win32 error, transferred byte
count or exact MISMATCH offset. Output is flushed immediately, also to a log.
The filesystem's logical sectors must divide512 for the writing workload;
512-byte sectors satisfy this requirement. Memory buffers are page-aligned.

Independent check
After a normal XP shutdown, read the test file from Linux through a known
storage path (for example the SSD's USB adapter), using NTFS read-only:

  python3 verify.py /path/to/mounted-volume/uniata-test.bin

This validates every byte and prints the first mismatch offset. A faster
equivalent whole-file check is sha256sum; the v1 64 MiB file must hash to:
  523af47c42ff38cf4f4980f71019b8076bd765d1ed08cd9ece1118227b762011

Windows PASS plus a matching independent Linux hash validates this particular
write/read workload and its persistence. It does not prove every driver path safe.
Windows mismatch with a good Linux file suggests a read-path problem; Linux
mismatch after Windows PASS requires investigating persistence or write/cache
behavior. A fair driver comparison uses the same file/workload and hardware
with the Microsoft IDE driver; QEMU uses a different controller.

Validation performed
- Native XP SP3 in QEMU pc/KVM, 128 MiB, on emulated Intel PIIX3 WDMA2.
- edb.chk read-only matrix: PASS, file alignment mask3.
- 64 MiB create, buffered/direct full verification, async/scatter matrix: PASS.
- CREATE_NEW on the existing file: correctly refused with error80.
- One deliberately changed bit at offset70000: Windows verifier identified
  offset0x11170, expected7d/actual7c, and returned FAIL. Bit restored; PASS again.
- After normal XP shutdown, the test file was extracted from a copy of the VM
  snapshot and checked by the independent Python implementation on Linux: PASS.
- Linux verifier also rejected a deliberately changed bit at the same offset.
- All imported functions exist in the local XP DLLs; stripped i386 PE5.1;
  no SSE/MMX register instructions found in the executable disassembly.

References
https://learn.microsoft.com/en-us/windows/win32/fileio/file-buffering
https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfilescatter
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntqueryinformationfile
