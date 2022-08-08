/*
Filesystem Lab disigned and implemented by Liang Junkai,RUC
*/

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fuse.h>
#include <errno.h>
#include <stdlib.h>
#include "disk.h"

#define DIRMODE (S_IFDIR|0755)
#define REGMODE (S_IFREG|0644)

// buffer
char buffer [BLOCK_SIZE];

// const
int rootInode = 0;

// struct define
struct FileStat
{
    mode_t  st_mode; //文件对应的模式
    nlink_t st_nlink;//文件的链接数
    uid_t   st_uid;  //文件所有者
    gid_t   st_gid;  //文件所有者的组
    off_t   st_size; //文件字节数
    time_t  st_accessTime;//被访问的时间
    time_t  st_modifyTime;//被修改的时间
    time_t  st_changeTime;//状态改变时间
};

struct Inode
{
    struct FileStat stat;
    int blocks; // 包含多少个blocks
    int flags; // 一些标记
    int dirPointer [14];
    int indirPointer [2];
};

struct Directory
{
    char name[25];
    int inodeId;
};

struct DirectoryDataBlock
{
    int childNum;
    struct Directory children[127];
};

// declarations
void writeStat(struct stat * attr, struct FileStat ori);
void writeInodeData(struct Inode * inode, const char * buf, int size);
int gotoChild(struct Inode current, const char * name, struct Inode * next);
struct Inode gotoInode(const char * path, int * found);
int addDirInode(struct Inode * current, int emptyOne, const char * name);
void clearInodeAllDataBlocks(int inodeId);
void initDirInode(struct Inode * fileInode);
void initDir(struct FileStat * fileStat);
void initFileInode(struct Inode * fileInode);
void initFile(struct FileStat * fileStat);
char getNth(char data, char n);
struct Inode getInode(int id);
void writeInode(int id, struct Inode * inode);
void readDataBlock(int id, char * buf);
void writeDataBlock(int id, char * buf);
int getEmptyInode();
void setInode(int id);
void freeInode(int id);
int getEmptyDataBlock();
void setDataBlock(int id);
void freeDataBlock(int id);

// utils
void printDirInode(int inodeId)
{
    struct Inode inode = getInode(inodeId);
    int blockNum = inode.blocks;
    if (inode.stat.st_mode == DIRMODE)
    {
        // Read Dir
        for (int i = 0; i < blockNum; i++)
        {
            readDataBlock(inode.dirPointer[i], buffer);
            struct DirectoryDataBlock *dir = (struct DirectoryDataBlock *)buffer;
            for (int j = 0; j < dir->childNum; j++)
            {
                struct Directory now = dir->children[j];
                printf("* %s\n", now.name);
            }
        }
    }
}

int ceil(double a)
{
    int r = (int)a;
    if((double)r >= a)
        return r;
    else
        return r + 1;
}

void writeStat(struct stat * attr, struct FileStat ori)
{
    attr->st_mode = ori.st_mode;
    attr->st_nlink = ori.st_nlink;
    attr->st_uid = ori.st_uid;
    attr->st_gid = ori.st_gid;
    attr->st_size = ori.st_size;
    attr->st_atime = ori.st_accessTime;
    attr->st_mtime = ori.st_modifyTime;
    attr->st_ctime = ori.st_modifyTime;
}

void writeInodeData(struct Inode * inode, const char * buf, int size)
{
    printf("(writeInodeData) write %d bytes to inode\n", size);

    char * current = buf;
    int * indir = (int *)malloc(BLOCK_SIZE);
    int tempBlock = -1;
//    int targetBlocks = ceil((double)size / BLOCK_SIZE);

    while(current - buf < size)
    {
        int newDataBlock = getEmptyDataBlock();
        setDataBlock(newDataBlock);
        printf("(writeInodeData) writing new data block %d\n", newDataBlock);
        writeDataBlock(newDataBlock, current);
        current += BLOCK_SIZE;

        if(inode->blocks < 14)
        {
            inode->dirPointer[inode->blocks] = newDataBlock;
            inode->blocks ++;
        }
        else
        {
            if(inode->blocks == 14)
            {
                tempBlock = getEmptyDataBlock();
                setDataBlock(tempBlock);
                inode->indirPointer[0] = tempBlock;
                indir[inode->blocks - 14] = newDataBlock;
                inode->blocks ++;
            }
            else if(inode->blocks < 14 + 1024)
            {
                tempBlock = inode->indirPointer[0];
                readDataBlock(tempBlock, (char *)indir);
                indir[inode->blocks - 14] = newDataBlock;
                inode->blocks ++;
            }
            else if(inode->blocks == 14 + 1024)
            {
//                writeDataBlock(tempBlock, (char *)indir);
                tempBlock = getEmptyDataBlock();
                setDataBlock(tempBlock);
                inode->indirPointer[1] = tempBlock;
                indir[inode->blocks - 14 - 1024] = newDataBlock;
                inode->blocks ++;
            }
            else
            {
                tempBlock = inode->indirPointer[1];
                readDataBlock(tempBlock, (char *)indir);
                indir[inode->blocks - 14 - 1024] = newDataBlock;
                inode->blocks ++;
            }
        }
    }

    if(tempBlock != -1)
    {
        writeDataBlock(tempBlock, (char *)indir);
    }

    free(indir);
}

int gotoChild(struct Inode current, const char * name, struct Inode * next)
{
    int blockNum = current.blocks;
    // TODO: consider indirect pointers
    if(blockNum > 14)
    {
        printf("Too many child directories!\n");
        exit(7);
    }

    for(int i = 0; i < blockNum; i++)
    {
        readDataBlock(current.dirPointer[i], buffer);
        struct DirectoryDataBlock * dir = (struct DirectoryDataBlock *)buffer;
        for(int j = 0; j < dir->childNum; j++)
        {
            struct Directory now = dir->children[j];
            if(strcmp(now.name, name) == 0)
            {
                *next = getInode(now.inodeId);
                return now.inodeId;
            }
        }
    }

    return 0;
}

struct Inode gotoInode(const char * path, int * found)
{
    struct Inode current = getInode(0);
    struct Inode next;
    int myid = 0;
    char lastName[32];
    strcpy(lastName, "/");

    char tempPath [256];
    strcpy(tempPath, path);
    char * currentName = strtok(tempPath, "/");
    while(currentName != NULL)
    {
//        if(currentName == NULL)
//            break;

        if(strlen(currentName) == 0)
        {
            continue;
        }
        printf("(gotoInode) Path (%s) From (%s) goto child (%s)\n", path, lastName, currentName);

        strcpy(lastName, currentName);

        int status = gotoChild(current, currentName, &next);
        if(!status)
        {
            // Cannot find
            printf("(gotoInode) cannot find child (%s)\n", currentName);
            *found = -1;
            return current;
        }
        current = next;
        myid = status;

        currentName = strtok(NULL, "/");
    }

    *found = myid;
    return current;
}

int addDirInode(struct Inode * current, int emptyOne, const char * name)
{
    printf("(addDirInode) Insert inode name (%s) id %d\n", name, emptyOne);

    int blockNum = current->blocks;
    // TODO: consider indirect pointers
    if(blockNum > 14)
    {
        printf("Too many child directories!\n");
        exit(7);
    }
    if(blockNum == 0)
    {
        blockNum ++;
        current->blocks = blockNum;
        int newBlock = getEmptyDataBlock();
        setDataBlock(newBlock);
        printf("(addDirInode) set data block %d\n", newBlock);
        current->dirPointer[current->blocks - 1] = newBlock;
        memset(buffer, 0, BLOCK_SIZE);
        writeDataBlock(newBlock, buffer);
    }

    readDataBlock(current->dirPointer[current->blocks - 1], buffer);
    struct DirectoryDataBlock * dir = (struct DirectoryDataBlock *)buffer;
    if(dir->childNum == 127)
    {
        blockNum ++;
        if(blockNum > 14)
        {
            printf("Too many child directories!\n");
            return -ENOSPC;
        }
        int newBlock = getEmptyDataBlock();
        setDataBlock(newBlock);
        printf("(addDirInode) set data block %d\n", newBlock);
        current->dirPointer[current->blocks - 1] = newBlock;
        dir->childNum = 1;
        dir->children[0].inodeId = emptyOne;
        strcpy(dir->children[0].name, name);
        writeDataBlock(newBlock, buffer);
    }
    else
    {
        dir->childNum ++;
        dir->children[dir->childNum - 1].inodeId = emptyOne;
        strcpy(dir->children[dir->childNum - 1].name, name);
        writeDataBlock(current->dirPointer[current->blocks - 1], buffer);
    }

    // Update time and other params
    current->blocks = blockNum;
    current->stat.st_modifyTime = time(NULL);
    current->stat.st_changeTime = time(NULL);
    current->stat.st_size = blockNum * 4096;

    return 0;
}

void clearInodeAllDataBlocks(int inodeId)
{
    struct Inode inode = getInode(inodeId);
    
    int * indir = (int *)malloc(BLOCK_SIZE);
    int indirIndex = -1;

    for(int i = 0; i < inode.blocks; i++)
    {
        if(i < 14)
        {
            // dir pointer
            freeDataBlock(inode.dirPointer[i]);
        }
        else
        {
            // indir pointer
            if(i < 14 + 1024)
            {
                if(indirIndex != 1)
                {
                    readDataBlock(inode.indirPointer[0], (char *)indir);
                    indirIndex = 1;
                }
                freeDataBlock(indir[i - 14]);
            }
            else
            {
                if(indirIndex != 2)
                {
                    readDataBlock(inode.indirPointer[1], indir);
                    indirIndex = 2;
                }
                freeDataBlock(indir[i - 14 - 1024]);
            }
        }
    }
    
    if(inode.blocks > 14)
        freeDataBlock(inode.indirPointer[0]);
    if(inode.blocks > 14 + 1024)
        freeDataBlock(inode.indirPointer[1]);
    
    inode.stat.st_changeTime = time(NULL);
    // inode.stat.st_modifyTime = time(NULL);
    inode.blocks = 0;
    inode.stat.st_size = 0;
    writeInode(inodeId, &inode);
    
    free(indir);
}

void initDirInode(struct Inode * fileInode)
{
    struct FileStat stat;
    initDir(&stat);
    fileInode->stat = stat;
    fileInode->flags = 0;
    fileInode->blocks = 0;
}

void initDir(struct FileStat * fileStat)
{
    fileStat->st_nlink = 1;
    fileStat->st_mode = DIRMODE;
    fileStat->st_uid = getuid();
    fileStat->st_gid = getgid();
    fileStat->st_size = 0;
    fileStat->st_accessTime = time(NULL);
    fileStat->st_changeTime = time(NULL);
    fileStat->st_modifyTime = time(NULL);
}

void initFileInode(struct Inode * fileInode)
{
    struct FileStat stat;
    initFile(&stat);
    fileInode->stat = stat;
    fileInode->flags = 0;
    fileInode->blocks = 0;
}

void initFile(struct FileStat * fileStat)
{
    fileStat->st_nlink = 1;
    fileStat->st_mode = REGMODE;
    fileStat->st_uid = getuid();
    fileStat->st_gid = getgid();
    fileStat->st_size = 0;
    fileStat->st_accessTime = time(NULL);
    fileStat->st_changeTime = time(NULL);
    fileStat->st_modifyTime = time(NULL);
}

char getNth(char data, char n)
{
    char mask = 1 << n;
    return (data & mask) >> n;
}

struct Inode getInode(int id)
{
    int blockId = (id >> 5) + 3;
    int inodeOffset = id & 0x1F;
    disk_read(blockId, buffer);
    struct Inode * map = (struct Inode *)buffer;
    return map[inodeOffset];
}

void writeInode(int id, struct Inode * inode)
{
    int blockId = (id >> 5) + 3;
    int inodeOffset = id & 0x1F;
    disk_read(blockId, buffer);
    struct Inode * map = (struct Inode *)buffer;
    memcpy(&map[inodeOffset], inode, sizeof(struct Inode));
    disk_write(blockId, buffer);
}

void readDataBlock(int id, char * buf)
{
    int blockId = id + 1536;
    disk_read(blockId, buf);
}

void writeDataBlock(int id, char * buf)
{
    int blockId = id + 1536;
    disk_write(blockId, buf);
}

int getEmptyInode()
{
    disk_read(0, buffer);
    char * map = (char *)buffer;
    for(int i = 0; i < BLOCK_SIZE; i++)
    {
        for(int j = 0; j < 8; j++)
        {
            if(!getNth(map[i], (char)j))
            {
                return (i << 3) | j;
            }
        }
    }
    return -1;
}

void setInode(int id)
{
    printf("Set Inode %d\n", id);

    disk_read(0, buffer);
    char * map = (char *)buffer;
    int inodeOffset = id & 0x7;
    int byteId = id >> 3;
    char mask = 1 << inodeOffset;
    map[byteId] = map[byteId] | mask;
    disk_write(0, buffer);
}

void freeInode(int id)
{
    printf("Free Inode %d\n", id);

    disk_read(0, buffer);
    char * map = (char *)buffer;
    int inodeOffset = id & 0x7;
    int byteId = id >> 3;
    int mask = ~(1 << inodeOffset);
    map[byteId] = map[byteId] & mask;
    disk_write(0, buffer);
}

int getEmptyDataBlock()
{
    disk_read(1, buffer);
    char * map = (char *)buffer;
    for(int i = 0; i < BLOCK_SIZE; i++)
    {
        for(int j = 0; j < 8; j++)
        {
            if(!getNth(map[i], (char)j))
            {
                return (i << 3) | j;
            }
        }
    }

    disk_read(2, buffer);
    for(int i = 0; i < BLOCK_SIZE; i++)
    {
        for(int j = 0; j < 8; j++)
        {
            if(!getNth(map[i], (char)j))
            {
                return ((i << 3) | j) + 32768;
            }
        }
    }

    return -1;
}

void setDataBlock(int id)
{
    int blockId = id >> 15;
    blockId += 1;
    disk_read(blockId, buffer);
    char * map = (char *)buffer;
    int inodeOffset = id & 0x7;
    int byteId = id >> 3;
    int mask = 1 << inodeOffset;
    map[byteId] = map[byteId] | mask;
    disk_write(blockId, buffer);
}

void freeDataBlock(int id)
{
    int blockId = id >> 15;
    blockId += 1;
    disk_read(blockId, buffer);
    char * map = (char *)buffer;
    int inodeOffset = id & 0x7;
    int byteId = id >> 3;
    int mask = ~(1 << inodeOffset);
    map[byteId] = map[byteId] & mask;
    disk_write(blockId, buffer);
}

// Format the virtual block device in the following function
// 0, 1, 2: bitmap blocks
// 0: inode bitmap (32768 bits)
// 1, 2: data bitmap (65536 bits)
// 3-1535: inode blocks (more than 32768 blocks)
// 1 inode block = 32 inodes (128 Bytes pre inode)
// 1536-65535: data blocks (250 MiB)

int mkfs()
{
    // Fast Format
    memset(buffer, 0, BLOCK_SIZE);
    disk_write(0, buffer);
    disk_write(1, buffer);
    disk_write(2, buffer);

    // Set up / directory
    setInode(0);
    struct Inode root;
    initDirInode(&root);
    addDirInode(&root, 0, ".");
    writeInode(0, &root);

    return 0;
}

// Filesystem operations that you need to implement
int fs_getattr (const char *path, struct stat *attr)
{
	printf("Getattr is called:%s\n",path);

    int found;
    struct Inode node = gotoInode(path, &found);
    if(found < 0)
    {
        printf("(Getattr) Cannot find path: %s\n", path);
        return -ENOENT;
    }
    writeStat(attr, node.stat);
    printf("(Getattr) inodeID: %d \t attr.size: %ld\t attr.mode: %d\n", found, attr->st_size, attr->st_mode);
	return 0;
}

int fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	printf("Readdir is called:%s\n", path);

    int found;
    struct Inode current = gotoInode(path, &found);
    if(found < 0)
    {
        return -ENOENT;
    }
    if(current.stat.st_mode != DIRMODE)
    {
        return -ENOTDIR;
    }
    printf("(ReadDir) found inode (%s) id %d\n", path, found);

    int blockNum = current.blocks;
    // TODO: consider indirect pointers
    if(blockNum > 14)
    {
        printf("Too many child directories!\n");
        exit(7);
    }

    // Read Dir
    for(int i = 0; i < blockNum; i++)
    {
        readDataBlock(current.dirPointer[i], buffer);
        struct DirectoryDataBlock * dir = (struct DirectoryDataBlock *)buffer;
        for(int j = 0; j < dir->childNum; j++)
        {
            struct Directory now = dir->children[j];
            printf("(ReadDir) Filler get name (%s)\n", now.name);
            filler(buf, now.name, NULL, 0);
        }
    }

    // Update atime
    current.stat.st_accessTime = time(NULL);
    writeInode(found, &current);

	return 0;
}

int fs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	printf("Read is called:%s\n",path);

    int found;
    struct Inode inode = gotoInode(path, &found);
    if(found < 0)
    {
        return -ENOENT;
    }

    int fileSize = inode.stat.st_size;
    if(fileSize <= offset)
    {
        return -ERANGE;
    }

    char * file = (char *)malloc(8 << 20);
    int * indir = (int *)malloc(BLOCK_SIZE);
    int indirIndex = 0;
    char * current = file;
    for(int i = 0; i < inode.blocks; i++)
    {
        if(i < 14)
        {
            // dir pointer
            int blockId = inode.dirPointer[i];
            readDataBlock(blockId, current);
            current += BLOCK_SIZE;
        }
        else
        {
            // indir pointer
            if(i < 14 + 1024)
            {
                if(indirIndex != 1)
                {
                    readDataBlock(inode.indirPointer[0], indir);
                    indirIndex = 1;
                }
                readDataBlock(indir[i - 14], current);
                current += BLOCK_SIZE;
            }
            else
            {
                if(indirIndex != 2)
                {
                    readDataBlock(inode.indirPointer[1], indir);
                    indirIndex = 2;
                }
                readDataBlock(indir[i - 14 - 1024], current);
                current += BLOCK_SIZE;
            }
        }
    }

    char * start = file + offset;
    int realReadLen = fileSize - offset < size ? fileSize - offset : size;
    
    memcpy(buf, start, realReadLen);

    inode.stat.st_accessTime = time(NULL);
    writeInode(found, &inode);
    free(indir);
    free(file);

	return realReadLen;
}

int fs_mknod (const char *path, mode_t mode, dev_t dev)
{
    printf("Mknod is called:%s\n", path);

    int found;
    // Create an inode
    // find parent node
    int parentFound;
    char temp[256];
    char name[32];
    strcpy(temp, path);
    for (int i = strlen(temp) - 2; i >= 0; i--)
    {
        if (temp[i] == '/')
        {
            temp[i] = '\0';
            strcpy(name, temp + i + 1);
            break;
        }
    }
    if (strlen(temp) == 0)
    {
        temp[0] = '/';
        temp[1] = '\0';
    }

    struct Inode parent = gotoInode(temp, &parentFound);
    if (parentFound < 0 || parent.stat.st_mode != DIRMODE)
    {
        printf("parent (%s) is not found or not a dir\n", temp);
        return -ENOENT;
    }

    printf("(Mknod) parent path: %s\n", temp);
    printf("(Mknod) name: %s\n", name);
    printf("Dir Before:\n");
    printDirInode(parentFound);

    int emptyOne = getEmptyInode();
    printf("(Mknod) empty inodeId: %d\n", emptyOne);
    found = emptyOne;
    int a = addDirInode(&parent, emptyOne, name);
    if(a < 0)
        return a;
    writeInode(parentFound, &parent);
    printf("Dir After Mknod:\n");
    printDirInode(parentFound);

    setInode(emptyOne);
    struct Inode inode = getInode(emptyOne);
    initFileInode(&inode);
    writeInode(emptyOne, &inode);
    printf("(Mknod) Write new inode %d\n", emptyOne);

    return 0;
}

int fs_mkdir (const char *path, mode_t mode)
{
	printf("Mkdir is called:%s\n",path);

    int found;
    // Create an inode
    // find parent node
    int parentFound;
    char temp[256];
    char name[32];
    strcpy(temp, path);
    for (int i = strlen(temp) - 2; i >= 0; i--)
    {
        if (temp[i] == '/')
        {
            temp[i] = '\0';
            strcpy(name, temp + i + 1);
            break;
        }
    }
    if (strlen(temp) == 0)
    {
        temp[0] = '/';
        temp[1] = '\0';
    }

    struct Inode parent = gotoInode(temp, &parentFound);
    if (parentFound < 0 || parent.stat.st_mode != DIRMODE)
    {
        return -ENOENT;
    }
    printf("(Mkdir) parent (%s) id %d found\n", temp, parentFound);
    int emptyOne = getEmptyInode();
    printf("(Mkdir) wait to create (%s) inodeId %d\n", name, emptyOne);

    found = emptyOne;
    int a = addDirInode(&parent, emptyOne, name);
    if(a < 0)
        return a;
    writeInode(parentFound, &parent);

    setInode(emptyOne);
    struct Inode inode = getInode(emptyOne);
    initDirInode(&inode);
    writeInode(emptyOne, &inode);
    addDirInode(&inode, emptyOne, ".");
    writeInode(emptyOne, &inode);

    printf("(Mkdir) Newly created dir: %s\n", name);
    printDirInode(emptyOne);

	return 0;
}

int fs_rmdir (const char *path)
{
	printf("Rmdir is called:%s\n", path);

    int found;

    struct Inode inode = gotoInode(path, &found);
    if(found < 0)
    {
        return -ENOENT;
    }
    if(inode.stat.st_mode != DIRMODE)
    {
        return -ENOTDIR;
    }

    int parentFound;
    char temp[256], name[32];
    strcpy(temp, path);
    for (int i = strlen(temp) - 2; i >= 0; i--)
    {
        if (temp[i] == '/')
        {
            temp[i] = '\0';
            strcpy(name, temp + i + 1);
            break;
        }
    }
    if (strlen(temp) == 0)
    {
        temp[0] = '/';
        temp[1] = '\0';
    }
    struct Inode parent = gotoInode(temp, &parentFound);
    printf("(Rmdir) need to remove found (%s) inodeId %d\n", name, found);
    printf("(Rmdir) parent found (%s) inodeId %d\n", temp, parentFound);

    // find need-to-remove one
    int targetI, targetJ;
    int flag = 1;
    for (int i = 0; i < parent.blocks; i++)
    {
        readDataBlock(parent.dirPointer[i], buffer);
        struct DirectoryDataBlock *dir = (struct DirectoryDataBlock *)buffer;
        for (int j = 0; j < dir->childNum; j++)
        {
            struct Directory now = dir->children[j];
            if (strcmp(now.name, name) == 0)
            {
                // block = *dir;
                targetI = i;
                targetJ = j;

                if (targetI == parent.blocks - 1)
                {
                    struct Directory target = dir->children[dir->childNum - 1];
                    strcpy(dir->children[j].name, target.name);
                    dir->children[j].inodeId = target.inodeId;
                    dir->childNum--;
                    writeDataBlock(parent.dirPointer[i], buffer);
                    flag = 0;
                }
                break;
            }
        }
    }
    // find last one
    if (flag == 1)
    {
        readDataBlock(parent.dirPointer[parent.blocks - 1], buffer);
        struct DirectoryDataBlock *dir = (struct DirectoryDataBlock *)buffer;
        struct Directory now = (dir->children[dir->childNum - 1]);
        dir->childNum--;
        if (dir->childNum == 0)
        {
            freeDataBlock(parent.dirPointer[parent.blocks - 1]);
            parent.blocks--;
            writeInode(parentFound, &parent);

            readDataBlock(parent.dirPointer[targetI], buffer);
            strcpy(dir->children[targetJ].name, now.name);
            dir->children[targetJ].inodeId = now.inodeId;
            writeDataBlock(parent.dirPointer[targetI], buffer);
        }
        else
        {
            writeDataBlock(parent.dirPointer[parent.blocks - 1], buffer);
            readDataBlock(parent.dirPointer[targetI], buffer);
            strcpy(dir->children[targetJ].name, now.name);
            dir->children[targetJ].inodeId = now.inodeId;
            writeDataBlock(parent.dirPointer[targetI], buffer);
        }
    }

    // delete THE inode
    clearInodeAllDataBlocks(found);
    freeInode(found);

    // update time
    parent.stat.st_modifyTime = time(NULL);
    parent.stat.st_changeTime = time(NULL);
    writeInode(parentFound, &parent);

    return 0;
}

int fs_unlink (const char *path)
{
	printf("Unlink is callded:%s\n",path);

    int found;
    struct Inode inode = gotoInode(path, &found);
    if(found < 0)
    {
        return -ENOENT;
    }
    if(inode.stat.st_mode != REGMODE)
    {
        printf("(Unlink) Inode %d\n", found);
        return -EISDIR;
    }

    int parentFound;
    char temp[256], name[32];
    strcpy(temp, path);
    for (int i = strlen(temp) - 2; i >= 0; i--)
    {
        if (temp[i] == '/')
        {
            temp[i] = '\0';
            strcpy(name, temp + i + 1);
            break;
        }
    }
    if (strlen(temp) == 0)
    {
        temp[0] = '/';
        temp[1] = '\0';
    }
    struct Inode parent = gotoInode(temp, &parentFound);

    // first, delete old one
    // find old one
    int targetI, targetJ;
    int flag = 1;
    for (int i = 0; i < parent.blocks; i++)
    {
        readDataBlock(parent.dirPointer[i], buffer);
        struct DirectoryDataBlock *dir = (struct DirectoryDataBlock *)buffer;
        for (int j = 0; j < dir->childNum; j++)
        {
            struct Directory now = dir->children[j];
            if (strcmp(now.name, name) == 0)
            {
                printf("(Unlink) found %s in parent (%s)\n", name, temp);
                // block = *dir;
                targetI = i;
                targetJ = j;

                if (targetI == parent.blocks - 1)
                {
                    struct Directory target = dir->children[dir->childNum - 1];
                    strcpy(dir->children[j].name, target.name);
                    dir->children[j].inodeId = target.inodeId;
                    dir->childNum--;
                    writeDataBlock(parent.dirPointer[i], buffer);
                    flag = 0;
                }
                break;
            }
        }
    }
    // find last one
    if (flag == 1)
    {
        readDataBlock(parent.dirPointer[parent.blocks - 1], buffer);
        struct DirectoryDataBlock *dir = (struct DirectoryDataBlock *)buffer;
        struct Directory now = (dir->children[dir->childNum - 1]);
        dir->childNum--;
        if (dir->childNum == 0)
        {
            freeDataBlock(parent.dirPointer[parent.blocks - 1]);
            parent.blocks--;
            writeInode(parentFound, &parent);

            readDataBlock(parent.dirPointer[targetI], buffer);
            strcpy(dir->children[targetJ].name, now.name);
            dir->children[targetJ].inodeId = now.inodeId;
            writeDataBlock(parent.dirPointer[targetI], buffer);
        }
        else
        {
            writeDataBlock(parent.dirPointer[parent.blocks - 1], buffer);
            readDataBlock(parent.dirPointer[targetI], buffer);
            strcpy(dir->children[targetJ].name, now.name);
            dir->children[targetJ].inodeId = now.inodeId;
            writeDataBlock(parent.dirPointer[targetI], buffer);
        }
    }

    // delete THE inode
    clearInodeAllDataBlocks(found);
    freeInode(found);

    // update time
    parent.stat.st_modifyTime = time(NULL);
    parent.stat.st_changeTime = time(NULL);
    writeInode(parentFound, &parent);

	return 0;
}

int fs_rename (const char *oldpath, const char *newpath)
{
	printf("Rename is called:%s\n",newpath);

    int found;
    struct Inode inode = gotoInode(oldpath, &found);
    if(found < 0)
    {
        return -ENOENT;
    }

    int parentFoundOld, parentFoundNew;
    char tempOld[256], nameOld[32];
    char tempNew[256], nameNew[32];

    strcpy(tempOld, oldpath);
    for (int i = strlen(tempOld) - 2; i >= 0; i--)
    {
        if (tempOld[i] == '/')
        {
            tempOld[i] = '\0';
            strcpy(nameOld, tempOld + i + 1);
            break;
        }
    }
    if (strlen(tempOld) == 0)
    {
        tempOld[0] = '/';
        tempOld[1] = '\0';
    }

    strcpy(tempNew, newpath);
    for (int i = strlen(tempNew) - 2; i >= 0; i--)
    {
        if (tempNew[i] == '/')
        {
            tempNew[i] = '\0';
            strcpy(nameNew, tempNew + i + 1);
            break;
        }
    }
    if (strlen(tempNew) == 0)
    {
        tempNew[0] = '/';
        tempNew[1] = '\0';
    }

    struct Inode parentOld = gotoInode(tempOld, &parentFoundOld);
    if (parentFoundOld < 0 || parentOld.stat.st_mode != DIRMODE)
    {
        return -ENOENT;
    }
    struct Inode parentNew = gotoInode(tempNew, &parentFoundNew);
    if (parentFoundNew < 0 || parentNew.stat.st_mode != DIRMODE)
    {
        return -ENOENT;
    }

    if(parentFoundNew == parentFoundOld)
    {
        // simply rename
        for(int i = 0; i < parentOld.blocks; i++)
        {
            readDataBlock(parentOld.dirPointer[i], buffer);
            struct DirectoryDataBlock * dir = (struct DirectoryDataBlock *)buffer;
            for(int j = 0; j < dir->childNum; j++)
            {
                struct Directory now = dir->children[j];
                if(strcmp(now.name, nameOld) == 0)
                {
                    strcpy(dir->children[j].name, nameNew);
                    writeDataBlock(parentOld.dirPointer[i], buffer);
                    break;
                }
            }
        }

        // update time
        parentOld.stat.st_modifyTime = time(NULL);
        parentOld.stat.st_changeTime = time(NULL);
        writeInode(parentFoundOld, &parentOld);

        return 0;
    }
    else
    {
        // first, delete old one
        // find old one
        int targetI, targetJ;
        int flag = 1;
        for (int i = 0; i < parentOld.blocks; i++)
        {
            readDataBlock(parentOld.dirPointer[i], buffer);
            struct DirectoryDataBlock *dir = (struct DirectoryDataBlock *)buffer;
            for (int j = 0; j < dir->childNum; j++)
            {
                struct Directory now = dir->children[j];
                if (strcmp(now.name, nameOld) == 0)
                {
                    // block = *dir;
                    targetI = i;
                    targetJ = j;

                    if(targetI == parentOld.blocks - 1)
                    {
                        struct Directory target = dir->children[dir->childNum - 1];
                        strcpy(dir->children[j].name, target.name);
                        dir->children[j].inodeId = target.inodeId;
                        dir->childNum --;
                        writeDataBlock(parentOld.dirPointer[i], buffer);
                        flag = 0;
                    }
                    break;
                }
            }
        }
        // find last one
        if (flag == 1)
        {
            readDataBlock(parentOld.dirPointer[parentOld.blocks - 1], buffer);
            struct DirectoryDataBlock *dir = (struct DirectoryDataBlock *)buffer;
            struct Directory now = (dir->children[dir->childNum - 1]);
            dir->childNum --;
            if (dir->childNum == 0)
            {
                freeDataBlock(parentOld.dirPointer[parentOld.blocks - 1]);
                parentOld.blocks--;
                writeInode(parentFoundOld, &parentOld);

                readDataBlock(parentOld.dirPointer[targetI], buffer);
                strcpy(dir->children[targetJ].name, now.name);
                dir->children[targetJ].inodeId = now.inodeId;
                writeDataBlock(parentOld.dirPointer[targetI], buffer);
            }
            else
            {
                writeDataBlock(parentOld.dirPointer[parentOld.blocks - 1], buffer);
                readDataBlock(parentOld.dirPointer[targetI], buffer);
                strcpy(dir->children[targetJ].name, now.name);
                dir->children[targetJ].inodeId = now.inodeId;
                writeDataBlock(parentOld.dirPointer[targetI], buffer);
            }
        }

        // insert new one
        addDirInode(&parentNew, found, nameNew);
        writeInode(parentFoundNew, &parentNew);

        // update time
        parentOld.stat.st_modifyTime = time(NULL);
        parentOld.stat.st_changeTime = time(NULL);
        writeInode(parentFoundOld, &parentOld);
        parentNew.stat.st_modifyTime = time(NULL);
        parentNew.stat.st_changeTime = time(NULL);
        writeInode(parentFoundNew, &parentNew);
    }

	return 0;
}

int fs_write (const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
	printf("Write is called:%s\n",path);

    int writeMode = fi->flags;
    if(writeMode & O_RDONLY)
        return -EACCES;
    
    if(size > (8 << 20))
        return -EACCES;
    
    int found;
    int realWriteLen = 0;
    struct Inode inode = gotoInode(path, &found);
    int fileSize = -1;

    char * file = (char *)malloc(8 << 20);
    if(!file)
    {
        printf("Cannot malloc.\n");
        exit(5);
    }
    int * indir = (char *)malloc(BLOCK_SIZE);
    char * current = file;

    if(found >= 0)
    {
        printf("(Write) File (Inode %d) existed\n", found);
        // File exsited
        if(writeMode & O_EXCL)
        {
            if(writeMode & O_CREAT)
            {
                return -EEXIST;
            }
        }

        fileSize = inode.stat.st_size;
        
        if(writeMode & O_APPEND)
        {
            fileSize += size;
            if(writeMode & O_TRUNC)
            {
                fileSize = size;
                current = file;
                // Clear all original data blocks
                clearInodeAllDataBlocks(found);
                writeInodeData(&inode, buf, size);
            }
            else
            {
                // Append
                // TODO: finish offset != 0
                if(offset != 0)
                {
                    exit(4);
                }

                char * tempBlock = (char *)malloc(BLOCK_SIZE);
                // align
                int offsetAlign = inode.stat.st_size % BLOCK_SIZE;
                if(offsetAlign != 0)
                {
                    if(inode.blocks <= 14)
                    {
                        // dir pointer
                        readDataBlock(inode.dirPointer[inode.blocks - 1], tempBlock);
                        memcpy(tempBlock + offsetAlign, buf, BLOCK_SIZE - offsetAlign);
                        writeDataBlock(inode.dirPointer[inode.blocks - 1], tempBlock);
                        buf += BLOCK_SIZE - offsetAlign;
                        size -= BLOCK_SIZE - offsetAlign;
                    }
                    else
                    {
                        // indir pointer
                        if(inode.blocks <= 14 + 1024)
                        {
                            readDataBlock(inode.indirPointer[0], tempBlock);
                            int lastBlock = ((int *)tempBlock)[inode.blocks - 14 - 1];
                            readDataBlock(lastBlock, tempBlock);
                            memcpy(tempBlock + offsetAlign, buf, BLOCK_SIZE - offsetAlign);
                            writeDataBlock(lastBlock, tempBlock);
                            buf += BLOCK_SIZE - offsetAlign;
                            size -= BLOCK_SIZE - offsetAlign;
                        }
                        else
                        {
                            readDataBlock(inode.indirPointer[1], tempBlock);
                            int lastBlock = ((int *)tempBlock)[inode.blocks - 14 - 1 - 1024];
                            readDataBlock(lastBlock, tempBlock);
                            memcpy(tempBlock + offsetAlign, buf, BLOCK_SIZE - offsetAlign);
                            writeDataBlock(lastBlock, tempBlock);
                            buf += BLOCK_SIZE - offsetAlign;
                            size -= BLOCK_SIZE - offsetAlign;
                        }
                    }
                }
                writeInodeData(&inode, buf, size);
                free(tempBlock);
            }
        }
        else
        {
            fileSize = offset + size;
            inode.stat.st_size = fileSize;
            // Clear all original data blocks
//            clearInodeAllDataBlocks(found);
            writeInodeData(&inode, buf, size);
        }
    }
    else
    {
        // File not exsited
        printf("(Write) write file (%s) not existed\n", path);
        if(!(writeMode & O_CREAT))
        {
            return -ENOENT;
        }

        // Create an inode
        // find parent node
        int parentFound;
        char temp[256];
        char name[32];
        strcpy(temp, path);
        for(int i = strlen(temp) - 2; i >= 0; i--)
        {
            if(temp[i] == '/')
            {
                temp[i] = '\0';
                strcpy(name, temp + i + 1);
                break;
            }
        }
        if(strlen(temp) == 0)
        {
            temp[0] = '/';
            temp[1] = '\0';
        }

        struct Inode parent = gotoInode(temp, &parentFound);
        if(parentFound < 0 || parent.stat.st_mode != DIRMODE)
        {
            return -ENOENT;
        }

        int emptyOne = getEmptyInode();
        found = emptyOne;
        addDirInode(&parent, emptyOne, name);
        writeInode(parentFound, &parent);

        setInode(emptyOne);
        inode = getInode(emptyOne);
        initFileInode(&inode);
        fileSize = size;
        writeInodeData(&inode, buf, size);
    }

    // inode.stat.st_accessTime = time(NULL);
    inode.stat.st_changeTime = time(NULL);
    inode.stat.st_modifyTime = time(NULL);
    inode.stat.st_size = fileSize;
    writeInode(found, &inode);
    free(indir);
    free(file);

    realWriteLen = size;
	return realWriteLen;
}

int fs_truncate (const char *path, off_t size)
{
	printf("Truncate is called:%s\n",path);

    if(size != 0)
    {
        printf("Cannot truncate size not zero..\n");
        exit(6);
    }

    int found;
    struct Inode inode = gotoInode(path, &found);
    if(found < 0)
    {
        return -ENOENT;
    }

    clearInodeAllDataBlocks(found);
    
	return 0;
}

int fs_utime (const char *path, struct utimbuf *buffer)
{
	printf("Utime is called:%s\n",path);

    int found;
    struct Inode inode = gotoInode(path, &found);
    if(found < 0)
    {
        return -ENOENT;
    }
    
    inode.stat.st_accessTime = buffer->actime;
    inode.stat.st_modifyTime = buffer->modtime;
    inode.stat.st_changeTime = time(NULL);
    writeInode(found, &inode);

	return 0;
}

int fs_statfs (const char *path, struct statvfs *stat)
{
	printf("Statfs is called:%s\n",path);

    stat->f_bsize = BLOCK_SIZE;
    stat->f_blocks = BLOCK_NUM;

    int freeBlocks = 0;

    disk_read(1, buffer);
    char * map = (char *)buffer;
    for(int i = 0; i < BLOCK_SIZE; i++)
    {
        for(int j = 0; j < 8; j++)
        {
            if(!getNth(map[i], (char)j))
            {
                freeBlocks ++;
            }
        }
    }

    disk_read(2, buffer);
    for(int i = 0; i < BLOCK_SIZE; i++)
    {
        for(int j = 0; j < 8; j++)
        {
            if(!getNth(map[i], (char)j))
            {
                freeBlocks ++;
            }
        }
    }

    stat->f_bavail = freeBlocks;
    stat->f_bfree = freeBlocks;
    stat->f_namemax = 24;

    int freeNodes = 0;
    disk_read(0, buffer);
    map = (char *)buffer;
    for(int i = 0; i < BLOCK_SIZE; i++)
    {
        for(int j = 0; j < 8; j++)
        {
            if(!getNth(map[i], (char)j))
            {
                freeNodes ++;
            }
        }
    }

    stat->f_favail = freeNodes;
    stat->f_ffree = freeNodes;
    stat->f_files = 32768 - freeNodes;

	return 0;
}

int fs_open (const char *path, struct fuse_file_info *fi)
{
	printf("Open is called:%s\n",path);

    // TODO: what else need to do?
    int found;
    struct Inode node = gotoInode(path, &found);
    if(found < 0)
        return -ENOENT;
    if(node.stat.st_mode != REGMODE)
        return -ENOENT;
    
    // permissions check
    int permOffset = 2;
    if(getuid() != node.stat.st_uid)
    {
        permOffset = 1;
        if(getgid() != node.stat.st_gid)
        {
            permOffset = 0;
        }
    }
    permOffset *= 3;
    int mask = 0x7;
    int perm = (node.stat.st_mode >> permOffset) & mask;

    int readOK = (perm >> 2) & 1;
    int writeOK = (perm >> 1) & 1;
    int execOK = perm & 1;

    if(!readOK)
    {
        return -EACCES;
    }
    if(!writeOK)
    {
        if(fi->flags & O_WRONLY)
            return -EACCES;
        if(fi->flags & O_RDWR)
            return -EACCES;
        // Not sure how to deal with O_APPEND
        if(fi->flags & O_APPEND)
            return -EACCES;
    }
    
	return 0;
}

//Functions you don't actually need to modify
int fs_release (const char *path, struct fuse_file_info *fi)
{
	printf("Release is called:%s\n",path);
	return 0;
}

int fs_opendir (const char *path, struct fuse_file_info *fi)
{
	printf("Opendir is called:%s\n",path);
	return 0;
}

int fs_releasedir (const char * path, struct fuse_file_info *fi)
{
	printf("Releasedir is called:%s\n",path);
	return 0;
}

static struct fuse_operations fs_operations = {
	.getattr    = fs_getattr,
	.readdir    = fs_readdir,
	.read       = fs_read,
	.mkdir      = fs_mkdir,
	.rmdir      = fs_rmdir,
	.unlink     = fs_unlink,
	.rename     = fs_rename,
	.truncate   = fs_truncate,
	.utime      = fs_utime,
	.mknod      = fs_mknod,
	.write      = fs_write,
	.statfs     = fs_statfs,
	.open       = fs_open,
	.release    = fs_release,
	.opendir    = fs_opendir,
	.releasedir = fs_releasedir
};

int main(int argc, char *argv[])
{
	if(disk_init())
		{
		printf("Can't open virtual disk!\n");
		return -1;
		}
	if(mkfs())
		{
		printf("Mkfs failed!\n");
		return -2;
		}
    return fuse_main(argc, argv, &fs_operations, NULL);
}
