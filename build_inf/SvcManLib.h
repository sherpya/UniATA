#ifndef __SERVICE_MANAGEMENT_LIB__H__
#define __SERVICE_MANAGEMENT_LIB__H__

#include "windows.h"
#include "winsvc.h"
#include <stdio.h>

UINT
NtServiceIsRunning(
    LPCTSTR ServiceName
    );

UINT
NtServiceStart(
    LPCTSTR ServiceName
    );

UINT
NtServiceInstall(
    LPCTSTR ServiceName,
    PCHAR   PathToExecutable
    );

#endif //__SERVICE_MANAGEMENT_LIB__H__
