#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <wchar.h>
#include <signal.h>
#define clear() system("clear");

extern int errno;

typedef struct __attribute__((__packed__))
{
    uint8_t BS_jmpBoot[3];    // x86 jump instr. to boot code
    uint8_t BS_OEMName[8];    // What created the filesystem
    uint16_t BPB_BytsPerSec;  // Bytes per Sector
    uint8_t BPB_SecPerClus;   // Sectors per Cluster
    uint16_t BPB_RsvdSecCnt;  // Reserved Sector Count
    uint8_t BPB_NumFATs;      // Number of copies of FAT
    uint16_t BPB_RootEntCnt;  // FAT12/FAT16: size of root DIR
    uint16_t BPB_TotSec16;    // Sectors, may be 0, see below
    uint8_t BPB_Media;        // Media type, e.g. fixed
    uint16_t BPB_FATSz16;     // Sectors in FAT (FAT12 or FAT16)
    uint16_t BPB_SecPerTrk;   // Sectors per Track
    uint16_t BPB_NumHeads;    // Number of heads in disk
    uint32_t BPB_HiddSec;     // Hidden Sector count
    uint32_t BPB_TotSec32;    // Sectors if BPB_TotSec16 == 0
    uint8_t BS_DrvNum;        // 0 = floppy, 0x80 = hard disk
    uint8_t BS_Reserved1;     //
    uint8_t BS_BootSig;       // Should = 0x29
    uint32_t BS_VolID;        // 'Unique' ID for volume
    uint8_t BS_VolLab[11];    // Non zero terminated string
    uint8_t BS_FilSysType[8]; // e.g. 'FAT16 ' (Not 0 term.)
} BootSector;

typedef struct __attribute__((__packed__))
{
    uint8_t DIR_Name[11];     // Non zero terminated string
    uint8_t DIR_Attr;         // File attributes
    uint8_t DIR_NTRes;        // Used by Windows NT, ignore
    uint8_t DIR_CrtTimeTenth; // Tenths of sec. 0...199
    uint16_t DIR_CrtTime;     // Creation Time in 2s intervals
    uint16_t DIR_CrtDate;     // Date file created
    uint16_t DIR_LstAccDate;  // Date of last read or write
    uint16_t DIR_FstClusHI;   // Top 16 bits file's 1st cluster
    uint16_t DIR_WrtTime;     // Time of last write
    uint16_t DIR_WrtDate;     // Date of last write
    uint16_t DIR_FstClusLO;   // Lower 16 bits file's 1st cluster
    uint32_t DIR_FileSize;    // File size in bytes
} ShortDirEntry;

typedef struct __attribute__((__packed__))
{
    uint8_t LDIR_Ord;        // Order/ position in sequence/ set
    uint8_t LDIR_Name1[10];  // First 5 UNICODE characters
    uint8_t LDIR_Attr;       // = ATTR_LONG_NAME (xx001111)
    uint8_t LDIR_Type;       // Should = 0
    uint8_t LDIR_Chksum;     // Checksum of short name
    uint8_t LDIR_Name2[12];  // Middle 6 UNICODE characters
    uint16_t LDIR_FstClusLO; // MUST be zero
    uint8_t LDIR_Name3[4];   // Last 2 UNICODE characters
} LongFilenameEntry;

// contains list of long name parts for one short dir entry
typedef struct
{
    int size;
    LongFilenameEntry *list;
} LongEntriesList;

// contains directory short names and corresponding long names
typedef struct
{
    int size;
    ShortDirEntry *dirList;
    LongEntriesList *longNames;
} Directory;

// contains raw data of directory read from clusters/root dir
typedef struct
{
    int size;
    void *content;
    off_t offset;
} DirectoryContent;

// contains boundaris of FAT16 structure
typedef struct
{
    off_t BootSectorStart;
    int BootSectorSize;
    off_t FatTableStart;
    int FatTableSize;
    off_t RootDirStart;
    int RootDirSize;
    off_t DataAreaStart;
    int clusterSize;
} Boundaries;

// contains main variables we work with in FAT16
typedef struct
{
    int fileHandler;
    Boundaries boundaries;
    BootSector* bs;
    uint16_t* FAT;
    Directory* rootDir;
} Volume;

// contains cluster chain and it's size
typedef struct
{
    int size;
    uint16_t *clusterChain;
} ClusterChain;

// custom filehandler
typedef struct
{
    Volume volume;
    ShortDirEntry shortEntry;
    void *content;
    off_t curOffset;
} File;

// contains long and short data of specific entry
typedef struct
{
    ShortDirEntry se;
    LongEntriesList le;
} ShortLongEntry;

// read bootsector into a structure
BootSector* read_BootSector(Volume v);
void print_BootSector(Volume v);

// read root directory into a raw data
DirectoryContent *read_RootDirectory(Volume v);
// read casual directory into a raw data(cluster rea)
DirectoryContent *read_Directory(Volume volume, ShortDirEntry entry);
// convert raw directory data into a structure with short and long entries
Directory* process_Directory(DirectoryContent *directory);

// print files, directories and their subdirectories recursively of a given dir(can work non recursively if last param is zero)
void print_Directory(Volume v, Directory d, int printLongNames, int tabsNum, int doRecursively);
// print one entry key info(if entry is directory and doRecursively == 1, will be recursive)
void print_dirEntry(Volume v, ShortDirEntry se, LongEntriesList le, int printLongNames, int tabsNum, int doRecursively);
// linux ls function implementation
void ls(Directory d);

// print content of a searched file
ssize_t cat(Volume v, Directory d, wchar_t *search_name);

// print given short entry file content into console
ssize_t print_file_content(Volume v, ShortDirEntry *entry);

// read fat table
uint16_t *read_FAT(Volume volume);
// form the cluster chain using starting cluster and FAT table
ClusterChain *read_Cluster_Chain(uint16_t *FAT, uint16_t startingCluster);
// get next cluster in sequence
uint16_t nextClusterInFat(uint16_t *FAT, uint16_t current);
// add new element to cluster chain
void addNewChainElement(ClusterChain *chain, uint16_t new);

int isDir(ShortDirEntry entry);
int isFile(ShortDirEntry entry);

// finds index of a given file name in the given directory
ssize_t find_entry_idx(Directory d, wchar_t *search_name);

// returns short dirname
wchar_t *get_shortdirname(ShortDirEntry entry);

// returns name of given directory either in short or long representation dependion on entry type
wchar_t *get_dirname(ShortDirEntry se, LongEntriesList le);

char *date_to_str(uint16_t date);
char *time_to_str(uint16_t time, uint8_t granularity);
char *attr_to_str(uint8_t attr);

File *openFile(Volume *volume, ShortDirEntry *entry);
off_t seekFile(File *f, off_t offset, int whence);
ssize_t readFile(File *f, void *buffer, size_t length);
int closeFile(File *f);

// runs continious loop that implements basic shell similar to linux
void shell(Volume v);

// clear allocated memmory
void finish(Volume v);
// clear allocated memmory
void free_directory(Directory* d);
// handle ctrl+c interupt to stop the shell and safely exit program
void ctrl_c_handler();

// recursively processes path by tokenizing and returns found entry
ShortLongEntry *process_path_token(Volume v, Directory d, wchar_t *token, wchar_t *sequence);
// starts path tokenization and returns found entry
ShortLongEntry *process_path(Volume v, wchar_t *path);
