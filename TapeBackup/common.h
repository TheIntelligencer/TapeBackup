/* =========================
    
 TapeBackup - ZEROTAPE format backup utility
 Version - 1.0.1-c

========================= */
#ifndef __TAPE_BACKUP
#define __TAPE_BACKUP

#define _WIN32_WINNT 0x0501 /* Windows XP */
#define _CRT_SECURE_NO_WARNINGS

//import section
#include <windows.h>
#include <winioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <tchar.h>
#include <ctype.h>
#include <stdint.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>

#ifndef STORAGE_PROPERTY_QUERY
#include "ntddstor.h"
#endif

/* --------------------------------------
Global selection state
-------------------------------------- */
typedef struct _TAPE_SELECTION {
    BOOL   hasSelection;
    WCHAR  devicePath[32]; /* e.g. L"\.\TAPE0" */
    WCHAR  vendor[64];
    WCHAR  model[64];
    WCHAR  serial[128];
    BOOL   mediaLoaded;
    ULONGLONG mediaCapacityBytes; /* if known, else 0 */
    DWORD  mediaBlockSize;        /* 0 if variable/unknown */
} TAPE_SELECTION;

#endif