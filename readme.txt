HOW TO BUILD:

1. change working directory to "driver"
2. execute one of the following commands:

Release/Debug version of UniATA without build_inf.exe
-----------------------------------------------------

nmake CFG="IdeDma - Win32 Release" NO_BUILD_INF=1
nmake CFG="IdeDma - Win32 Debug" NO_BUILD_INF=1

Release/Debug version of UniATA with build_inf.exe
--------------------------------------------------

nmake CFG="IdeDma - Win32 Release"
nmake CFG="IdeDma - Win32 Debug"

Build all and pack source tree to archive
-----------------------------------------

assume version is 384.r

nmake PKG VER="384r"
or
nmake PKG VER="384r" NO_BUILD_INF=1

new archives
BusMaster_v384r.rar and BusMaster_v384r_src.rar
will be created

