/*

Neo systems generic SPI Flash file system

*/
#include "neofs.h"
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

extern uint8_t FlashBuffer[4096*2048];

bool           NEOFS_Debug=0;

#define NEOFS_MAX_NUMBER_OF_OPEN_FILES (4)
#define NEOFS_DEFAULT_GARBAGE_COLLECT_MODE NEOFS_GARBAGE_COLLECT_MODE_RELUCTANT

#define NEOFS_MIN_NUM_FREE_SECTORS (NEOFS_NUM_PAGES_PER_SECTOR)

#define DEBUG 1
#if DEBUG
char debugstr[128];
#define DPRINTF(...)  \
    if (NEOFS_Debug) \
    { \
        sprintf(debugstr,"%s:%s:%d",__BASE_FILE__,__FUNCTION__,__LINE__); \
        printf("%40s: ",debugstr); \
        printf(__VA_ARGS__); \
    }
#else
#define DPRINTF(...)
#endif

typedef struct
{
    uint8_t  u8Flags;
    uint16_t u16CurSector;
    uint32_t u32Curpos;      // cur position in the file (logic position in file)
    uint32_t u32CurAddr;     // where in flash are we writing
    uint32_t u32CurPageAddr; // start address of current page where we are working
    uint32_t u32StartAddr;   // adress (in flash) where file starts

    uint8_t  u8CacheBuf[NEOFS_PAGE_SIZE];
    uint32_t u32CacheStart; // address of cache start
    uint32_t u32CacheEnd; // address of cache end

    NEOFS_LOG_HEADER LastLogHeader;
    NEOFS_LOG_HEADER CurLogHeader;
    bool fLastLogInvalid;
} NEOFS_FILEDESCRIPTOR;

NEOFS_FILEDESCRIPTOR NEOFS_FileDescriptor[NEOFS_MAX_NUMBER_OF_OPEN_FILES];

//uint8_t NEOFS_FlashBuffer[NEOFS_SECTOR_COUNT*NEOFS_SECTOR_SIZE];

static void DebugPrintBuffer(void* buf, int size, uint32_t u32Offset)
{
#if DEBUG
    uint8_t* p = buf;
    int i;

    if (NEOFS_Debug)
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

int NEOFS_Diskread(void* Buf, int size, uint32_t u32Offset)
{
    DPRINTF("disk read %d, %u (0x%08X)\n",size,u32Offset,u32Offset);
    memcpy(Buf,FlashBuffer+u32Offset,size);
    DebugPrintBuffer(Buf,size,u32Offset);
    return 0;
}

int NEOFS_Diskwrite(void* Buf, int size, uint32_t u32Offset)
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

int NEOFS_Diskerase(uint16_t u16Sector)
{
    memset(FlashBuffer+(u16Sector * NEOFS_SECTOR_SIZE),0xFF,NEOFS_SECTOR_SIZE);
    return 0;
}

int NEOFS_Format(void)
{
    int i;

    for(i=0; i<NEOFS_SECTOR_COUNT; i++)
    {
        NEOFS_Diskerase(i);
    }
    return 0;
}

static uint32_t AdvanceToNextPage(int fd)
{
    NEOFS_PAGE_HEADER ThisPageheader;
    NEOFS_PAGE_HEADER NextPageheader;

    DPRINTF("Advance to next page\n");
    // find the next page address
    NEOFS_Diskread(&ThisPageheader,sizeof(NEOFS_PAGE_HEADER),NEOFS_FileDescriptor[fd].u32CurPageAddr);
    DPRINTF("ThisPageheader.u32NextPageAddr = %08X\n",ThisPageheader.u32NextPageAddr);
    if (ThisPageheader.u32NextPageAddr != 0xFFFFFFFF)
    {
        NEOFS_FileDescriptor[fd].u32CurPageAddr = ThisPageheader.u32NextPageAddr;
        DPRINTF("NEOFS_FileDescriptor[fd].u32CurPageAddr = %08X\n",NEOFS_FileDescriptor[fd].u32CurPageAddr);

    }

    // iterate through all the page forwarding addresses
    NEOFS_Diskread(&NextPageheader,sizeof(NEOFS_PAGE_HEADER),NEOFS_FileDescriptor[fd].u32CurPageAddr);
    DPRINTF("NextPageheader.u32ThisPageFwdAddr = %08X\n",NextPageheader.u32ThisPageFwdAddr);
    while(NextPageheader.u32ThisPageFwdAddr != 0xFFFFFFFF)
    {
        NEOFS_FileDescriptor[fd].u32CurPageAddr = NextPageheader.u32ThisPageFwdAddr;
        DPRINTF("NEOFS_FileDescriptor[fd].u32CurPageAddr = %08X\n",NEOFS_FileDescriptor[fd].u32CurPageAddr);
        NEOFS_Diskread(&NextPageheader,sizeof(NEOFS_PAGE_HEADER),NEOFS_FileDescriptor[fd].u32CurPageAddr);
        DPRINTF("NextPageheader.u32ThisPageFwdAddr = %08X\n",NextPageheader.u32ThisPageFwdAddr);
    }

    NEOFS_FileDescriptor[fd].u32CurAddr    = NEOFS_FileDescriptor[fd].u32CurPageAddr + offsetof(NEOFS_PAGE,Contents);
    NEOFS_FileDescriptor[fd].u32CacheStart = 0xFFFFFFFF;
    NEOFS_FileDescriptor[fd].u32CacheEnd   = 0xFFFFFFFF;
    DPRINTF("NEOFS_FileDescriptor[fd].u32CurAddr = %08X\n",NEOFS_FileDescriptor[fd].u32CurAddr);
    return NEOFS_FileDescriptor[fd].u32CurPageAddr;
}

static int GetFileDescriptor(void)
{
    int i;

    for(i=0; i<NEOFS_MAX_NUMBER_OF_OPEN_FILES; i++)
    {
        if (NEOFS_FileDescriptor[i].u8Flags == 0)
        {
            NEOFS_FileDescriptor[i].u8Flags = 1;
            return i;
        }
    }
    return -1;
}

static void ReturnFileDescriptor(int fd)
{
    NEOFS_FileDescriptor[fd].u8Flags = 0;
}

int NEOFS_GoToLastLogEntry(int fd)
{
    uint32_t u32Offs = 0;
    uint8_t  u8TempTag;
    bool     fDone = false;

    do
    {
        NEOFS_Diskread(&u8TempTag,sizeof(u8TempTag),NEOFS_FileDescriptor[fd].u32CurAddr + u32Offs);
        if ((u8TempTag & (NEOFS_TAG_USED | NEOFS_TAG_COMPLETED | NEOFS_TAG_OBSOLETE)) == NEOFS_TAG_OBSOLETE)
        {
            // valid log entry
            NEOFS_Diskread(&NEOFS_FileDescriptor[fd].LastLogHeader,sizeof(NEOFS_LOG_HEADER),
                NEOFS_FileDescriptor[fd].u32CurAddr + u32Offs);
            u32Offs += NEOFS_FileDescriptor[fd].LastLogHeader.u16Size;
            NEOFS_FileDescriptor[fd].fLastLogInvalid = false;
        }
        else if (u8TempTag != 0xFF)
        {
            // we have a problem here!
            // we were interrupted while creating this log entry!
            // if the log entries appear to be invalid, then we have to abandon this sector
            u8TempTag = (0xFF  & NEOFS_TAG_USED & NEOFS_TAG_COMPLETED & NEOFS_TAG_OBSOLETE);
            NEOFS_Diskwrite(&u8TempTag,sizeof(u8TempTag),NEOFS_FileDescriptor[fd].u32CurAddr + u32Offs);
            NEOFS_FileDescriptor[fd].fLastLogInvalid = true;
        }
        else if (u8TempTag == 0xFF)
        {
            fDone = true;
        }
    } while(!fDone);
    return 0;
}

void InvalidateReadCache(int fd)
{
    NEOFS_FileDescriptor[fd].u32CacheStart = 0xFFFFFFFF;
    NEOFS_FileDescriptor[fd].u32CacheEnd   = 0xFFFFFFFF;
}

void DumpPageheader(NEOFS_PAGE_HEADER* PageHeader)
{
    DPRINTF("u8Tag %02X\n",PageHeader->u8Tag);
    DPRINTF("u8Size %d\n",PageHeader->u8Size);
    DPRINTF("u32NextPageAddr %08X\n",PageHeader->u32NextPageAddr);
    DPRINTF("u32ThisPageFwdAddr %08X\n",PageHeader->u32ThisPageFwdAddr);
}

int NEOFS_Seek(int fd, uint32_t u32Offset)
{
    NEOFS_PAGE_HEADER PageHeader;
    bool     fDone = false;
    uint32_t u32NextPageAddr = NEOFS_FileDescriptor[fd].u32StartAddr;
    uint32_t u32Len = 0;

    NEOFS_FileDescriptor[fd].u32CurPageAddr = NEOFS_FileDescriptor[fd].u32StartAddr;
    NEOFS_FileDescriptor[fd].u32CurAddr = NEOFS_FileDescriptor[fd].u32StartAddr + offsetof(NEOFS_PAGE,Contents);

    DPRINTF("Seek %u\n",u32Offset);
    AdvanceToNextPage(fd); // skip the fileheader page
    NEOFS_FileDescriptor[fd].u32Curpos = 0;
    u32NextPageAddr = NEOFS_FileDescriptor[fd].u32CurPageAddr;
    do
    {
        NEOFS_Diskread(&PageHeader,sizeof(PageHeader),u32NextPageAddr);
        DumpPageheader(&PageHeader);
        u32Len += PageHeader.u8Size;
        NEOFS_FileDescriptor[fd].u32Curpos += PageHeader.u8Size;
        DPRINTF("Len = %u\n",u32Len);
        if (PageHeader.u32NextPageAddr == 0xFFFFFFFF)
        {
            // end of file
            DPRINTF("End of file\n");
            fDone = true;
            NEOFS_FileDescriptor[fd].u32Curpos   = u32Len;
            NEOFS_FileDescriptor[fd].u32CurAddr += (u32Len - NEOFS_FileDescriptor[fd].u32Curpos);

            NEOFS_Diskread(NEOFS_FileDescriptor[fd].u8CacheBuf,PageHeader.u8Size,NEOFS_FileDescriptor[fd].u32CurAddr);
            NEOFS_FileDescriptor[fd].u32CacheStart = NEOFS_FileDescriptor[fd].u32CurAddr;
            NEOFS_FileDescriptor[fd].u32CacheEnd   = NEOFS_FileDescriptor[fd].u32CurAddr + PageHeader.u8Size;
        }
        else if ((NEOFS_FileDescriptor[fd].u32Curpos - u32Offset) <= PageHeader.u8Size)
        {
            // seek pos is in this sector
            DPRINTF("Seek page reached\n");
            fDone = true;
            NEOFS_FileDescriptor[fd].u32Curpos   = u32Offset;
            NEOFS_FileDescriptor[fd].u32CurAddr += (u32Offset - NEOFS_FileDescriptor[fd].u32Curpos);

            // fill the read cache
            NEOFS_Diskread(NEOFS_FileDescriptor[fd].u8CacheBuf,PageHeader.u8Size,NEOFS_FileDescriptor[fd].u32CurAddr);
            NEOFS_FileDescriptor[fd].u32CacheStart = NEOFS_FileDescriptor[fd].u32CurAddr;
            NEOFS_FileDescriptor[fd].u32CacheEnd   = NEOFS_FileDescriptor[fd].u32CurAddr + PageHeader.u8Size;
        }
        else
        {
            AdvanceToNextPage(fd);
            DPRINTF("PageHeader.u32NextPageAddr %08X\n",PageHeader.u32NextPageAddr);
        }
    } while(!fDone);

    // InvalidateReadCache(fd);
    return 0;
}

void CopySector(uint16_t u16Dest, uint16_t u16Src)
{
    uint8_t u8Buf[16];
    int i=0;

    for(i=0; i<NEOFS_SECTOR_SIZE; i+=16)
    {
        NEOFS_Diskread(u8Buf,16,u16Src * NEOFS_SECTOR_SIZE + i);
        NEOFS_Diskwrite(u8Buf,16,u16Src * NEOFS_SECTOR_SIZE + i);
    }
}

bool CheckAllFF(void* Buf,int size)
{
    uint8_t* p = (uint8_t*)Buf;
    int i;
    bool AllFF = true;

    for(i=0; i<size; i++)
    {
        if (*p != 0xFF) AllFF = false;
        p++;
    }
    return AllFF;
}

int CountNumObsoleteOrFreePages(uint16_t u16Sector)
{
    int count = 0;
    //int cleancount = 0;
    int j;
    uint8_t u8Tag;

    for(j=0; j<NEOFS_NUM_PAGES_PER_SECTOR; j++)
    {
        NEOFS_Diskread(&u8Tag,sizeof(u8Tag),u16Sector*NEOFS_SECTOR_SIZE + j*NEOFS_PAGE_SIZE);
        if ((u8Tag == 0xFFu) || ((u8Tag & (~NEOFS_TAG_OBSOLETE))==0))
        {
            // obsolete
            count++;
        }
        //if (u8Tag == 0xFFu)
        //{
        //    cleancount++;
        //}
    }
    DPRINTF("Number of free or reclaimable pages in sector %u: %d\n",u16Sector,count);
    //if (cleancount == count)
    //{
    //    return 0;
    //}
    //else
    //{
        return count;
    //}
}

uint16_t NextSector(uint16_t u16Sector)
{
    return 0; // TODO

}

uint32_t NextPage(uint32_t u32Page)
{
    return 0; // TODO
}

uint8_t ReadPageData(uint32_t* u32Address, void* u8Buf)
{
#if 0
//    uint8_t* p = (uint8_t*)u8Buf;
    NEOFS_PAGE_HEADER PageHeader;

    NEOFS_Diskread(&PageHeader,sizeof(PageHeader),*u32Address);
    NEOFS_Diskread(u8Buf,PageHeader.u8Size,*u32Address + sizeof(PageHeader));

    return PageHeader.u8Size;
#endif // 0
    return 0;
}

int WriteDataStreamToPage(void* Buf,int size, uint32_t u32Address, NEOFS_PAGE_HEADER* Header)
{
    int NumToWrite;
    int NumAvailInPage = Header->u8Size - sizeof(NEOFS_PAGE_HEADER);

    if (NumAvailInPage > 0)
    {
        // there is still space available on the current page
        if (size > NumAvailInPage)
        {
            NumToWrite = NumAvailInPage;
        }
        else
        {
            NumToWrite = size;
        }
        NEOFS_Diskwrite(Buf,NumToWrite,u32Address);
        Header->u8Size += (uint8_t)NumToWrite;
        return size - NumToWrite;
    }
    else
    {
        return size;
    }
}

int PackFile(uint16_t u16Sector)
{
#if 0
    int i;
    uint8_t  u8Tag;
    uint16_t u16Free=0;
    uint8_t  u8Buf[NEOFS_SECTOR_SIZE];
    uint32_t u32ReadAddr;
    uint32_t u32WriteAddr;
//    volatile uint32_t u32BaseWriteAddr; // TODO remove volatile
    uint8_t  u8LenRead;
    uint8_t  u8LenWritten;
    NEOFS_PAGE_HEADER TempHeader;

    u32ReadAddr = u16Sector * NEOFS_SECTOR_SIZE;

    u16Free=0xFFFF;
    for(i=NEOFS_START_SECTOR; (u16Free == 0xFFFF) && (i<NEOFS_SECTOR_COUNT); i++)
    {
        NEOFS_Diskread(&u8Tag,sizeof(u8Tag),i*NEOFS_SECTOR_SIZE);
        if (u8Tag == 0xFFu)
        {
            u16Free = i;
        }
    }

    if (u16Free != 0xFFFF)
    {
        // we have a sector to try and compress the file with
        u32WriteAddr = u16Free * NEOFS_SECTOR_SIZE;
//        u32BaseWriteAddr = u32WriteAddr;
        u8LenRead = 0;
        do
        {
            u8LenRead = ReadPageData(&u32ReadAddr,u8Buf);
            u8LenWritten = WriteDataStreamToPage(u8Buf,u8LenRead,u32WriteAddr,&TempHeader);

            u32WriteAddr += u8LenWritten;
            if ((u8LenWritten != u8LenRead) ||
                (TempHeader.u8Size >= (NEOFS_PAGE_SIZE - sizeof(NEOFS_PAGE_HEADER)))
                )
            {
                // sector is full
                u16Free=0xFFFF;
                for(i=NEOFS_START_SECTOR; (u16Free == 0xFFFF) && (i<NEOFS_SECTOR_COUNT); i++)
                {
                    NEOFS_Diskread(&u8Tag,sizeof(u8Tag),i*NEOFS_SECTOR_SIZE);
                    if (u8Tag == 0xFFu)
                    {
                        u16Free = i;
                    }
                }
                if (u16Free != 0xFFFF)
                {
                    // we have a sector to try and compress the file with
                    TempHeader.u32NextAddr1 = u16Free * NEOFS_SECTOR_SIZE;
                    TempHeader.u32NextAddr2 = 0xFFFFFFFF;
                    NEOFS_Diskwrite(&TempHeader,sizeof(TempHeader),u16Free * NEOFS_SECTOR_SIZE);
                }
                else
                {
                    return -1;
                }
                WriteDataStreamToPage(u8Buf,u8LenRead - u8LenWritten,u32WriteAddr,&TempHeader);
            }
        } while (u8LenRead != 0);
    }
    else
    {
        return -1;
    }
#endif
    return 0;
}

uint16_t NEOFS_GarbageCollect(NEOFS_GARBAGE_COLLECT_MODE ModeE)
{
    uint16_t u16Result = 0xFFFF;
    int i;
    //uint8_t u8Tag;
    //uint16_t u16Offs=0;
    //bool fEraseSector = true;
    int NumObsoletePages;
    int count = 0;

    printf("Do garbage collection\n");

    for(i=NEOFS_START_SECTOR; i<NEOFS_SECTOR_COUNT; i++)
    {
        NumObsoletePages = CountNumObsoleteOrFreePages(i);
        if (NumObsoletePages == NEOFS_NUM_PAGES_PER_SECTOR)
        {
            // we can claim this sector
            printf("reclaim sector %d\n",i);
            NEOFS_Diskerase(i);
            count++;
            u16Result = (uint16_t)i;
            if ((ModeE == NEOFS_GARBAGE_COLLECT_MODE_RELUCTANT) && (count > 1))
            {
                printf("Return sector(reluctant) %u\n",u16Result);
                return u16Result;
            }
        }
    }
    if (count==0)
    {
        printf("Cannot reclaim anything\n");
        return NEOFS_NO_SECTOR_FOUND;
    }
    else
    {
        printf("Return sector(aggressive) %u\n",u16Result);
        return u16Result;
    }
#if 0
    if (u16Result == 0xFFFF)
    {
        // we'll have to move some pages around if we can - this might take long
        // we'll have to go per file and try to claim back space

        for(i=0; i<NEOFS_SECTOR_COUNT; i++)
        {
            NEOFS_Diskread(&u8Tag,sizeof(uint8_t),i*NEOFS_SECTOR_SIZE);
            {
                if ((u8Tag & NEOFS_PAGETYPE_MASK) == NEOFS_PAGE_FILEHEADER)
                {
                    // this is the start of a file
                    int ThisFileSector = i;
                    //int NumObsolotePages=0;

                    do
                    {
                        NumObsoletePages += CountNumObsoletePages(ThisFileSector);
                    } while ((ThisFileSector = NextSector(ThisFileSector)) != 0xFFFF);

                    if (NumObsoletePages > 0)
                    {
                        PackFile(i);
                    }
                }
            }
        }

    }
#endif // 0

    return 0;
}

uint16_t FindFreeSector(void)
{
    uint8_t u8Tag;
    uint16_t i;
    int count = 0;

    for(i=NEOFS_START_SECTOR; i<NEOFS_SECTOR_COUNT; i++)
    {
        NEOFS_Diskread(&u8Tag,sizeof(u8Tag),i*NEOFS_SECTOR_SIZE);
        if (u8Tag == 0xFFu)
        {
            count++;

            if (count > NEOFS_MIN_NUM_FREE_SECTORS)
            {
                // we have at least NEOFS_MIN_NUM_FREE_SECTORS free sectors
                DPRINTF("Return sector num = %u\n",i);
                return i;
            }
        }
    }

    DPRINTF("Not enought free sectors - do garbage collection!\n");
    // if we have reached here then there is no more space!
    return NEOFS_GarbageCollect(NEOFS_DEFAULT_GARBAGE_COLLECT_MODE);
}

uint32_t FindFreePage(uint16_t* u16CurSector)
{
    uint8_t u8Tag;
    uint16_t i;
    uint32_t j;

    i=*u16CurSector;

    do
    {
        for(j=0; j<NEOFS_NUM_PAGES_PER_SECTOR; j++)
        {
            NEOFS_Diskread(&u8Tag,sizeof(u8Tag),i*NEOFS_SECTOR_SIZE + j*NEOFS_PAGE_SIZE);
            if (u8Tag == 0xFFu)
            {
                DPRINTF("Free page found: Sect = %u, Page = %u\n",i,j);
                *u16CurSector = i;
                return (i*NEOFS_SECTOR_SIZE + j*NEOFS_PAGE_SIZE);
            }
        }

        // no free page found in current sector -> move to a new sector
        DPRINTF("Look for a free sector...\n");
        i = FindFreeSector();
    } while(i != NEOFS_NO_SECTOR_FOUND);

    return NEOFS_NO_PAGE_FOUND;
}

int CheckWriteCache(int fd)
{
    return 0;
}

int FlushCache(int fd, bool fForce)
{
    bool fNewpagerequired = false;
    int i;
    uint8_t u8TempBuf[NEOFS_PAGE_SIZE];
    uint8_t u8Temp;
    uint32_t u32NextfreePage;
    NEOFS_PAGE_HEADER PageHeader;
    NEOFS_PAGE_HEADER OldPageHeader;

    if (NEOFS_FileDescriptor[fd].u32CacheStart == 0xFFFFFFFF) return 0;

    DPRINTF("Flush cache\n");

    // read the existing contents on the flash
    NEOFS_Diskread(u8TempBuf,NEOFS_FileDescriptor[fd].u32CacheEnd - NEOFS_FileDescriptor[fd].u32CacheStart,
        NEOFS_FileDescriptor[fd].u32CacheStart);

    // now check if the new buffer can be written over the existing flash buffer
    for(i=0; i<(NEOFS_FileDescriptor[fd].u32CacheEnd - NEOFS_FileDescriptor[fd].u32CacheStart); i++)
    {
        u8Temp = (NEOFS_FileDescriptor[fd].u8CacheBuf[i] ^ u8TempBuf[i]);
        DPRINTF("Check if new page required: %02X %02X => %02X\n",
            NEOFS_FileDescriptor[fd].u8CacheBuf[i],u8TempBuf[i],
            (u8Temp & u8TempBuf[i]) == u8Temp);
        if ((u8Temp & u8TempBuf[i]) != u8Temp)
        {
            fNewpagerequired = true;
        }
    }

    if (fNewpagerequired)
    {
        DPRINTF("New page required\n");

        NEOFS_Diskread(&OldPageHeader,sizeof(OldPageHeader),NEOFS_FileDescriptor[fd].u32CurPageAddr);

        // get a new page
        u32NextfreePage = FindFreePage(&NEOFS_FileDescriptor[fd].u16CurSector);
        if (u32NextfreePage == 0xFFFFFFFF)
        {
            // return error
            DPRINTF("Disk is full\n");
            return -1;
        }

        // if the cache is less bytes than the new sector - then just return so that the cache can fill up first
        if (((NEOFS_FileDescriptor[fd].u32CurAddr - NEOFS_FileDescriptor[fd].u32CacheStart) < sizeof(NEOFS_PAGE_CONTENTS)) && !fForce)
        {
            DPRINTF("File not packed optimally\n");
            return -2;
        }

        // set the used bit
        memset(&PageHeader,0xFF,sizeof(PageHeader));
        PageHeader.u8Tag  &= NEOFS_TAG_USED;
        NEOFS_Diskwrite(&PageHeader.u8Tag,sizeof(PageHeader.u8Tag),u32NextfreePage);

        // write the data
        DPRINTF("Write on new page\n");
        NEOFS_Diskwrite(NEOFS_FileDescriptor[fd].u8CacheBuf,NEOFS_FileDescriptor[fd].u32CacheEnd - NEOFS_FileDescriptor[fd].u32CacheStart,
            u32NextfreePage + offsetof(NEOFS_PAGE,Contents));

        u8Temp = NEOFS_FileDescriptor[fd].u32CacheEnd - NEOFS_FileDescriptor[fd].u32CacheStart;
        NEOFS_Diskwrite(&u8Temp,sizeof(u8Temp),u32NextfreePage + offsetof(NEOFS_PAGE_HEADER,u8Size));

        // set the completed bit
        PageHeader.u8Tag  &= NEOFS_TAG_COMPLETED;
        NEOFS_Diskwrite(&PageHeader.u8Tag,sizeof(PageHeader.u8Tag),u32NextfreePage);

        // set the address for the next page from the old page header into this new page
        PageHeader.u32NextPageAddr = OldPageHeader.u32NextPageAddr;
        NEOFS_Diskwrite(&PageHeader.u32NextPageAddr,sizeof(PageHeader.u32NextPageAddr),u32NextfreePage + offsetof(NEOFS_PAGE_HEADER,u32NextPageAddr));

        // set the new forwarding address
        OldPageHeader.u32ThisPageFwdAddr = u32NextfreePage;
        NEOFS_Diskwrite(&OldPageHeader.u32ThisPageFwdAddr,sizeof(OldPageHeader.u32ThisPageFwdAddr),NEOFS_FileDescriptor[fd].u32CurPageAddr + offsetof(NEOFS_PAGE_HEADER,u32ThisPageFwdAddr));

        // mark the old page as obsolete
        OldPageHeader.u8Tag &= NEOFS_TAG_OBSOLETE;
        NEOFS_Diskwrite(&OldPageHeader.u8Tag,sizeof(OldPageHeader.u8Tag),NEOFS_FileDescriptor[fd].u32CurPageAddr);

        NEOFS_FileDescriptor[fd].u32CurPageAddr = u32NextfreePage;
        NEOFS_FileDescriptor[fd].u32CurAddr     = u32NextfreePage;
        NEOFS_FileDescriptor[fd].u32CacheStart  = 0xFFFFFFFF;
        NEOFS_FileDescriptor[fd].u32CacheEnd    = 0xFFFFFFFF;
    }
    else
    {
        NEOFS_Diskread(&PageHeader,sizeof(PageHeader),NEOFS_FileDescriptor[fd].u32CurPageAddr);

        // set the used bit
        PageHeader.u8Tag  &= NEOFS_TAG_USED;
        NEOFS_Diskwrite(&PageHeader.u8Tag,sizeof(PageHeader.u8Tag),NEOFS_FileDescriptor[fd].u32CurPageAddr);

        // write the data
        DPRINTF("Write over existing page\n");
        NEOFS_Diskwrite(NEOFS_FileDescriptor[fd].u8CacheBuf,NEOFS_FileDescriptor[fd].u32CacheEnd - NEOFS_FileDescriptor[fd].u32CacheStart,
            NEOFS_FileDescriptor[fd].u32CacheStart);

        u8Temp = NEOFS_FileDescriptor[fd].u32CacheEnd - NEOFS_FileDescriptor[fd].u32CacheStart;
        NEOFS_Diskwrite(&u8Temp,sizeof(u8Temp),NEOFS_FileDescriptor[fd].u32CurPageAddr + offsetof(NEOFS_PAGE_HEADER,u8Size));

        // set the completed bit
        PageHeader.u8Tag  &= NEOFS_TAG_COMPLETED;
        NEOFS_Diskwrite(&PageHeader.u8Tag,sizeof(PageHeader.u8Tag),NEOFS_FileDescriptor[fd].u32CurPageAddr);
    }
    return 0;
}

int LoadCache(int fd)
{
    NEOFS_PAGE_HEADER PageHeader;
    uint32_t u32NextAddr;
    uint32_t u32NextfreePage;

    DPRINTF("Load cache\n");

    NEOFS_Diskread(&PageHeader,sizeof(PageHeader),NEOFS_FileDescriptor[fd].u32CurPageAddr);

    if (((NEOFS_FileDescriptor[fd].u32CurAddr - NEOFS_FileDescriptor[fd].u32CacheStart) > PageHeader.u8Size) ||
        (NEOFS_FileDescriptor[fd].u32CurAddr >= NEOFS_FileDescriptor[fd].u32CacheEnd) ||
        (NEOFS_FileDescriptor[fd].u32CurAddr < NEOFS_FileDescriptor[fd].u32CacheStart) ||
        (NEOFS_FileDescriptor[fd].u32CacheStart == 0xFFFFFFFF))
    {
        // we are not on the right page
        u32NextAddr = AdvanceToNextPage(fd);
        DPRINTF("Next addr = %08X\n",u32NextAddr);
        if (u32NextAddr != 0xFFFFFFFF)
        {
            NEOFS_FileDescriptor[fd].u32CurPageAddr = u32NextAddr;
            NEOFS_FileDescriptor[fd].u32CurAddr     = u32NextAddr + offsetof(NEOFS_PAGE,Contents);
        }
    }

    if ((NEOFS_FileDescriptor[fd].u8Flags & NEOFS_MODE_WRITE) && (PageHeader.u32NextPageAddr == 0xFFFFFFFF))
    {
        // we are writing,
        // and there is no next page address - so we have to allocate one
        DPRINTF("Writing file ... get new page to write to\n");

        // get a new page
        u32NextfreePage = FindFreePage(&NEOFS_FileDescriptor[fd].u16CurSector);
        if (u32NextfreePage == 0xFFFFFFFF)
        {
            // return error
            DPRINTF("Disk is full\n");
            return -1;
        }
        // set the next page address in the current header
        PageHeader.u32NextPageAddr = u32NextfreePage;
        NEOFS_Diskwrite(&PageHeader.u32NextPageAddr,sizeof(PageHeader.u32NextPageAddr),
            NEOFS_FileDescriptor[fd].u32CurPageAddr + offsetof(NEOFS_PAGE,Header.u32NextPageAddr));
        NEOFS_FileDescriptor[fd].u32CurPageAddr = u32NextfreePage;
        NEOFS_FileDescriptor[fd].u32CurAddr = NEOFS_FileDescriptor[fd].u32CurPageAddr + offsetof(NEOFS_PAGE,Contents);
    }
    // fill the read cache
    NEOFS_Diskread(NEOFS_FileDescriptor[fd].u8CacheBuf,PageHeader.u8Size,NEOFS_FileDescriptor[fd].u32CurAddr);
    NEOFS_FileDescriptor[fd].u32CacheStart = NEOFS_FileDescriptor[fd].u32CurAddr;
    NEOFS_FileDescriptor[fd].u32CacheEnd   = NEOFS_FileDescriptor[fd].u32CurAddr + PageHeader.u8Size;

    DPRINTF("NEOFS_FileDescriptor[fd].u32CurPageAddr = %08X\n",NEOFS_FileDescriptor[fd].u32CurPageAddr);
    DPRINTF("NEOFS_FileDescriptor[fd].u32CurAddr     = %08X\n",NEOFS_FileDescriptor[fd].u32CurAddr);
    return 0;
}

int NEOFS_Read(int fd, void* buf, int size)
{
    int i;
    int result=0;
    uint8_t* p = buf;

    DPRINTF("Read From file %d bytes\n",size);
    DPRINTF("Cur addr    = %08X\n",NEOFS_FileDescriptor[fd].u32CurAddr);
    DPRINTF("Cache start = %08X\n",NEOFS_FileDescriptor[fd].u32CacheStart);
    DPRINTF("Cache end   = %08X\n",NEOFS_FileDescriptor[fd].u32CacheEnd);

    for(i=0; i<size; i++)
    {
        // check if where we are writing is in the cache or not
        if (((NEOFS_FileDescriptor[fd].u32CurAddr >= NEOFS_FileDescriptor[fd].u32CacheEnd) ||
             (NEOFS_FileDescriptor[fd].u32CurAddr < NEOFS_FileDescriptor[fd].u32CacheStart)) ||
            (NEOFS_FileDescriptor[fd].u32CacheStart == 0xFFFFFFFF)
           )
        {
            DPRINTF("Cache miss\n");
            // cache miss - flush the cache
            if (LoadCache(fd) != 0)
            {
                DPRINTF("Cache problem\n");
                return result;
            }
        }
        // TODO do not read beyond end of file
        *p = NEOFS_FileDescriptor[fd].u8CacheBuf[NEOFS_FileDescriptor[fd].u32CurAddr - NEOFS_FileDescriptor[fd].u32CacheStart];
        DPRINTF("Byte Read from file: %08X:%02X\n",NEOFS_FileDescriptor[fd].u32Curpos,*p);
        p++;
        buf++;
        NEOFS_FileDescriptor[fd].u32Curpos++;
        NEOFS_FileDescriptor[fd].u32CurAddr++;
        result++;
    }
    return result;

}

#if 0
int NextWriteSector(int fd)
{
    uint32_t u32FreePage = FindFreePage(&NEOFS_FileDescriptor[fd].u16CurSector);

    DPRINTF("Freepage addr = %08X\n",u32FreePage);
    if (u32FreePage != NEOFS_NO_PAGE_FOUND)
    {
        NEOFS_FileDescriptor[fd].u32CurAddr     = u32FreePage + offsetof(NEOFS_PAGE,Contents);
        NEOFS_FileDescriptor[fd].u32CurPageAddr = u32FreePage;
        NEOFS_FileDescriptor[fd].u32StartAddr   = u32FreePage;

        NEOFS_FileDescriptor[fd].u32CacheStart  = u32FreePage + offsetof(NEOFS_PAGE,Contents);
        NEOFS_FileDescriptor[fd].u32CacheEnd    = u32FreePage + offsetof(NEOFS_PAGE,Contents) + NEOFS_PAGE_DATA_SIZE;

        DPRINTF("u32CurAddr     = %08X\n",NEOFS_FileDescriptor[fd].u32CurAddr);
        DPRINTF("u32CurPageAddr = %08X\n",NEOFS_FileDescriptor[fd].u32CurPageAddr);
        DPRINTF("u32StartAddr   = %08X\n",NEOFS_FileDescriptor[fd].u32StartAddr);
        DPRINTF("u32CacheStart  = %08X\n",NEOFS_FileDescriptor[fd].u32CacheStart);
        DPRINTF("u32CacheEnd    = %08X\n",NEOFS_FileDescriptor[fd].u32CacheEnd);
        DPRINTF("OK");
        return 0;
    }
    else
    {
        DPRINTF("Failed");
        return -1;
    }
}
#endif // 0

int NEOFS_Write(int fd, void* buf, int size)
{
    int i;
    int result=0;
    uint8_t* p=buf;

    DPRINTF("Writing to file %d bytes\n",size);
    for(i=0; i<size; i++)
    {
        // check if where we are writing is in the cache or not
        if ((NEOFS_FileDescriptor[fd].u32CurAddr >= NEOFS_FileDescriptor[fd].u32CacheEnd) ||
            (NEOFS_FileDescriptor[fd].u32CurAddr < NEOFS_FileDescriptor[fd].u32CacheStart) ||
            (NEOFS_FileDescriptor[fd].u32CacheStart == 0xFFFFFFFF)
            )
        {
            // cache miss - flush the cache
            DPRINTF("Flush the cache ...\n");
            FlushCache(fd,false);
            //NextWriteSector(fd);
            // hier is n probleem - ons moet aanbeweeg na die regte volgende sector toe
            LoadCache(fd);
        }
        DPRINTF("Cache: Curpos = %08X Curaddr = %08X, Start = %08X end = %08X\n",
                NEOFS_FileDescriptor[fd].u32Curpos,NEOFS_FileDescriptor[fd].u32CurAddr,
                NEOFS_FileDescriptor[fd].u32CacheStart,NEOFS_FileDescriptor[fd].u32CacheEnd);
        DPRINTF("Write in cache: %u <= %02X\n",NEOFS_FileDescriptor[fd].u32CurAddr - NEOFS_FileDescriptor[fd].u32CacheStart,*p);
        NEOFS_FileDescriptor[fd].u8CacheBuf[NEOFS_FileDescriptor[fd].u32CurAddr - NEOFS_FileDescriptor[fd].u32CacheStart] = *p;
        p++;
        NEOFS_FileDescriptor[fd].u32Curpos++;
        NEOFS_FileDescriptor[fd].u32CurAddr++;
        result++;
    }
    DPRINTF("Num written = %d\n",result);
    return result;
}

int NEOFS_Open(char* filename, NEOFS_MODE ModeE)
{
    NEOFS_FILE_HEADER FileHeader;
    NEOFS_PAGE_HEADER PageHeader;

    int i;
    uint8_t u8Tag;
//    uint16_t u16TempSector;
    int fd = GetFileDescriptor();
    uint16_t u16FreeSector;
    uint32_t u32FreePage;

    if (fd < 0)
    {
        DPRINTF("FAIL: no descriptors available\n");
        return -1;
    }

    for(i=0; i<NEOFS_SECTOR_COUNT; i++)
    {
        NEOFS_Diskread(&u8Tag,sizeof(uint8_t),i*NEOFS_SECTOR_SIZE);
        if ((u8Tag & NEOFS_PAGETYPE_MASK) == (NEOFS_PAGE_FILEHEADER & NEOFS_PAGETYPE_MASK))
        {
            // this is the start of a file
            NEOFS_Diskread(&FileHeader,sizeof(FileHeader),i*NEOFS_SECTOR_SIZE + offsetof(NEOFS_PAGE,Contents));
            if (strcmp(FileHeader.Filename,filename) == 0)
            {
                // file exists!
                DPRINTF("File %s found\n",filename);
                NEOFS_FileDescriptor[fd].u8Flags        = ModeE;
                NEOFS_FileDescriptor[fd].u32StartAddr   = i*NEOFS_SECTOR_SIZE;
                NEOFS_FileDescriptor[fd].u32CurAddr     = i*NEOFS_SECTOR_SIZE;
                NEOFS_FileDescriptor[fd].u32CurPageAddr = i*NEOFS_SECTOR_SIZE;
                AdvanceToNextPage(fd);
                NEOFS_Seek(fd,0);
                return fd;
            }
        }
    }

    // file does not exist
    DPRINTF("File %s NOT found\n",filename);

    // if write flag is on -> create a new file
    if (ModeE & NEOFS_MODE_WRITE)
    {
        // create a new write file
        DPRINTF("Create write file\n");
        u16FreeSector = FindFreeSector();
        if (u16FreeSector != NEOFS_NO_SECTOR_FOUND)
        {
            DPRINTF("Sector found: %u\n",u16FreeSector);
            PageHeader.u8Tag = (NEOFS_TAG_USED & NEOFS_PAGE_FILEHEADER);
            PageHeader.u8Size = 0xff;
            PageHeader.u32NextPageAddr    = 0xffffffff;
            PageHeader.u32ThisPageFwdAddr = 0xffffffff;
            NEOFS_Diskwrite(&PageHeader,sizeof(PageHeader),u16FreeSector * NEOFS_SECTOR_SIZE);

            strcpy(FileHeader.Filename,filename);
            NEOFS_Diskwrite(&FileHeader,sizeof(FileHeader),u16FreeSector * NEOFS_SECTOR_SIZE + offsetof(NEOFS_PAGE,Contents));

            DPRINTF("Initialising descriptor\n");
            NEOFS_FileDescriptor[fd].u16CurSector = u16FreeSector;
            u32FreePage = FindFreePage(&NEOFS_FileDescriptor[fd].u16CurSector);
            PageHeader.u32NextPageAddr = u32FreePage;
            NEOFS_Diskwrite(&PageHeader.u32NextPageAddr,sizeof(PageHeader.u32NextPageAddr),u16FreeSector * NEOFS_SECTOR_SIZE + offsetof(NEOFS_PAGE_HEADER,u32NextPageAddr));
            if (u32FreePage != NEOFS_NO_PAGE_FOUND)
            {
                NEOFS_FileDescriptor[fd].u8Flags      = ModeE;
                NEOFS_FileDescriptor[fd].u32Curpos    = 0;
                NEOFS_FileDescriptor[fd].u32CurAddr   = u32FreePage + offsetof(NEOFS_PAGE,Contents);
                NEOFS_FileDescriptor[fd].u32CurPageAddr = u32FreePage;
                NEOFS_FileDescriptor[fd].u32StartAddr = u32FreePage;

                NEOFS_FileDescriptor[fd].u32CacheStart = u32FreePage + offsetof(NEOFS_PAGE,Contents);
                NEOFS_FileDescriptor[fd].u32CacheEnd   = u32FreePage + offsetof(NEOFS_PAGE,Contents) + NEOFS_PAGE_DATA_SIZE;

                return fd;
            }
            else
            {
                ReturnFileDescriptor(fd);
                return -1;
            }
        }
        else
        {
            DPRINTF("DISK is FULL!\n");
        }
    }
    ReturnFileDescriptor(fd);
    return -1;
}

void NEOFS_Close(int fd)
{
    uint8_t u8Tag = NEOFS_TAG_EOF;

    DPRINTF("Close File\n");

    if (fd < 0) return;

    if (NEOFS_FileDescriptor[fd].u8Flags & NEOFS_MODE_WRITE)
    {
        FlushCache(fd,true);

        // set the end of file bit
        NEOFS_Diskwrite(&u8Tag,sizeof(u8Tag),NEOFS_FileDescriptor[fd].u32CurPageAddr);
    }

    // give back the file descriptor
    ReturnFileDescriptor(fd);
}

int NEOFS_Dir(int first, char* name, int* len)
{
    NEOFS_FILE_HEADER FileHeader;

    static int i;
    uint8_t u8Tag;
    int fd;
    bool found = false;

    if (first) i=0;

    name[0] = 0;
    *len = -1;

    DPRINTF("dir list\n");
    for(; (i<NEOFS_SECTOR_COUNT) && !found; i++)
    {
        NEOFS_Diskread(&u8Tag,sizeof(uint8_t),i*NEOFS_SECTOR_SIZE);
        if ((u8Tag & NEOFS_PAGETYPE_MASK) == (NEOFS_PAGE_FILEHEADER & NEOFS_PAGETYPE_MASK))
        {
            fd = GetFileDescriptor();
            if (fd < 0)
            {
                DPRINTF("FAIL: no descriptors available\n");
                return -1;
            }

            // this is the start of a file
            NEOFS_Diskread(&FileHeader,sizeof(FileHeader),i*NEOFS_SECTOR_SIZE + offsetof(NEOFS_PAGE,Contents));
            DPRINTF("File %s found ",FileHeader.Filename);
            NEOFS_FileDescriptor[fd].u32StartAddr   = i*NEOFS_SECTOR_SIZE;
            NEOFS_FileDescriptor[fd].u32CurAddr     = i*NEOFS_SECTOR_SIZE;
            NEOFS_FileDescriptor[fd].u32CurPageAddr = i*NEOFS_SECTOR_SIZE;
            AdvanceToNextPage(fd);
            NEOFS_Seek(fd,0xFFFFFFFF);
            DPRINTF("Len = %d \n",NEOFS_FileDescriptor[fd].u32Curpos);

            ReturnFileDescriptor(fd);

            *len = NEOFS_FileDescriptor[fd].u32Curpos;
            strcpy(name,FileHeader.Filename);
            found = true;
        }
    }
    if (found) return 0; else return -1;
}

uint32_t NEOFS_DiskFree(void)
{
    uint8_t u8Tag;
    uint16_t i,j;
    uint32_t count = 0;

    for(i=NEOFS_START_SECTOR; i<NEOFS_SECTOR_COUNT; i++)
    {
        for(j=0; j<NEOFS_NUM_PAGES_PER_SECTOR; j++)
        {
            NEOFS_Diskread(&u8Tag,sizeof(u8Tag),i*NEOFS_SECTOR_SIZE + j*NEOFS_PAGE_SIZE);
            if ((u8Tag == 0xFFu) || ((u8Tag & (~NEOFS_TAG_OBSOLETE))==0))
            {
                count++;
            }
        }
    }
    return count;
}

uint32_t NEOFS_GetNumErasedPages(void)
{
    uint8_t u8Tag;
    uint16_t i,j;
    uint32_t count = 0;

    for(i=NEOFS_START_SECTOR; i<NEOFS_SECTOR_COUNT; i++)
    {
        for(j=0; j<NEOFS_NUM_PAGES_PER_SECTOR; j++)
        {
            NEOFS_Diskread(&u8Tag,sizeof(u8Tag),i*NEOFS_SECTOR_SIZE + j*NEOFS_PAGE_SIZE);
            if ((u8Tag == 0xFFu) || ((u8Tag & (~NEOFS_TAG_OBSOLETE))==0))
            {
                count++;
            }
        }
    }
    return count;
}

uint32_t NEOFS_GetNumCleanSectors(void)
{
    uint8_t u8Tag;
    uint16_t i,j;
    uint32_t count = 0;
    uint32_t cleancount = 0;

    for(i=NEOFS_START_SECTOR; i<NEOFS_SECTOR_COUNT; i++)
    {
        count=0;
        for(j=0; j<NEOFS_NUM_PAGES_PER_SECTOR; j++)
        {
            NEOFS_Diskread(&u8Tag,sizeof(u8Tag),i*NEOFS_SECTOR_SIZE + j*NEOFS_PAGE_SIZE);
            if (u8Tag == 0xFFu)
            {
                count++;
            }
        }
        if (count == NEOFS_NUM_PAGES_PER_SECTOR)
        {
            cleancount++;
        }
    }
    return cleancount;
}

uint32_t NEOFS_GetNumReclaimableSectors(void)
{
    uint8_t u8Tag;
    uint16_t i,j;
    uint32_t count = 0;
    uint32_t cleancount = 0;

    for(i=NEOFS_START_SECTOR; i<NEOFS_SECTOR_COUNT; i++)
    {
        count=0;
        for(j=0; j<NEOFS_NUM_PAGES_PER_SECTOR; j++)
        {
            NEOFS_Diskread(&u8Tag,sizeof(u8Tag),i*NEOFS_SECTOR_SIZE + j*NEOFS_PAGE_SIZE);
            if ((u8Tag == 0xFFu) || ((u8Tag & (~NEOFS_TAG_OBSOLETE))==0))
            {
                count++;
            }
        }
        if (count == NEOFS_NUM_PAGES_PER_SECTOR)
        {
            cleancount++;
        }
    }
    return cleancount;
}
