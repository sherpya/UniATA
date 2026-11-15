# UniATA

UniATA is a universal ATA/ATAPI storage miniport driver by Alexander A.
Telyatnikov (Alter) for the Windows NT family. It supports IDE, SATA and AHCI
controllers, bus-master DMA, disks and ATAPI devices. Some RAID controllers
can be used as ordinary ATA controllers; RAID arrays are not supported.

This repository imports the upstream release history and carries local
maintenance work. The imported base is **UniATA 0.47b**. It is not the upstream
project's official repository.

- [Upstream project and downloads](https://alter.org.ua/en/soft/win/uni_ata/)
- [Upstream change log](chglog.txt)
- [Original build instructions](building.txt)
- [Linux cross-build instructions](BUILD-LINUX.txt)

## Build on Linux

Install GNU make, the i686 MinGW-w64 C/C++ toolchain and binutils, Python 3,
and `pefile` for the static checks. The cross build targets **Windows XP SP3
x86 / i586**, independently of the older Windows versions supported upstream.

```sh
make -j4
make check
make test
make DEBUG=1 -j4
make DEBUG=1 check
```

Release output is `build/release/uniata.sys`. Debug output is
`build/debug-compact/uniata.sys`; use `DEBUG_LOG=full` with `DEBUG=1` for the
unfiltered upstream diagnostics. Both use NT `DbgPrint`, leaving transport
ownership to the Windows debugger. Release builds omit debug logging.

The original NMAKE files remain in `driver/`. The cross build uses the
MinGW-w64 DDK headers and local compatibility helpers; it does not link the
bundled proprietary-toolchain libraries or require Wine.

## Repository layout

| Path | Contents |
| --- | --- |
| `driver/` | Miniport source, hardware definitions and original build files |
| `driver/Dist/` | Upstream installation files and platform-specific notes |
| `atactl/` | ATA controller management utility source |
| `build_atacmd/`, `build_inf/` | Upstream command-table and INF generators |
| `cross/` | Linux build support, PE validation and host regression tests |
| `tools/IoIntegrityProbe/` | XP-compatible I/O comparison and data-integrity probe |

## Installation and validation

For XP, start with [the upstream installation notes](driver/Dist/readme_xp.txt).
They distinguish `uata_xph.inf` for onboard IDE controllers from `uata_xp.inf`
for additional/RAID controllers. The management-port device also needs
`uata_comm.inf`. The files in `driver/Dist/` are imported upstream material;
`make` produces a driver binary, not an updated or signed installation package.

`make check` validates the PE format, imports, entry point and instruction set.
`make test` runs host-side regression tests. Neither establishes hardware
compatibility. Use a recoverable test installation when replacing a boot
storage driver, and preserve the previously working driver and disk image.
The [I/O probe](tools/IoIntegrityProbe/README.txt) verifies a known test pattern
and supports independent readback after shutdown through another storage path.

## License and credits

The driver identifies its license as GPLv2 in its source headers. Preserve
individual copyright and attribution notices, including those for the original
NT miniport skeleton, FreeBSD-derived code and subsequent contributions.
Refer to the source headers for each component; the imported release also
contains binary build/distribution files that are not rebuilt by this Makefile.
