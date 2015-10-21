#ifndef NEOFS_H_INCLUDED
#define NEOFS_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include "cassert.h"

#define NEOFS_SECTOR_COUNT           (512)
#define NEOFS_SECTOR_SIZE            (4096)
#define NEOFS_PAGE_SIZE              (128)
#define NEOFS_NUM_PAGES_PER_SECTOR   (NEOFS_SECTOR_SIZE / NEOFS_PAGE_SIZE)
#define NEOFS_START_SECTOR           (0)

#define NEOFS_NO_SECTOR_FOUND        (0xFFFF)
#define NEOFS_NO_PAGE_FOUND          (0xFFFFFFFF)

typedef enum
{
    NEOFS_MODE_READ  = 0x01,
    NEOFS_MODE_WRITE = 0x02
} NEOFS_MODE;

typedef enum
{
    NEOFS_GARBAGE_COLLECT_MODE_GREEDY,
    NEOFS_GARBAGE_COLLECT_MODE_RELUCTANT
} NEOFS_GARBAGE_COLLECT_MODE;

#define NEOFS_FILEID_FREE     (0xFFFF)
#define NEOFS_FILEID_OBSOLETE (0x0000)
typedef struct __attribute__((packed))
{
    uint8_t  u8Tag;
} NEOFS_SECTOR_HEADER;

typedef struct __attribute__((packed))
{
    uint16_t u16NextSector; // ffff is not assigned
} NEOFS_SECTOR_FOOTER;

#define NEOFS_TAG_OBSOLETE    (~0x01)
#define NEOFS_TAG_USED        (~0x02)
#define NEOFS_TAG_COMPLETED   (~0x04)
#define NEOFS_TAG_EOF         (~0x08)
#define NEOFS_PAGE_FILEDATA   (0xDF)
#define NEOFS_PAGE_FILEHEADER (0xEF)
#define NEOFS_PAGETYPE_MASK   (0xF0)

typedef struct __attribute__((packed))
{
    uint8_t  u8Tag;
    uint32_t u32Offset;
    uint16_t u16Size;
    uint16_t u16LinkPreviousSector;
    uint16_t u16LinkPreviousOffsetInSector;
} NEOFS_LOG_HEADER;

typedef struct __attribute__((packed))
{
    uint8_t  u8Tag;
    uint8_t  u8Size;
    uint32_t u32NextPageAddr;      // pointer to next page
    uint32_t u32ThisPageFwdAddr;   // pointer to alternate page if this page is invalid
} NEOFS_PAGE_HEADER;

typedef struct __attribute__((packed))
{
} NEOFS_PAGE_FOOTER;

typedef struct __attribute__((packed))
{
    NEOFS_SECTOR_HEADER  Header;
    uint8_t            u8Data[NEOFS_SECTOR_SIZE - sizeof(NEOFS_SECTOR_HEADER) - sizeof(NEOFS_SECTOR_FOOTER)];
    NEOFS_SECTOR_FOOTER  Footer;
} NEOFS_SECTOR;

#define NEOFS_FILENAME_MAXLEN (32)
#define NEOFS_PAGE_DATA_SIZE (NEOFS_PAGE_SIZE - sizeof(NEOFS_PAGE_HEADER) - sizeof(NEOFS_PAGE_FOOTER))
typedef struct __attribute__((packed))
{
    char      Filename[NEOFS_FILENAME_MAXLEN];
} NEOFS_FILE_HEADER;

typedef struct __attribute__((packed))
{
    uint8_t      u8Data[NEOFS_PAGE_DATA_SIZE];
} NEOFS_FILE_DATA;

typedef union
{
    NEOFS_FILE_HEADER Header;
    NEOFS_FILE_DATA   Data;
    uint8_t         u8Data;
} NEOFS_PAGE_CONTENTS;

typedef struct __attribute__((packed))
{
    NEOFS_PAGE_HEADER   Header;
    NEOFS_PAGE_CONTENTS Contents;
    NEOFS_PAGE_FOOTER   Footer;
} NEOFS_PAGE;
CASSERT(sizeof(NEOFS_PAGE) == NEOFS_PAGE_SIZE,NEOFS_PAGE_struct_not_correct_size)

#if 0

#define NEOFS_FILETABLE_MAGICNUM (0x6abe82e4)
typedef struct __attribute__((packed))
{
    uint32_t u32MagicNumber1;
    uint32_t u32MagicNumber2;
    uint8_t u8Spare[sizeof(NEOFS_FILE_HEADER)-8];
} NEOFS_FILE_TABLE_HEADER;
#endif // 0

int NEOFS_Format(void);
int NEOFS_Open(char* filename, NEOFS_MODE ModeE);
int NEOFS_Write(int fd, void* buf, int size);
int NEOFS_Read(int fd, void* buf, int size);
void NEOFS_Close(int fd);
int NEOFS_Dir(int first, char* name, int* len);
uint32_t NEOFS_DiskFree(void);
uint32_t NEOFS_GetNumErasedPages(void);
uint32_t NEOFS_GetNumCleanSectors(void);
uint32_t NEOFS_GetNumReclaimableSectors(void);

#endif // NEOFS_H_INCLUDED
