#ifndef NEOCIRCFS_H_INCLUDED
#define NEOCIRCFS_H_INCLUDED

#include <stdint.h>
#include <stdbool.h>
#include "cassert.h"

/* ***************************************************************************

Each record is constrcuted as follow:

-------------------------------------------------------------------------------
| 1 Byte  | Start Tag                                                         |
-------------------------------------------------------------------------------
| n Bytes | n is determined by T_NEOCFS_FILE_DESCRIPTOR_FIXED::u32RecordSize  |
|         | (This is the "contents of every record)                           |
-------------------------------------------------------------------------------
| 1 Byte  | Finalisations TAG                                                 |
-------------------------------------------------------------------------------

****************************************************************************** */

extern bool NEOCFS_Debug;

#define NEOCFS_SECTOR_COUNT           (512)
#define NEOCFS_SECTOR_SIZE            (4096)
#define NEOCFS_PAGE_SIZE              (128)
#define NEOCFS_NUM_PAGES_PER_SECTOR   (NEOFS_SECTOR_SIZE / NEOFS_PAGE_SIZE)
#define NEOCFS_START_SECTOR           (0)

#define NEOCFS_FILENAME_LEN (16)

#define NEOCFS_TOKENPASTE(x, y) x ## y
#define NEOCFS_TOKENPASTE2(x, y) NEOCFS_TOKENPASTE(x, y)
#define NEOCFS_UNIQUE NEOCFS_TOKENPASTE2(NEOCFS_File, __LINE__)

#define NEOCFS_NOT_FOUND (0xFFFFFFFF)

typedef enum
{
    NEOCFS_RESULT_CODE_SUCCESS   = 0,
    NEOCFS_RESULT_CODE_NO_INIT   = -1,
    NEOCFS_RESULT_CODE_GENERR    = -2,
    NEOCFS_RESULT_CODE_DISK_FAIL = -3
} NEOCFS_RESULT_CODE_EN;

typedef enum
{
    NEOCFS_START_TAG_WRITE_STARTED = ~0x01,
} NEOCFS_START_TAG_FLAGS_EN;

typedef enum
{
    NEOCFS_END_TAG_WRITE_DONE = ~0x01,
    NEOCFS_END_TAG_OBSOLETE   = ~0x02
} NEOCFS_END_TAG_FLAGS_EN;

typedef enum
{
    // Set to force the file to delete oldest data - even if it is not obsolete
    NEOCFS_FILE_FLAGS_OVERWRITE_OLDEST = 0x00000001,
} NEOCFS_FILE_FLAGS_EN;

typedef struct
{
    char     cFilename[NEOCFS_FILENAME_LEN];
    uint32_t u32RecordSize;
    uint32_t u32StartAddr;
    uint32_t u32EndAddr;
    uint32_t u32Flags;
    uint32_t u32Tail;
    uint32_t u32Head;
    uint32_t u32CurReadPos;
} NEOCFS_FILE_DESCRIPTOR_ST;

#define NEOCFS_DECLARE_CIRC_FILE(filename,recsize,startaddr,endaddr) \
    static const NEOCFS_FILE_DESCRIPTOR_ST __attribute__((used, section("neocfs_file_descriptors"))) NEOCFS_UNIQUE = { \
        filename,recsize,startaddr,endaddr,0 \
    }

void NEOCFS_Init(void);
void NEOCFS_Dir(void);
int NEOCFS_OpenByDescriptor(NEOCFS_FILE_DESCRIPTOR_ST* fd);
int NEOCFS_Format(void);

#endif // NEOCIRCFS_H_INCLUDED
