#include "archive.h"

unsigned TarChecksum(const TAR_HDR *h)
{
    unsigned            sum = 0;
    const unsigned char *p = (const unsigned char*)h;
    size_t              i;

    for (i = 0; i < 512; i++)
    {
        if (i >= 148 && i < 156)
            sum += ' ';
        else
            sum += p[i];
    }

    return sum;
}

void TarBuildName(const TAR_HDR *h, char *out,
    size_t outsz, const char *overrideName)
{
    size_t len1;
    size_t len2;

    if (overrideName && overrideName[0])
    {
        a_strncpyz(out, outsz, overrideName);
        return;
    }

    if (h->prefix[0])
    {
        len1 = a_strnlen(h->prefix, sizeof(h->prefix));
        len2 = a_strnlen(h->name, sizeof(h->name));
        if (len1 + 1 + len2 < outsz)
        {
            memcpy(out, h->prefix, len1);
            out[len1] = '/';
            memcpy(out + len1 + 1, h->name, len2);
            out[len1 + 1 + len2] = 0;
            return;
        }
    }

    a_strncpyz(out, outsz, h->name);
}

unsigned TarChecksum512(const void* hdr)
{
    unsigned            sum = 0;
    const unsigned char *p = (const unsigned char*)hdr;
    size_t              i;

    for (i = 0; i < 512; i++)
        if (i >= 148 && i < 156)
            sum += ' ';
        else
            sum += p[i];

    return sum;
}

void U64ToOctal(ULONGLONG v, char* out, size_t n)
{
    char    tmp[32];
    int     i = 0;
    size_t  len, pos, pad;

    do
    {
        tmp[i++] = (char)('0' + (v & 7)); v >>= 3;
    } while (v && i < (int)sizeof(tmp));

    len = i;
    pos = 0;
    pad = (n > 1 && len < n - 1) ? (n - 1 - len) : 0;

    for (; pos < pad; pos++) out[pos] = ' ';

    while (len > 0 && pos < n - 1) out[pos++] = tmp[--i];

    if (pos < n) out[pos] = ' ';
}

void TarInitHeader(TAR_HDR_FULL *hdr, const char *name, ULONGLONG size)
{
    unsigned sum;

    memset(hdr, 0, 512);
    a_strncpyz(hdr->name, sizeof(hdr->name), name);
    a_strncpyz(hdr->mode, sizeof(hdr->mode), "000644");
    a_strncpyz(hdr->uid, sizeof(hdr->uid), "0000000");
    a_strncpyz(hdr->gid, sizeof(hdr->gid), "0000000");
    U64ToOctal(size, hdr->size, sizeof(hdr->size));
    U64ToOctal(0, hdr->mtime, sizeof(hdr->mtime));
    hdr->typeflag = '0';
    memcpy(hdr->magic, "ustar ", 6);
    memcpy(hdr->version, "00", 2);
    a_strncpyz(hdr->uname, sizeof(hdr->uname), "user");
    a_strncpyz(hdr->gname, sizeof(hdr->gname), "group");
    memset(hdr->chksum, ' ', sizeof(hdr->chksum));
    sum = TarChecksum512(hdr);
    U64ToOctal(sum, hdr->chksum, sizeof(hdr->chksum));
}

BOOL WriteMetadataSection(HANDLE ht, const ZEROTAPE_HEADER* zh)
{
    TAR_HDR_FULL    th;
    DWORD           wr = 0;
    BYTE            pad[512] = { 0 };
    DWORD           padneed = 512 - 128;

    TarInitHeader(&th, "metadata", 128);
    if (!WriteFile(ht, &th, 512, &wr, NULL) || wr != 512)
    {
        PrintLastErrorW(L"Failed to write metadata tar header", 0);
        return FALSE;
    }

    if (!WriteFile(ht, zh, 128, &wr, NULL) || wr != 128)
    {
        PrintLastErrorW(L"Failed to write metadata payload", 0);
        return FALSE;
    }

    if (!WriteFile(ht, pad, padneed, &wr, NULL) ||
        wr != padneed)
    {
        PrintLastErrorW(L"Failed to write metadata padding", 0);
        return FALSE;
    }

    if (!WriteFile(ht, pad, 512, &wr, NULL) ||
        wr != 512)
    {
        PrintLastErrorW(L"Failed to write TAR zero block 1", 0);
        return FALSE;
    }

    if (!WriteFile(ht, pad, 512, &wr, NULL) || wr != 512)
    {
        PrintLastErrorW(L"Failed to write TAR zero block 2", 0);
        return FALSE;
    }

    if (!TapeWriteFilemark(ht))
    {
        PrintLastErrorW(L"Failed to write filemark after metadata", 0);
        return FALSE;
    }

    return TRUE;
}

BOOL ReadMetadataFromTape(HANDLE ht, ZEROTAPE_HEADER* out)
{
    TAR_HDR_FULL    th;
    DWORD           retbytecount = 0;
    ULONGLONG       fsz;
    DWORD           skip;
    BYTE            tmp[512];

    wprintf(L"Please wait until tape rewound...\r\n");
    if (!TapeRewind(ht))
    {
        PrintLastErrorW(L"Failed to rewind", 0);
        return FALSE;
    }

    if (!ReadFile(ht, &th, 512, &retbytecount, NULL) || retbytecount != 512)
    {
        PrintLastErrorW(L"Failed to read first TAR header", 0);
        return FALSE;
    }

    if (memcmp(th.magic, "ustar ", 6) != 0)
    {
        wprintf(L"First section is not a TAR archive.\r\n");
        return FALSE;
    }

    fsz = OctalToULL(th.size, sizeof(th.size));
    if (fsz != 128ULL)
        wprintf(L"Metadata size unexpected: %I64u (expected 128)\r\n", fsz);

    if (!ReadFile(ht, out, 128, &retbytecount, NULL) || retbytecount != 128)
    {
        PrintLastErrorW(L"Failed to read metadata payload", 0);
        return FALSE;
    }

    skip = (DWORD)((512 - (fsz % 512)) % 512);
    if (skip) ReadFile(ht, tmp, skip, &retbytecount, NULL);

    ReadFile(ht, tmp, 512, &retbytecount, NULL);
    ReadFile(ht, tmp, 512, &retbytecount, NULL); /* end of tar */
    return TRUE;
}

BOOL PositionToSecondSection(HANDLE ht)
{
    DWORD result;

    wprintf(L"Please wait until tape rewound...\r\n");
    if (!TapeRewind(ht)) return FALSE;

    result = SetTapePosition(ht, TAPE_SPACE_FILEMARKS, 0, 1, 0, FALSE);
    if (result != NO_ERROR)
    {
        SetLastError(result);
        PrintLastErrorW(L"Failed to position to second section", result);
        return FALSE;
    }

    return TRUE;
}

BOOL ParsePaxAndGet(const BYTE* buf, DWORD len, const char* key,
    char* out, size_t outsz)
{
    /* Format: <len> <key>=<value>\n, where len includes entire string */

    size_t      i, lnum, pos = 0;
    size_t      recStart, recLen;
    const char  *rec, *eq;
    size_t      keyLen, valLen;

    out[0] = 0;
    while (pos < len)
    {
        /* getting length */
        i = pos;
        lnum = 0;
        while (i < len && buf[i] >= '0' && buf[i] <= '9')
        {
            lnum = lnum * 10 + (buf[i] - '0');
            i++;
        }

        if (i >= len || buf[i] != ' ') break;

        recStart = pos;
        recLen = lnum;
        if (recLen == 0 || recStart + recLen > len) break;

        /* record string in buf[recStart .. recStart+recLen-1] */
        rec = (const char*)(buf + recStart);
        eq = memchr(rec, '=', recLen);
        if (eq)
        {
            keyLen = (size_t)(eq - rec);
            /* rec ends; cut the value carefully */
            if (keyLen > 0 && keyLen < 128)
            {
                if ((size_t)keyLen == strlen(key) && memcmp(rec, key, keyLen) == 0)
                {
                    valLen = recLen - (size_t)(eq - rec) - 1 /* '=' */ - 1 /* '\n' */;
                    if (valLen >= outsz) valLen = outsz - 1;
                    memcpy(out, eq + 1, valLen);
                    out[valLen] = 0;
                    return TRUE;
                }
            }
        }
        pos += recLen;
    }
    return FALSE;
}

void AnsiOrUtf8ToWide(const char* s, size_t n, WCHAR* out, size_t cch)
{
    size_t i, m;

    if (!s)
    {
        out[0] = 0;
        return;
    }

    if (!MultiByteGuessToWide(s, n, out, cch))
    {
        m = (n < cch - 1) ? n : (cch - 1);
        for (i = 0; i<m; i++) out[i] = (WCHAR)(unsigned char)s[i];
        out[m] = 0;
    }
}

BOOL VerifyTarOnTape(HANDLE h, FILE* flog)
{
    TAPE_READER         tr;
    DWORD               tick = 0;
    VERIFY_STATS        st;
    char                pendingLongName[4096];
    char                pendingLongLink[4096];
    TAR_HDR             hdr;
    DWORD               got;
    int                 allZero;
    size_t              i;
    TAR_HDR             hdr2;
    int                 allZero2;
    const unsigned char *ph;
    const unsigned char *p2;
    unsigned            stored;
    unsigned            calc;
    ULONGLONG           fsize;
    char                type;
    int                 bad;
    char                fullname[4096];
    ULONGLONG           left;
    ULONGLONG           pad;
    DWORD               step;
    DWORD               g;
    WCHAR               wname[1024];
    WCHAR               line[1024];
    BYTE                discard[2048];
    WCHAR               sum[256];

    /***NEW:*/
    WCHAR               pendingLongNameW[4096];
    WCHAR               pendingLongLinkW[4096];

    pendingLongNameW[0] = 0;
    pendingLongLinkW[0] = 0;
    /*WEN***/

    TapeReaderInit(&tr, h);
    ZeroMemory(&st, sizeof(st));
    pendingLongName[0] = 0;
    pendingLongLink[0] = 0;

    for (;;)
    {
        got = TapeReaderGet(&tr, (BYTE*)&hdr, 512);

        if (got == 0) {
            wprintf(L"Reached filemark or end of data.\r\n");
            break;
        }

        if (got < 512) {
            wprintf(L"Short header.\r\n");
            st.filesBad++;
            break;
        }

        allZero = 1;
        ph = (const unsigned char*)&hdr;

        for (i = 0; i < 512; i++) {
            if (ph[i] != 0) {
                allZero = 0;
                break;
            }
        }

        if (allZero) {
            got = TapeReaderGet(&tr, (BYTE*)&hdr2, 512);
            if (got == 512) {
                allZero2 = 1;
                p2 = (const unsigned char*)&hdr2;
                for (i = 0; i < 512; i++) {
                    if (p2[i] != 0) {
                        allZero2 = 0;
                        break;
                    }
                }

                if (allZero2) {
                    wprintf(L"End of TAR archive.\r\n");
                    break;
                }
            }
            continue;
        }

        stored = (unsigned)OctalToULL(hdr.chksum, sizeof(hdr.chksum));
        calc = TarChecksum(&hdr);
        fsize = OctalToULL(hdr.size, sizeof(hdr.size));
        type = hdr.typeflag;
        bad = (stored != calc);

        if (type == 'L' || type == 'K' || type == 'x')
        {
            /* reading whole extended block*/
            ULONGLONG   left = fsize;
            BYTE        *payload;
            DWORD       off = 0;
            DWORD       step;
            ULONGLONG   pad;

            payload = (BYTE*)malloc((size_t)left);
            if (!payload)
            {
                wprintf(L"\nOOM\n");
                return FALSE;
            }

            while (left>0)
            {
                DWORD g;

                step = (left > 64 * 1024) ? 64 * 1024 : (DWORD)left;
                g = TapeReaderGet(&tr, payload + off, step);
                if (g == 0) { free(payload); break; }
                off += g; left -= g;
            }

            /* align to 512 */
            pad = ((fsize + 511ULL)&~511ULL) - fsize;
            while (pad>0)
            {
                BYTE skip[512];
                DWORD s, g2;

                s = (pad > sizeof(skip)) ?
                    (DWORD)sizeof(skip) :
                    (DWORD)pad;

                g2 = TapeReaderGet(&tr, skip, s);

                if (g2 == 0) break; pad -= g2;
            }

            if (type == 'L' || type == 'K')
            {
                /* GNU longname/linkname — getting as UTF-8 (7-Zip / GNU tar) */
                if (type == 'L')
                    Utf8ToWide((const char*)payload, off, pendingLongNameW, 4096);
                else
                    Utf8ToWide((const char*)payload, off, pendingLongLinkW, 4096);
            }
            else
            {
                /* PAX: finding path/linkpath (UTF-8) */
                char tmp[4096];

                if (ParsePaxAndGet(payload, off, "path", tmp, sizeof(tmp)))
                    Utf8ToWide(tmp, strlen(tmp), pendingLongNameW, 4096);
                if (ParsePaxAndGet(payload, off, "linkpath", tmp, sizeof(tmp)))
                    Utf8ToWide(tmp, strlen(tmp), pendingLongLinkW, 4096);
            }

            free(payload);
            continue; /* reading next usual header */
        }

        //building correct filename
        if (pendingLongNameW[0])
        {
            wcsncpy(wname, pendingLongNameW, 1023);
            wname[1023] = 0;
        }
        else
        {
            /* usual ustar name/prefix → glue to bytes, after guess→wide */
            TarBuildName(&hdr, fullname, sizeof(fullname), NULL);
            AnsiOrUtf8ToWide(fullname, a_strnlen(fullname, sizeof(fullname)), wname, 1024);
        }
        //reset for next iterations
        pendingLongNameW[0] = 0;

        wprintf(L"[%ws] size=%I64u bytes checksum=%ws\r\n", wname, fsize, bad ? L"FAIL" : L"OK");
        fflush(stdout);
        if (flog)
        {
            //FPrintLineUtf8 already did it (\r\n)!
            _snwprintf(line, 1024, L"%ws %I64u bytes %ws", wname, fsize, bad ? L"FAIL" : L"OK");
            FPrintLineUtf8(flog, line);
        }

        left = fsize;
        while (left > 0)
        {
            step = (left > sizeof(discard)) ? (DWORD)sizeof(discard) : (DWORD)left;
            g = TapeReaderGet(&tr, discard, step);
            if (g == 0) break; left -= g;
            st.bytesProcessed += g;
        }
        pad = ((fsize + 511ULL)&~511ULL) - fsize;
        while (pad > 0)
        {
            step = (pad > sizeof(discard)) ? (DWORD)sizeof(discard) : (DWORD)pad;
            g = TapeReaderGet(&tr, discard, step);
            if (g == 0) break; pad -= g; st.bytesProcessed += g;
        }
        pendingLongName[0] = 0;
        pendingLongLink[0] = 0;
    }

    wprintf(L"Verification summary: files=%I64u, bad=%I64u, bytes=%I64u\r\n", st.filesTotal, st.filesBad, st.bytesProcessed);
    if (flog)
    {
        FPrintLineUtf8(flog, L"========");

        //FPrintLineUtf8 already did it (\r\n)!
        _snwprintf(sum, 256, L"SUMMARY: files=%I64u bad=%I64u bytes=%I64u", st.filesTotal, st.filesBad, st.bytesProcessed);
        FPrintLineUtf8(flog, sum);
    }

    return (st.filesBad == 0);
}

BOOL ListTarTOCToFile(HANDLE h, FILE* fout)
{
    TAPE_READER         tr;
    DWORD               tick = 0;
    char                pendingLongName[4096];
    char                pendingLongLink[4096];
    TAR_HDR             hdr, hdr2;
    DWORD               retbytes;
    int                 allZero = 1;
    int                 allZero2 = 1;
    size_t              i, n;
    const unsigned char *ph;
    const unsigned char *p2;
    ULONGLONG           fsize;
    char                type;
    ULONGLONG           left;
    DWORD               step, step2;
    ULONGLONG           pad;
    char                fullname[4096];
    WCHAR               wname[1024];
    BYTE                discard[2048];

    /***NEW:*/
    WCHAR               pendingLongNameW[4096];
    WCHAR               pendingLongLinkW[4096];

    pendingLongNameW[0] = 0;
    pendingLongLinkW[0] = 0;
    /*WEN***/

    TapeReaderInit(&tr, h);
    pendingLongName[0] = 0;
    pendingLongLink[0] = 0;
    for (;;)
    {
        retbytes = TapeReaderGet(&tr, (BYTE*)&hdr, 512);
        if (retbytes == 0)
        {
            wprintf(L"Reached filemark or end of data.\r\n");
            break;
        }

        if (retbytes < 512)
        {
            wprintf(L"Short header.\r\n");
            break;
        }

        ph = (const unsigned char*)&hdr;
        for (i = 0; i < 512; i++)
            if (ph[i] != 0)
            {
                allZero = 0;
                break;
            }

        if (allZero)
        {
            retbytes = TapeReaderGet(&tr, (BYTE*)&hdr2, 512);
            if (retbytes == 512)
            {
                p2 = (const unsigned char*)&hdr2;
                for (i = 0; i < 512; i++)
                    if (p2[i] != 0)
                    {
                        allZero2 = 0;
                        break;
                    }

                if (allZero2)
                {
                    wprintf(L"End of TAR archive.\r\n");
                    break;
                }
            }
            continue;
        }

        fsize = OctalToULL(hdr.size, sizeof(hdr.size));
        type = hdr.typeflag;
      
        if (type == 'L' || type == 'K' || type == 'x')
        {
            /* reading whole extended block*/
            ULONGLONG   left = fsize;
            BYTE        *payload;
            DWORD       off = 0;
            DWORD       step;
            ULONGLONG   pad;

            payload = (BYTE*)malloc((size_t)left);
            if (!payload)
            {
                wprintf(L"\nOOM\n");
                return FALSE;
            }

            while (left>0)
            {
                DWORD g;

                step = (left > 64 * 1024) ? 64 * 1024 : (DWORD)left;
                g = TapeReaderGet(&tr, payload + off, step);
                if (g == 0) { free(payload); break; }
                off += g; left -= g;
            }

            /* align to 512 */
            pad = ((fsize + 511ULL)&~511ULL) - fsize;
            while (pad>0)
            {
                BYTE skip[512];
                DWORD s, g2;

                s = (pad > sizeof(skip)) ?
                    (DWORD)sizeof(skip) :
                    (DWORD)pad;

                g2 = TapeReaderGet(&tr, skip, s);

                if (g2 == 0) break; pad -= g2;
            }

            if (type == 'L' || type == 'K')
            {
                /* GNU longname/linkname — getting as UTF-8 (7-Zip / GNU tar) */
                if (type == 'L')
                    Utf8ToWide((const char*)payload, off, pendingLongNameW, 4096);
                else
                    Utf8ToWide((const char*)payload, off, pendingLongLinkW, 4096);
            }
            else
            {
                /* PAX: finding path/linkpath (UTF-8) */
                char tmp[4096];

                if (ParsePaxAndGet(payload, off, "path", tmp, sizeof(tmp)))
                    Utf8ToWide(tmp, strlen(tmp), pendingLongNameW, 4096);
                if (ParsePaxAndGet(payload, off, "linkpath", tmp, sizeof(tmp)))
                    Utf8ToWide(tmp, strlen(tmp), pendingLongLinkW, 4096);
            }

            free(payload);
            continue; /* reading next usual header */
        }

        //building correct filename
        if (pendingLongNameW[0])
        {
            wcsncpy(wname, pendingLongNameW, 1023);
            wname[1023] = 0;
        }
        else
        {
            /* usual ustar name/prefix → glue to bytes, after guess→wide */
            TarBuildName(&hdr, fullname, sizeof(fullname), NULL);
            AnsiOrUtf8ToWide(fullname, a_strnlen(fullname, sizeof(fullname)), wname, 1024);
        }
        //reset for next iterations
        pendingLongNameW[0] = 0;

        //some two empty strings defeat fix
        n = wcsnlen(wname, 1024);
        if (n > 0)
            if (wname[0] != L'\0')
            {
                wname[n] = 0;
                wprintf(L"%ws\r\n", wname);
                if (fout)
                    FPrintLineUtf8(fout, wname);
            }

        left = fsize;
        while (left > 0)
        {
            step2 = (left > sizeof(discard)) ?
                (DWORD)sizeof(discard) :
                (DWORD)left;

            retbytes = TapeReaderGet(&tr, discard, step2);
            if (retbytes == 0) break;
            left -= retbytes;
        }

        pad = ((fsize + 511ULL) &~511ULL) - fsize;
        while (pad > 0)
        {
            step = (pad > sizeof(discard)) ?
                (DWORD)sizeof(discard) :
                (DWORD)pad;

            retbytes = TapeReaderGet(&tr, discard, step);
            if (retbytes == 0) break;
            pad -= retbytes;
        }
    }

    return TRUE;
}

/* --------------------------------------
Section #2 I/O
-------------------------------------- */
BOOL WriteArchiveToSecondSection(HANDLE ht,
    HANDLE hf, ULONGLONG totalSize)
{
    BYTE        *buf;
    ULONGLONG   done = 0;
    BOOL        ok = TRUE;
    DWORD       toRead;
    DWORD       retbytes = 0;
    DWORD       written = 0;
    unsigned    pct;

    buf = (BYTE*)malloc(TAPE_IO_BUF);
    if (!buf)
    {
        wprintf(L"Out of memory.\r\n");
        return FALSE;
    }

    while (done < totalSize)
    {
        toRead = (DWORD)((totalSize - done) > TAPE_IO_BUF ?
            TAPE_IO_BUF :
            (totalSize - done));

        if (!ReadFile(hf, buf, toRead, &retbytes, NULL))
        {
            PrintLastErrorW(L"Failed to read source file", 0);
            ok = FALSE;
            break;
        }

        if (retbytes == 0) break;

        if (!WriteFile(ht, buf, retbytes, &written, NULL) || written != retbytes)
        {
            PrintLastErrorW(L"Failed to write to tape", 0);
            ok = FALSE;
            break;
        }

        done += retbytes;
        pct = (unsigned)((done * 100ULL) / totalSize);
        DrawProgressBar(pct, done, totalSize);
    }

    //wprintf(L"");
    free(buf);

    return ok;
}

BOOL CopySecondSectionToFileAndOrHash(HANDLE ht, ULONGLONG totalSize,
    HANDLE hf, unsigned char outSha1[20])
{
    BYTE        *buf;
    SHA1_CTX    ctx;
    ULONGLONG   done = 0;
    BOOL        ok = TRUE;
    DWORD       toRead;
    DWORD       retbytes = 0;
    BOOL        result;
    DWORD       written = 0;
    unsigned    pct;

    buf = (BYTE*)malloc(TAPE_IO_BUF);
    if (!buf)
    {
        wprintf(L"Out of memory.\r\n");
        return FALSE;
    }

    if (outSha1) sha1_init(&ctx);
    while (done < totalSize)
    {
        toRead = (DWORD)((totalSize - done) > TAPE_IO_BUF ?
            TAPE_IO_BUF :
            (totalSize - done));

        result = ReadFile(ht, buf, toRead, &retbytes, NULL);
        if (!result || retbytes == 0)
        {
            PrintLastErrorW(L"Read from tape failed before reaching expected size", 0);
            ok = FALSE;
            break;
        }

        if (hf)
            if (!WriteFile(hf, buf, retbytes, &written, NULL) ||
                written != retbytes)
            {
                PrintLastErrorW(L"Failed to write destination file", 0);
                ok = FALSE;
                break;
            }

        if (outSha1) sha1_update(&ctx, buf, retbytes);
        done += retbytes;

        pct = (unsigned)((done * 100ULL) / totalSize);

        DrawProgressBar(pct, done, totalSize);
    }

    if (outSha1 && ok) sha1_final(&ctx, outSha1);

    wprintf(L"\r\n");
    free(buf);
    return ok;
}
