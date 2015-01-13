#ifndef CFS_COFFEE_ARCH_H_INCLUDED
#define CFS_COFFEE_ARCH_H_INCLUDED

#include <stdint.h>
#include <stdlib.h>

extern uint8_t FlashBuffer[4096*2048];

#define COFFEE_NAME_LENGTH    (8)
#define COFFEE_MAX_OPEN_FILES (4)
#define COFFEE_FD_SET_SIZE	  (8)

#define COFFEE_PAGE_SIZE       (256)
#define COFFEE_SECTOR_SIZE     (4096)
#define COFFEE_NUM_SECTORS     (2048)
#define COFFEE_START           (0)
#define COFFEE_SIZE            (COFFEE_NUM_SECTORS * COFFEE_SECTOR_SIZE - COFFEE_START)
#define COFFEE_LOG_SIZE	       (128)
#define COFFEE_LOG_TABLE_LIMIT (128)
#define COFFEE_DYN_SIZE        (COFFEE_PAGE_SIZE*1)

uint8_t TempBuffer[4096];
int cfs_i;
void InvertTempBuffer(int bufsize)
{
    int i;
    for(i=0; i<bufsize; i++)
    {
        TempBuffer[i] ^= 0xFF;
    }
}
//            memcpy(FlashBuffer+offset,TempBuffer,size);
#define COFFEE_WRITE(buf, size, offset) \
            PRINTF("%s line %d\n",__FILE__,__LINE__); \
            PRINTF("COFFEE_WRITE %d at %d\n",size,offset); \
            memcpy(TempBuffer,buf,size); \
            InvertTempBuffer(size); \
            for(cfs_i=0; cfs_i<size; cfs_i++) \
            { \
                FlashBuffer[offset+cfs_i] &= TempBuffer[cfs_i]; \
            } \
            for(cfs_i=0; cfs_i<size; cfs_i++) \
            { \
                if ((cfs_i % 16) == 0) PRINTF("%08X ",cfs_i + offset); \
                PRINTF("%02X ",((uint8_t*)buf)[cfs_i]); \
                if ((cfs_i % 16) == 15) PRINTF("\n"); \
            } \
            PRINTF("\n");

#define COFFEE_READ(buf, size, offset) \
            PRINTF("%s line %d\n",__FILE__,__LINE__); \
            PRINTF("COFFEE_READ %d at %d\n",size,offset); \
            memcpy(TempBuffer,FlashBuffer+offset,size); \
            InvertTempBuffer(size); \
            memcpy(buf,TempBuffer,size); \
            for(cfs_i=0; cfs_i<size; cfs_i++) \
            { \
                if ((cfs_i % 16) == 0) PRINTF("%08X ",cfs_i + offset); \
                PRINTF("%02X ",((uint8_t*)buf)[cfs_i]); \
                if ((cfs_i % 16) == 15) PRINTF("\n"); \
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

#define COFFEE_ERASE(sector) \
            memset(FlashBuffer + (COFFEE_SECTOR_SIZE*sector),0xFF,COFFEE_SECTOR_SIZE)

typedef int32_t coffee_page_t;

#endif // CFS-COFFEE-ARCH_H_INCLUDED
