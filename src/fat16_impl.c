#include "fat16_header.h"

int keepRunningShell = 1;

// handle ctrl+c interupt to stop the shell and safely exit program
void ctrl_c_handler(){
    keepRunningShell = 0;
    printf("\n");
}

// clear allocated memmory
void finish(Volume v){
    if (v.FAT)
        free(v.FAT);
    if (v.bs)
        free(v.bs);
    if (v.rootDir)
        free_directory(v.rootDir);
    close(v.fileHandler);
}

// clear allocated memmory
void free_directory(Directory* d){
    for (int i = 0; i < d->size; i++){
        if (d->longNames[i].list != NULL)
            free(d->longNames[i].list);
    }
    if (d->dirList != NULL)
        free(d->dirList);
    if (d->dirList != NULL)
        free(d->longNames);
    if (d != NULL)
        free(d);
}

// read bootsector into a structure
BootSector* read_BootSector(Volume v){
    BootSector* bs = (BootSector*) malloc(sizeof(BootSector));
    if (read(v.fileHandler, bs, sizeof(BootSector)) == -1)
        perror("Error reading bootsector");
    return bs;
}

void print_BootSector(Volume v){
    printf("Bytes per sector - %u\n", v.bs->BPB_BytsPerSec);
    printf("Sectors per Cluster - %u\n", v.bs->BPB_SecPerClus);
    printf("Reserved Sector Count - %u\n", v.bs->BPB_RsvdSecCnt);
    printf("Number of copies of FAT - %u\n", v.bs->BPB_NumFATs);
    printf("FAT12/FAT16: size of root DIR - %u\n", v.bs->BPB_RootEntCnt);
    printf("Sectors, may be 0, see below - %u\n", v.bs->BPB_TotSec16);
    printf("Sectors in FAT (FAT12 or FAT16) - %u\n", v.bs->BPB_FATSz16);
    printf("Sectors if BPB_TotSec16 == 0 - %u\n", v.bs->BPB_TotSec32);
    printf("Non zero terminated string -");
    for (int i = 0; i < (int)sizeof(v.bs->BS_VolLab); i++)
        printf(" %u", v.bs->BS_VolLab[i]);
    printf("\n");
}

// print files, directories and their subdirectories recursively of a given dir(can work non recursively if last param is zero)
void print_Directory(Volume v, Directory d, int printLongNames, int tabsNum, int doRecursively){
    if (tabsNum == 0)
        printf("Starting cluster | Last modified time | Last modified date | Atributes | File size(bytes) | File name\n");
    for (int i = 0; i < d.size; i++)
        print_dirEntry(v, d.dirList[i], d.longNames[i], printLongNames, tabsNum, doRecursively);
}

// print one entry key info(if entry is directory and doRecursively == 1, will be recursive)
// tabsNum indicates which level of subdirectory it is
// printLongNames indicates whether to print short or long names of the entries
void print_dirEntry(Volume v, ShortDirEntry se, LongEntriesList le, int printLongNames, int tabsNum, int doRecursively){
    // print required subdir level
    for (int i = 0; i < tabsNum; i++) printf("  ");
    if (tabsNum) printf("\\\n");
    for (int i = 0; i < tabsNum; i++) printf("  ");
    if (tabsNum) printf(" ");

    uint32_t first_cluster = (se.DIR_FstClusHI << 16) | (se.DIR_FstClusLO);
    printf("%016x   ", first_cluster);

    char *time = time_to_str(se.DIR_WrtTime, 0);
    printf("%-21s", time);
    free(time);

    char *date = date_to_str(se.DIR_WrtDate);
    printf("%-21s", date);
    free(date);

    char *attr = attr_to_str(se.DIR_Attr);
    printf("%-12s", attr);
    free(attr);

    printf("%-19u", se.DIR_FileSize);

    // get either short or long name
    wchar_t *name;
    if (printLongNames)
        name = get_dirname(se, le);
    else
        name = get_shortdirname(se);
    printf("%ls\n", name);
    free(name);
    // recursively print subdirectories
    if (isDir(se) && doRecursively && se.DIR_Name[0] != '.'){
        Directory* subdir = process_Directory(read_Directory(v, se));
        print_Directory(v, *subdir, printLongNames, tabsNum + 1, doRecursively);
        free_directory(subdir);
    }
}

// print given short entry file content into console
ssize_t print_file_content(Volume v, ShortDirEntry *entry){
    // abort non-files
    if (!isFile(*entry)){
        errno = EISDIR;
        return -1;
    }
    // get file handler
    File *f = openFile(&v, entry);
    if (f == NULL){
        return -1;
    }
    else{
        // read content
        char *buf = (char *)malloc(entry->DIR_FileSize * sizeof(char) + 1);
        if (readFile(f, buf, entry->DIR_FileSize) == -1)
            return -1;
        else{
            buf[entry->DIR_FileSize] = '\0';
            printf("%s\n", buf);
        }
        // close file/clear memmory
        if (closeFile(f) == -1)
            return -1;
        return 1;
    }
}

// linux ls function implementation
void ls(Directory d){
    for (int i = 0; i < d.size; i++){
        wchar_t *name = get_dirname(d.dirList[i], d.longNames[i]);
        printf("%ls   ", name);
        free(name);
    }
    printf("\n");
}

// check if it directory
int isDir(ShortDirEntry entry) {
    return ((int)(entry.DIR_Attr & 0x10) != 0);
}

// check if it is file
int isFile(ShortDirEntry entry) {
    return ((int)(entry.DIR_Attr & 0x18) == 0);
}

// runs continious loop that implements basic shell similar to linux
void shell(Volume v){
    wchar_t buf[500];
    char user[30] = "username";
    char *loginname = getlogin();
    if (loginname != NULL){
        strcpy(user, loginname);
    }
    char hostname[50];
    gethostname(hostname, 50);
    while (keepRunningShell){
        printf("\033[0;32m%s@%s\033[0;37m:$ ", user, hostname);
        fgetws(buf, sizeof(buf), stdin); // read command
        buf[wcscspn(buf, L"\n")] = 0;
        if (buf[0] == '\0') continue;
        wchar_t *sequence; 
        wchar_t *command = wcstok(buf, L" ", &sequence);
        wchar_t path[500] = L"";
        int is_r_flag = 0;
        int is_a_flag = 0;
        wchar_t* token = wcstok(NULL, L" ", &sequence);
        while (token!=NULL){
            if (wcscmp(token, L"-r") == 0) is_r_flag = 1;
            else if (wcscmp(token, L"-a") == 0) is_a_flag = 1;
            else {
                wcscpy(path, token);
                if (sequence){
                    wcscat(path, L" ");
                    wcscat(path, sequence);
                }
                break;
            }
            token = wcstok(NULL, L" ", &sequence);
        }
        ShortLongEntry* e = NULL;
        Directory *dir = NULL;
        if (path[0]){
            e = process_path(v, path);
            if (e == NULL){
                perror("Error processing path");
                continue;
            }
            else{
                if (!isFile(e->se)){
                    DirectoryContent* dc = read_Directory(v, e->se);
                    if (dc == NULL){
                        perror("Error reading directory");
                        continue;
                    }
                    else {
                        dir = process_Directory(dc);
                        if (dir == NULL){
                            perror("Error processing directory");
                            continue;
                        }
                    }
                }
            }
        }
        if (wcscmp(command, L"cat") == 0 && path[0] && e){
            if (print_file_content(v, &e->se) == -1) perror("Error printing file content");   
        }
        else if ((wcscmp(command, L"ls") == 0) && is_a_flag && is_r_flag && path[0] && dir)
            print_Directory(v, *dir, 1, 0, 1);
        else if ((wcscmp(command, L"ls") == 0) && is_a_flag && is_r_flag)
            print_Directory(v, *v.rootDir, 1, 0, 1);
        else if (wcscmp(command, L"ls") == 0 && is_a_flag && path[0] && dir)
            print_Directory(v, *dir, 1, 0, 0);
        else if (wcscmp(command, L"ls") == 0 && is_a_flag)
            print_Directory(v, *v.rootDir, 1, 0, 0);
        else if (wcscmp(command, L"ls") == 0 && path[0] && dir)
            ls(*dir);
        else if (wcscmp(command, L"ls") == 0)
            ls(*v.rootDir);
        else if (wcscmp(command, L"exit") == 0)
            return;
        else if (wcscmp(command, L"help") == 0){
            printf("Supported commands:\n");
            printf("cat(prints content of given file)\n");
            printf("ls(prints names of entries in given direcotory)(empty path assumes root dir)\n");
            printf("ls -a(prints more data about given directory)\n");
            printf("ls -r -a or ls -a -r(same functionality as above, but recursive)\n");
            printf("clear(clears the screen)\n");
            printf("exit\n");
        }
        else if(wcscmp(command, L"clear") == 0){
            clear();
        }
        else if (keepRunningShell)
            printf("-shell: %ls: command not found or bad arguments provided\n", buf);  
        if (e) free(e); 
        if (dir) free_directory(dir);
    }
}

// recursively processes path by tokenizing and returns found entry
ShortLongEntry *process_path_token(Volume v, Directory d, wchar_t *token, wchar_t *sequence){ 
    // find token in current directory  
    ssize_t idx = find_entry_idx(d, token);
    if (idx != -1){
        ShortDirEntry entry = d.dirList[idx];
        token = wcstok(NULL, L"/\\", &sequence);
        if (token == NULL){
            ShortLongEntry *sle = malloc(sizeof(ShortLongEntry));
            sle->se = entry;
            sle->le = d.longNames[idx];
            return sle;
        }
        // check if it root directory refference
        if (entry.DIR_FstClusLO == 0)
            return process_path_token(v, *v.rootDir, token, sequence);

        if (isFile(entry)){
            ShortLongEntry *sle = malloc(sizeof(ShortLongEntry));
            sle->se = entry;
            sle->le = d.longNames[idx];
            return sle;
        }
        else{
            Directory* dir = process_Directory(read_Directory(v, entry));
            ShortLongEntry* sle = process_path_token(v, *dir, token, sequence);
            free_directory(dir);
            return sle;
        }
    }
    return NULL;
}

// starts path tokenization and returns found entry
ShortLongEntry *process_path(Volume v, wchar_t *path){
    wchar_t *token;
    wchar_t *sequence;
    token = wcstok(path, L"/\\", &sequence);
    if (token != NULL) return process_path_token(v, *v.rootDir, token, sequence);
    else return NULL;
}

// finds index of a given file name in the given directory
ssize_t find_entry_idx(Directory d, wchar_t *search_name){
    if ( wcscmp(search_name, L".") == 0 && d.dirList[0].DIR_FstClusLO == 0) return 0; // then it is root directory and use SCC.211 volume
    for (int i = 0; i < d.size; i++){
        wchar_t *name = get_dirname(d.dirList[i], d.longNames[i]);
        if (wcscmp(name, search_name) == 0){
            free(name);
            return i;
        }
        free(name);
    }
    errno = ENOENT;
    return -1;
}

// print content of a searched file
ssize_t cat(Volume v, Directory d, wchar_t *search_name){
    ssize_t idx = find_entry_idx(d, search_name);
    if (idx != -1){
        if (print_file_content(v, &d.dirList[idx]) == -1)
            perror("Error printing file content");
        return 1;
    }
    errno = ENOENT;
    return -1;
}

// return short dirname
wchar_t *get_shortdirname(ShortDirEntry entry)
{
    uint8_t *name = entry.DIR_Name;
    wchar_t *shortname = (wchar_t *)malloc(sizeof(wchar_t) * 13);
    shortname[0] = '\0';
    int size = 0;
    wchar_t c;
    for (int i = 0; i < 11 && name[i] != 0x20; i++, size++){
        if (isFile(entry) && (i + 3 == 11 || name[i + 3] == 0x20)){
            wcsncat(shortname, L".", 1);
            size++;
        }
        c = (wchar_t)name[i];
        wcsncat(shortname, &c, 1);
    }
    return (wchar_t *)realloc(shortname, sizeof(wchar_t) * size + 1);
}

wchar_t *get_longdirname(LongEntriesList entry){
    if (entry.size == 0) return NULL;
    wchar_t *longname = (wchar_t *)malloc(sizeof(wchar_t) * (5 + 6 + 2 + 1) * entry.size);
    longname[0] = '\0';
    int size = 0;
    wchar_t c;
    for (int j = entry.size - 1; j >= 0; j--){
        for (int i = 0; i + 1 < 10 && !(entry.list[j].LDIR_Name1[i + 1] == 0xFF); i += 2, size += 1){
            c = (entry.list[j].LDIR_Name1[i + 1] << 8 | entry.list[j].LDIR_Name1[i]);
            wcsncat(longname, &c, 1);
        }
        for (int i = 0; i + 1 < 12 && !(entry.list[j].LDIR_Name2[i + 1] == 0xFF); i += 2, size += 1){
            c = (entry.list[j].LDIR_Name2[i + 1] << 8 | entry.list[j].LDIR_Name2[i]);
            wcsncat(longname, &c, 1);
        }
        for (int i = 0; i + 1 < 4 && !(entry.list[j].LDIR_Name3[i + 1] == 0xFF); i += 2, size += 1){
            c = (entry.list[j].LDIR_Name3[i + 1] << 8 | entry.list[j].LDIR_Name3[i]);
            wcsncat(longname, &c, 1);
        }
    }
    return (wchar_t *)realloc(longname, sizeof(wchar_t) * size);
}

// returns name of given directory either in short or long representation dependion on entry type
wchar_t *get_dirname(ShortDirEntry se, LongEntriesList le){
    wchar_t *name = get_longdirname(le);
    if (name == NULL)
        name = get_shortdirname(se);
    return name;
}

char *date_to_str(uint16_t date){
    char *res = (char *)malloc(sizeof(char) * 11);
    sprintf(res, "%02d/%02d/%04d", (date & 0x001f), ((date & 0x01e0) >> 5), 1980 + ((date & 0xfe00) >> 9));
    return res;
}

char *time_to_str(uint16_t time, uint8_t granularity){
    char *res = (char *)malloc(sizeof(char) * 9);
    sprintf(res, "%02d:%02d:%02d", ((time & 0xf800) >> 11), ((time & 0x07e0) >> 5), (int)((time & 0x001f) * 2 + granularity / 100.));
    return res;
}

char *attr_to_str(uint8_t attr){
    char *res = (char *)malloc(sizeof(char) * 8);
    for (int i = 7; i >= 2; i--){
        uint8_t bit = (attr & 0x1);
        attr = attr >> 1;
        if (bit == 1){
            switch (i){
                case 7: res[i - 2] = 'R'; break;
                case 6: res[i - 2] = 'H'; break;
                case 5: res[i - 2] = 'S'; break;
                case 4: res[i - 2] = 'V'; break;
                case 3: res[i - 2] = 'D'; break;
                case 2: res[i - 2] = 'A'; break;
                default: continue;
            }
        }
        else res[i - 2] = '-';
    }
    return res;
}

// read fat table
uint16_t *read_FAT(Volume volume){
    uint16_t *FAT = (uint16_t *)malloc(volume.boundaries.FatTableSize);
    if (lseek(volume.fileHandler, volume.boundaries.FatTableStart, SEEK_SET) == -1)
        perror("Error setting fat start offset\n");
    if (read(volume.fileHandler, FAT, volume.boundaries.FatTableSize) == -1)
        perror("Error reading fat from the file\n");
    return FAT;
}

// form the cluster chain using starting cluster and FAT table
ClusterChain *read_Cluster_Chain(uint16_t *FAT, uint16_t startingCluster){
    ClusterChain *chain = (ClusterChain *)malloc(sizeof(ClusterChain));
    chain->size = 0;
    chain->clusterChain = NULL;
    uint16_t cluster = startingCluster;
    while (cluster < 0xfff8){
        addNewChainElement(chain, cluster);
        cluster = nextClusterInFat(FAT, cluster);
    }
    return chain;
}

// get next cluster in sequence
uint16_t nextClusterInFat(uint16_t *FAT, uint16_t current){
    return *(FAT + current);
}

// add new element to cluster chain
void addNewChainElement(ClusterChain *chain, uint16_t new){
    chain->size += 1;
    chain->clusterChain = (uint16_t *)realloc(chain->clusterChain, chain->size * sizeof(uint16_t));
    chain->clusterChain[chain->size - 1] = new;
}

// read root directory into a raw data
DirectoryContent *read_RootDirectory(Volume v){
    if (lseek(v.fileHandler, v.boundaries.RootDirStart, SEEK_SET) == -1){
        perror("Error setting offset for root directory start");
        return NULL;
    }
    else{
        DirectoryContent *rootdir = (DirectoryContent *)malloc(sizeof(DirectoryContent));
        rootdir->size = v.boundaries.RootDirSize;
        rootdir->offset = 0;
        rootdir->content = malloc(rootdir->size * sizeof(ShortDirEntry));
        if (read(v.fileHandler, rootdir->content, rootdir->size * sizeof(ShortDirEntry)) == -1)
            perror("Error reading root directory");
        return rootdir;
    }
}

// read casual directory into a raw data(cluster rea)
DirectoryContent *read_Directory(Volume volume, ShortDirEntry entry){
    if (entry.DIR_FstClusLO == 0) return read_RootDirectory(volume); // if it is root directory passed
    if (isFile(entry)) {
        errno = EINVAL;
        return NULL;
    }
    ClusterChain *chain = read_Cluster_Chain(volume.FAT, entry.DIR_FstClusLO);
    DirectoryContent *dir = (DirectoryContent *)malloc(sizeof(DirectoryContent));
    dir->size = (chain->size * volume.boundaries.clusterSize) / sizeof(ShortDirEntry);
    dir->offset = 0;
    dir->content = malloc(chain->size * volume.boundaries.clusterSize);
    for (int i = 0; i < chain->size; i++){
        if (lseek(volume.fileHandler, volume.boundaries.DataAreaStart + (chain->clusterChain[i] - 2) * volume.boundaries.clusterSize, SEEK_SET) == -1)
            perror("Error offsetting cluster for directory");
        if (read(volume.fileHandler, (uint8_t*)dir->content + (i * volume.boundaries.clusterSize), volume.boundaries.clusterSize) == -1)
            perror("Error reading cluster for directory");
    }
    free(chain->clusterChain);
    free(chain);
    return dir;
}

// convert raw directory data into a structure with short and long entries
Directory* process_Directory(DirectoryContent *directory){
    if (directory == NULL){
        errno = EINVAL;
        return NULL;
    }
    Directory* result = (Directory*) malloc(sizeof(Directory));
    result->size = 0;
    result->dirList = (ShortDirEntry *)malloc(directory->size * sizeof(ShortDirEntry));
    result->longNames = (LongEntriesList *)malloc(directory->size * sizeof(LongEntriesList));
    // long names list for single short dir entry
    LongEntriesList *names = (LongEntriesList *)malloc(sizeof(LongEntriesList));
    names->size = 0;
    names->list = NULL;
    ShortDirEntry *e = (ShortDirEntry *)malloc(sizeof(ShortDirEntry));
    for (int i = 0; i < directory->size; i++){
        memcpy(e, (uint8_t*)directory->content + directory->offset, sizeof(ShortDirEntry));
        directory->offset += sizeof(ShortDirEntry);
        if (e->DIR_Name[0] == 0xE5) continue;
        if (e->DIR_Name[0] == 0x00) break;
        if (e->DIR_Attr == 0xf){
            names->size += 1;
            names->list = (LongFilenameEntry *)realloc(names->list, names->size * sizeof(LongFilenameEntry));
            directory->offset -= sizeof(ShortDirEntry);
            memcpy(&names->list[names->size - 1], (uint8_t*)directory->content + directory->offset, sizeof(LongFilenameEntry));
            directory->offset += sizeof(LongFilenameEntry);
            continue;
        }
        result->size += 1;
        result->dirList[result->size - 1] = *e;
        result->longNames[result->size - 1] = *names;
        names = (LongEntriesList *)malloc(sizeof(LongEntriesList *));
        names->size = 0;
        names->list = NULL;
    }
    result->dirList = (ShortDirEntry *)realloc(result->dirList, sizeof(ShortDirEntry) * result->size);
    result->longNames = (LongEntriesList *)realloc(result->longNames, sizeof(LongEntriesList) * result->size);
    free(e);
    // free raw directory content
    free(directory->content);
    free(directory);
    return result;
}

File *openFile(Volume *volume, ShortDirEntry *entry){
    // directory, not a file
    if ((int)(entry->DIR_Attr & 0x18) != 0){
        errno = EISDIR;
        return NULL;
    }
    ClusterChain *chain = read_Cluster_Chain(volume->FAT, entry->DIR_FstClusLO);
    File *f = (File *)malloc(sizeof(File));
    f->volume = *volume;
    f->shortEntry = *entry;
    f->content = malloc(chain->size * volume->boundaries.clusterSize);
    f->curOffset = 0;
    for (int i = 0; i < chain->size; i++){
        if (lseek(volume->fileHandler, volume->boundaries.DataAreaStart + (chain->clusterChain[i] - 2) * volume->boundaries.clusterSize, SEEK_SET) == -1)
            perror("Error offsetting cluster when opening file");
        if (read(volume->fileHandler, (uint8_t*)f->content + (i * volume->boundaries.clusterSize), volume->boundaries.clusterSize) == -1)
            perror("Error reading cluster when opening file");
    }
    f->content = realloc(f->content, entry->DIR_FileSize);
    free(chain->clusterChain);
    free(chain);
    return f;
}

int closeFile(File *f){
    if (f == NULL){
        errno = EBADF;
        return -1;
    }
    free(f->content);
    free(f);
    return 1;
}

ssize_t readFile(File *f, void *buffer, size_t length){
    if (f == NULL){
        errno = EBADF;
        return -1;
    }
    if (buffer == NULL){
        errno = EFAULT;
        return -1;
    }
    if (f->curOffset >= f->shortEntry.DIR_FileSize){
        return 0;
    }
    // if reading too much
    else if ((off_t)length > (f->shortEntry.DIR_FileSize - f->curOffset)){
        memcpy(buffer, (uint8_t*)f->content + f->curOffset, (f->shortEntry.DIR_FileSize - f->curOffset));
        f->curOffset = f->shortEntry.DIR_FileSize;
        return f->shortEntry.DIR_FileSize - f->curOffset;
    }
    else{
        memcpy(buffer, (uint8_t*)f->content + f->curOffset, length);
        f->curOffset += length;
        return length;
    }
}

off_t seekFile(File *f, off_t offset, int whence){
    if (f == NULL){
        errno = EBADF;
        return -1;
    }
    else if (whence == SEEK_SET){
        // bad offset
        if (offset < 0 || offset > f->shortEntry.DIR_FileSize){
            errno = EINVAL;
            return -1;
        }
        f->curOffset = offset;
        return f->curOffset;
    }
    else if (whence == SEEK_CUR){
        // bad offset
        if (f->curOffset + offset < 0 || f->curOffset + offset > f->shortEntry.DIR_FileSize){
            errno = EINVAL;
            return -1;
        }
        f->curOffset = f->curOffset + offset;
        return f->curOffset;
    }
    else if (whence == SEEK_END){
        // bad offset
        if (f->shortEntry.DIR_FileSize + offset < 0 || f->shortEntry.DIR_FileSize + offset > f->shortEntry.DIR_FileSize){
            errno = EINVAL;
            return -1;
        }
        f->curOffset = f->shortEntry.DIR_FileSize + offset;
        return f->curOffset;
    }
    else{
        errno = EINVAL;
        return -1;
    }
}
