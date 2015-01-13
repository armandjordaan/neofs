#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "neofs.h"

uint8_t FlashBuffer[4096*2048];
extern bool           NEOFS_Debug;

#define MAX_TEST_FILE_SIZE (1024*1024)
#define MAX_NUM_TEST_FILES (100)

uint8_t infile[MAX_TEST_FILE_SIZE];
uint8_t outfile[MAX_TEST_FILE_SIZE];

int test_file_dataoffs[MAX_NUM_TEST_FILES];
int test_file_len[MAX_NUM_TEST_FILES];
char filename[MAX_NUM_TEST_FILES][NEOFS_FILENAME_MAXLEN];

const char REPORT_DIR[] = "c:\\Users\\Armand\\Desktop\\temp\\neofs\\";

char adjective[20][30] = {
    "nice",
    "cool",
    "clever",
    "funny",
    "bad",
    "fast",
    "slow",
    "neat",
    "bold",
    "beatup",
    "lost",
    "found",
    "final",
    "first",
    "sexy",
    "lazy",
    "tired",
    "soft",
    "hard",
    "flaming"
};

char noun[20][30] = {
    "cabbage",
    "cucumber",
    "car",
    "burger",
    "house",
    "sandwich",
    "sand",
    "phone",
    "computer",
    "grass",
    "wheel",
    "street",
    "bottle",
    "device",
    "table",
    "chair",
    "screen",
    "trouser",
    "steak",
    "coffee"
};

void randomize_file_data(void);

void dumpffs(char* name)
{
    FILE* fp;
    char  fname[256];

    strcpy(fname,REPORT_DIR);
    strcat(fname,name);
    if ((fp = fopen(fname,"wb")) != NULL)
    {
        fwrite(FlashBuffer,1,sizeof(FlashBuffer),fp);
    }
    fclose(fp);
}

int write_file(int id)
{
    FILE* fp;
    char fn[256];

    if ((id == 12) || (id==0))
    {
        NEOFS_Debug = 1;
    }
    else
    {
        NEOFS_Debug = 0;
    }
    int fd = NEOFS_Open(filename[id],NEOFS_MODE_WRITE);
    int result = NEOFS_Write(fd,outfile + test_file_dataoffs[id],test_file_len[id]);
    NEOFS_Close(fd);

    strcpy(fn,REPORT_DIR);
    strcat(fn,filename[id]);
    strcat(fn,".orig");
    if ((fp = fopen(fn,"wb")) != NULL)
    {
        fwrite(outfile + test_file_dataoffs[id],1,test_file_len[id],fp);
    }
    fclose(fp);
    return result;
}

int verify_file(int id)
{
    FILE *fp;
    char fn[256];

    int fd = NEOFS_Open(filename[id],NEOFS_MODE_READ);
    int result = NEOFS_Read(fd,infile,test_file_len[id]);

    NEOFS_Close(fd);
    if (result != test_file_len[id]) return -1;

    strcpy(fn,REPORT_DIR);
    strcat(fn,filename[id]);
    strcat(fn,".read");
    if ((fp = fopen(fn,"wb")) != NULL)
    {
        fwrite(infile,1,test_file_len[id],fp);
    }
    fclose(fp);

    return memcmp(infile,outfile+test_file_dataoffs[id],test_file_len[id]);
}

void print_disk_usage_status(void)
{
    printf("Number of free pages = %u\n",NEOFS_DiskFree());
    printf("Number of Erased pages = %u\n",NEOFS_GetNumErasedPages());
    printf("Number of completely usable sectors = %u\n",NEOFS_GetNumCleanSectors());
    printf("Number of reclaimable sectors = %u\n",NEOFS_GetNumReclaimableSectors());
}

int test_mfs(void)
{
    int i,j;
    int result;

    printf("format .....\n");
    NEOFS_Format();
    dumpffs("mfs_format.ffs");
    print_disk_usage_status();

    for(i=0; i<MAX_NUM_TEST_FILES; i++)
    {
        printf("Write file %s\n",filename[i]);
        write_file(i);
    }
    dumpffs("mfs_written.ffs");
    for(i=0; i<MAX_NUM_TEST_FILES; i++)
    {
        printf("Verify File: %s ",filename[i]);
        result = verify_file(i);
        if (result == 0)
        {
            printf("verified correctly.\n");
        }
        else
        {
            printf("verify FAILED!\n");
        }
    }
    print_disk_usage_status();

    for(i=0; i<MAX_NUM_TEST_FILES; i++)
    {
        test_file_dataoffs[i] = rand();
        test_file_len[i] = rand();
        printf("Write file %s, len = %d, dataoffs = %d \n",filename[i],test_file_len[i],test_file_dataoffs[i]);
        write_file(i);

        for(j=0; j<MAX_NUM_TEST_FILES; j++)
        {
            printf("Verify File: %s (%d,%d, len=%d)",filename[j],i,j,test_file_len[j]);
            if ((i==12) && (j == 0))
            {
                NEOFS_Debug = 1;
            }
            else
            {
                NEOFS_Debug = 0;
            }

            result = verify_file(j);
            if (result == 0)
            {
                printf("verified correctly.\n");
            }
            else
            {
                printf("verify FAILED!\n");
                print_disk_usage_status();
                return -1;
            }
        }
        print_disk_usage_status();
    }

    #if 0
    printf("format .....\n");
    fd = NEOFS_Open("test.txt",NEOFS_MODE_WRITE);
    printf("File handle = %d\n",fd);
    NEOFS_Write(fd,outfile1,sizeof(outfile1));
    NEOFS_Close(fd);
    dumpffs("mfs_write1.ffs");

    fd = NEOFS_Open("test.txt",NEOFS_MODE_READ);
    printf("File handle = %d\n",fd);
    NEOFS_Read(fd,infile,sizeof(outfile1));
    NEOFS_Close(fd);
    printf("Read From file: %s\n",infile);

    fd = NEOFS_Open("test.txt",NEOFS_MODE_WRITE);
    printf("File handle = %d\n",fd);
    NEOFS_Write(fd,outfile1,sizeof(outfile1));
    NEOFS_Close(fd);
    dumpffs("mfs_write1.ffs");

    fd = NEOFS_Open("test.txt",NEOFS_MODE_READ);
    printf("File handle = %d\n",fd);
    NEOFS_Read(fd,infile,sizeof(outfile1));
    NEOFS_Close(fd);
    printf("Read From file: %s\n",infile);

    fd = NEOFS_Open("test.txt",NEOFS_MODE_WRITE);
    printf("File handle = %d\n",fd);
    NEOFS_Write(fd,outfile2,sizeof(outfile2));
    NEOFS_Close(fd);
    dumpffs("mfs_write2.ffs");

    fd = NEOFS_Open("test.txt",NEOFS_MODE_READ);
    printf("File handle = %d\n",fd);
    NEOFS_Read(fd,infile,sizeof(outfile2));
    NEOFS_Close(fd);
    printf("Read From file: %s\n",infile);

    fd = NEOFS_Dir(1,name,&len);
    if (fd >= 0) printf("File: %s, len = %d\n",name,len);
    while(fd >= 0)
    {
        fd = NEOFS_Dir(0,name,&len);
        if (fd >= 0) printf("File: %s, len = %d\n",name,len);
    }
    #endif // 0

    return 0;


}

void randomize_file_data(void)
{
    int i,r;

    for(i=0; i<MAX_TEST_FILE_SIZE; i++)
    {
        r = rand();
        outfile[i] = (uint8_t)r;
    }
}

void init(void)
{
    int totalsize;
    int i,j;
    srand(0);
    char tempstr[NEOFS_FILENAME_MAXLEN];
    uint8_t done,found;

    randomize_file_data();

    totalsize = 0;
    for(i=0; i<MAX_NUM_TEST_FILES; i++)
    {
        test_file_dataoffs[i] = rand();
        test_file_len[i] = rand();
        totalsize += test_file_len[i];
        memset(filename[i],0,sizeof(filename[i]));

        done = 0;
        while(!done)
        {
            sprintf(tempstr,"%s.%s",adjective[rand()%20],noun[rand()%20]);
            found = 0;
            if (i!=0)
            {
                for(j=0; j<i; j++)
                {
                    if (strcmp(tempstr,filename[j]) == 0)
                    {
                        // the name already exists
                        found = 1;
                    }
                }
            }
            done = !found;
        }
        strcpy(filename[i],tempstr);
        printf("File %d: Len = %d, Offset = %d, name = %s\n",i,test_file_len[i],test_file_dataoffs[i],filename[i]);
    }
    printf("Total files len = %d\n",totalsize);
}

int main()
{
    init();
    test_mfs();

    return 0;
}
