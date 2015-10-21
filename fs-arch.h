#ifndef FS_ARCH_H_INCLUDED
#define FS_ARCH_H_INCLUDED


#include <stdint.h>
#include <stdlib.h>

extern uint8_t FlashBuffer[4096*2048];

#define FS_NAME_LENGTH    (40)
#define FS_MAX_OPEN_FILES (4)
#define FS_FD_SET_SIZE	  (8)

#define FS_PAGE_SIZE       (256)
#define FS_SECTOR_SIZE     (4096)
#define FS_NUM_SECTORS     (2048)
#define FS_START           (0)
#define FS_SIZE            (FS_NUM_SECTORS * FS_SECTOR_SIZE - FS_START)
#define FS_LOG_SIZE	       (128)
#define FS_LOG_TABLE_LIMIT (128)
#define FS_DYN_SIZE        (FS_PAGE_SIZE*1)

int FS_i;

#define FS_WRITE(buf, size, offset) \
            PRINTF("%s line %d\n",__FILE__,__LINE__); \
            PRINTF("FS_WRITE %d at %d\n",size,offset); \
            memcpy(FlashBuffer+offset,buf,size); \
            for(FS_i=0; FS_i<size; FS_i++) \
            { \
                if ((FS_i % 16) == 0) PRINTF("%08X ",FS_i + offset); \
                PRINTF("%02X ",((uint8_t*)buf)[FS_i]); \
                if ((FS_i % 16) == 15) PRINTF("\n"); \
            } \
            PRINTF("\n");

#define FS_READ(buf, size, offset) \
            PRINTF("%s line %d\n",__FILE__,__LINE__); \
            PRINTF("FS_READ %d at %d\n",size,offset); \
            memcpy(buf,FlashBuffer+offset,size); \
            for(FS_i=0; FS_i<size; FS_i++) \
            { \
                if ((FS_i % 16) == 0) PRINTF("%08X ",FS_i + offset); \
                PRINTF("%02X ",((uint8_t*)buf)[FS_i]); \
                if ((FS_i % 16) == 15) PRINTF("\n"); \
            } \
            PRINTF("\n");
/*
            for(cfs_i=0; cfs_i<size; cfs_i++) \
            { \
                if ((cfs_i % 16) == 0) PRINTF("%08X ",cfs_i + offset); \
                PRINTF("%02X ",((uint8_t)buf)[cfs_i]); \
                if ((cfs_i % 16) == 15) PRINTF("\n"); \
            } \

*/

#define FS_ERASE(sector) \
            memset(FlashBuffer + (FS_SECTOR_SIZE*sector),0xFF,FS_SECTOR_SIZE)

typedef int32_t FS_page_t;


#endif // FS-ARCH_H_INCLUDED
