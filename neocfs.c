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
char debugstr[1024];
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

/** \brief Diskread implementation
 *
 * \param void* Buf : pointer to buffer where read data will be stored
 * \param int size : number of bytes to read
 * \param uint32_t u32Offset : offset of where to read, relative to start of disk
 * \return Number of bytes read (or -1 if failed)
 *
 * \note  This is implementation specific. Implement your SPI device's read routine here
 */
int NEOCFS_Diskread(void* Buf, int size, uint32_t u32Offset)
{
    DPRINTF("disk read %d, %u (0x%08X)\n",size,u32Offset,u32Offset);
    memcpy(Buf,FlashBuffer+u32Offset,size);
    DebugPrintBuffer(Buf,size,u32Offset);
    return size;
}

/** \brief Diskwrite implementation
 *
 * \param void* Buf : pointer to buffer of data to be written
 * \param int size : number of bytes to write
 * \param uint32_t u32Offset : offset of where to write, relative to start of disk
 * \return Number of bytes written (or -1 if failed)
 *
 * \note  This is implementation specific. Implement your SPI device's write routine here
 */
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

/** \brief Diskerase implementation
 *
 * \param uint16_t u16Sector : sector to be erased
 * \return 0 on success (or -1 if failed)
 *
 * \note  This is implementation specific. Implement your SPI device's erase routine here
 */
int NEOCFS_Diskerase(uint16_t u16Sector)
{
    DPRINTF("Erase at sector %d\n",u16Sector);
    memset(FlashBuffer+(u16Sector * NEOCFS_SECTOR_SIZE),0xFF,NEOCFS_SECTOR_SIZE);
    return 0;
}


/** \brief NEOCFS_Format
 *
 * \param void
 * \return int (0 = success, any other means failure)
 *
 * \note  format the disk, ie for SPI flash, set entire allocated area to 1's
 */
int NEOCFS_Format(void)
{
    int i,t;

    for(i=NEOCFS_START_SECTOR; i<(NEOCFS_START_SECTOR + NEOCFS_SECTOR_COUNT); i++)
    {
        DPRINTF("Erasing sector: %d of %d at sector %d\n",i-NEOCFS_START_SECTOR,NEOCFS_SECTOR_COUNT,i);
        t = NEOCFS_Diskerase(i);
        if (t != 0)
        {
            return t;
        }
    }
    return 0;
}

/** \brief int isPowerOfTwo (uint32_t x)
 *
 * \param uint32_t x = value to check if it is a power of 2
 * \return return 0 if not a power of 2, return 1 if a power of two
 *
 */
static int isPowerOfTwo (uint32_t x)
{
    while (((x % 2) == 0) && x > 1) /* While x is even and > 1 */
    {
        x /= 2;
    }
    return (x == 1);
}

/** \brief uint32_t NextRecordAddress(NEOCFS_FILE_DESCRIPTOR_ST* fd,uint32_t u32Addr)
 *
 * \param fd NEOCFS_FILE_DESCRIPTOR_ST* - pointer to file descriptor
 * \param u32Addr uint32_t - current address
 * \return uint32_t - address of next record
 *
 * \note  get the address of the next record for the file descriptor
 */
static uint32_t NextRecordAddress(NEOCFS_FILE_DESCRIPTOR_ST* fd, uint32_t u32Addr)
{
    uint32_t u32Result = u32Addr + fd->u32RecordSize+2;

    if (u32Result >= fd->u32EndAddr)
    {
        u32Result = fd->u32StartAddr;
    }
    return u32Result;
}

/** \brief void NEOCFS_Init(void)
 *
 * \param void
 * \return void
 *
 * \note  Initialise the file system
 */
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

/** \brief bool CheckInitialised(void)
 *
 * \param void
 * \return bool (true if initailised, false if not initialised)
 *
 * \note  Check file systems initialise status
 */
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

/** \brief void NEOCFS_Dir(void)
 *
 * \param void
 * \return void
 *
 * \note  List all files
 */
void NEOCFS_Dir(void)
{
    NEOCFS_FILE_DESCRIPTOR_ST* i;

    if (!CheckInitialised()) return;

    for(i=__start_neocfs_file_descriptors; i<__stop_neocfs_file_descriptors; i++)
    {
        DPRINTF("File: %s\n",i->cFilename);
    }
}

/** \brief NEOCFS_FILE_DESCRIPTOR_ST* NEOCFS_OpenByName(char* filename)
 *
 * \param filename char* - filename
 * \return NEOCFS_FILE_DESCRIPTOR_ST* - returns pointer to the file descriptor
 *
 * \note Open a file by its name
 * \note Returns NULL if file is not found
 */
NEOCFS_FILE_DESCRIPTOR_ST* NEOCFS_OpenByName(const char* filename)
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

/** \brief void NEOCFS_FormatFile(NEOCFS_FILE_DESCRIPTOR_ST* fd)
 *
 * \param fd NEOCFS_FILE_DESCRIPTOR_ST*
 * \return void
 *
 * \note  Formats teh filem by erasing all sectors
 */
void NEOCFS_FormatFile(NEOCFS_FILE_DESCRIPTOR_ST* fd)
{
    uint32_t i;

    for(i=fd->u32StartAddr; i<fd->u32EndAddr; i+=NEOCFS_SECTOR_SIZE)
    {
        NEOCFS_Diskerase(i / NEOCFS_SECTOR_SIZE);
    }
}

/** \brief uint32_t FindTail(NEOCFS_FILE_DESCRIPTOR_ST* fd)
 *
 * \param fd NEOCFS_FILE_DESCRIPTOR_ST* - pointer to file descriptor
 * \return uint32_t - address of the tail
 *
 * \note Find the tail of the log
 */
static uint32_t FindTail(NEOCFS_FILE_DESCRIPTOR_ST* fd)
{
    uint32_t i,j,n,m;
    uint8_t u8Temp1,u8Temp2;
    bool fAllFF = true;

    // find the Tail
    // the tail is where a FF end tag is followed by a non FF tag, followed by a non obsolete tag
    n = ((fd->u32EndAddr - fd->u32StartAddr) / (fd->u32RecordSize+2))+5;
    i = fd->u32StartAddr;

    while(n)
    {
        NEOCFS_Diskread(&u8Temp1,sizeof(u8Temp1),i);
        j = NextRecordAddress(fd,i);
        NEOCFS_Diskread(&u8Temp2,sizeof(u8Temp2),j);

        DPRINTF("A:%08X D:%02X, A:%08X D:%02X\n",i,u8Temp1,j,u8Temp2);

        if (u8Temp1 != 0xFF)
        {
            fAllFF = false;
        }
        if ((u8Temp1 == 0xFF) && (u8Temp2 != 0xFF))
        {
            DPRINTF("Transition from FF to non FF\n");

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
                    DPRINTF("Found tail at %08X\n",j);
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

/** \brief uint32_t FindHead(NEOCFS_FILE_DESCRIPTOR_ST* fd)
 *
 * \param fd NEOCFS_FILE_DESCRIPTOR_ST* - pointer to filedescriptor
 * \return uint32_t - offset of start of log
 *
 * \note  find the start of the log
 */
uint32_t FindHead(NEOCFS_FILE_DESCRIPTOR_ST* fd)
{
    // find the place where data is followed by FF

    uint32_t i,m;
    uint8_t u8Temp1;

    i = fd->u32Tail;
    m = ((fd->u32EndAddr - fd->u32StartAddr) / (fd->u32RecordSize+2)) + 1;

    DPRINTF("Find head\n");

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

/** \brief int NEOCFS_OpenByDescriptor(NEOCFS_FILE_DESCRIPTOR_ST* fd)
 *
 * \param fd NEOCFS_FILE_DESCRIPTOR_ST* - pointer to file descriptor
 * \return int - returns NEOCFS_RESULT_CODE_SUCCESS_xxxxx code
 *
 * \note Open a file by its descriptor
 */
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

/** \brief Close the file : void NEOCFS_CloseFile(NEOCFS_FILE_DESCRIPTOR_ST* fd)
 *
 * \param fd NEOCFS_FILE_DESCRIPTOR_ST* - pointer to file descriptor
 * \return void
 *
 */
void NEOCFS_CloseFile(NEOCFS_FILE_DESCRIPTOR_ST* fd)
{
    fd->u32CurReadPos = 0;
    fd->u32Head = 0;
    fd->u32Tail = 0;
}

/** \brief static int GarbageCollect(NEOCFS_FILE_DESCRIPTOR_ST* fd)
 *
 * \param fd NEOCFS_FILE_DESCRIPTOR_ST*
 * \return int
 *
 * \note Do garbage collection, ie see how much flash can be reclaimed for file writing
 */
static int GarbageCollect(NEOCFS_FILE_DESCRIPTOR_ST* fd)
{
    uint8_t  u8Tag;
    uint32_t i,j;
    bool     fFreeSpaceFound;

    DPRINTF("Garbage collection\n");

    for(j = fd->u32Head; j<(fd->u32Head + (2*NEOCFS_SECTOR_SIZE)); j++)
    {
        // first check if the file supports overwrite of oldest data ...
        if (fd->u32Flags & NEOCFS_FILE_FLAGS_OVERWRITE_OLDEST)
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

/** \brief int NEOCFS_WriteRecord(NEOCFS_FILE_DESCRIPTOR_ST* fd, void* data)
 *
 * \param fd NEOCFS_FILE_DESCRIPTOR_ST* - pointer to file descriptor
 * \param data void* - pointer to data record
 * \return int - return a NEOCFS_RESULT_CODE_xxxxx code
 *
 * \note write a single record to the file
 */
int NEOCFS_WriteRecord(NEOCFS_FILE_DESCRIPTOR_ST* fd, void* data)
{
    int result;
    uint8_t  u8Temp;
    uint8_t  u8StartTag = NEOCFS_START_TAG_WRITE_STARTED;
    uint8_t  u8EndTag   = NEOCFS_END_TAG_WRITE_DONE;
    uint32_t u32Temp;

    NEOCFS_Diskread(&u8Temp,sizeof(u8Temp),fd->u32Head);
    if (u8Temp != 0xFF)
    {
        // this record is NOT available
        result = GarbageCollect(fd);

        // if we cannot claim space in the file, then return error
        if (result != NEOCFS_RESULT_CODE_SUCCESS) return result;
    }

    /* first chec to see that we are leaving enough space between the header and the tail */
    u32Temp = (fd->u32Head & NEOCFS_SECTOR_MASK) + (NEOCFS_SECTOR_SIZE * 2);
    DPRINTF("Write record H:%08X T:%08X Temp:%08X\n",fd->u32Head,fd->u32Tail,u32Temp);
    if (u32Temp > fd->u32EndAddr)
    {
        u32Temp = fd->u32StartAddr;
    }
    if ((fd->u32Tail & NEOCFS_SECTOR_MASK) == u32Temp)
    {
        DPRINTF("Log is full T:%08X Temp:%08X\n",fd->u32Tail,u32Temp);
        return NEOCFS_RESULT_CODE_LOG_FULL;
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
        DPRINTF("Write wrap around before H:%08X E:%08X\n",fd->u32Head,fd->u32EndAddr);
        // we are at the end - wrap to start again
        fd->u32Head = fd->u32StartAddr;
        DPRINTF("Write wrap around after H:%08X S:%08X\n",fd->u32Head,fd->u32StartAddr);
    }

    return NEOCFS_RESULT_CODE_SUCCESS;
}

/** \brief int NEOCFS_SeekFromTail(NEOCFS_FILE_DESCRIPTOR_ST* fd, uint32_t pos)
 *
 * \param fd NEOCFS_FILE_DESCRIPTOR_ST* - pointer to fiel descriptor
 * \param pos uint32_t - position to seek to
 * \return int - returns NEOCFS_RESULT_CODE_xxxxxx
 *
 * \note position file file pointer relative to the end of the file.
 */
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

/** \brief int NEOCFS_ReadRecord(NEOCFS_FILE_DESCRIPTOR_ST* fd, void* data)
 *
 * \param fd NEOCFS_FILE_DESCRIPTOR_ST* - pointer to file descriptor
 * \param data void* - pointer to where record will be stored
 * \return int - returns code NEOCFS_RESULT_CODE_xxxxx
 *
 * \note File pointer is not advanced
 */
int NEOCFS_ReadRecord(NEOCFS_FILE_DESCRIPTOR_ST* fd, void* data)
{
    uint8_t u8Temp;

    NEOCFS_Diskread(&u8Temp,sizeof(u8Temp),fd->u32CurReadPos);

    if (u8Temp == 0xFF)
    {
        DPRINTF("Record is empty\n");
        // end of log reached
        return NEOCFS_RESULT_CODE_LOG_EMPTY;
    }
    else
    {
        DPRINTF("Record contains data\n");
        if (NEOCFS_Diskread(data,fd->u32RecordSize,fd->u32CurReadPos+1) == fd->u32RecordSize)
        {
            return NEOCFS_RESULT_CODE_SUCCESS;
        }
        else
        {
            return NEOCFS_RESULT_CODE_GENERR;
        }
    }

}

/** \brief int NEOCFS_NextRecord(NEOCFS_FILE_DESCRIPTOR_ST* fd)
 *
 * \param fd NEOCFS_FILE_DESCRIPTOR_ST* - pointer to the file descriptor
 * \return int - return code NEOCFS_RESULT_CODE_xxxxxx
 *
 * \note Advances the file pointer to the next record
 */
int NEOCFS_NextRecord(NEOCFS_FILE_DESCRIPTOR_ST* fd)
{
    fd->u32CurReadPos += (fd->u32RecordSize+2);

    if (fd->u32CurReadPos >= fd->u32EndAddr)
    {
        fd->u32CurReadPos = fd->u32StartAddr;
    }
    return NEOCFS_RESULT_CODE_SUCCESS;
}

/** \brief int NEOCFS_MarkObsolete(NEOCFS_FILE_DESCRIPTOR_ST* fd)
 *
 * \param fd NEOCFS_FILE_DESCRIPTOR_ST* - pointer to the file descriptor
 * \return int - return NEOCFS_RESULT_CODE_xxxxx
 *
 * \note mark the record as obsolete
 */
int NEOCFS_MarkObsolete(NEOCFS_FILE_DESCRIPTOR_ST* fd)
{
    uint8_t u8Tag;

    DPRINTF("fd->u32CurReadPos = %08X, fd->u32Tail = %08X\n",fd->u32CurReadPos,fd->u32Tail);
    if (fd->u32CurReadPos == fd->u32Tail)
    {
        //NEOCFS_Diskread(&u8Tag,sizeof(u8Tag),fd->u32Tail+fd->u32RecordSize+1);
        NEOCFS_Diskread(&u8Tag,sizeof(u8Tag),fd->u32Tail);
        u8Tag &= NEOCFS_END_TAG_OBSOLETE;
        //NEOCFS_Diskwrite(&u8Tag,sizeof(u8Tag),fd->u32Tail+fd->u32RecordSize+1);
        NEOCFS_Diskwrite(&u8Tag,sizeof(u8Tag),fd->u32Tail);
        fd->u32Tail += (fd->u32RecordSize + 2);
    }
    return NEOCFS_RESULT_CODE_SUCCESS;
}
