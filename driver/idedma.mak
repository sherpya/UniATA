#////////////////////////////////////////////////////////////////////
#// Alexander A. Telyatnikov (Alter) 2002-2004. Kiev, Ukraine
#// All rights reserved
#////////////////////////////////////////////////////////////////////

!IF "$(CFG)" == ""
CFG=UniATA - Win32 Debug
!MESSAGE No configuration specified. Defaulting to UniATA - Win32 Debug.
!ENDIF 

!IF "$(CFG)" != "UniATA - Win32 Release" && "$(CFG)" != "UniATA - Win32 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "UniATA.mak" CFG="UniATA - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "UniATA - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "UniATA - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 
!ERROR An invalid configuration is specified.
!ENDIF 

!IF "$(OS)" == "Windows_NT"
NULL=
!ELSE 
NULL=nul
!ENDIF 

#makefile for nt4
BaseDir=$(BASEDIR)
DDKINC=/I $(BaseDir)\inc

CPP=cl.exe
MTL=midl.exe
RSC=rc.exe
LINK32=link.exe

!IF  "$(CFG)" == "UniATA - Win32 Release"

OUTDIR=.\Release
INTDIR=.\Release

!ELSEIF  "$(CFG)" == "UniATA - Win32 Debug"

OUTDIR=.\Debug
INTDIR=.\Debug

!ENDIF 

# Begin Custom Macros
OutDir=$(OUTDIR)
BaseDir=$(BASEDIR)
DistDir=$(OUTDIR)_Dist
# End Custom Macros

TargetPath=$(OUTDIR)\IdeDma.sys
InputPath=$(OUTDIR)\IdeDma.sys
SOURCE="$(InputPath)"

DEF_FILE= \
	".\IdeDma.def"
LINK32_OBJS= \
	"$(INTDIR)\id_probe.obj" \
	"$(INTDIR)\id_ata.obj" \
	"$(INTDIR)\id_dma.obj" \
	"$(INTDIR)\id_init.obj" \
	"$(INTDIR)\idedma.res"


!IF  "$(CFG)" == "UniATA - Win32 Release"
ALL : "$(OUTDIR)\IdeDma.sys" ".\copy.msg"
!ELSEIF  "$(CFG)" == "UniATA - Win32 Debug"
ALL : "$(OUTDIR)\IdeDma.sys" ".\copy.msg"
!ENDIF 

"build_inf.exe" : "bm_devs.h"
!IF "$(NO_BUILD_INF)" == ""
	cd ..\build_inf
	nmake CFG="build_inf - Win32 Release"
	cd ..\driver
	copy ..\build_inf\Release\build_inf.exe .\build_inf.exe
	copy ..\build_inf\Release\build_inf.exe .\Dist\build_inf.exe
!ENDIF 

"Dist\uata2k.in_" :
	@echo do nothing

"Dist\uata2k.inf" : "build_inf.exe" "Dist\uata2k.in_"
	build_inf.exe Dist\uata2k.in_ > Dist\uata2k.inf

"Dist\uata2kh.inf" : "Dist\uata2k.inf"
        copy Dist\uata2k.inf Dist\uata2kh.inf
        srchrep -src "Class=SCSIAdapter" -dest "Class=hdc" Dist\uata2kh.inf
        srchrep -src "ClassGuid={4D36E97B-E325-11CE-BFC1-08002BE10318}" -dest "ClassGuid={4D36E96A-E325-11CE-BFC1-08002BE10318}" Dist\uata2kh.inf

#ALL :
#	nmake CFG="UniATA - Win32 Release"
#	nmake CFG="UniATA - Win32 Debug"

PKG : "BusMaster_v$(VER).rar"

"Release\idedma.sys" :
	nmake CFG="UniATA - Win32 Release"

"Debug\idedma.sys" :
	nmake CFG="UniATA - Win32 Debug"

"BusMaster_v$(VER).rar" : "$(OUTDIR)\IdeDma.sys" ".\copy.msg"
!IF  "$(CFG)" == "UniATA - Win32 Release"
	nmake CFG="UniATA - Win32 Debug"
	nmake CFG="UniATA - Win32 Release"
!ELSEIF  "$(CFG)" == "UniATA - Win32 Debug"
	nmake CFG="UniATA - Win32 Release"
	nmake CFG="UniATA - Win32 Debug"
!ENDIF 
	rar a -s -mdD -m5 BusMaster_v$(VER).rar @pkg_files.dist
	rar a -s -mdD -m5 BusMaster_v$(VER)_src.rar @pkg_files.src

CLEAN :
	-@erase $(LINK32_OBJS)
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(OUTDIR)\IdeDma.sys"
	-@erase ".\copy.msg"
!IF  "$(CFG)" == "UniATA - Win32 Debug"
	-@erase "$(OUTDIR)\IdeDma.map"
	-@erase "$(OUTDIR)\IdeDma.pdb"
!ENDIF 

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

"$(DistDir)" :
    if not exist "$(DistDir)/$(NULL)" mkdir "$(DistDir)"

!IF  "$(CFG)" == "UniATA - Win32 Release"

CPP_PROJ=/nologo /Gz /MT /W3 /GX /O2 $(DDKINC) /DNDEBUG /DWIN32 /D_WINDOWS /D_MBCS /D_USRDLL /DIdeDma_EXPORTS /D_X86_ /D_WIN32_WINNT=0x0400 /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /c 

.c{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

MTL_PROJ=/nologo /D "NDEBUG" /mktyplib203 /win32 
	
LINK32_FLAGS=$(BaseDir)\Lib\i386\Free\ntoskrnl.lib $(BaseDir)\Lib\i386\Free\int64.lib $(BaseDir)\Lib\i386\Free\Hal.lib $(BaseDir)\Lib\i386\Free\Ntdll.lib $(BaseDir)\Lib\i386\Free\ScsiPort.lib /nologo /entry:"DriverEntry" /incremental:no /pdb:"$(OUTDIR)\IdeDma.pdb" /machine:I386 /nodefaultlib /def:".\IdeDma.def" /out:"$(OUTDIR)\IdeDma.sys" /driver /subsystem:native 
#LINK32_FLAGS=$(BaseDir)\Lib\i386\Free\ntoskrnl.lib $(BaseDir)\Lib\i386\Free\int64.lib $(BaseDir)\Lib\i386\Checked\Hal.lib /nologo /entry:"DriverEntry" /incremental:no /pdb:"$(OUTDIR)\IdeDma.pdb" /machine:I386 /nodefaultlib /def:".\IdeDma.def" /out:"$(OUTDIR)\IdeDma.sys" /driver /subsystem:native 

!ELSEIF  "$(CFG)" == "UniATA - Win32 Debug"

CPP_PROJ=/nologo /Gz /MTd /W3 /GX /Z7 /Od $(DDKINC) /D_DEBUG /DDBG /DWIN32 /D_WINDOWS /D_MBCS /D_USRDLL /DIdeDma_EXPORTS /D_X86_ /D_WIN32_WINNT=0x0400 /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /c 

.c{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.obj::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

MTL_PROJ=/nologo /D "_DEBUG" /mktyplib203 /win32 

LINK32_FLAGS=$(BaseDir)\Lib\i386\Checked\ntoskrnl.lib $(BaseDir)\Lib\i386\Checked\int64.lib $(BaseDir)\Lib\i386\Checked\Hal.lib $(BaseDir)\Lib\i386\Checked\Ntdll.lib $(BaseDir)\Lib\i386\Checked\ScsiPort.lib /nologo /entry:"DriverEntry" /incremental:no /pdb:"$(OUTDIR)\IdeDma.pdb" /map:"$(INTDIR)\IdeDma.map" /debug /machine:I386 /nodefaultlib /def:".\IdeDma.def" /out:"$(OUTDIR)\IdeDma.sys" /pdbtype:sept /driver /subsystem:native,4.00 

!ENDIF 

"$(OUTDIR)\IdeDma.sys" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

".\copy.msg" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)" "$(DistDir)" "Dist\uata2k.inf" "Dist\uata2kh.inf"
	<<tempfile.bat 
	@echo off 
	copy $(TargetPath) C:\WINNT\System32\Drivers\IdeDma.sys 
	copy $(TargetPath) C:\WINNT\System32\Drivers\IdeDmb.sys 
	copy $(TargetPath) $(DistDir)\uniata.sys
	copy Dist\* $(DistDir)
	echo $(DistDir)
	echo Driver copied > copy.msg 
<< 

!IF "$(NO_EXTERNAL_DEPS)" != "1"
!IF EXISTS("IdeDma.dep")
!INCLUDE "IdeDma.dep"
!ELSE 
!MESSAGE Warning: cannot find "IdeDma.dep"
!ENDIF 
!ENDIF 


REL_PATH=.
SRC_EXT=cpp

SRC=id_ata
!INCLUDE build.mak

SRC=id_dma
!INCLUDE build.mak

SRC=id_init.cpp
!INCLUDE build.mak

SRC=id_probe
!INCLUDE build.mak

SRC=idedma

"$(INTDIR)\$(SRC).res" : "$(SRC).rc" "$(INTDIR)"
    -@erase ".\$(SRC).res"
    $(RSC) $(RSC_PROJ) "$(SRC).rc"
    -@erase "$(INTDIR)\$(SRC).res"
    -@move "$(SRC).res" "$(INTDIR)\$(SRC).res"

