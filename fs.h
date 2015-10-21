#ifndef FS_H_INCLUDED
#define FS_H_INCLUDED

#include "fs-arch.h"

#define FS_INVALID_ADDR (0xFFFFFFFFu)

typedef enum
{
    FS_RESULT_SUCCESS = 0,
    FS_RESULT_GEN_ERROR
} FS_RESULT;

typedef enum
{
    FS_FLAGS_READ   = 0x01,
    FS_FLAGS_WRITE  = 0x02,
    FS_FLAGS_APPEND = 0x04,
} FS_FLAGS;

typedef enum
{
    FS_GS_MODE_GREEDY,
    FS_GS_MODE_RELUCTANT
} FS_GS_MODE;

#define FS_HDR_TYPE_FILE_START  (5)
#define FS_HDR_TYPE_FILE_DATA   (6)
#define FS_HDR_TYPE_FILE_END    (7)

typedef struct
{
    uint8_t fUsed : 1;
    uint8_t fObsolete : 1;
    uint8_t u8Type : 3;
} FS_HDR_TAG;

typedef struct
{
    FS_HDR_TAG Tag;
    uint8_t u8ByteVal;
} FS_HDR_TAG_UN;

typedef struct __attribute__((packed))
{
    FS_HDR_TAG_UN HdrTag;
    uint8_t Spare[3];
    uint32_t u32NextAddr1;
    uint32_t u32NextAddr2;
} FS_HDR;

typedef struct __attribute__((packed))
{
    FS_HDR  Hdr;
    char    Name[FS_NAME_LENGTH];
} FS_START_SECTOR;

typedef struct __attribute__((packed))
{
    FS_HDR  Hdr;
    char    data[FS_PAGE_SIZE - sizeof(FS_HDR)];
} FS_DATA_SECTOR;

typedef union
{
    FS_START_SECTOR StartSector;
    FS_DATA_SECTOR  DataSector;
    uint8_t u8SectorData[FS_PAGE_SIZE];
} FS_BUFFERED_SECTOR;

typedef struct
{
    int InUse;
    uint32_t u32NextAddr; // next address on flash to write
    uint32_t u32CurPos; // current file position
    FS_BUFFERED_SECTOR Sector;
} FS_FILE_DESCRIPTOR;

FS_RESULT FS_Format(void);
void FS_Init(void);

#endif // FS_H_INCLUDED
