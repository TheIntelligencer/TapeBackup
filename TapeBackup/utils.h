#ifndef __TAPE_BACKUP_UTILS
#define __TAPE_BACKUP_UTILS

#include "common.h"

/* ---- Potato-PC-compatible ANSI helpers (no SSE2 assumptions) ---- */
size_t a_strnlen(const char *s, size_t maxn);
void a_strncpyz(char *dst, size_t dstsz, const char *src);
void a_strncatn(char *dst, size_t dstsz, const char *src, size_t n);

/* =========================
Multi-byte/russian support helpers
========================= */
BOOL Utf8ToWide(const char* bytes, size_t n,
    WCHAR* out, size_t cchOut);

BOOL MultiByteGuessToWide(const char* bytes, size_t n,
    WCHAR* out, size_t cchOut);

/* --------------------------------------
Utility helpers (UI, strings, errors)
-------------------------------------- */
void ShowConsoleCursor(VOID);
void HideConsoleCursor(VOID);
void PrintLastErrorW(LPCWSTR prefix, DWORD code);
void TrimNewlineInPlace(WCHAR *s);
BOOL ReadLineW(LPWSTR buf, size_t cch);
BOOL AskYesNo(LPCWSTR q, BOOL defNo);
BOOL EnsureDirectoryExistsW(LPCWSTR path);
BOOL GetFileSize64W(LPCWSTR path, ULONGLONG *out);
void HumanSize(ULONGLONG bytes, WCHAR *out, size_t cch);
void DrawProgressBar(unsigned percent, ULONGLONG done, ULONGLONG total);

/* Paths & UTF-8 logging */
BOOL GetExeDirectoryW(WCHAR *outDir, size_t cch);
void JoinPath2W(WCHAR *out, size_t cch, LPCWSTR dir, LPCWSTR name);
FILE* OpenUtf8FileForWrite(LPCWSTR path);
void FPrintLineUtf8(FILE *f, LPCWSTR line);

/* Format helpers */
void BytesToHex(const unsigned char* in, size_t n,
    WCHAR *out, size_t cch);

void FormatSystemTimeStr(const unsigned char* stBytes,
    WCHAR *out, size_t cch);

/* --------------------------------------
Storage helpers (optional vendor/model/serial)
-------------------------------------- */
void SafeCopyAnsiToWideField(WCHAR *dst,
    size_t cch, const char *src);

BOOL QueryStorageStrings(HANDLE h, WCHAR *vendor,
    size_t cvendor, WCHAR *model, size_t cmodel,
    WCHAR *serial, size_t cserial);

/* --------------------------------------
SHA1 (tiny minimal)
-------------------------------------- */
typedef struct _SHA1_CTX {
    uint32_t        h[5];
    uint64_t        len;
    unsigned char   buf[64];
    size_t          idx;
} SHA1_CTX;

uint32_t ROL32(uint32_t v, int s);
void sha1_init(SHA1_CTX* c);
void sha1_block(SHA1_CTX* c, const unsigned char* p);
void sha1_update(SHA1_CTX* c, const void* data, size_t len);
void sha1_final(SHA1_CTX* c, unsigned char out[20]);

//octal number to ulonglong decimal
ULONGLONG OctalToULL(const char *s, size_t n);

/* --------------------------------------
ZEROTAPE helpers
-------------------------------------- */
void PutLE64(unsigned char out[8], ULONGLONG v);
ULONGLONG GetLE64(const unsigned char in[8]);

#endif
