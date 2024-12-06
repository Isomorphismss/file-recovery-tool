#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <openssl/sha.h>

#pragma pack(push,1)
typedef struct BootEntry {
    unsigned char  BS_jmpBoot[3];     // Assembly instruction to jump to boot code
    unsigned char  BS_OEMName[8];     // OEM Name in ASCII
    unsigned short BPB_BytsPerSec;    // Bytes per sector. Allowed values include 512, 1024, 2048, and 4096
    unsigned char  BPB_SecPerClus;    // Sectors per cluster (data unit). Allowed values are powers of 2, but the cluster size must be 32KB or smaller
    unsigned short BPB_RsvdSecCnt;    // Size in sectors of the reserved area
    unsigned char  BPB_NumFATs;       // Number of FATs
    unsigned short BPB_RootEntCnt;    // Maximum number of files in the root directory for FAT12 and FAT16. This is 0 for FAT32
    unsigned short BPB_TotSec16;      // 16-bit value of number of sectors in file system
    unsigned char  BPB_Media;         // Media type
    unsigned short BPB_FATSz16;       // 16-bit size in sectors of each FAT for FAT12 and FAT16. For FAT32, this field is 0
    unsigned short BPB_SecPerTrk;     // Sectors per track of storage device
    unsigned short BPB_NumHeads;      // Number of heads in storage device
    unsigned int   BPB_HiddSec;       // Number of sectors before the start of partition
    unsigned int   BPB_TotSec32;      // 32-bit value of number of sectors in file system. Either this value or the 16-bit value above must be 0
    unsigned int   BPB_FATSz32;       // 32-bit size in sectors of one FAT
    unsigned short BPB_ExtFlags;      // A flag for FAT
    unsigned short BPB_FSVer;         // The major and minor version number
    unsigned int   BPB_RootClus;      // Cluster where the root directory can be found
    unsigned short BPB_FSInfo;        // Sector where FSINFO structure can be found
    unsigned short BPB_BkBootSec;     // Sector where backup copy of boot sector is located
    unsigned char  BPB_Reserved[12];  // Reserved
    unsigned char  BS_DrvNum;         // BIOS INT13h drive number
    unsigned char  BS_Reserved1;      // Not used
    unsigned char  BS_BootSig;        // Extended boot signature to identify if the next three values are valid
    unsigned int   BS_VolID;          // Volume serial number
    unsigned char  BS_VolLab[11];     // Volume label in ASCII. User defines when creating the file system
    unsigned char  BS_FilSysType[8];  // File system type label in ASCII
} BootEntry;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct DirEntry {
    unsigned char  DIR_Name[11];      // File name
    unsigned char  DIR_Attr;          // File attributes
    unsigned char  DIR_NTRes;         // Reserved
    unsigned char  DIR_CrtTimeTenth;  // Created time (tenths of second)
    unsigned short DIR_CrtTime;       // Created time (hours, minutes, seconds)
    unsigned short DIR_CrtDate;       // Created day
    unsigned short DIR_LstAccDate;    // Accessed day
    unsigned short DIR_FstClusHI;     // High 2 bytes of the first cluster address
    unsigned short DIR_WrtTime;       // Written time (hours, minutes, seconds
    unsigned short DIR_WrtDate;       // Written day
    unsigned short DIR_FstClusLO;     // Low 2 bytes of the first cluster address
    unsigned int   DIR_FileSize;      // File size in bytes. (0 for directories)
} DirEntry;
#pragma pack(pop)

int main(int argc, char *argv[])
{
    char ch;
    int i, j, k;
    char* deletedFile = (char*) malloc(20);
    unsigned char* providedSHA = (unsigned char*) malloc(SHA_DIGEST_LENGTH);
    int option_i = 0, option_l = 0, nonContRmvFlg = 0, contRmvFlg = 0, shaFlag = 0;
    char* errMsg = "Usage: ./nyufile disk <options>\n  -i                     Print the file system information.\n  -l                     List the root directory.\n  -r filename [-s sha1]  Recover a contiguous file.\n  -R filename -s sha1    Recover a possibly non-contiguous file.\n";
    if (argc < 3 || argc > 6){
        printf(errMsg);
        exit(1);
    }
    while ((ch = getopt(argc, argv, "r:R:s:il")) != -1){
        if (ch == 'i'){
            if (argc != 3){
                printf(errMsg);
                exit(1);
            }
            option_i++;
        }  
        else if (ch == 'l'){
            if (argc != 3){
                printf(errMsg);
                exit(1);
            }
            option_l++;
        }
        else if (ch == 'r'){
            memcpy(deletedFile, optarg, strlen(optarg) + 1);
            contRmvFlg++;
        }
        else if (ch == 'R'){
            nonContRmvFlg++;
            memcpy(deletedFile, optarg, strlen(optarg) + 1);
        }
        else if (ch == 's'){
            shaFlag++;
            for (i = 0; i < SHA_DIGEST_LENGTH; i++) {
                sscanf(optarg + 2*i, "%2hhx", &providedSHA[i]);
            }
        }
        else {
            printf(errMsg);
            exit(1);
        }
    }
    if (nonContRmvFlg != 0 && shaFlag == 0){
        printf(errMsg);
        exit(1);
    }

    // Open the disk image
    int diskFd = open(argv[optind], O_RDWR);
    struct stat file_stat;
    if (stat(argv[optind], &file_stat))
        exit(1);
    int size = file_stat.st_size;
    void* mapped_address = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, diskFd, 0);
    
    BootEntry* fatEntry = (BootEntry*) mapped_address; // Boot entry of the disk

    char* totalFileName; // File name in the familiar format
    size_t dirEntryOffset, fatOffset;
    DirEntry* dirEntry;
    void* baseAddress;
    unsigned int* fatAddressArr[fatEntry->BPB_NumFATs];

    for (i = 0; i < fatEntry->BPB_NumFATs; i++){
        fatOffset = fatEntry->BPB_RsvdSecCnt*fatEntry->BPB_BytsPerSec + fatEntry->BPB_FATSz32*i*fatEntry->BPB_BytsPerSec;
        fatAddressArr[i] = (unsigned int*)((char*)mapped_address + fatOffset);
    }

    unsigned int currentAddress = fatEntry->BPB_RootClus;
    unsigned short highPart, lowPart;
    unsigned int startingCluster;
    int entryCount = 0, clusterCount = 0;

    if (option_i){
        printf("Number of FATs = %d\n", fatEntry->BPB_NumFATs);
        printf("Number of bytes per sector = %d\n", fatEntry->BPB_BytsPerSec);
        printf("Number of sectors per cluster = %d\n", fatEntry->BPB_SecPerClus);
        printf("Number of reserved sectors = %d\n", fatEntry->BPB_RsvdSecCnt);
    }
    else if (option_l){
        while (currentAddress < 0x0ffffff8 && currentAddress != 0x00){ // Traverse all possible clusters
            clusterCount = 0;
            dirEntryOffset = ((currentAddress - 2)*fatEntry->BPB_SecPerClus + fatEntry->BPB_RsvdSecCnt + (fatEntry->BPB_NumFATs*fatEntry->BPB_FATSz32))*fatEntry->BPB_BytsPerSec;
            baseAddress = (char*)mapped_address + dirEntryOffset;
            dirEntry = (DirEntry*)(baseAddress); // Calculate the byte address using the cluster number

            while (dirEntry->DIR_Name[0] != 0x00){ // Traverse the entries in the cluster until we reach EOF or end of cluster

                totalFileName = (char*) malloc(sizeof(char)*13);

                highPart = dirEntry->DIR_FstClusHI;
                lowPart = dirEntry->DIR_FstClusLO;
                startingCluster = (highPart << 16) | lowPart; // Calculate the starting cluster of the entry

                clusterCount++;
                if (clusterCount > (fatEntry->BPB_SecPerClus*fatEntry->BPB_BytsPerSec)/sizeof(DirEntry)){ // Finished Traversing this cluster
                    break;
                }
                if (dirEntry->DIR_Name[0] == 0xe5 || dirEntry->DIR_Attr & 0x0f){ // Deleted file or Long file
                    baseAddress = (char*)baseAddress + sizeof(DirEntry); // Next entry within this same cluster
                    dirEntry = (DirEntry*)(baseAddress);
                    continue;
                } 
                entryCount++;

                for (i = 0; i < 8 && dirEntry->DIR_Name[i] != ' '; i++){ // Copy the name (not extension)
                    totalFileName[i] = dirEntry->DIR_Name[i];
                }
                if (dirEntry->DIR_Attr & 0x10){ // Directory
                    totalFileName[i] = '/';
                    totalFileName[i+1] = '\0';
                    printf("%s (starting cluster = %d)\n", totalFileName, startingCluster);
                }
                else { // Normal File
                    if (dirEntry->DIR_Name[8] != ' '){ // There exists extension
                        totalFileName[i] = '.';
                        i++;
                        for (j = 8; j < 11 && dirEntry->DIR_Name[j] != ' '; j++){ 
                            totalFileName[i] = dirEntry->DIR_Name[j];
                            i++;
                        }
                        totalFileName[i] = '\0';
                    }
                    else{ // There is no extension
                        totalFileName[i] = '\0';
                    }
                    if (dirEntry->DIR_FileSize == 0)
                        printf("%s (size = %d)\n", totalFileName, dirEntry->DIR_FileSize);
                    else 
                        printf("%s (size = %d, starting cluster = %d)\n", totalFileName, dirEntry->DIR_FileSize, startingCluster);
                }   
                baseAddress = (char*)baseAddress + sizeof(DirEntry); // Next entry within this same cluster
                dirEntry = (DirEntry*)(baseAddress);
                free(totalFileName);
            }
            currentAddress = fatAddressArr[0][currentAddress]; // Get next cluster number, use the first FAT
        }
        printf("Total number of entries = %d\n", entryCount);
    }
    else if (contRmvFlg){
        int dltFileNameLen = strlen(deletedFile);
        DirEntry* dirEntryArr[100];
        DirEntry* confirmDirEntry;
        int namePassed = 0, extpassed = 0, fileFound = 0, clusterNum = 0;
        while (currentAddress < 0x0ffffff8 && currentAddress != 0x00){ // Traverse all possible clusters
            clusterCount = 0;
            dirEntryOffset = ((currentAddress - 2)*fatEntry->BPB_SecPerClus + fatEntry->BPB_RsvdSecCnt + (fatEntry->BPB_NumFATs*fatEntry->BPB_FATSz32))*fatEntry->BPB_BytsPerSec;
            baseAddress = (char*)mapped_address + dirEntryOffset;
            dirEntry = (DirEntry*)(baseAddress);
            while (dirEntry->DIR_Name[0] != 0x00){ // Traverse the entries in the cluster until we reach EOF or end of cluster
                namePassed = 0;
                extpassed = 0;
                clusterCount++;
                if (clusterCount > (fatEntry->BPB_SecPerClus*fatEntry->BPB_BytsPerSec)/sizeof(DirEntry)){ // Finished Traversing this cluster
                    break;
                }
                if (dirEntry->DIR_Name[0] == 0xe5){ // Deleted file
                    for (i = 1; i < dltFileNameLen; i++){
                        if (deletedFile[i] == '.'){ // File has extension
                            if (i == 8 || dirEntry->DIR_Name[i] == ' ') // Name part matches
                                namePassed++;
                            break;
                        }
                        else if (deletedFile[i] != dirEntry->DIR_Name[i]){
                            break;
                        }
                        else if (i == dltFileNameLen - 1){ // Name part matches (no extension part)
                            namePassed++;
                            break;
                        }
                    }
                    if (namePassed){ // If name part matches, then we compare extension
                        j = 8;
                        i++;
                        for (; i < dltFileNameLen; i++){
                            if (deletedFile[i] != dirEntry->DIR_Name[j]){
                                break;
                            }
                            j++;
                        }
                        if ((i == dltFileNameLen) && (j == 11 || dirEntry->DIR_Name[j] == ' ')){ // Extension part matches
                            extpassed++;
                        }
                    }
                    if (dltFileNameLen == 1 && dirEntry->DIR_Name[1] == ' ' && dirEntry->DIR_Name[8] == ' '){ // File with only 1 char as name, then we auto pass it
                        namePassed++;
                        extpassed++;
                    }
                    if (namePassed && extpassed){ // If there is a file match
                        dirEntryArr[fileFound] = dirEntry;
                        fileFound++; // Flag that indicates how many match we have
                    }
                }    
                baseAddress = (char*)baseAddress + sizeof(DirEntry); // Next entry within this same cluster
                dirEntry = (DirEntry*)(baseAddress);
            }
            currentAddress = fatAddressArr[0][currentAddress]; // Use the first FAT to find the next cluster address
        }
        if (fileFound){
            if (fileFound > 1 || shaFlag){ // If multiple matches
                if (shaFlag){ // If SHA is provided
                    unsigned char md[SHA_DIGEST_LENGTH]; // SHA1 result of the file
                    unsigned char* d; // Starting location
                    int finalFileFound = 0;
                    for (i = 0; i < fileFound; i++){
                        highPart = dirEntryArr[i]->DIR_FstClusHI;
                        lowPart = dirEntryArr[i]->DIR_FstClusLO;
                        startingCluster = (highPart << 16) | lowPart;
                        d = (char*)mapped_address + ((startingCluster - 2)*fatEntry->BPB_SecPerClus + fatEntry->BPB_RsvdSecCnt + (fatEntry->BPB_NumFATs*fatEntry->BPB_FATSz32))*fatEntry->BPB_BytsPerSec;
                        SHA1(d, dirEntryArr[i]->DIR_FileSize, md);
                        if (memcmp(md, providedSHA, SHA_DIGEST_LENGTH) == 0){
                            finalFileFound++;
                            confirmDirEntry = dirEntryArr[i];
                            break;
                        }
                    }
                    if (finalFileFound){
                        confirmDirEntry->DIR_Name[0] = deletedFile[0];
                        clusterNum = ceil((double) confirmDirEntry->DIR_FileSize/(fatEntry->BPB_BytsPerSec*fatEntry->BPB_SecPerClus)); // How many clusters the file occupies
                        for (k = 0; k < clusterNum; k++){ // Traverse and restore the FAT chain
                            if (k == clusterNum - 1){ // If we reach end of chain in FAT
                                for (j = 0; j < fatEntry->BPB_NumFATs; j++){ // Update every FAT
                                    fatAddressArr[j][startingCluster] = EOF;
                                }
                            } 
                            else{
                                for (j = 0; j < fatEntry->BPB_NumFATs; j++){ // Update every FAT
                                    fatAddressArr[j][startingCluster] = startingCluster + 1;
                                }
                            }
                            startingCluster++;
                        }
                        printf("%s: successfully recovered with SHA-1\n", deletedFile);
                        exit(0);
                    }
                    else{
                        printf("%s: file not found\n", deletedFile);
                        exit(1);
                    }
                }
                else{ // filename is ambiguous!!
                    printf("%s: multiple candidates found\n", deletedFile);
                    exit(1); 
                }
            }
            else{ // If only one match
                highPart = dirEntryArr[0]->DIR_FstClusHI;
                lowPart = dirEntryArr[0]->DIR_FstClusLO;
                startingCluster = (highPart << 16) | lowPart;
                dirEntryArr[0]->DIR_Name[0] = deletedFile[0]; // Restore the first char from 0xe5
                clusterNum = ceil((double) dirEntryArr[0]->DIR_FileSize/(fatEntry->BPB_BytsPerSec*fatEntry->BPB_SecPerClus)); // How many clusters the file occupies
                for (k = 0; k < clusterNum; k++){ // Traverse and restore the FAT chain
                    if (k == clusterNum - 1){ // If we reach end of chain in FAT
                        for (j = 0; j < fatEntry->BPB_NumFATs; j++){ // Update every FAT
                            fatAddressArr[j][startingCluster] = EOF;
                        }
                    } 
                    else{
                        for (j = 0; j < fatEntry->BPB_NumFATs; j++){ // Update every FAT
                            fatAddressArr[j][startingCluster] = startingCluster + 1;
                        }
                    }
                    startingCluster++;
                }
                printf("%s: successfully recovered\n", deletedFile);
                return 0;
            }
        }
        else{
            printf("%s: file not found\n", deletedFile);
        }
    }
    if (nonContRmvFlg){
        int dltFileNameLen = strlen(deletedFile);
        DirEntry* dirEntryArr[100];
        DirEntry* confirmDirEntry;
        int namePassed = 0, extpassed = 0, fileFound = 0, clusterNum = 0;
        while (currentAddress < 0x0ffffff8 && currentAddress != 0x00){ // Traverse all possible clusters
            clusterCount = 0;
            dirEntryOffset = ((currentAddress - 2)*fatEntry->BPB_SecPerClus + fatEntry->BPB_RsvdSecCnt + (fatEntry->BPB_NumFATs*fatEntry->BPB_FATSz32))*fatEntry->BPB_BytsPerSec;
            baseAddress = (char*)mapped_address + dirEntryOffset;
            dirEntry = (DirEntry*)(baseAddress);
            while (dirEntry->DIR_Name[0] != 0x00){ // Traverse the entries in the cluster until we reach EOF or end of cluster
                namePassed = 0;
                extpassed = 0;
                clusterCount++;
                if (clusterCount > (fatEntry->BPB_SecPerClus*fatEntry->BPB_BytsPerSec)/sizeof(DirEntry)){ // Finished Traversing this cluster
                    break;
                }
                if (dirEntry->DIR_Name[0] == 0xe5){ // Deleted file
                    for (i = 1; i < dltFileNameLen; i++){
                        if (deletedFile[i] == '.'){ // File has extension
                            if (i == 8 || dirEntry->DIR_Name[i] == ' ') // Name part matches
                                namePassed++;
                            break;
                        }
                        else if (deletedFile[i] != dirEntry->DIR_Name[i]){
                            break;
                        }
                        else if (i == dltFileNameLen - 1){ // Name part matches (no extension part)
                            namePassed++;
                            break;
                        }
                    }
                    if (namePassed){ // If name part matches, then we compare extension
                        j = 8;
                        i++;
                        for (; i < dltFileNameLen; i++){
                            if (deletedFile[i] != dirEntry->DIR_Name[j]){
                                break;
                            }
                            j++;
                        }
                        if ((i == dltFileNameLen) && (j == 11 || dirEntry->DIR_Name[j] == ' ')){ // Extension part matches
                            extpassed++;
                        }
                    }
                    if (dltFileNameLen == 1 && dirEntry->DIR_Name[1] == ' ' && dirEntry->DIR_Name[8] == ' '){ // File with only 1 char as name, then we auto pass it
                        namePassed++;
                        extpassed++;
                    }
                    if (namePassed && extpassed){ // If there is a file match
                        dirEntryArr[fileFound] = dirEntry;
                        fileFound++; // Flag that indicates how many match we have
                    }
                }    
                baseAddress = (char*)baseAddress + sizeof(DirEntry); // Next entry within this same cluster
                dirEntry = (DirEntry*)(baseAddress);
            }
            currentAddress = fatAddressArr[0][currentAddress]; // Use the first FAT to find the next cluster address
        }
        if (fileFound){
            if (fileFound > 1 || shaFlag){ // If multiple matches
                if (shaFlag){ // If SHA is provided
                    unsigned char md[SHA_DIGEST_LENGTH]; // SHA1 result of the file
                    unsigned char* d; // Starting location
                    int finalFileFound = 0;
                    for (i = 0; i < fileFound; i++){
                        highPart = dirEntryArr[i]->DIR_FstClusHI;
                        lowPart = dirEntryArr[i]->DIR_FstClusLO;
                        startingCluster = (highPart << 16) | lowPart;
                        d = (char*)mapped_address + ((startingCluster - 2)*fatEntry->BPB_SecPerClus + fatEntry->BPB_RsvdSecCnt + (fatEntry->BPB_NumFATs*fatEntry->BPB_FATSz32))*fatEntry->BPB_BytsPerSec;
                        SHA1(d, dirEntryArr[i]->DIR_FileSize, md);
                        if (memcmp(md, providedSHA, SHA_DIGEST_LENGTH) == 0){
                            finalFileFound++;
                            confirmDirEntry = dirEntryArr[i];
                            break;
                        }
                    }
                    if (finalFileFound){
                        confirmDirEntry->DIR_Name[0] = deletedFile[0];
                        clusterNum = ceil((double) confirmDirEntry->DIR_FileSize/(fatEntry->BPB_BytsPerSec*fatEntry->BPB_SecPerClus)); // How many clusters the file occupies
                        for (k = 0; k < clusterNum; k++){ // Traverse and restore the FAT chain
                            if (k == clusterNum - 1){ // If we reach end of chain in FAT
                                for (j = 0; j < fatEntry->BPB_NumFATs; j++){ // Update every FAT
                                    fatAddressArr[j][startingCluster] = EOF;
                                }
                            } 
                            else{
                                for (j = 0; j < fatEntry->BPB_NumFATs; j++){ // Update every FAT
                                    fatAddressArr[j][startingCluster] = startingCluster + 1;
                                }
                            }
                            startingCluster++;
                        }
                        printf("%s: successfully recovered with SHA-1\n", deletedFile);
                        exit(0);
                    }
                    else{
                        printf("%s: file not found\n", deletedFile);
                        exit(1);
                    }
                }
                else{ // filename is ambiguous!!
                    printf("%s: multiple candidates found\n", deletedFile);
                    exit(1); 
                }
            }
            else{ // If only one match
                highPart = dirEntryArr[0]->DIR_FstClusHI;
                lowPart = dirEntryArr[0]->DIR_FstClusLO;
                startingCluster = (highPart << 16) | lowPart;
                dirEntryArr[0]->DIR_Name[0] = deletedFile[0]; // Restore the first char from 0xe5
                clusterNum = ceil((double) dirEntryArr[0]->DIR_FileSize/(fatEntry->BPB_BytsPerSec*fatEntry->BPB_SecPerClus)); // How many clusters the file occupies
                for (k = 0; k < clusterNum; k++){ // Traverse and restore the FAT chain
                    if (k == clusterNum - 1){ // If we reach end of chain in FAT
                        for (j = 0; j < fatEntry->BPB_NumFATs; j++){ // Update every FAT
                            fatAddressArr[j][startingCluster] = EOF;
                        }
                    } 
                    else{
                        for (j = 0; j < fatEntry->BPB_NumFATs; j++){ // Update every FAT
                            fatAddressArr[j][startingCluster] = startingCluster + 1;
                        }
                    }
                    startingCluster++;
                }
                printf("%s: successfully recovered\n", deletedFile);
                return 0;
            }
        }
        else{
            printf("%s: file not found\n", deletedFile);
        }
    }
}
