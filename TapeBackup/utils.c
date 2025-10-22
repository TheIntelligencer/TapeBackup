#include "utils.h"

/* ---- Potato-PC-compatible ANSI helpers (no SSE2 assumptions) ---- */
size_t a_strnlen(const char *s, size_t maxn)
{
    size_t i = 0;
    if (!s) return 0;
    while (i < maxn && s[i]) i++;
    return i;
}

void a_strncpyz(char *dst, size_t dstsz, const char *src)
{
    size_t i = 0;

    if (!dst || dstsz == 0) return;
    if (!src)
    {
        dst[0] = 0; return;
    }
    for (; i + 1 < dstsz && src[i]; ++i)
        dst[i] = src[i]; dst[i] = 0;
}

 void a_strncatn(char *dst, size_t dstsz, const char *src, size_t n)
{
    size_t d = a_strnlen(dst, dstsz ? dstsz - 1 : 0);
    size_t i = 0;
    if (d >= dstsz) return;
    while (d + 1 < dstsz && i < n && src && src[i]) {
        dst[d++] = src[i++];
    }
    if (dstsz > 0) dst[(d < dstsz) ? d : (dstsz - 1)] = 0;
}

/* =========================
Multi-byte/russian support helpers
========================= */
BOOL Utf8ToWide(const char* bytes, size_t n,
    WCHAR* out, size_t cchOut)
{
    int need;

    if (!bytes) return FALSE;

    need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        bytes, (int)n, NULL, 0);
    if (need <= 0) return FALSE;

    if ((size_t)need >= cchOut) need = (int)cchOut - 1;

    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        bytes, (int)n, out, need);

    out[need] = 0;
    return TRUE;
}

BOOL MultiByteGuessToWide(const char* bytes, size_t n,
    WCHAR* out, size_t cchOut)
{
    int need;

    /* 1) try UTF-8 strictly; 2) if not success — go to system ACP */
    if (Utf8ToWide(bytes, n, out, cchOut)) return TRUE;

    need = MultiByteToWideChar(CP_ACP, 0, bytes,
        (int)n, NULL, 0);
    if (need <= 0) return FALSE;

    if ((size_t)need >= cchOut) need = (int)cchOut - 1;

    MultiByteToWideChar(CP_ACP, 0, bytes,
        (int)n, out, need);

    out[need] = 0;
    return TRUE;
}

/* --------------------------------------
Utility helpers (UI, strings, errors)
-------------------------------------- */
void ShowConsoleCursor(VOID)
{
    HANDLE              hconsole;
    CONSOLE_CURSOR_INFO cursorinfo;

    hconsole = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleCursorInfo(hconsole, &cursorinfo);
    cursorinfo.bVisible = TRUE;
    SetConsoleCursorInfo(hconsole, &cursorinfo);
}

void HideConsoleCursor(VOID)
{
    HANDLE              hconsole;
    CONSOLE_CURSOR_INFO cursorinfo;

    hconsole = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleCursorInfo(hconsole, &cursorinfo);
    cursorinfo.bVisible = FALSE;
    SetConsoleCursorInfo(hconsole, &cursorinfo);
}

void PrintLastErrorW(LPCWSTR prefix, DWORD code)
{
    LPVOID msg = NULL;
    DWORD errcode;

    errcode = (code == 0) ? GetLastError() : code;

    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, errcode, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPWSTR)&msg, 0, NULL);
    if (msg)
    {
        wprintf(L"%s (error %lu): %s\r\n", prefix, (unsigned long)errcode, (LPWSTR)msg);
        LocalFree(msg);
    }
    else
    {
        wprintf(L"%s (error %lu).\r\n", prefix, (unsigned long)errcode);
    }
}

void TrimNewlineInPlace(WCHAR *s)
{
    size_t n;

    if (!s) return;

    n = wcslen(s);
    while (n > 0 && (s[n - 1] == L'\r' || s[n - 1] == L'\n'))
    {
        s[n - 1] = 0;
        n--;
    }
}

BOOL ReadLineW(LPWSTR buf, size_t cch)
{
    ShowConsoleCursor();
    if (!buf || cch == 0) return FALSE;
    if (!fgetws(buf, (int)cch, stdin)) return FALSE;
    TrimNewlineInPlace(buf);
    HideConsoleCursor();
    return TRUE;
}

BOOL AskYesNo(LPCWSTR q, BOOL defNo)
{
    WCHAR ans[16];
    WCHAR c;

    wprintf(L"%s (Y/N)%s: ", q, defNo ? L" [default N]" : L"");

    if (!ReadLineW(ans, 16))
        return defNo ? FALSE : TRUE;

    if (ans[0] == 0)
        return defNo ? FALSE : TRUE;

    c = towupper(ans[0]);
    return c == L'Y';
}

BOOL EnsureDirectoryExistsW(LPCWSTR path)
{
    DWORD attr;

    attr = GetFileAttributesW(path);

    if (attr == INVALID_FILE_ATTRIBUTES)
        return FALSE;

    return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

BOOL GetFileSize64W(LPCWSTR path, ULONGLONG *out)
{
    HANDLE          h;
    LARGE_INTEGER   li;
    BOOL            ok;

    h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (h == INVALID_HANDLE_VALUE)
        return FALSE;

    ok = GetFileSizeEx(h, &li);
    CloseHandle(h);
    if (!ok) return FALSE;

    if (out)
        *out = (ULONGLONG)li.QuadPart;
    else
    {
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }

    return TRUE;
}

void HumanSize(ULONGLONG bytes, WCHAR *out, size_t cch)
{
    int     i;
    double  v;
    const   WCHAR *u[] = { L"B", L"KiB", L"MiB", L"GiB", L"TiB" };

    v = (double)bytes;
    i = 0;
    while (v >= 1024.0 && i < 4)
    {
        v /= 1024.0;
        i++;
    }

    _snwprintf(out, (int)cch, L"%.1f %s", v, u[i]);
}

void DrawProgressBar(unsigned percent, ULONGLONG done, ULONGLONG total)
{
    WCHAR       done_humanized[64];
    WCHAR       total_humanized[64];
    const int   width = 30;
    int         filled;
    int         i;

    wprintf(L"\r[");
    filled = (int)((percent * width) / 100);
    for (i = 0; i < width; i++)
        wprintf(i < filled ? L"#" : L"-");

    HumanSize(done, done_humanized, 64);
    HumanSize(total, total_humanized, 64);

    wprintf(L"] %3u%% (%s / %s)        ", percent, done_humanized, total_humanized);
}

/* Paths & UTF-8 logging */
BOOL GetExeDirectoryW(WCHAR *outDir, size_t cch)
{
    WCHAR path[MAX_PATH];
    DWORD pathlen = 0;
    WCHAR *p;

    pathlen = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (pathlen == 0 || pathlen >= MAX_PATH) return FALSE;

    //removing last part of full file name in order to get this exe dir
    //(by replacing last backslash with zero-terminator)
    p = wcsrchr(path, L'\\');
    if (!p)
    {
        SetLastError(ERROR_BAD_PATHNAME);
        return FALSE;
    }
    *p = 0;

    wcsncpy(outDir, path, cch - 1);
    //adding '\0' to result
    outDir[cch - 1] = 0;
    return TRUE;
}

void JoinPath2W(WCHAR *out, size_t cch, LPCWSTR dir, LPCWSTR name)
{
    _snwprintf(out, (int)cch, L"%s\\%s", dir, name);
}

FILE* OpenUtf8FileForWrite(LPCWSTR path)
{
    FILE            *f;
    unsigned char   bom[3];

    f = _wfopen(path, L"wb");
    if (!f) return NULL;

    bom[0] = 0xEF;
    bom[1] = 0xBB;
    bom[2] = 0xBF;
    fwrite(bom, 1, 3, f);
    return f;
}

void FPrintLineUtf8(FILE *f, LPCWSTR line)
{
    int clinelen;
    char *clinebuf;

    if (!f || !line) return;

    clinelen = WideCharToMultiByte(CP_UTF8, 0, line, -1, NULL, 0, NULL, NULL);
    if (clinelen <= 0) return;

    clinebuf = (char*)malloc(clinelen + 2);
    if (!clinebuf) return;

    WideCharToMultiByte(CP_UTF8, 0, line, -1, clinebuf, clinelen, NULL, NULL);
    fwrite(clinebuf, 1, strlen(clinebuf), f);
    fwrite("\r\n", 1, 2, f);
    free(clinebuf);
}

/* Format helpers */
void BytesToHex(const unsigned char* in, size_t n,
    WCHAR *out, size_t cch)
{
     const WCHAR* hex = L"0123456789abcdef";
    size_t              i;
    size_t              pos = 0;

    for (i = 0; i < n && pos + 2 < cch; ++i)
    {
        out[pos++] = hex[(in[i] >> 4) & 0xF];
        out[pos++] = hex[in[i] & 0xF];
    }

    if (pos < cch) out[pos] = 0;
}

void FormatSystemTimeStr(const unsigned char* stBytes,
    WCHAR *out, size_t cch)
{
    SYSTEMTIME st;

    memcpy(&st, stBytes, sizeof(SYSTEMTIME));

    _snwprintf(out, (int)cch, L"%04u-%02u-%02u %02u:%02u:%02u",
        (unsigned)st.wYear, (unsigned)st.wMonth,
        (unsigned)st.wDay, (unsigned)st.wHour,
        (unsigned)st.wMinute, (unsigned)st.wSecond);
}

/* --------------------------------------
Storage helpers (optional vendor/model/serial)
-------------------------------------- */
void SafeCopyAnsiToWideField(WCHAR *dst,
    size_t cch, const char *src)
{
    size_t      n = 0;

    //args validation
    if (!dst || cch == 0) return;
    if (!src) return;

    dst[0] = 0;
    while (src[n] && n + 1 < cch)
    {
        dst[n] = (WCHAR)(unsigned char)src[n];
        n++;
    }
    dst[n] = 0;
}

BOOL QueryStorageStrings(HANDLE h, WCHAR *vendor,
    size_t cvendor, WCHAR *model, size_t cmodel,
    WCHAR *serial, size_t cserial)
{
    DWORD                       bytes = 0;
    STORAGE_PROPERTY_QUERY      spq;
    STORAGE_DEVICE_DESCRIPTOR   *sdd;
    BOOL                        result;
    BYTE                        *buf;
    DWORD                       errcode = 0;
    size_t                      tmp;

    if (vendor) vendor[0] = 0;
    if (model) model[0] = 0;
    if (serial) serial[0] = 0;

    ZeroMemory(&spq, sizeof(spq));

    tmp = sizeof(spq);

    spq.PropertyId = StorageDeviceProperty;
    spq.QueryType = PropertyStandardQuery;

    result = DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY,
        &spq, sizeof(spq), NULL, 0, &bytes, NULL);

    if (bytes == 0) bytes = 1024;
    buf = (BYTE*)malloc(bytes);
    if (!buf)
    {
        SetLastError(ERROR_NOACCESS);
        return FALSE;
    }

    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY,
        &spq, sizeof(spq), buf, bytes, &bytes, NULL))
    {
        free(buf);
        return FALSE;
    }

    sdd = (STORAGE_DEVICE_DESCRIPTOR*)buf;
    if (sdd->VendorIdOffset && sdd->VendorIdOffset < bytes)
        SafeCopyAnsiToWideField(vendor, cvendor, (const char*)(buf + sdd->VendorIdOffset));

    if (sdd->ProductIdOffset && sdd->ProductIdOffset < bytes)
        SafeCopyAnsiToWideField(model, cmodel, (const char*)(buf + sdd->ProductIdOffset));

    if (sdd->SerialNumberOffset && sdd->SerialNumberOffset < bytes)
        SafeCopyAnsiToWideField(serial, cserial, (const char*)(buf + sdd->SerialNumberOffset));

    free(buf);

    return TRUE;
}


/* --------------------------------------
SHA1 (tiny minimal)
-------------------------------------- */

uint32_t ROL32(uint32_t v, int s) { return (v << s) | (v >> (32 - s)); }

void sha1_init(SHA1_CTX* c)
{
    c->h[0] = 0x67452301;
    c->h[1] = 0xEFCDAB89;
    c->h[2] = 0x98BADCFE;
    c->h[3] = 0x10325476;
    c->h[4] = 0xC3D2E1F0;
    c->len = 0;
    c->idx = 0;
}

void sha1_block(SHA1_CTX* c, const unsigned char* p)
{
    uint32_t    w[80];
    int         i;
    uint32_t    a, b, cc, d, e, t;
    uint32_t f, k;

    for (i = 0; i < 16; i++)
        w[i] = (p[4 * i] << 24) |
        (p[4 * i + 1] << 16) |
        (p[4 * i + 2] << 8) |
        (p[4 * i + 3]);

    for (i = 16; i < 80; i++)
        w[i] = ROL32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    a = c->h[0];
    b = c->h[1];
    cc = c->h[2];
    d = c->h[3];
    e = c->h[4];

    for (i = 0; i < 80; i++)
    {
        if (i < 20)
        {
            f = (b&cc) | ((~b)&d);
            k = 0x5A827999;
        }
        else if (i < 40)
        {
            f = b^cc^d;
            k = 0x6ED9EBA1;
        }
        else if (i < 60)
        {
            f = (b&cc) | (b&d) | (cc&d);
            k = 0x8F1BBCDC;
        }
        else
        {
            f = b^cc^d;
            k = 0xCA62C1D6;
        }

        t = ROL32(a, 5) + f + e + k + w[i];
        e = d;
        d = cc;
        cc = ROL32(b, 30);
        b = a;
        a = t;
    }

    c->h[0] += a;
    c->h[1] += b;
    c->h[2] += cc;
    c->h[3] += d;
    c->h[4] += e;
}

void sha1_update(SHA1_CTX* c, const void* data, size_t len)
{
    const unsigned char* p = (const unsigned char*)data;
    size_t n;

    c->len += (uint64_t)len;
    while (len > 0)
    {
        n = 64 - c->idx;
        if (n > len) n = len;
        memcpy(c->buf + c->idx, p, n);
        c->idx += n;
        p += n;
        len -= n;
        if (c->idx == 64)
        {
            sha1_block(c, c->buf);
            c->idx = 0;
        }
    }
}

void sha1_final(SHA1_CTX* c, unsigned char out[20])
{
    uint64_t        bitlen;
    unsigned char   b0 = 0x80, z = 0;
    unsigned char   lenbuf[8];
    int             i;

    bitlen = c->len * 8ULL;
    sha1_update(c, &b0, 1);

    while (c->idx != 56) sha1_update(c, &z, 1);

    for (i = 0; i < 8; i++)
        lenbuf[7 - i] = (unsigned char)(bitlen >> (8 * i));

    sha1_update(c, lenbuf, 8);

    for (i = 0; i < 5; i++)
    {
        out[4 * i] = (unsigned char)(c->h[i] >> 24);
        out[4 * i + 1] = (unsigned char)(c->h[i] >> 16);
        out[4 * i + 2] = (unsigned char)(c->h[i] >> 8);
        out[4 * i + 3] = (unsigned char)(c->h[i]);
    }
}

//octal number to ulonglong decimal
ULONGLONG OctalToULL(const char *s, size_t n)
{
    ULONGLONG v = 0;
    size_t i = 0;
    /* skipping starting spaces/zeroes/tabulations */
    while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\0')) i++;
    for (; i < n; i++)
    {
        char c = s[i];
        if (c<'0' || c>'7') break;
        v = (v << 3) + (ULONGLONG)(c - '0');
    }
    return v;
}

void PutLE64(unsigned char out[8], ULONGLONG v)
{
    int i;

    for (i = 0; i < 8; i++)
        out[i] = (unsigned char)((v >> (8 * i)) & 0xFF);
}

ULONGLONG GetLE64(const unsigned char in[8])
{
    int         i;
    ULONGLONG   v = 0;

    for (i = 0; i < 8; i++)
        v |= ((ULONGLONG)in[i]) << (8 * i);

    return v;
}
