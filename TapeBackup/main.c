#include "common.h"
#include "utils.h"
#include "tape.h"
#include "archive.h"

TAPE_SELECTION g_state;

/* --------------------------------------
   Drive enumeration and selection
   -------------------------------------- */
void ShowCurrentSelection(void)
{
    WCHAR   cap[64];

    wprintf(L"Selected tape drive:\r\n");
    if (!g_state.hasSelection) 
    {
        wprintf(L"<none selected>\r\n");
        wprintf(L"========\r\n");
        return;
    }

    HumanSize(g_state.mediaCapacityBytes, cap, 64);
    wprintf(L"Device Path - %s\r\n", g_state.devicePath);
    wprintf(L"Vendor - %s\r\n", g_state.vendor);
    wprintf(L"Model - %s\r\n", g_state.model);
    wprintf(L"Serial - %s\r\n", g_state.serial);
    wprintf(L"Media - %s\r\n", g_state.mediaLoaded ? L"Loaded" : L"Not loaded");
    wprintf(L"Capacity - %s\r\n", g_state.mediaCapacityBytes ? cap : L"Unknown");
    wprintf(L"Block Size - %lu\r\n", (unsigned long)g_state.mediaBlockSize);
    wprintf(L"========\r\n");
}

BOOL ProbeTapeDevice(int id, TAPE_SELECTION *out) 
{ 
    WCHAR           path[32];
    HANDLE          tape;
    TAPE_SELECTION  ts;
    ULONGLONG       cap = 0;
    DWORD           bs = 0;
    BOOL            wp = FALSE;
    
    _snwprintf(path, 32, L"\\\\.\\TAPE%d", id);  
    tape = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, 
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, 
        OPEN_EXISTING, 0, NULL); 
    if (tape == INVALID_HANDLE_VALUE) 
        return FALSE;  
    
    ZeroMemory(&ts, sizeof(ts)); 
    wcsncpy(ts.devicePath, path, 31); 
    ts.devicePath[31] = 0;
    
    QueryStorageStrings(tape, ts.vendor, 64, ts.model, 64, ts.serial, 128); 
    ts.mediaLoaded = TapeIsMediaLoaded(tape);    
    
    TapeGetMediaInfo(tape, &cap, &bs, &wp); 
    ts.mediaCapacityBytes = cap; 
    ts.mediaBlockSize = bs; 
    ts.hasSelection = TRUE; 
    
    if (out) *out = ts; 
    CloseHandle(tape); 
    return TRUE;
}

BOOL SelectTapeInteractive(void)
{
    int             found = 0;
    int             ids[64];
    TAPE_SELECTION  tmp;
    int             i;
    WCHAR           cap[64];
    WCHAR           buf[32];
    int             chosen;
    TAPE_SELECTION  tsel;

    wprintf(L"Scanning for tape drives...\r\n");
    wprintf(L"========\r\n");
    for (i = 0; i < 64; i++)
    {
        if (ProbeTapeDevice(i, &tmp)) 
        {
            HumanSize(tmp.mediaCapacityBytes, cap, 64);
            wprintf(L"ID = %d\r\n", i);
            wprintf(L"Device Path - %s\r\n", tmp.devicePath);
            wprintf(L"Vendor - %s\r\n", tmp.vendor);
            wprintf(L"Model - %s\r\n", tmp.model);
            wprintf(L"Serial - %s\r\n", tmp.serial);
            wprintf(L"Media - %s\r\n", tmp.mediaLoaded ? L"Loaded" : L"Not loaded");
            wprintf(L"Capacity - %s\r\n", tmp.mediaCapacityBytes ? cap : L"Unknown");
            wprintf(L"Block Size - %lu\r\n", (unsigned long)tmp.mediaBlockSize);
            wprintf(L"========\r\n");
            ids[found++] = i;
        }
    }

    if (found == 0) 
    {
        wprintf(L"No tape drives available.\r\n");
        return FALSE;
    }

    wprintf(L"Enter drive ID to select (media must be loaded): ");
    if (!ReadLineW(buf, 32)) return FALSE;
    chosen = _wtoi(buf);
    for (i = 0; i < found; i++) 
    {
        if (ids[i] == chosen) 
        {
            if (!ProbeTapeDevice(chosen, &tsel)) 
            {
                wprintf(L"Failed to open selected drive.\r\n");
                return FALSE;
            }

            if (!tsel.mediaLoaded) 
            {
                wprintf(L"Selected drive has no media loaded. Please insert a tape.\r\n");
                return FALSE;
            }

            g_state = tsel;
            wprintf(L"Selected %ws\r\n", g_state.devicePath); return TRUE;
        }
    }

    wprintf(L"Invalid selection.\r\n");
    return FALSE;
}

/* --------------------------------------
   High-level actions
   -------------------------------------- */
BOOL IsTarHeaderLikely(const TAR_HDR *h) 
{
    /* header must be non-zero */
    const unsigned char *p = (const unsigned char*)h;
    size_t              i; 
    int                 allZero = 1;
    unsigned            stored;
    unsigned            calc;

    for (i = 0; i < 512; i++) 
        if (p[i] != 0) 
        { 
            allZero = 0;
            break; 
        }
    
    if (allZero) return FALSE;

    /* parse stored checksum (octal, may be NUL/space padded) */
    stored = (unsigned)OctalToULL(h->chksum, sizeof(h->chksum));
    calc = TarChecksum(h);
    if ((stored == calc) && (stored != 0)) 
        return TRUE;

    /* Some tools write with signed char sum quirks; allow small tolerance */
    if ((calc == stored + (' ' * 8)) || 
        (calc + (' ' * 8) == stored)) 
        return TRUE;

    /* As a fallback, accept common magic values if present */
    if (memcmp(h->magic, "ustar", 5) == 0) 
        return TRUE;

    return FALSE;
}

BOOL IsLikelyTarFile(LPCWSTR path)
{
    HANDLE      h; 
    BYTE        b[1024]; 
    DWORD       rd = 0; 
    BOOL        result;
    int         zero1, zero2;
    size_t      i;

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    result = ReadFile(h, b, sizeof(b), &rd, NULL);
    CloseHandle(h);
    
    if (!result || rd < 512) return FALSE;

    /* empty tar: two zero 512 byte blocks */
    if (rd >= 1024) 
    {
        zero1 = 1;
        zero2 = 1;

        for (i = 0; i < 512; i++) 
            if (b[i]) { 
                zero1 = 0; 
                break; 
            } 
        
        for (i = 512; i < 1024; i++) 
            if (b[i]) { 
                zero2 = 0; 
                break; 
            }
        
        if (zero1 && zero2) return TRUE;
    }

    /* usual case: validate first header by checksum */
    result = IsTarHeaderLikely((const TAR_HDR*)b);
    
    return result;
}

BOOL ActionRewind(void) 
{ 
    HANDLE  tape;
    BOOL    ok;

    if (!g_state.hasSelection) 
    { 
        wprintf(L"No tape drive selected. Use 'Select Tape' first.\r\n"); 
        return FALSE; 
    }  
    
    tape = CreateFileW(g_state.devicePath, GENERIC_READ | GENERIC_WRITE, 
        FILE_SHARE_READ | FILE_SHARE_WRITE, 
        NULL, OPEN_EXISTING, 0, NULL); 
    
    if (tape == INVALID_HANDLE_VALUE) 
    { 
        PrintLastErrorW(L"Cannot open tape drive", 0); 
        return FALSE; 
    } 
    
    wprintf(L"Please wait until tape rewound...\r\n");
    ok = TapeRewind(tape); 
    if (!ok) 
        PrintLastErrorW(L"Failed to rewind", 0); 
    else 
        wprintf(L"Rewound to BOT.\r\n"); 
    
    CloseHandle(tape); 
    return ok;
}

BOOL ActionPrintMetadata(void)
{
    HANDLE              tape;
    ZEROTAPE_HEADER     zh;
    WCHAR               nameW[64];
    ULONGLONG           sz;
    WCHAR               szW[64];
    WCHAR               sha1W[64];
    WCHAR               fmtW[16];
    WCHAR               timeW[64];

    if (!g_state.hasSelection) 
    {
        wprintf(L"No tape drive selected. Use 'Select Tape' first.\r\n");
        return FALSE;
    }

    tape = CreateFileW(g_state.devicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
        OPEN_EXISTING, 0, NULL);

    if (tape == INVALID_HANDLE_VALUE) 
    {
        PrintLastErrorW(L"Cannot open tape drive", 0);
        return FALSE;
    }

    if (!TapeIsMediaLoaded(tape)) 
    {
        wprintf(L"No media loaded in the selected drive.\r\n");
        CloseHandle(tape);
        return FALSE;
    }

    if (!ReadMetadataFromTape(tape, &zh)) 
    {
        wprintf(L"Failed to read ZEROTAPE metadata.\r\n");
        CloseHandle(tape);
        return FALSE;
    }

    if (memcmp(zh.magic, "ZEROTAPE", 8) != 0 || zh.version != 0) 
    {
        wprintf(L"Invalid ZEROTAPE header.\r\n");
        CloseHandle(tape);
        return FALSE;
    }

    MultiByteToWideChar(CP_ACP, 0, zh.name, -1, nameW, 64);
    sz = GetLE64(zh.sizeofarchive);
    HumanSize(sz, szW, 64);
    BytesToHex(zh.sha1, 20, sha1W, 64);
    _snwprintf(fmtW, 16, L"%s", (zh.format == 1) ? L"tar" : L"raw");
    FormatSystemTimeStr(zh.creationdate, timeW, 64);

    wprintf(L"Tape Name - %s\r\n", nameW);
    wprintf(L"Size - %s (%I64u bytes)\r\n", szW, sz);
    wprintf(L"SHA1 - %s\r\n", sha1W);
    wprintf(L"Format - %s\r\n", fmtW);
    wprintf(L"Created - %s\r\n", timeW);
    CloseHandle(tape);
    return TRUE;
}

BOOL ActionMakeBackup(void)
{
    WCHAR           path[MAX_PATH];
    ULONGLONG       fsz = 0;
    HANDLE          tape;
    WCHAR           wname[64];
    char            tname[32] = { 0 };
    int             n;
    ULONGLONG       overhead = 2048;
    BYTE            tiny[256];
    DWORD           got = 0;
    BOOL            rok;
    WCHAR           need[64], have[64];
    unsigned char   digest[20];
    BYTE            b[64 * 1024];
    DWORD           rd;
    SHA1_CTX        c;
    HANDLE          hf, hf2;
    ULONGLONG       done = 0;
    ZEROTAPE_HEADER zh;
    SYSTEMTIME      st;


    if (!g_state.hasSelection) 
    {
        wprintf(L"No tape drive selected. Use 'Select Tape' first.\r\n");
        return FALSE;
    }

    wprintf(L"Enter path to TAR file to write to tape: ");
    if (!ReadLineW(path, MAX_PATH)) return FALSE;

    if (!IsLikelyTarFile(path)) 
    {
        if (GetLastError() != NO_ERROR)
            PrintLastErrorW(L"Failed to recognize tar file!", GetLastError());
        else
            wprintf(L"The selected file does not look like a TAR. Aborting.\r\n");
        return FALSE;
    }

    if (!GetFileSize64W(path, &fsz)) 
    {
        PrintLastErrorW(L"Cannot access TAR file", 0);
        return FALSE;
    }

    tape = CreateFileW(g_state.devicePath, 
        GENERIC_READ | GENERIC_WRITE, 
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, 
        OPEN_EXISTING, 0, NULL); 

    if (tape == INVALID_HANDLE_VALUE) 
    { 
        PrintLastErrorW(L"Cannot open tape drive", 0); 
        return FALSE; 
    } 
    
    if (!TapeIsMediaLoaded(tape)) 
    { 
        wprintf(L"No media loaded in the selected drive.\r\n"); 
        CloseHandle(tape); 
        return FALSE; 
    }
    
    TapeSetCompression(tape, FALSE);
    
    wprintf(L"Enter tape name (ASCII, up to 31 chars): ");
    if (!ReadLineW(wname, 64)) 
    {
        CloseHandle(tape);
        return FALSE;
    }

    n = WideCharToMultiByte(CP_ACP, 0, wname, -1, tname, 31, NULL, NULL);
    tname[(n > 0 && n < 32) ? n : 31] = 0;

    
    if ((g_state.mediaCapacityBytes > 0) && (fsz + overhead > g_state.mediaCapacityBytes)) 
    {
        HumanSize(fsz + overhead, need, 64);
        HumanSize(g_state.mediaCapacityBytes, have, 64);
        wprintf(L"Selected TAR (with overhead %s) exceeds media capacity (%s).\r\n", need, have);
        CloseHandle(tape);
        return FALSE;
    }

    wprintf(L"Please wait until tape rewound...\r\n");
    if (!TapeRewind(tape)) 
    {
        PrintLastErrorW(L"Failed to rewind tape", 0);
        CloseHandle(tape); 
        return FALSE;
    }

    rok = ReadFile(tape, tiny, sizeof(tiny), &got, NULL);
    if (rok && got > 0) 
    {
        if (!AskYesNo(L"Tape seems to contain data. Proceed and overwrite?", FALSE)) 
        {
            CloseHandle(tape);
            return FALSE;
        }
    }

    if (!AskYesNo(L"Start writing (metadata + archive) to tape?", TRUE))
    {
        CloseHandle(tape);
        return FALSE;
    }
    
     hf = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) 
    {
        PrintLastErrorW(L"Failed to open source file", 0);
        CloseHandle(tape);
        return FALSE;
    }

    wprintf(L"Please wait until sha1 calculated...\r\n");
    sha1_init(&c);
    while (ReadFile(hf, b, sizeof(b), &rd, NULL) && rd > 0) 
    {
        unsigned pct;

        sha1_update(&c, b, rd); done += rd;
        pct = (unsigned)((done * 100ULL) / fsz);
        DrawProgressBar(pct, done, fsz);
    }
    sha1_final(&c, digest);
    wprintf(L"\r\n");
    CloseHandle(hf);

    memset(&zh, 0, sizeof(zh)); 
    memcpy(zh.magic, "ZEROTAPE", 8); 
    zh.version = 0; 
    a_strncpyz(zh.name, sizeof(zh.name), tname); 
    PutLE64(zh.sizeofarchive, fsz);
    memcpy(zh.sha1, digest, 20); zh.format = 1; 
    GetLocalTime(&st); 
    memcpy(zh.creationdate, &st, sizeof(SYSTEMTIME));
    
    wprintf(L"Please wait until tape rewound...\r\n");
    if (!TapeRewind(tape)) 
    { 
        PrintLastErrorW(L"Failed to rewind", 0); 
        CloseHandle(tape); 
        return FALSE; 
    } 
    
    wprintf(L"Writing metadata...\r\n");
    if (!WriteMetadataSection(tape, &zh)) 
    { 
        CloseHandle(tape); 
        return FALSE; 
    }
    
    hf2 = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, 
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL); 
    if (hf2 == INVALID_HANDLE_VALUE) 
    { 
        PrintLastErrorW(L"Failed to open source file", 0); 
        CloseHandle(tape); 
        return FALSE; 
    } 
    
    wprintf(L"Writing backup...\r\n");
    if (!WriteArchiveToSecondSection(tape, hf2, fsz)) 
    {
        wprintf(L"Failed to write backup!\r\n");
        CloseHandle(hf2); 
        CloseHandle(tape); 
        return FALSE; 
    } 
    wprintf(L"\r\n");
    CloseHandle(hf2);
    
    if (!TapeWriteFilemark(tape)) 
        PrintLastErrorW(L"Failed to write filemark at end of section #2", 0);
    
    CloseHandle(tape); 
    wprintf(L"Make Backup completed.\r\n"); 
    return TRUE;
}

BOOL ActionVerifyBackup(void)
{
    HANDLE              ht;
    WCHAR               dir[MAX_PATH];
    WCHAR               logPath[MAX_PATH * 2];
    FILE                *flog = NULL;
    ZEROTAPE_HEADER     zh;
    ULONGLONG           size2;
    unsigned char       digest[20];
    BOOL                okHash;
    BOOL                match;
    BOOL                okTar;
    BOOL                overall;

    // for getting metadata
    WCHAR               nameW[64];
    ULONGLONG           sz;
    WCHAR               szW[64];
    WCHAR               sha1W[64];
    WCHAR               fmtW[16];
    WCHAR               timeW[64];
    WCHAR               tmpbuf[128];

    if (!g_state.hasSelection) 
    {
        wprintf(L"No tape drive selected. Use 'Select Tape' first.\r\n");
        return FALSE;
    }

    ht = CreateFileW(g_state.devicePath, GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (ht == INVALID_HANDLE_VALUE) 
    {
        PrintLastErrorW(L"Cannot open tape drive", 0);
        return FALSE;
    }

    if (!TapeIsMediaLoaded(ht)) 
    {
        wprintf(L"No media loaded in the selected drive.\r\n");
        CloseHandle(ht);
        return FALSE;
    }

    if (!ReadMetadataFromTape(ht, &zh))
    {
        wprintf(L"Failed to read ZEROTAPE metadata.\r\n");
        if (flog) fclose(flog);
        CloseHandle(ht);
        return FALSE;
    }

    if (GetExeDirectoryW(dir, MAX_PATH)) 
    {
        JoinPath2W(logPath, MAX_PATH * 2, dir, L"verify_log.txt");
        flog = OpenUtf8FileForWrite(logPath);
    }

    //FPrintLineUtf8 already did it (\r\n)!
    if (flog) FPrintLineUtf8(flog, L"# TapeBackup Verify Log (UTF-8)");
    
    if (flog) FPrintLineUtf8(flog, L"========");
    
    //NEW: adding to log/printing on screen info about tape;
    MultiByteToWideChar(CP_ACP, 0, zh.name, -1, nameW, 64);
    sz = GetLE64(zh.sizeofarchive);
    HumanSize(sz, szW, 64);
    BytesToHex(zh.sha1, 20, sha1W, 64);
    _snwprintf(fmtW, 16, L"%s", (zh.format == 1) ? L"tar" : L"raw");
    FormatSystemTimeStr(zh.creationdate, timeW, 64);

    wprintf(L"Tape Name - %ws\r\n", nameW);
    memset(tmpbuf, 0, sizeof(WCHAR) * 128);
    _snwprintf(tmpbuf, 128, L"Tape Name - %ws", nameW);
    if (flog) FPrintLineUtf8(flog, tmpbuf);

    wprintf(L"Size - %ws (%I64u bytes)\r\n", szW, sz);
    memset(tmpbuf, 0, sizeof(WCHAR) * 128);
    _snwprintf(tmpbuf, 128, L"Size - %ws(%I64u bytes)", szW, sz);
    if (flog) FPrintLineUtf8(flog, tmpbuf);

    wprintf(L"SHA1 - %ws\r\n", sha1W);
    memset(tmpbuf, 0, sizeof(WCHAR) * 128);
    _snwprintf(tmpbuf, 128, L"SHA1 - %ws", sha1W);
    if (flog) FPrintLineUtf8(flog, tmpbuf);

    wprintf(L"Format - %ws\r\n", fmtW);
    memset(tmpbuf, 0, sizeof(WCHAR) * 128);
    _snwprintf(tmpbuf, 128, L"Format - %ws", fmtW);
    if (flog) FPrintLineUtf8(flog, tmpbuf);

    wprintf(L"Created - %ws\r\n", timeW);
    memset(tmpbuf, 0, sizeof(WCHAR) * 128);
    _snwprintf(tmpbuf, 128, L"Created - %ws", timeW);
    if (flog) FPrintLineUtf8(flog, tmpbuf);

    wprintf(L"========\r\n");
    if (flog) FPrintLineUtf8(flog, L"========");
   
    if (memcmp(zh.magic, "ZEROTAPE", 8) != 0 || zh.version != 0) 
    {
        wprintf(L"Invalid ZEROTAPE header.\r\n");
        if (flog) fclose(flog);
        CloseHandle(ht);
        return FALSE;
    }

    size2 = GetLE64(zh.sizeofarchive);
    if (!PositionToSecondSection(ht)) 
    {
        if (flog) fclose(flog);
        CloseHandle(ht);
        return FALSE;
    }

    wprintf(L"Step 1/2: verifying archive\r\n");
    okHash = CopySecondSectionToFileAndOrHash(ht, size2, NULL, digest);
    if (!okHash) 
    {
        if (flog) fclose(flog);
        CloseHandle(ht);
        return FALSE;
    }

    match = (memcmp(digest, zh.sha1, 20) == 0);
    wprintf(L"SHA1 match: %ws\r\n", match ? L"OK" : L"MISMATCH");
    //FPrintLineUtf8 already did it (\r\n)!
    if (flog) FPrintLineUtf8(flog, match ? L"SHA1 OK" : L"SHA1 MISMATCH");

    wprintf(L"Step 2/2: verifying files in archive\r\n");
    okTar = TRUE;
    if (zh.format == 1) 
    {
        if (!PositionToSecondSection(ht))
            okTar = FALSE;
        else
            okTar = VerifyTarOnTape(ht, flog);
    }

    if (flog) 
    {
        fclose(flog);
        wprintf(L"Log saved: %s\r\n", logPath);
    }

    CloseHandle(ht);
    overall = match && okTar;
    wprintf(L"Verify Backup %s.\r\n", overall ? L"completed" : L"found errors");
    return overall;
}

BOOL ActionRestoreBackup(void) 
{
    HANDLE              tape;
    ZEROTAPE_HEADER     zh;
    ULONGLONG           size2;
    WCHAR               dir[MAX_PATH];
    WCHAR               outpath[MAX_PATH * 2];
    WCHAR               wtitle[64];
    int                 need;
    const WCHAR         *ext;
    DWORD               attrs;
    HANDLE              hf;
    BOOL                ok;

    if (!g_state.hasSelection) 
    {
        wprintf(L"No tape drive selected. Use 'Select Tape' first.\r\n");
        return FALSE;
    } 
    
    tape = CreateFileW(g_state.devicePath, 
        GENERIC_READ | GENERIC_WRITE, 
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, 
        OPEN_EXISTING, 0, NULL); 
    
    if (tape == INVALID_HANDLE_VALUE) 
    { 
        PrintLastErrorW(L"Cannot open tape drive", 0); 
        return FALSE; 
    } 
    
    if (!TapeIsMediaLoaded(tape)) 
    { 
        wprintf(L"No media loaded in the selected drive.\r\n"); 
        CloseHandle(tape); 
        return FALSE; 
    } 
    
    if (!ReadMetadataFromTape(tape, &zh)) 
    { 
        wprintf(L"Failed to read ZEROTAPE metadata.\r\n"); 
        CloseHandle(tape); 
        return FALSE; 
    } 
    
    if (memcmp(zh.magic, "ZEROTAPE", 8) != 0 || zh.version != 0)
    { 
        wprintf(L"Invalid ZEROTAPE header.\r\n"); 
        CloseHandle(tape); 
        return FALSE; 
    } 
    
    size2 = GetLE64(zh.sizeofarchive);

    wprintf(L"Enter destination directory to save the archive: "); 
    if (!ReadLineW(dir, MAX_PATH)) 
    { 
        CloseHandle(tape); 
        return FALSE; 
    } 
    
    if (!EnsureDirectoryExistsW(dir)) 
    { 
        wprintf(L"Destination directory not accessible.\r\n"); 
        CloseHandle(tape); 
        return FALSE; 
    } 
    
    need = MultiByteToWideChar(CP_ACP, 0, zh.name, -1, wtitle, 64); 
    if (need == 0) wcscpy(wtitle, L"tape");  
    ext = (zh.format == 1) ? L".tar" : L".bin"; 
    _snwprintf(outpath, MAX_PATH * 2, L"%s\\%s%s", dir, wtitle, ext);
    
    attrs = GetFileAttributesW(outpath); 
    if (attrs != INVALID_FILE_ATTRIBUTES) 
        if (!AskYesNo(L"File exists. Overwrite?", FALSE)) 
        { 
            CloseHandle(tape); 
            return FALSE; 
        }
    
    if (!PositionToSecondSection(tape)) 
    { 
        CloseHandle(tape); 
        return FALSE; 
    } 
    
    hf = CreateFileW(outpath, GENERIC_WRITE, 0, NULL, 
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); 
    if (hf == INVALID_HANDLE_VALUE) 
    { 
        PrintLastErrorW(L"Cannot create destination file", 0); 
        CloseHandle(tape); 
        return FALSE; 
    }
    
    ok = CopySecondSectionToFileAndOrHash(tape, size2, hf, NULL); 
    CloseHandle(hf); 
    CloseHandle(tape); 
    wprintf(L"Restore Backup %s.\r\n", ok ? L"completed" : L"failed"); 
    return ok;
}

BOOL ActionReadBackupTOC(void) 
{
    HANDLE              tape;
    ZEROTAPE_HEADER     zh;
    WCHAR               dir[MAX_PATH];
    WCHAR               outPath[MAX_PATH * 2];
    FILE                *fout = NULL;
    BOOL                ok;

    // for getting metadata
    WCHAR               nameW[64];
    ULONGLONG           sz;
    WCHAR               szW[64];
    WCHAR               sha1W[64];
    WCHAR               fmtW[16];
    WCHAR               timeW[64];
    WCHAR               tmpbuf[128];

    if (!g_state.hasSelection) 
    { 
        wprintf(L"No tape drive selected. Use 'Select Tape' first.\r\n"); 
        return FALSE; 
    }  
    
    tape = CreateFileW(g_state.devicePath, 
        GENERIC_READ | GENERIC_WRITE, 
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, 
        OPEN_EXISTING, 0, NULL); 
    
    if (tape == INVALID_HANDLE_VALUE) 
    { 
        PrintLastErrorW(L"Cannot open tape drive", 0); 
        return FALSE; 
    } 
    
    if (!TapeIsMediaLoaded(tape)) 
    { 
        wprintf(L"No media loaded in the selected drive.\r\n"); 
        CloseHandle(tape); 
        return FALSE; 
    }  
    
    if (!ReadMetadataFromTape(tape, &zh)) 
    { 
        wprintf(L"Failed to read ZEROTAPE metadata.\r\n"); 
        CloseHandle(tape); 
        return FALSE; 
    } 
    
    if (zh.format != 1) 
    { 
        wprintf(L"Archive format is not TAR; TOC cannot be read.\r\n"); 
        CloseHandle(tape); 
        return FALSE; 
    } 
    
    if (!PositionToSecondSection(tape)) 
    {
        wprintf(L"Can't locate data section on tape; TOC cannot be read.\r\n");
        CloseHandle(tape); 
        return FALSE; 
    } 
    
    if (GetExeDirectoryW(dir, MAX_PATH)) 
    { 
        JoinPath2W(outPath, MAX_PATH * 2, dir, L"toc.txt"); 
        fout = OpenUtf8FileForWrite(outPath);
    } 
    
    //FPrintLineUtf8 already did it (\r\n)!
    if (fout) FPrintLineUtf8(fout, L"# TapeBackup TOC (UTF-8)"); 
    
    if (fout) FPrintLineUtf8(fout, L"========");

    //NEW: adding to log/printing on screen info about tape;
    MultiByteToWideChar(CP_ACP, 0, zh.name, -1, nameW, 64);
    sz = GetLE64(zh.sizeofarchive);
    HumanSize(sz, szW, 64);
    BytesToHex(zh.sha1, 20, sha1W, 64);
    _snwprintf(fmtW, 16, L"%s", (zh.format == 1) ? L"tar" : L"raw");
    FormatSystemTimeStr(zh.creationdate, timeW, 64);

    wprintf(L"Tape Name - %ws\r\n", nameW);
    memset(tmpbuf, 0, sizeof(WCHAR) * 128);
    _snwprintf(tmpbuf, 128, L"Tape Name - %ws", nameW);
    if (fout) FPrintLineUtf8(fout, tmpbuf);

    wprintf(L"Size - %ws (%I64u bytes)\r\n", szW, sz);
    memset(tmpbuf, 0, sizeof(WCHAR) * 128);
    _snwprintf(tmpbuf, 128, L"Size - %ws(%I64u bytes)", szW, sz);
    if (fout) FPrintLineUtf8(fout, tmpbuf);

    wprintf(L"SHA1 - %ws\r\n", sha1W);
    memset(tmpbuf, 0, sizeof(WCHAR) * 128);
    _snwprintf(tmpbuf, 128, L"SHA1 - %ws", sha1W);
    if (fout) FPrintLineUtf8(fout, tmpbuf);

    wprintf(L"Format - %ws\r\n", fmtW);
    memset(tmpbuf, 0, sizeof(WCHAR) * 128);
    _snwprintf(tmpbuf, 128, L"Format - %ws", fmtW);
    if (fout) FPrintLineUtf8(fout, tmpbuf);

    wprintf(L"Created - %ws\r\n", timeW);
    memset(tmpbuf, 0, sizeof(WCHAR) * 128);
    _snwprintf(tmpbuf, 128, L"Created - %ws", timeW);
    if (fout) FPrintLineUtf8(fout, tmpbuf);

    wprintf(L"========\r\n");
    if (fout) FPrintLineUtf8(fout, L"========");

    ok = ListTarTOCToFile(tape, fout); 
    if (fout) 
    { 
        fclose(fout); 
        wprintf(L"TOC saved: %s\r\n", outPath); 
    } 
    
    CloseHandle(tape); 
    return ok;
}

BOOL ActionCleanTape(void) 
{ 
    HANDLE      tape;

    if (!g_state.hasSelection) 
    { 
        wprintf(L"No tape drive selected. Use 'Select Tape' first.\r\n"); 
        return FALSE; 
    } 
    
    tape = CreateFileW(g_state.devicePath, 
        GENERIC_READ | GENERIC_WRITE, 
        FILE_SHARE_READ | FILE_SHARE_WRITE, 
        NULL, OPEN_EXISTING, 0, NULL);

    if (tape == INVALID_HANDLE_VALUE) 
    { 
        PrintLastErrorW(L"Cannot open tape drive", 0); 
        return FALSE; 
    } 
    
    if (!TapeIsMediaLoaded(tape)) 
    { 
        wprintf(L"No media loaded in the selected drive.\r\n"); 
        CloseHandle(tape); 
        return FALSE; 
    } 
    
    if (!AskYesNo(L"WARNING: All data on the tape will be destroyed. Proceed?", FALSE)) 
    { 
        CloseHandle(tape); 
        return FALSE; 
    } 

    //Note that you must rewind tape to beginning before erasing!
    wprintf(L"Please wait until tape rewound...\r\n");
    if (!TapeRewind(tape))
    {
        PrintLastErrorW(L"Failed to rewind tape", 0);
        CloseHandle(tape);
        return FALSE;
    }
    
    wprintf(L"Tape erasing. This may take a while...\r\n"); 
    if (!TapeEraseLong(tape)) 
    { 
        PrintLastErrorW(L"Erase command failed", 0); 
        CloseHandle(tape); 
        return FALSE; 
    }

    //Prepaing tape to work; This is required by some sequential devices
    if (AskYesNo(L"Do you want to prepare this tape to work?\r\n\
Note that this can be required for some tape drives", TRUE))
    {
        if (!TapePrepareToWork(tape))
        {
            PrintLastErrorW(L"Failed to prepare tape to work!", 0);
            CloseHandle(tape);
            return FALSE;
        }

        if (!TapeSetVariableBlockSize(tape))
        {
            PrintLastErrorW(L"Failed to set variable block size for current tape.\r\n\
It means, that this tape drive not supported for now!", 0);
            CloseHandle(tape);
            return FALSE;
        }
    }
    
    CloseHandle(tape); 
    return TRUE;
}

BOOL ActionPrepareTape(void)
{
    HANDLE      tape;

    if (!g_state.hasSelection)
    {
        wprintf(L"No tape drive selected. Use 'Select Tape' first.\r\n");
        return FALSE;
    }

    tape = CreateFileW(g_state.devicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);

    if (tape == INVALID_HANDLE_VALUE)
    {
        PrintLastErrorW(L"Cannot open tape drive", 0);
        return FALSE;
    }

    if (!TapeIsMediaLoaded(tape))
    {
        wprintf(L"No media loaded in the selected drive.\r\n");
        CloseHandle(tape);
        return FALSE;
    }

    wprintf(L"Tape is preparing to work now...\r\n");

    if (!TapePrepareToWork(tape))
    {
        PrintLastErrorW(L"Failed to prepare tape to work!", 0);
        CloseHandle(tape);
        return FALSE;
    }

    if (!TapeSetVariableBlockSize(tape))
    {
        PrintLastErrorW(L"Failed to set variable block size for current tape.\r\n\
It means, that this tape drive not supported for now!", 0);
        CloseHandle(tape);
        return FALSE;
    }

    wprintf(L"Tape successfully prepared to work! Possibly you need to resect it via main menu!\r\n");
    CloseHandle(tape);
    return TRUE;
}

BOOL ActionSelectTape(void) { return SelectTapeInteractive(); }

/* --------------------------------------
Menu and main loop
-------------------------------------- */
void PrintMenu(void) 
{
    ShowCurrentSelection();
    wprintf(L"1. Make Backup\r\n");
    wprintf(L"2. Verify Backup\r\n");
    wprintf(L"3. Restore Backup\r\n");
    wprintf(L"4. Read Backup TOC\r\n");
    wprintf(L"5. Print Backup Info\r\n");
    wprintf(L"6. Rewind Tape\r\n");
    wprintf(L"7. Clean Tape\r\n");
    wprintf(L"8. Prepare Tape\r\n");
    wprintf(L"9. Select Tape\r\n");
    wprintf(L"0. Exit\r\n");
    wprintf(L"Enter choice: ");
}

int wmain(int argc, char **argv) 
{
    WCHAR       in[16];
    int         choice;

    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    ZeroMemory(&g_state, sizeof(g_state));
    for (;;) 
    {
        HideConsoleCursor();
        PrintMenu();
        if (!ReadLineW(in, 16)) break;  
        system("cls");
        choice = _wtoi(in);
        switch (choice) 
        {
            case 1: ActionMakeBackup(); break;
            case 2: ActionVerifyBackup(); break;
            case 3: ActionRestoreBackup(); break; 
            case 4: ActionReadBackupTOC(); break; 
            case 5: ActionPrintMetadata(); break;
            case 6: ActionRewind(); break; 
            case 7: ActionCleanTape(); break;
            case 8: ActionPrepareTape(); break;
            case 9: ActionSelectTape(); break;
            case 0: wprintf(L"Exiting.\r\n"); return 0;
            default: wprintf(L"Unknown choice.\r\n"); break;
        }
        wprintf(L"\r\n");
        ShowConsoleCursor();
        system("pause");
        system("cls");
    }
    
    return 0;
}