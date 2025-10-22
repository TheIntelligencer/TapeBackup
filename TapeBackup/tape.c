#include "tape.h"

/* --------------------------------------
Tape low-level helpers
-------------------------------------- */
BOOL TapeRewind(HANDLE h)
{
    DWORD result;

    result = SetTapePosition(h, TAPE_REWIND, 0, 0, 0, FALSE);
    if (result != NO_ERROR)
    {
        SetLastError(result);
        return FALSE;
    }

    SetLastError(NO_ERROR);
    return TRUE;
}

BOOL TapeGetMediaInfo(HANDLE h, ULONGLONG *capBytes,
    DWORD *blockSize, BOOL *writeProtected)
{
    DWORD                       tapempsize;
    TAPE_GET_MEDIA_PARAMETERS   tapemp;
    DWORD                       result;

    tapempsize = sizeof(TAPE_GET_MEDIA_PARAMETERS);
    ZeroMemory(&tapemp, sizeof(tapemp));

    result = GetTapeParameters(h, GET_TAPE_MEDIA_INFORMATION, &tapempsize, &tapemp);

    if (result != NO_ERROR)
    {
        if (capBytes) *capBytes = 0;
        if (blockSize) *blockSize = 0;
        if (writeProtected) *writeProtected = FALSE;
        SetLastError(result);
        return FALSE;
    }

    if (capBytes) *capBytes = (ULONGLONG)tapemp.Capacity.QuadPart;
    if (blockSize) *blockSize = tapemp.BlockSize;
    if (writeProtected) *writeProtected = tapemp.WriteProtected;

    SetLastError(NO_ERROR);
    return TRUE;
}

BOOL TapeGetDriveInfo(HANDLE h, TAPE_GET_DRIVE_PARAMETERS *out)
{
    DWORD tapedps;
    DWORD result;

    tapedps = sizeof(TAPE_GET_DRIVE_PARAMETERS);
    result = GetTapeParameters(h, GET_TAPE_DRIVE_INFORMATION, &tapedps, out);
    if (result != NO_ERROR)
    {
        SetLastError(result);
        return FALSE;
    }

    SetLastError(NO_ERROR);
    return TRUE;
}

BOOL TapeSetCompression(HANDLE h, BOOL enable)
{
    TAPE_GET_DRIVE_PARAMETERS   tapedp;
    TAPE_SET_DRIVE_PARAMETERS   tapesdp;
    DWORD                       result;

    ZeroMemory(&tapedp, sizeof(tapedp));
    if (!TapeGetDriveInfo(h, &tapedp)) return FALSE;

    if (!(tapedp.FeaturesLow & TAPE_DRIVE_COMPRESSION) &&
        !(tapedp.FeaturesHigh & TAPE_DRIVE_COMPRESSION))
    {
        SetLastError(NO_ERROR);
        return FALSE;
    }

    ZeroMemory(&tapesdp, sizeof(tapesdp));
    tapesdp.Compression = enable ? TRUE : FALSE;
    result = SetTapeParameters(h, SET_TAPE_DRIVE_INFORMATION, &tapesdp);
    if (result != NO_ERROR)
    {
        SetLastError(result);
        return FALSE;
    }

    SetLastError(NO_ERROR);
    return TRUE;
}

BOOL TapeWriteFilemark(HANDLE h)
{
    DWORD result;

    result = WriteTapemark(h, TAPE_FILEMARKS, 1, FALSE);
    if (result != NO_ERROR)
    {
        SetLastError(result);
        return FALSE;
    }

    SetLastError(NO_ERROR);
    return TRUE;
}

BOOL TapeEraseLong(HANDLE h)
{
    DWORD result;

    result = EraseTape(h, TAPE_ERASE_LONG, FALSE);
    if (result != NO_ERROR)
    {
        SetLastError(result);
        return FALSE;
    }

    SetLastError(NO_ERROR);
    return TRUE;
}

BOOL TapeIsMediaLoaded(HANDLE h)
{
    DWORD result;

    result = GetTapeStatus(h);

    if (result == NO_ERROR)
    {
        SetLastError(NO_ERROR);
        return TRUE;
    }

    if (result == ERROR_NO_MEDIA_IN_DRIVE)
        SetLastError(NO_ERROR);
    else
        SetLastError(result);

    return FALSE;
}

BOOL TapePrepareToWork(HANDLE h)
{
    DWORD                       result = 0;
    TAPE_GET_DRIVE_PARAMETERS   gtdi;
    DWORD                       gtdi_size;

    memset(&gtdi, 0, sizeof(gtdi));
    gtdi_size = sizeof(TAPE_GET_DRIVE_PARAMETERS);
    result = GetTapeParameters(h, GET_TAPE_DRIVE_INFORMATION,
        &gtdi_size, &gtdi);
    if (result != NO_ERROR)
    {
        SetLastError(result);
        return FALSE;
    }
    
    if ((gtdi.FeaturesHigh & TAPE_DRIVE_LOAD_UNLOAD) != 0)
    {
        result = PrepareTape(h, TAPE_LOAD, FALSE);
        if (result != NO_ERROR)
        {
            SetLastError(result);
            return FALSE;
        }
    }

    if ((gtdi.FeaturesHigh & TAPE_DRIVE_TENSION) != 0)
    {
        result = PrepareTape(h, TAPE_TENSION, FALSE);
        if (result != NO_ERROR)
        {
            SetLastError(result);
            return FALSE;
        }
    }

    return TRUE;
}

BOOL TapeSetVariableBlockSize(HANDLE h)
{
    DWORD                       result = 0;
    TAPE_SET_MEDIA_PARAMETERS   tsmp;

    memset(&tsmp, 0, sizeof(TAPE_SET_MEDIA_PARAMETERS));
    //Setting dynamic block size if this possible
    tsmp.BlockSize = 0;

    result = SetTapeParameters(h, SET_TAPE_MEDIA_INFORMATION, &tsmp);
    if (result != NO_ERROR)
    {
        SetLastError(result);
        return FALSE;
    }

    return TRUE;
}

void TapeReaderInit(TAPE_READER *tr, HANDLE h)
{
    ZeroMemory(tr, sizeof(*tr));
    tr->h = h;
}

BOOL TapeReaderFill(TAPE_READER *tr)
{
    DWORD   retbytes = 0;
    BOOL    result;
    DWORD   resultcode;

    if (tr->atFilemark) return FALSE;

    result = ReadFile(tr->h, tr->buf, TAPE_IO_BUF, &retbytes, NULL);
    if (!result)
    {
        resultcode = GetLastError();
        if (resultcode == ERROR_FILEMARK_DETECTED ||
            resultcode == ERROR_END_OF_MEDIA)
        {
            tr->atFilemark = TRUE;
            tr->pos = 0;
            tr->avail = 0;
            SetLastError(NO_ERROR);
        }

        return FALSE;
    }

    tr->pos = 0;
    tr->avail = retbytes;

    return (retbytes > 0);
}

DWORD TapeReaderGet(TAPE_READER *tr, BYTE *dst, DWORD need)
{
    DWORD total = 0;
    DWORD take;

    while (need > 0)
    {
        if (tr->avail == 0)
            if (!TapeReaderFill(tr))
                break;

        take = tr->avail;
        if (take > need) take = need;
        if (dst) memcpy(dst + total, tr->buf + tr->pos, take);
        tr->pos += take;
        tr->avail -= take;
        total += take;
        need -= take;
    }

    return total;
}
