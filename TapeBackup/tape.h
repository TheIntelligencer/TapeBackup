#ifndef __TAPE_BACKUP_TAPE
#define __TAPE_BACKUP_TAPE

#include "common.h"

#define TAPE_IO_BUF 64 * 1024

/* --------------------------------------
Tape low-level helpers
-------------------------------------- */
BOOL TapeRewind(HANDLE h);
BOOL TapeGetMediaInfo(HANDLE h, ULONGLONG *capBytes,
    DWORD *blockSize, BOOL *writeProtected);
BOOL TapeGetDriveInfo(HANDLE h, TAPE_GET_DRIVE_PARAMETERS *out);
BOOL TapeSetCompression(HANDLE h, BOOL enable);
BOOL TapeWriteFilemark(HANDLE h);
BOOL TapeEraseLong(HANDLE h);
BOOL TapeIsMediaLoaded(HANDLE h);
BOOL TapePrepareToWork(HANDLE h);
BOOL TapeSetVariableBlockSize(HANDLE h);

/* --------------------------------------
Buffered tape reader (for TAR)
-------------------------------------- */
typedef struct _TAPE_READER {
    HANDLE  h;
    BYTE    buf[TAPE_IO_BUF];
    DWORD   pos;
    DWORD   avail;
    BOOL    atFilemark;
} TAPE_READER;

void TapeReaderInit(TAPE_READER *tr, HANDLE h);
BOOL TapeReaderFill(TAPE_READER *tr);
DWORD TapeReaderGet(TAPE_READER *tr, BYTE *dst, DWORD need);

#endif