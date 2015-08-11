/*

Neo systems circular log SPI Flash file system

*/
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "neocfs.h"

bool           NEOCFS_Debug=1;
extern uint8_t FlashBuffer[4096*2048];

static bool    NEOCFS_Initialised = false;

#define DEBUG 1
#if DEBUG
char debugstr[128];
#define DPRINTF(...)  \
    if (NEOCFS_Debug) \
    { \
        sprintf(debugstr,"%s:%s:%d",__BASE_FILE__,__FUNCTION__,__LINE__); \
        printf("%40s: ",debugstr); \
        printf(__VA_ARGS__); \
    }
#else
#define DPRINTF(...)
#endif

extern NEOCFS_FILE_DESCRIPTOR_ST* __start_neocfs_file_descriptors;
extern NEOCFS_FILE_DESCRIPTOR_ST* __stop_neocfs_file_descriptors;

static void DebugPrintBuffer(void* buf, int size, uint32_t u32Offset)
{
#if DEBUG
    uint8_t* p = buf;
    int i;

    if (NEOCFS_Debug)
    {
        for(i=0; i<size; i++)
        {
            if ((i%16) == 0)
            {
                printf("%08X: ",u32Offset+i);
            }
            printf("%02X ",p[i]);
            if ((i%16) == 15)
            {
                printf("\n");
            }
        }
        printf("\n");
    }
#endif
}

int NEOCFS_Diskread(void* Buf, int size, uint32_t u32Offset)
{
    DPRINTF("disk read %d, %u (0x%08X)\n",size,u32Offset,u32Offset);
    memcpy(Buf,FlashBuffer+u32Offset,size);
    DebugPrintBuffer(Buf,size,u32Offset);
    return 0;
}

int NEOCFS_Diskwrite(void* Buf, int size, uint32_t u32Offset)
{
    uint8_t* p = Buf;
    int i;

    DPRINTF("disk write %d, %u\n",size,u32Offset);
    DebugPrintBuffer(Buf,size,u32Offset);
    for(i=0; i<size; i++)
    {
        FlashBuffer[u32Offset+i] &= p[i];
    }
    return 0;
}

int NEOCFS_Diskerase(uint16_t u16Sector)
{
    memset(FlashBuffer+(u16Sector * NEOCFS_SECTOR_SIZE),0xFF,NEOCFS_SECTOR_SIZE);
    return 0;
}

int NEOCFS_Format(void)
{
    int i;

    for(i=0; i<NEOCFS_SECTOR_COUNT; i++)
    {
        NEOCFS_Diskerase(i);
    }
    return 0;
}

static int isPowerOfTwo (uint32_t x)
{
 while (((x % 2) == 0) && x > 1) /* While x is even and > 1 */
   x /= 2;
 return (x == 1);
}

static uint32_t NextRecordAddress(NEOCFS_FILE_DESCRIPTOR_ST* fd,uint32_t u32Addr)
{
    uint32_t u32Result = u32Addr + fd->u32RecordSize+2;

    if (u32Result >= fd->u32EndAddr)
    {
        u32Result = fd->u32StartAddr;
    }
    return u32Result;
}

void NEOCFS_Init(void)
{
    NEOCFS_FILE_DESCRIPTOR_ST* i;

    NEOCFS_Initialised = false;
    DPRINTF("__start_neocfs_file_descriptors = %08X\n",(unsigned int)__start_neocfs_file_descriptors);
    DPRINTF("__stop_neocfs_file_descriptors = %08X\n",(unsigned int)__stop_neocfs_file_descriptors);
    for(i=__start_neocfs_file_descriptors; i<__stop_neocfs_file_descriptors; i++)
    {
        DPRINTF("Checking File: %s -> ",i->cFilename);

        if (!isPowerOfTwo(i->u32RecordSize + 2))
        {
            DPRINTF("size is NOT power of two");
            return;
        }
        DPRINTF("\n");
    }
    NEOCFS_Initialised = true;
}

static bool CheckInitialised(void)
{
    if (!NEOCFS_Initialised)
    {
        DPRINTF("NEOCFS_Init failed\n");
        return false;
    }
    else
    {
        return true;
    }
}

void NEOCFS_Dir(void)
{
    NEOCFS_FILE_DESCRIPTOR_ST* i;

    if (!CheckInitialised()) return;

    for(i=__start_neocfs_file_descriptors; i<__stop_neocfs_file_descriptors; i++)
    {
        DPRINTF("File: %s\n",i->cFilename);
    }
}

NEOCFS_FILE_DESCRIPTOR_ST* NEOCFS_OpenByName(char* filename)
{
    NEOCFS_FILE_DESCRIPTOR_ST* i;

    if (!CheckInitialised()) return NULL;

    for(i=__start_neocfs_file_descriptors; i<__stop_neocfs_file_descriptors; i++)
    {
        if (strcmp(i->cFilename,filename) == 0)
        {
            if (NEOCFS_OpenByDescriptor(i) == NEOCFS_RESULT_CODE_SUCCESS)
            {
                return i;
            }
        }
    }
    return NULL;
}

void NEOCFS_FormatFile(NEOCFS_FILE_DESCRIPTOR_ST* fd)
{
    uint32_t i;

    for(i=fd->u32StartAddr; i<fd->u32EndAddr; i+=NEOCFS_SECTOR_SIZE)
    {
        NEOCFS_Diskerase(i / NEOCFS_SECTOR_SIZE);
    }
}

static uint32_t FindTail(NEOCFS_FILE_DESCRIPTOR_ST* fd)
{
    uint32_t i,j,n,m;
    uint8_t u8Temp1,u8Temp2;
    bool fAllFF = true;

    // find the Tail
    // the tail is where a a FF end tag is followed by a non FF tag, followed by a non obsolete tag
    n = ((fd->u32EndAddr - fd->u32StartAddr) / (fd->u32RecordSize+2))+1;
    i = fd->u32StartAddr;

    while(n)
    {
        NEOCFS_Diskread(&u8Temp1,sizeof(u8Temp1),i);
        j = NextRecordAddress(fd,i);
        NEOCFS_Diskread(&u8Temp2,sizeof(u8Temp2),j);

        if (u8Temp1 != 0xFF)
        {
            fAllFF = false;
        }
        if ((u8Temp1 == 0xFF) && (u8Temp2 != 0xFF))
        {
            fAllFF = false;

            // data has started ....

            // .. but is it obsolete or not?
            m = ((fd->u32EndAddr - fd->u32StartAddr) / (fd->u32RecordSize+2))+1;
            while(m)
            {
                NEOCFS_Diskread(&u8Temp1,sizeof(u8Temp1),j);
                if ((u8Temp1 & ~NEOCFS_END_TAG_OBSOLETE) != 0)
                {
                    // this one is not obsolete - so this is where the file starts!
                    return j;
                }
                m--;
                j = NextRecordAddress(fd,j);
            }

            // all the records in the entire file are Obsolete!
            NEOCFS_FormatFile(fd);
        }
        n--;
        i = NextRecordAddress(fd,i);
    }
    if (fAllFF)
    {
        return fd->u32StartAddr;
    }
    else
    {
        return NEOCFS_NOT_FOUND;
    }

}

uint32_t FindHead(NEOCFS_FILE_DESCRIPTOR_ST* fd)
{
    // find the place where data is followed by FF

    uint32_t i,m;
    uint8_t u8Temp1;

    i = fd->u32Tail;
    m = ((fd->u32EndAddr - fd->u32StartAddr) / (fd->u32RecordSize+2)) + 1;

    while(m)
    {
        NEOCFS_Diskread(&u8Temp1,sizeof(u8Temp1),i);

        if (u8Temp1 == 0xFF)
        {
            return i;
        }
        m--;
        i = NextRecordAddress(fd,i);
    }
    return NEOCFS_NOT_FOUND;
}

int NEOCFS_OpenByDescriptor(NEOCFS_FILE_DESCRIPTOR_ST* fd)
{
    DPRINTF("Opening %s",fd->cFilename);

    fd->u32Tail = FindTail(fd);
    if (fd->u32Tail == NEOCFS_NOT_FOUND) return NEOCFS_RESULT_CODE_GENERR;

    fd->u32Head = FindHead(fd);
    if (fd->u32Head == NEOCFS_NOT_FOUND) return NEOCFS_RESULT_CODE_GENERR;

    fd->u32CurReadPos = fd->u32Tail;

    DPRINTF("Tail = %08X, Head = %08X\n",fd->u32Tail,fd->u32Head);

    // then find the head
    return NEOCFS_RESULT_CODE_SUCCESS;
}

void NEOCFS_CloseFile(NEOCFS_FILE_DESCRIPTOR_ST* fd)
{

}

static int GarbageCollect(NEOCFS_FILE_DESCRIPTOR_ST* fd)
{
    uint8_t  u8Tag;
    uint32_t i,j;
    bool     fFreeSpaceFound;

    for(j = fd->u32Head; j<(fd->u32Head + (2*NEOCFS_SECTOR_SIZE)); j++)
    {
        // first check if the file supports overwrite of oldest data ...
        if (j & NEOCFS_FILE_FLAGS_OVERWRITE_OLDEST)
        {
            // then its easy - we just wipe the sector
            if (NEOCFS_Diskerase(fd->u32Head / NEOCFS_SECTOR_SIZE) != 0)
            {
                return NEOCFS_RESULT_CODE_DISK_FAIL;
            }
        }
        else
        {
            // else see if we can free up some space in the file to write to
            fFreeSpaceFound = true;
            for(i=j; i<(j + NEOCFS_SECTOR_SIZE); i += (fd->u32RecordSize+2))
            {
                NEOCFS_Diskread(&u8Tag,sizeof(u8Tag),i);
                if ((u8Tag & ~NEOCFS_END_TAG_OBSOLETE) != 0)
                {
                    fFreeSpaceFound = false;
                }
            }

            if (!fFreeSpaceFound) return NEOCFS_RESULT_CODE_GENERR;

            if (NEOCFS_Diskerase(fd->u32Head / NEOCFS_SECTOR_SIZE))
            {
                return NEOCFS_RESULT_CODE_SUCCESS;
            }
            else
            {
                return NEOCFS_RESULT_CODE_DISK_FAIL;
            }

        }
    }
    return NEOCFS_RESULT_CODE_SUCCESS;
}

int NEOCFS_WriteRecord(NEOCFS_FILE_DESCRIPTOR_ST* fd, void* data)
{
    int result;
    uint8_t u8Temp;
    uint8_t u8StartTag = NEOCFS_START_TAG_WRITE_STARTED;
    uint8_t u8EndTag   = NEOCFS_END_TAG_WRITE_DONE;

    NEOCFS_Diskread(&u8Temp,sizeof(u8Temp),fd->u32Head);
    if (u8Temp != 0xFF)
    {
        // this record is NOT available
        result = GarbageCollect(fd);

        // if we cannot claim space in the file, then return error
        if (result != NEOCFS_RESULT_CODE_SUCCESS) return result;
    }

    // write the record to the circular file
    NEOCFS_Diskwrite(&u8StartTag,sizeof(u8StartTag),fd->u32Head);
    NEOCFS_Diskwrite(data,fd->u32RecordSize,fd->u32Head+1);
    NEOCFS_Diskwrite(&u8EndTag,sizeof(u8EndTag),fd->u32Head+fd->u32RecordSize+1);

    // now advance the header pointer
    fd->u32Head += fd->u32RecordSize + 2;

    // check if this is the end of the circ file ....
    if (fd->u32Head >= fd->u32EndAddr)
    {
        // we are at the end - wrap to start again
        fd->u32Head = fd->u32StartAddr;
    }

    return NEOCFS_RESULT_CODE_SUCCESS;
}

int NEOCFS_SeekFromTail(NEOCFS_FILE_DESCRIPTOR_ST* fd, uint32_t pos)
{
    uint32_t u32Addr = fd->u32Tail;

    while(pos != 0)
    {
        u32Addr += (fd->u32RecordSize+2);
        if (u32Addr >= fd->u32EndAddr)
        {
            u32Addr = fd->u32StartAddr;
        }
        pos--;
    }
    fd->u32CurReadPos = u32Addr;
    return NEOCFS_RESULT_CODE_SUCCESS;
}

int NEOCFS_ReadRecord(NEOCFS_FILE_DESCRIPTOR_ST* fd, void* data)
{
    if (NEOCFS_Diskread(data,fd->u32RecordSize,fd->u32CurReadPos+1) == 0)
    {
        return NEOCFS_RESULT_CODE_SUCCESS;
    }
    else
    {
        return NEOCFS_RESULT_CODE_GENERR;
    }
}

int NEOCFS_NextRecord(NEOCFS_FILE_DESCRIPTOR_ST* fd)
{
    fd->u32CurReadPos += (fd->u32RecordSize+2);

    if (fd->u32CurReadPos >= fd->u32EndAddr)
    {
        fd->u32CurReadPos = fd->u32StartAddr;
    }
    return NEOCFS_RESULT_CODE_SUCCESS;
}

int NEOCFS_MarkObsolete(NEOCFS_FILE_DESCRIPTOR_ST* fd)
{
    uint8_t u8Tag;

    if (fd->u32CurReadPos == fd->u32Head)
    {
        NEOCFS_Diskread(&u8Tag,sizeof(u8Tag),fd->u32Head+fd->u32RecordSize+1);
        u8Tag &= NEOCFS_END_TAG_OBSOLETE;
        NEOCFS_Diskwrite(&u8Tag,sizeof(u8Tag),fd->u32Head+fd->u32RecordSize+1);
    }
    return NEOCFS_RESULT_CODE_SUCCESS;
}
