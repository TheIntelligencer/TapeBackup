# TapeBackup
## Summary
TapeBackup - Windows console utility that backup tar archives on tapes in ZEROTAPE format

## Tape organization
![](https://github.com/TheIntelligencer/TapeBackup/blob/main/img/TapeBackupTapeOrganization.png)<br>
This utility stores two tar archives divided by filemark on single partition of the tape. Also, there is another filemark at the end of second tar archive. First archive, that always locates at the beginning and has fixed size, contains single metadata file that contains some information about archive that is represented as C structure (for detais see "ZEROTAPE header" block). Second archive is main data archive.

## ZEROTAPE header
```c
  /* ---- ZEROTAPE metadata header (128 bytes) ---- */
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
```

## Usage
Just launch program, select needed action, enter it number and press enter. Next, you need to follow further instructions that will shown on screen<br>
VERY IMPORTANT: you need to prepare tape for work before doing any other operations (except clean). Just select number 8 first.

## Compatibility
This program requires at least Windows XP SP3 and working physical or virtual tape drive device, that is correctly recognized by Windows <br>
VERY IMPORTANT: This program supports tape drives with dynamic block size support only!

## Build
You need at least Visual Studio 2015 in order to build this project.
