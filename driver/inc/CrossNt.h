#ifndef __CROSS_VERSION_LIB_NT__H__
#define __CROSS_VERSION_LIB_NT__H__

extern "C" {

#pragma pack(push, 8)

#include <ntddk.h>                  // various NT definitions
#include <stddef.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include "ntddk_ex.h"
#include "tools.h"

#pragma pack(pop)

extern "C"
NTSTATUS
CrNtInit(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    );

extern "C"
PVOID
CrNtGetModuleBase(
    IN PCHAR  pModuleName
    );

extern "C"
PVOID
CrNtFindModuleBaseByPtr(
    IN PVOID  ptrInSection,
    IN PCHAR  ptrExportedName
    );

extern "C"
PVOID
CrNtGetProcAddress(
    PVOID ModuleBase,
    PCHAR pFunctionName
    );

#ifdef _DEBUG

// NT3.51 doesn't export strlen() and strcmp()
// The same time, Release build doesn't depend no these functions since they are inlined

size_t __cdecl CrNtstrlen (
        const char * str
        );

int __cdecl CrNtstrcmp (
        const char * src,
        const char * dst
        );

#define strlen CrNtstrlen
#define strcmp CrNtstrcmp

#endif //_DEBUG

#define CROSSNT_DECL_API

#include "CrNtDecl.h"
#include "CrNtStubs.h"

#undef CROSSNT_DECL_API

}; // end extern "C"

#endif //__CROSS_VERSION_LIB_NT__H__