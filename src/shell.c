#include "../include/fat16_header.h"

int main(){
    Volume v;
    v.fileHandler = open("../image/fat16.img", O_CREAT, S_IRUSR);
    if (v.fileHandler != -1)
    {   
        v.bs = NULL;
        v.FAT = NULL;
        v.rootDir = NULL;
        v.boundaries.BootSectorStart = 0;
        v.boundaries.BootSectorSize = sizeof(BootSector);
        // read boot sector
        v.bs = read_BootSector(v);
        // define boundaries
        v.boundaries.FatTableStart = v.bs->BPB_RsvdSecCnt * v.bs->BPB_BytsPerSec;
        v.boundaries.FatTableSize = v.bs->BPB_FATSz16 * v.bs->BPB_BytsPerSec;
        v.boundaries.RootDirStart = v.boundaries.FatTableStart + v.boundaries.FatTableSize * v.bs->BPB_NumFATs;
        v.boundaries.RootDirSize = sizeof(ShortDirEntry) * v.bs->BPB_RootEntCnt;
        v.boundaries.DataAreaStart = v.boundaries.RootDirStart + v.boundaries.RootDirSize;
        v.boundaries.clusterSize = v.bs->BPB_BytsPerSec * v.bs->BPB_SecPerClus;
        v.FAT = read_FAT(v);

        Directory* rootDir = process_Directory(read_RootDirectory(v));
        v.rootDir = rootDir;

        // struct sigaction act;
        // act.sa_handler = ctrl_c_handler;
        // sigaction(SIGINT, &act, NULL);
        shell(v);

        finish(v);
    }
    else
    {
        perror("Error opening the FAT16 image");
    }
    return 1;
}
