#////////////////////////////////////////////////////////////////////
#// Alexander A. Telyatnikov (Alter) 2002-2003. Kiev, Ukraine
#// All rights reserved
#////////////////////////////////////////////////////////////////////

!IF "$(CFG)" == ""
CFG=IdeDma - Win32 Debug
!MESSAGE No configuration specified. Defaulting to IdeDma - Win32 Debug.
!ENDIF 

!IF "$(CFG)" != "IdeDma - Win32 Release" && "$(CFG)" != "IdeDma - Win32 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "IdeDma.mak" CFG="IdeDma - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "IdeDma - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "IdeDma - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
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
LINK32=link.exe

!IF  "$(CFG)" == "IdeDma - Win32 Release"

OUTDIR=.\Release
INTDIR=.\Release

!ELSEIF  "$(CFG)" == "IdeDma - Win32 Debug"

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

!IF  "$(CFG)" == "IdeDma - Win32 Release"
ALL : "$(OUTDIR)\IdeDma.sys" ".\copy.msg"
!ELSEIF  "$(CFG)" == "IdeDma - Win32 Debug"
ALL : "$(OUTDIR)\IdeDma.sys" "$(OUTDIR)\IdeDma.bsc" ".\copy.msg"
!ENDIF 

"build_inf.exe" : "bm_devs.h"
	cd ../build_inf
	nmake CFG="build_inf - Win32 Release"

"Dist\uniata2k.in_" :
	@echo do nothing

"Dist\uniata2k.inf" : "build_inf.exe" "Dist\uniata2k.in_"
	build_inf.exe Dist\uniata2k.in_ > Dist\uniata2k.inf

#ALL :
#	nmake CFG="IdeDma - Win32 Release"
#	nmake CFG="IdeDma - Win32 Debug"

PKG : "BusMaster_v$(VER).rar"

"BusMaster_v$(VER).rar" : "$(OUTDIR)\IdeDma.sys" ".\copy.msg"
!IF  "$(CFG)" == "IdeDma - Win32 Release"
	nmake CFG="IdeDma - Win32 Debug"
	nmake CFG="IdeDma - Win32 Release"
!ELSEIF  "$(CFG)" == "IdeDma - Win32 Debug"
	nmake CFG="IdeDma - Win32 Release"
	nmake CFG="IdeDma - Win32 Debug"
!ENDIF 
	rar a -s -mdD -m5 BusMaster_v$(VER).rar @pkg_files.dist
	rar a -s -mdD -m5 BusMaster_v$(VER)_src.rar @pkg_files.src

CLEAN :
	-@erase "$(INTDIR)\id_ata.obj"
	-@erase "$(INTDIR)\id_dma.obj"
	-@erase "$(INTDIR)\id_probe.obj"
	-@erase "$(INTDIR)\vc60.idb"
	-@erase "$(OUTDIR)\IdeDma.sys"
	-@erase ".\copy.msg"
!IF  "$(CFG)" == "IdeDma - Win32 Debug"
	-@erase "$(INTDIR)\id_ata.sbr"
	-@erase "$(INTDIR)\id_dma.sbr"
	-@erase "$(INTDIR)\id_probe.sbr"
	-@erase "$(OUTDIR)\IdeDma.bsc"
	-@erase "$(OUTDIR)\IdeDma.map"
	-@erase "$(OUTDIR)\IdeDma.pdb"
!ENDIF 

"$(OUTDIR)" :
    if not exist "$(OUTDIR)/$(NULL)" mkdir "$(OUTDIR)"

"$(DistDir)" :
    if not exist "$(DistDir)/$(NULL)" mkdir "$(DistDir)"

DEF_FILE= \
	".\IdeDma.def"
LINK32_OBJS= \
	"$(INTDIR)\id_probe.obj" \
	"$(INTDIR)\id_ata.obj" \
	"$(INTDIR)\id_dma.obj"

!IF  "$(CFG)" == "IdeDma - Win32 Release"

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

.c{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

MTL=midl.exe
MTL_PROJ=/nologo /D "NDEBUG" /mktyplib203 /win32 
RSC=rc.exe
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\IdeDma.bsc" 
BSC32_SBRS= \
	
LINK32_FLAGS=$(BaseDir)\Lib\i386\Free\ntoskrnl.lib $(BaseDir)\Lib\i386\Free\int64.lib $(BaseDir)\Lib\i386\Free\Hal.lib $(BaseDir)\Lib\i386\Free\Ntdll.lib $(BaseDir)\Lib\i386\Free\ScsiPort.lib /nologo /entry:"DriverEntry" /incremental:no /pdb:"$(OUTDIR)\IdeDma.pdb" /machine:I386 /nodefaultlib /def:".\IdeDma.def" /out:"$(OUTDIR)\IdeDma.sys" /driver /subsystem:native 
#LINK32_FLAGS=$(BaseDir)\Lib\i386\Free\ntoskrnl.lib $(BaseDir)\Lib\i386\Free\int64.lib $(BaseDir)\Lib\i386\Checked\Hal.lib /nologo /entry:"DriverEntry" /incremental:no /pdb:"$(OUTDIR)\IdeDma.pdb" /machine:I386 /nodefaultlib /def:".\IdeDma.def" /out:"$(OUTDIR)\IdeDma.sys" /driver /subsystem:native 

"$(OUTDIR)\IdeDma.sys" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ELSEIF  "$(CFG)" == "IdeDma - Win32 Debug"

CPP_PROJ=/nologo /Gz /MTd /W3 /GX /Z7 /Od $(DDKINC) /D_DEBUG /DDBG /DWIN32 /D_WINDOWS /D_MBCS /D_USRDLL /DIdeDma_EXPORTS /D_X86_ /D_WIN32_WINNT=0x0400 /FR"$(INTDIR)\\" /Fo"$(INTDIR)\\" /Fd"$(INTDIR)\\" /FD /c 

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

.c{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cpp{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

.cxx{$(INTDIR)}.sbr::
   $(CPP) @<<
   $(CPP_PROJ) $< 
<<

MTL=midl.exe
MTL_PROJ=/nologo /D "_DEBUG" /mktyplib203 /win32 
RSC=rc.exe
BSC32=bscmake.exe
BSC32_FLAGS=/nologo /o"$(OUTDIR)\IdeDma.bsc" 
BSC32_SBRS= \
	"$(INTDIR)\id_probe.sbr" \
	"$(INTDIR)\id_ata.sbr" \
	"$(INTDIR)\id_dma.sbr"

"$(OUTDIR)\IdeDma.bsc" : "$(OUTDIR)" $(BSC32_SBRS)
    $(BSC32) @<<
  $(BSC32_FLAGS) $(BSC32_SBRS)
<<

LINK32_FLAGS=$(BaseDir)\Lib\i386\Checked\ntoskrnl.lib $(BaseDir)\Lib\i386\Checked\int64.lib $(BaseDir)\Lib\i386\Checked\Hal.lib $(BaseDir)\Lib\i386\Checked\Ntdll.lib $(BaseDir)\Lib\i386\Checked\ScsiPort.lib /nologo /entry:"DriverEntry" /incremental:no /pdb:"$(OUTDIR)\IdeDma.pdb" /map:"$(INTDIR)\IdeDma.map" /debug /machine:I386 /nodefaultlib /def:".\IdeDma.def" /out:"$(OUTDIR)\IdeDma.sys" /pdbtype:sept /driver /subsystem:native,4.00 

"$(OUTDIR)\IdeDma.sys" : "$(OUTDIR)" $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ENDIF 

".\copy.msg" : $(SOURCE) "$(INTDIR)" "$(OUTDIR)" "$(DistDir)" "Dist\uniata2k.inf"
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


!IF "$(CFG)" == "IdeDma - Win32 Release" || "$(CFG)" == "IdeDma - Win32 Debug"
SOURCE=.\id_ata.cpp

!IF  "$(CFG)" == "IdeDma - Win32 Release"


"$(INTDIR)\id_ata.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "IdeDma - Win32 Debug"


"$(INTDIR)\id_ata.obj"	"$(INTDIR)\id_ata.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 


SOURCE=.\id_dma.cpp

!IF  "$(CFG)" == "IdeDma - Win32 Release"


"$(INTDIR)\id_dma.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "IdeDma - Win32 Debug"


"$(INTDIR)\id_dma.obj"	"$(INTDIR)\id_dma.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 


SOURCE=.\id_probe.cpp

!IF  "$(CFG)" == "IdeDma - Win32 Release"


"$(INTDIR)\id_probe.obj" : $(SOURCE) "$(INTDIR)"


!ELSEIF  "$(CFG)" == "IdeDma - Win32 Debug"


"$(INTDIR)\id_probe.obj"	"$(INTDIR)\id_probe.sbr" : $(SOURCE) "$(INTDIR)"


!ENDIF 


!ENDIF 

