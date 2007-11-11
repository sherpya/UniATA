@echo off
echo rebuilding .INF files
build_inf.exe uata_2k.in_ > uata_2k.inf

set SET_BAT=$set_root$.bat

copy $set_root$.ba_ $set_root$.bat >nul 2>nul
.\build_inf.exe --ver >> %SET_BAT%
call %SET_BAT% ver_dot
del %SET_BAT%

copy $set_root$.ba_ $set_root$.bat >nul 2>nul
.\build_inf.exe --timestamp >> %SET_BAT%
call %SET_BAT% ver_ts
del %SET_BAT%

srchrep -src "%%BUILD_TS%%" -dest "%ver_ts%"      uata_2k.inf > nul
srchrep -src "%%DRIVER_VER%%" -dest "%ver_dot%"   uata_2k.inf > nul

copy uata_2k.in_ uata_nt4.inf
srchrep -src "%%BUILD_TS%%" -dest "%ver_ts%"    uata_nt4.inf > nul
srchrep -src "%%DRIVER_VER%%" -dest "%ver_dot%"    uata_nt4.inf > nul
srchrep -src ";DEVLIST" -dest "%%DeviceDesc%%"  uata_nt4.inf > nul
srchrep -src ";DEVNAMELIST" -dest "DeviceDesc"  uata_nt4.inf > nul
srchrep -src "%%EDITION%%" -dest " (Win NT4)"   uata_nt4.inf > nul
srchrep -src "uata_2k.inf" -dest "uata_nt4.inf" uata_nt4.inf > nul
srchrep -src "%%CLASSNAME%%" -dest "IDE ATA/ATAPI"       uata_nt4.inf > nul
srchrep -src "System Bus Extender" -dest "SCSI miniport" uata_nt4.inf > nul

srchrep -src "%%EDITION%%" -dest " (Win 2000)"       uata_2k.inf  > nul
copy uata_2k.inf uata_2kh.inf
srchrep -src "%%COMMENT%%" -dest " (SCSI)"           uata_2k.inf  > nul
srchrep -src "Class=SCSIAdapter" -dest "Class=hdc"   uata_2kh.inf > nul
srchrep -src "ClassGuid={4D36E97B-E325-11CE-BFC1-08002BE10318}" -dest "ClassGuid={4D36E96A-E325-11CE-BFC1-08002BE10318}" uata_2kh.inf > nul
srchrep -src "%%COMMENT%%" -dest ""                  uata_2kh.inf > nul
srchrep -src "uata_2k.inf" -dest "uata_2kh.inf"      uata_2kh.inf > nul
srchrep -src "%%CLASSNAME%%" -dest "IDE ATA/ATAPI"   uata_2kh.inf > nul
srchrep -src "%%CLASSNAME%%" -dest "SCSI/RAID"       uata_2k.inf  > nul

build_inf.exe uata_xp.in_ > uata_xp.inf
srchrep -src "%%BUILD_TS%%" -dest "%ver_ts%"         uata_xp.inf > nul
srchrep -src "%%DRIVER_VER%%" -dest "%ver_dot%"      uata_xp.inf > nul

copy uata_xp.inf uata_xph.inf
srchrep -src "%%COMMENT%%" -dest ""                  uata_xph.inf > nul
srchrep -src "Class=SCSIAdapter" -dest "Class=hdc"   uata_xph.inf > nul
srchrep -src "ClassGuid={4D36E97B-E325-11CE-BFC1-08002BE10318}" -dest "ClassGuid={4D36E96A-E325-11CE-BFC1-08002BE10318}" uata_xph.inf > nul
srchrep -src "%%COMMENT%%" -dest " (SCSI)"           uata_xp.inf  > nul

copy uniata_w2k.re_ uniata_w2k.reg
srchrep -src "%%DRIVER_VER%%" -dest "%ver_dot%"      uniata_w2k.reg > nul

move uata_nt4.inf   ../NT4
move uata_2k*.inf   ../2K
move uniata_w2k.reg ../2K
move uata_xp*.inf   ../XP
