#include <stdint.h>
#include <string.h>

#define __SHORTFILE__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)

#define DEBUG 1
#if DEBUG
#include <stdio.h>
#define PRINTF(...) \
    printf("%s:%s:%d: ",__SHORTFILE__,__FUNCTION__,__LINE__); \
    printf(__VA_ARGS__)
#else
#define PRINTF(...)
#endif

#include "fs.h"

#define FS_NUM_PAGES            ((FS_SECTOR_SIZE / FS_PAGE_SIZE) * FS_NUM_SECTORS)
#define FS_NUM_PAGES_PER_SECTOR (FS_SECTOR_SIZE / FS_PAGE_SIZE)
#define true  1
#define false 0
typedef uint8_t bool;

FS_FILE_DESCRIPTOR FileDescriptor[FS_MAX_OPEN_FILES];
FS_GS_MODE FS_GS_MODE_DefaultVal = FS_GS_MODE_RELUCTANT;

/******************************************************************************
 * \brief
 *
 * \param
 * \param
 * \return
 *
 ******************************************************************************/
void FS_Init(void)
{
    memset(FileDescriptor,0,sizeof(FileDescriptor));

    PRINTF("sizeof(FS_BUFFERED_SECTOR) = %d \n",sizeof(FS_BUFFERED_SECTOR));
    PRINTF("sizeof(FS_START_SECTOR) = %d \n",sizeof(FS_START_SECTOR));
    PRINTF("sizeof(FS_DATA_SECTOR) = %d \n",sizeof(FS_DATA_SECTOR));
    PRINTF("sizeof(FS_DATA_SECTOR.data) = %d \n",sizeof(FileDescriptor[0].Sector.DataSector.data));

}

/******************************************************************************
 * \brief
 *
 * \param void
 * \return FS_RESULT
 *
 ******************************************************************************/
FS_RESULT FS_Format(void)
{
    uint32_t i;

    PRINTF("Formatting %u sectors\n", FS_NUM_SECTORS);

    for(i = 0; i < FS_NUM_SECTORS; i++)
    {
        FS_ERASE(i);
    }

    PRINTF(" done!\n");
    FS_Init();

    return FS_RESULT_SUCCESS;
}

/******************************************************************************
 *  \brief
 *
 * \param
 * \param
 * \return
 *
 *******************************************************************************/
uint32_t FS_FindFile(char* name)
{
    uint32_t        i;
    FS_HDR_TAG      HdrTag;
    FS_START_SECTOR StartSector;

    for(i=0; i<FS_NUM_PAGES; i++)
    {
        FS_READ(&HdrTag,i*FS_PAGE_SIZE,sizeof(HdrTag));
        if (HdrTag.u8Type == FS_HDR_TYPE_FILE_START)
        {
            FS_READ(&StartSector,i*FS_PAGE_SIZE,sizeof(StartSector));
            if (strcmp(StartSector.Name,name) == 0)
            {
                return i*FS_PAGE_SIZE;
            }
        }
    }
    return FS_INVALID_ADDR;
}

int FS_GetDescriptor(void)
{
    int i;

    for(i=0; i<FS_MAX_OPEN_FILES; i++)
    {
        if (!FileDescriptor[i].InUse)
        {
            FileDescriptor[i].InUse = 1;
            return i;
        }
    }
    return -1;
}

void FS_ReleaseDescriptor(int fd)
{
    FileDescriptor[fd].InUse = 0;
}

uint32_t FS_GarbageCollect(FS_GS_MODE ModeE)
{
    uint32_t        i,j,count;
    FS_HDR_TAG      HdrTag;
    uint32_t        u32NextAddr = FS_INVALID_ADDR;
    bool            fDone = false;

    for(i=0; (i<FS_NUM_SECTORS) && (!fDone); i++)
    {
        count = 0;
        for(j=0; j<FS_NUM_PAGES_PER_SECTOR; j++)
        {
            FS_READ(&HdrTag,(i*FS_SECTOR_SIZE) + (j*FS_PAGE_SIZE),sizeof(HdrTag));
            if (HdrTag.fObsolete == 0)
            {
                count++;
            }
        }
        if (count == FS_NUM_PAGES_PER_SECTOR)
        {
            // we can reclaim this sector
            FS_ERASE(i);
            if (u32NextAddr == FS_INVALID_ADDR)
            {
                u32NextAddr = i*FS_SECTOR_SIZE;
            }
            if (ModeE == FS_GS_MODE_RELUCTANT)
            {
                fDone = true;
            }
        }
    }
    return u32NextAddr;
}

uint32_t FS_FindNextFreePage(void)
{
    uint32_t        i;
    uint8_t         u8Hdr;

    for(i=0; i<FS_NUM_PAGES; i++)
    {
        FS_READ(&u8Hdr,i*FS_PAGE_SIZE,sizeof(u8Hdr));
        if (u8Hdr == 0xFF)
        {
            return i*FS_PAGE_SIZE;
        }
    }

    // no more free pages
    return FS_GarbageCollect(FS_GS_MODE_DefaultVal);
}

uint32_t FS_AdvanceToNextPage(int fd)
{
    if (FileDescriptor[fd].Sector.StartSector.Hdr.u32NextAddr1 == 0)
    {
        return FileDescriptor[fd].Sector.StartSector.Hdr.u32NextAddr2;
    }
    else
    {
        return FileDescriptor[fd].Sector.StartSector.Hdr.u32NextAddr1;
    }
}

/******************************************************************************
 * \brief
 *
 * \param
 * \param
 * \return
 *
 *****************************************************************************/
int FS_Create(char* name, FS_FLAGS FlagsE)
{
    uint32_t Addr = FS_FindFile(name);
    int fd = FS_GetDescriptor();
    uint32_t u32NextAddr;

    if (fd < 0) return FS_RESULT_GEN_ERROR;

    if (Addr == FS_INVALID_ADDR)
    {
        // create a new file
        if (FlagsE & FS_FLAGS_READ)
        {
            FS_ReleaseDescriptor(fd);
            return -1;
        }
        if ((FlagsE & FS_FLAGS_WRITE) == 0)
        {
            FS_ReleaseDescriptor(fd);
            return -1;
        }

        u32NextAddr = FS_FindNextFreePage();
        if (u32NextAddr == FS_INVALID_ADDR)
        {
            FS_ReleaseDescriptor(fd);
            return -1;
        }

        memset(FileDescriptor[fd].Sector.u8SectorData,0xFF,FS_PAGE_SIZE);
        FileDescriptor[fd].Sector.StartSector.Hdr.HdrTag.Tag.fUsed = 0;
        strcpy(FileDescriptor[fd].Sector.StartSector.Name,name);
        FileDescriptor[fd].u32NextAddr = FS_FindNextFreePage();
        if (FileDescriptor[fd].u32NextAddr == FS_INVALID_ADDR)
        {
            FS_ReleaseDescriptor(fd);
            return -1;
        }
        FS_WRITE(&(FileDescriptor[fd].Sector),sizeof(FileDescriptor[fd].Sector.StartSector),FileDescriptor[fd].u32NextAddr);
        FileDescriptor[fd].u32NextAddr = FileDescriptor[fd].Sector.StartSector.Hdr.u32NextAddr1 = FS_FindNextFreePage();
        return fd;
    }
    else
    {
        // file was found
        FS_READ(&(FileDescriptor[fd].Sector),sizeof(FileDescriptor[fd].Sector.StartSector),Addr);
        if (FS_AdvanceToNextPage(fd) != FS_RESULT_SUCCESS)
        {
            FS_ReleaseDescriptor(fd);
            return -1;
        }
        return fd;
    }
}

int FS_Write(int fd, void* buf, int size)
{
    int i;

    for(i=0; i<size; i++)
    {
        // TODO FileDescriptor[fd].Sector.DataSector.data[] = buf[i];
        // TODO if (FileDescriptor[fd].WritePtr == )
        {
            // buffer is full => write to disk
        }
    }
    return -1;
}
