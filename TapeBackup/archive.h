#ifndef __TAPE_BACKUP_ARCHIVE
#define __TAPE_BACKUP_ARCHIVE

#include "common.h"
#include "utils.h"
#include "tape.h"

/* ---- ZEROTAPE metadata header (128 bytes) ---- */
#pragma pack(push,1)
typedef struct _ZEROTAPE_HEADER {
    char          magic[8];         /* "ZEROTAPE" */
    unsigned char version;          /* 0 */
    char          name[32];         /* ASCII NUL-terminated, max 31 chars */
    unsigned char sizeofarchive[8]; /* little-endian 64-bit, section #2 size */
    unsigned char sha1[20];         /* SHA-1 of section #2 */
    unsigned char format;           /* 0=raw, 1=tar */
    unsigned char creationdate[16]; /* SYSTEMTIME (16 bytes), local time */
    unsigned char reserved[42];     /* must be zero */
} ZEROTAPE_HEADER;                  /* total 128 */
#pragma pack(pop)

/* --------------------------------------
TAR structures & helpers (POSIX ustar + GNU longname/longlink)
-------------------------------------- */
#pragma pack(push,1)
typedef struct _TAR_HDR {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} TAR_HDR;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _TAR_HDR_FULL {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
} TAR_HDR_FULL;
#pragma pack(pop)

 unsigned TarChecksum(const TAR_HDR *h);
 void TarBuildName(const TAR_HDR *h, char *out,
    size_t outsz, const char *overrideName);
 unsigned TarChecksum512(const void* hdr);
 void U64ToOctal(ULONGLONG v, char* out, size_t n);
 void TarInitHeader(TAR_HDR_FULL *hdr, const char *name, ULONGLONG size);
 BOOL WriteMetadataSection(HANDLE ht, const ZEROTAPE_HEADER* zh);
 BOOL ReadMetadataFromTape(HANDLE ht, ZEROTAPE_HEADER* out);
 BOOL PositionToSecondSection(HANDLE ht);

/* --------------------------------------
TAR verification & TOC (only when format==1)
-------------------------------------- */
typedef struct _VERIFY_STATS {
    ULONGLONG filesTotal;
    ULONGLONG filesBad;
    ULONGLONG bytesProcessed;
} VERIFY_STATS;

 BOOL ParsePaxAndGet(const BYTE* buf, DWORD len, const char* key,
    char* out, size_t outsz);
 void AnsiOrUtf8ToWide(const char* s, size_t n, WCHAR* out, size_t cch);
 BOOL VerifyTarOnTape(HANDLE h, FILE* flog);
 BOOL ListTarTOCToFile(HANDLE h, FILE* fout);

/* --------------------------------------
Section #2 I/O
-------------------------------------- */
 BOOL WriteArchiveToSecondSection(HANDLE ht,
    HANDLE hf, ULONGLONG totalSize);
 BOOL CopySecondSectionToFileAndOrHash(HANDLE ht, ULONGLONG totalSize,
    HANDLE hf, unsigned char outSha1[20]);

#endif