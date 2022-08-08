#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fuse.h>
#include <errno.h>

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
	time_t  st_changeime;//状态改变时间
};

struct Inode
{
    struct FileStat stat;
    int blocks;
    int flags;
    int dirPointer [14];
    int indirPointer [2];
};

int main()
{
    printf("%ld\n", sizeof(struct Inode));
}