/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 *
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>
#include <pthread.h>

#include "block.h"
#include "rufs.h"

char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here
#define INODE_SIZE 256
#define INODE_ENTRIES (BLOCK_SIZE / INODE_SIZE)
#define INODE_BLOCKS (MAX_INUM / INODE_ENTRIES)
#define DIRENT_SIZE 256
#define DIR_ENTRIES (BLOCK_SIZE / DIRENT_SIZE)
#define DP_ENTRIES 16
#define IP_ENTRIES 8
#define POINTER_SIZE 4
#define INDIRECT_ENTRIES (BLOCK_SIZE / POINTER_SIZE)

struct superblock superblock;
unsigned char inode_bitmap[BLOCK_SIZE];
unsigned char data_bitmap[BLOCK_SIZE];
struct inode curr_iblock[INODE_ENTRIES];
struct dirent curr_dirblock[DIR_ENTRIES];
char curr_fileblock[BLOCK_SIZE];
int curr_sipblock[INDIRECT_ENTRIES];
int curr_dipblock[INDIRECT_ENTRIES];
struct inode curr_inode;
struct dirent curr_dirent;
int mblocks_used = 0;
double iblocks_used = 0;
int dblocks_used = 0;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {

	// Step 1: Read inode bitmap from disk
	// Step 2: Traverse inode bitmap to find an available slot
	// Step 3: Update inode bitmap and write to disk 

	// printf("get_avail_ino START\n");
	bio_read(superblock.i_bitmap_blk, &inode_bitmap);
	for (int i = 0; i < MAX_INUM / 8; i++) {
        if (inode_bitmap[i] == 0xFF) {
            continue;
        }
        for (int j = 0; j < 8; j++) {
            if (!(inode_bitmap[i] & (1 << j))) {
				int available = i * 8 + j;
				set_bitmap(inode_bitmap, available);
				iblocks_used += 1.0 / INODE_ENTRIES;
				bio_write(superblock.i_bitmap_blk, &inode_bitmap);
				// printf("get_avail_ino SUCCESS: ino=%d\n", available);
                return available;
            }
        }
    }
	// printf("get_avail_ino FAIL\n");
	return -1;
}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {

	// Step 1: Read data block bitmap from disk
	// Step 2: Traverse data block bitmap to find an available slot
	// Step 3: Update data block bitmap and write to disk 

	// printf("get_avail_blkno START\n");
	bio_read(superblock.d_bitmap_blk, &data_bitmap);
	for (int i = 0; i < MAX_DNUM / 8; i++) {
        if (data_bitmap[i] == 0xFF) {
            continue;
        }
        for (int j = 0; j < 8; j++) {
            if (!(data_bitmap[i] & (1 << j))) {
				int available = i * 8 + j;
				set_bitmap(data_bitmap, available);
				dblocks_used++;
				bio_write(superblock.d_bitmap_blk, &data_bitmap);
				// printf("get_avail_blkno SUCCESS: blkno=%d\n", available);
                return available;
            }
        }
    }
	// printf("get_avail_blkno FAIL\n");
	return -1;
}

int release_ino(uint16_t ino) {
	// printf("release_ino START: ino=%hu\n", ino);
	bio_read(superblock.i_bitmap_blk, &inode_bitmap);
	unset_bitmap(inode_bitmap, ino);
	iblocks_used -= 1.0 / INODE_ENTRIES;
	bio_write(superblock.i_bitmap_blk, &inode_bitmap);
	// printf("release_ino SUCCESS\n");
	return 0;
}

int release_blkno(int blkno) {
	// printf("release_blkno START: blkno=%d\n", blkno);
	bio_read(superblock.d_bitmap_blk, &data_bitmap);
	unset_bitmap(data_bitmap, blkno);
	dblocks_used--;
	bio_write(superblock.d_bitmap_blk, &data_bitmap);
	// printf("release_blkno SUCCESS\n");
	return 0;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {

  // Step 1: Get the inode's on-disk block number
  // Step 2: Get offset of the inode in the inode on-disk block
  // Step 3: Read the block from disk and then copy into inode structure

	// printf("readi START: ino=%hu\n", ino);
	int ino_blk = ino / INODE_ENTRIES;
	int ino_ofs = ino % INODE_ENTRIES;
	bio_read(superblock.i_start_blk + ino_blk, &curr_iblock);
	memcpy(inode, &curr_iblock[ino_ofs], sizeof(struct inode));
	// printf("readi SUCCESS\n");
	return 0;
}

int writei(uint16_t ino, struct inode *inode) {

	// Step 1: Get the block number where this inode resides on disk
	// Step 2: Get the offset in the block where this inode resides on disk
	// Step 3: Write inode to disk 

	// printf("writei START: ino=%hu\n", ino);
	int ino_blk = ino / INODE_ENTRIES;
	int ino_ofs = ino % INODE_ENTRIES;
	bio_read(superblock.i_start_blk + ino_blk, &curr_iblock);
	curr_iblock[ino_ofs] = *inode;
	bio_write(superblock.i_start_blk + ino_blk, &curr_iblock);
	// printf("writei SUCCESS\n");
	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {

  // Step 1: Call readi() to get the inode using ino (inode number of current directory)
  // Step 2: Get data block of current directory from inode
  // Step 3: Read directory's data block and check each directory entry.
  //If the name matches, then copy directory entry to dirent structure

	// printf("dir_find START: ino=%hu fname=%s name_len=%zu\n", ino, fname, name_len);
	readi(ino, &curr_inode);

	for (int i = 0; i < DP_ENTRIES; i++) {
		if (curr_inode.direct_ptr[i] == 0) {
			break;
		}
		bio_read(curr_inode.direct_ptr[i], curr_dirblock);
		for (int j = 0; j < DIR_ENTRIES; j++) {
			curr_dirent = curr_dirblock[j];
			if (curr_dirent.valid == 1 && curr_dirent.len == name_len && strcmp(curr_dirent.name, fname) == 0) {
				memcpy(dirent, &curr_dirent, sizeof(struct dirent));
				// printf("dir_find SUCCESS: ino=%hu\n", curr_dirent.ino);
				return 0;
			}
		}
		
	}
	// printf("dir_find FAIL\n");
	return -1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	// Step 2: Check if fname (directory name) is already used in other entries
	// Step 3: Add directory entry in dir_inode's data block and write to disk
	// Allocate a new data block for this directory if it does not exist
	// Update directory inode
	// Write directory entry

	// printf("dir_add START: d_ino=%hu f_ino=%hu fname=%s name_len=%zu\n", dir_inode.ino, f_ino, fname, name_len);
	int need_allocation = 0;
	int min_avail_i = -1;
	int min_avail_j = -1;
	for (int i = 0; i < DP_ENTRIES; i++) {
		if (dir_inode.direct_ptr[i] == 0) {
			if (min_avail_i == -1 && min_avail_j == -1) {
				need_allocation = 1;
				min_avail_i = i;
				min_avail_j = 0;
			}
			break;
		}
		bio_read(dir_inode.direct_ptr[i], curr_dirblock);
		for (int j = 0; j < DIR_ENTRIES; j++) {
			curr_dirent = curr_dirblock[j];
			if (curr_dirent.valid == 1 && curr_dirent.len == name_len && strcmp(curr_dirent.name, fname) == 0) {
				// printf("dir_add FAIL\n");
				return -1;
			}
			if (curr_dirent.valid == 0 && min_avail_i == -1 && min_avail_j == -1) {
				min_avail_i = i;
				min_avail_j = j;
			}
		}
	}

	memset(&curr_dirent, 0, sizeof(struct dirent));
	curr_dirent.ino = f_ino;
	curr_dirent.valid = 1;
	strcpy(curr_dirent.name, fname);
	curr_dirent.len = name_len;

	if (need_allocation == 1) {
		dir_inode.direct_ptr[min_avail_i] = superblock.d_start_blk + get_avail_blkno();
		memset(curr_dirblock, 0, DIR_ENTRIES * sizeof(struct dirent));
		bio_write(dir_inode.direct_ptr[min_avail_i], curr_dirblock);
	}

	bio_read(dir_inode.direct_ptr[min_avail_i], curr_dirblock);
	curr_dirblock[min_avail_j] = curr_dirent;
	bio_write(dir_inode.direct_ptr[min_avail_i], curr_dirblock);
	
	dir_inode.size += DIRENT_SIZE;
	dir_inode.link += 1;
	time(&dir_inode.vstat.st_mtime);
	time(&dir_inode.vstat.st_atime);
	writei(dir_inode.ino, &dir_inode);
	curr_inode = dir_inode;
	// printf("dir_add SUCCESS\n");
	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	// Step 2: Check if fname exist
	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	// printf("dir_remove START: d_ino=%hu fname=%s name_len=%zu\n", dir_inode.ino, fname, name_len);
	for (int i = 0; i < DP_ENTRIES; i++) {
		if (dir_inode.direct_ptr[i] == 0) {
			break;
		}
		bio_read(dir_inode.direct_ptr[i], curr_dirblock);
		for (int j = 0; j < DIR_ENTRIES; j++) {
			curr_dirent = curr_dirblock[j];
			if (curr_dirent.valid == 1 && curr_dirent.len == name_len && strcmp(curr_dirent.name, fname) == 0) {
				memset(&curr_dirblock[j], 0, sizeof(struct dirent));
				bio_write(dir_inode.direct_ptr[i], curr_dirblock);

				dir_inode.size -= DIRENT_SIZE;
				dir_inode.link -= 1;
				time(&dir_inode.vstat.st_mtime);
				time(&dir_inode.vstat.st_atime);
				writei(dir_inode.ino, &dir_inode);
				curr_inode = dir_inode;
				// printf("dir_remove SUCCESS\n");
				return 0;
			}
		}
	}
	// printf("dir_remove FAIL\n");
	return -1;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

	// printf("get_node_by_path START: path=%s ino=%hu\n", path, ino);
	char* delim = "/";
	char* token;
	token = strtok(path, delim);

	while (token != NULL) {
		if (dir_find(ino, token, strlen(token), &curr_dirent) != 0) {
			// printf("get_node_by_path FAIL\n");
			return -2;
		}
		ino = curr_dirent.ino;
		token = strtok(NULL, delim);
	}

	readi(ino, inode);
	// printf("get_node_by_path SUCCESS: ino=%hu\n", ino);
	return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {

	// Call dev_init() to initialize (Create) Diskfile
	// write superblock information
	// initialize inode bitmap
	// initialize data block bitmap
	// update bitmap information for root directory
	// update inode for root directory

	printf("rufs_mkfs START\n");
	dev_init(diskfile_path);

	memset(&superblock, 0, sizeof(struct superblock));
	superblock.max_inum = MAX_INUM;
	superblock.max_dnum = MAX_DNUM;
	superblock.i_bitmap_blk = 1;
	superblock.d_bitmap_blk = 2;
	superblock.i_start_blk = 3;
	superblock.d_start_blk = 3 + INODE_BLOCKS;
	bio_write(0, &superblock);
	mblocks_used++;

	memset(inode_bitmap, 0, MAX_INUM / 8);
	bio_write(superblock.i_bitmap_blk, inode_bitmap);
	mblocks_used++;

	memset(data_bitmap, 0, MAX_DNUM / 8);
	bio_write(superblock.d_bitmap_blk, data_bitmap);
	mblocks_used++;

	memset(&curr_inode, 0, sizeof(struct inode));
	curr_inode.ino = get_avail_ino();
	curr_inode.valid = 1;
	curr_inode.size = 0;
	curr_inode.type = S_IFDIR | 0755;
	curr_inode.link = 0;
	time(&curr_inode.vstat.st_mtime);
	time(&curr_inode.vstat.st_atime);
	writei(curr_inode.ino, &curr_inode);

	dir_add(curr_inode, curr_inode.ino, ".", 1);
	printf("rufs_mkfs SUCCESS\n");
	return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs
  // Step 1b: If disk file is found, just initialize in-memory data structures
  // and read superblock from disk

	printf("rufs_init START\n");
    pthread_mutex_lock(&mutex);
	if (dev_open(diskfile_path) >= 0) {
		bio_read(0, &superblock);
	} else {
		rufs_mkfs();
	}
    pthread_mutex_unlock(&mutex);
	printf("rufs_init SUCCESS\n");
	return NULL;
}

static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	// Step 2: Close diskfile

	printf("rufs_destroy START\n");
    pthread_mutex_destroy(&mutex);
	dev_close();
	printf("rufs_destroy SUCCESS\n");
}

static int rufs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path
	// Step 2: fill attribute of file into stbuf from inode

	printf("rufs_getattr START: path=%s\n", path);
    pthread_mutex_lock(&mutex);
	if (get_node_by_path(path, 0, &curr_inode) != 0) {
    	pthread_mutex_unlock(&mutex);
		printf("rufs_getattr FAIL\n");
		return -2;
	};
	stbuf->st_uid = getuid();
	stbuf->st_gid = getgid();
	stbuf->st_nlink = curr_inode.link;
	stbuf->st_size = curr_inode.size;
	stbuf->st_mtime = curr_inode.vstat.st_mtime;
	stbuf->st_atime = curr_inode.vstat.st_atime;
	stbuf->st_mode = curr_inode.type;
    pthread_mutex_unlock(&mutex);
	printf("rufs_getattr SUCCESS: m=%d i=%f d=%d\n", mblocks_used, iblocks_used, dblocks_used);
	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	// Step 2: If not find, return -1

	printf("rufs_opendir START: path=%s\n", path); // linux cd
    pthread_mutex_lock(&mutex);
	if (get_node_by_path(path, 0, &curr_inode) != 0) {
    	pthread_mutex_unlock(&mutex);
		printf("rufs_opendir FAIL\n");
		return -2;
	};
    pthread_mutex_unlock(&mutex);
	printf("rufs_opendir SUCCESS\n");
    return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	// Step 2: Read directory entries from its data blocks, and copy them to filler

	printf("rufs_readdir START: path=%s\n", path); // linux ls
    pthread_mutex_lock(&mutex);
	if (get_node_by_path(path, 0, &curr_inode) != 0) {
    	pthread_mutex_unlock(&mutex);
		printf("rufs_readdir FAIL\n");
		return -2;
	};

	for (int i = 0; i < DP_ENTRIES; i++) {
		if (curr_inode.direct_ptr[i] == 0) {
			break;
		}
		bio_read(curr_inode.direct_ptr[i], curr_dirblock);
		for (int j = 0; j < DIR_ENTRIES; j++) {
			curr_dirent = curr_dirblock[j];
			if (curr_dirent.valid == 1) {
				filler(buffer, curr_dirent.name, NULL, 0);
			}
		}
	}
    pthread_mutex_unlock(&mutex);
	printf("rufs_readdir SUCCESS\n");
	return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	// Step 2: Call get_node_by_path() to get inode of parent directory
	// Step 3: Call get_avail_ino() to get an available inode number
	// Step 4: Call dir_add() to add directory entry of target directory to parent directory
	// Step 5: Update inode for target directory
	// Step 6: Call writei() to write inode to disk

	printf("rufs_mkdir START: path=%s\n", path); // linux mkdir
    pthread_mutex_lock(&mutex);
	char* dir = dirname(strdup(path));
	char* base = basename(strdup(path));
	if (get_node_by_path(dir, 0, &curr_inode) != 0) {
    	pthread_mutex_unlock(&mutex);
		printf("rufs_mkdir FAIL\n");
		return -2;
	};
	uint16_t child_ino = get_avail_ino();
	dir_add(curr_inode, child_ino, base, strlen(base));
	uint16_t parent_ino = curr_inode.ino;

	memset(&curr_inode, 0, sizeof(struct inode));
	curr_inode.ino = child_ino;
	curr_inode.valid = 1;
	curr_inode.size = 0;
	curr_inode.type = S_IFDIR | mode;
	curr_inode.link = 0;
	time(&curr_inode.vstat.st_mtime);
	time(&curr_inode.vstat.st_atime);
	writei(curr_inode.ino, &curr_inode);

	dir_add(curr_inode, child_ino, ".", 1);
	dir_add(curr_inode, parent_ino, "..", 2);
    pthread_mutex_unlock(&mutex);
	printf("rufs_mkdir SUCCESS\n");
	return 0;
}

static int rufs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name
	// Step 2: Call get_node_by_path() to get inode of target directory
	// Step 3: Clear data block bitmap of target directory
	// Step 4: Clear inode bitmap and its data block
	// Step 5: Call get_node_by_path() to get inode of parent directory
	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

	printf("rufs_rmdir START: path=%s\n", path); // linux rmdir
    pthread_mutex_lock(&mutex);
	char* dir = dirname(strdup(path));
	char* base = basename(strdup(path));
	if (get_node_by_path(path, 0, &curr_inode) != 0) {
    	pthread_mutex_unlock(&mutex);
		printf("rufs_rmdir FAIL\n");
		return -2;
	};

	for (int i = 0; i < DP_ENTRIES; i++) {
		if (curr_inode.direct_ptr[i] == 0) {
			break;
		}
		release_blkno(curr_inode.direct_ptr[i] - superblock.d_start_blk);
		memset(&curr_dirblock, 0, DIR_ENTRIES * sizeof(struct dirent));
		bio_write(curr_inode.direct_ptr[i], curr_dirblock);
		curr_inode.direct_ptr[i] = 0;
	}

	release_ino(curr_inode.ino);

	if (get_node_by_path(dir, 0, &curr_inode) != 0) {
    	pthread_mutex_unlock(&mutex);
		printf("rufs_rmdir FAIL\n");
		return -2;
	};
	dir_remove(curr_inode, base, strlen(base));
    pthread_mutex_unlock(&mutex);
	printf("rufs_rmdir SUCCESS\n");
	return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	// Step 2: Call get_node_by_path() to get inode of parent directory
	// Step 3: Call get_avail_ino() to get an available inode number
	// Step 4: Call dir_add() to add directory entry of target file to parent directory
	// Step 5: Update inode for target file
	// Step 6: Call writei() to write inode to disk

	printf("rufs_create START: path=%s\n", path); // linux touch
    pthread_mutex_lock(&mutex);
	char* dir = dirname(strdup(path));
	char* base = basename(strdup(path));
	if (get_node_by_path(dir, 0, &curr_inode) != 0) {
    	pthread_mutex_unlock(&mutex);
		printf("rufs_create FAIL\n");
		return -2;
	};
	uint16_t child_ino = get_avail_ino();
	dir_add(curr_inode, child_ino, base, strlen(base));

	memset(&curr_inode, 0, sizeof(struct inode));
	curr_inode.ino = child_ino;
	curr_inode.valid = 1;
	curr_inode.size = 0;
	curr_inode.type = S_IFREG | mode;
	curr_inode.link = 1;
	time(&curr_inode.vstat.st_mtime);
	time(&curr_inode.vstat.st_atime);
	writei(curr_inode.ino, &curr_inode);
	printf("rufs_create SUCCESS\n");
    pthread_mutex_unlock(&mutex);
	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path
	// Step 2: If not find, return -1

	printf("rufs_open START: path=%s\n", path); // linux cat
    pthread_mutex_lock(&mutex);
	if (get_node_by_path(path, 0, &curr_inode) != 0) {
    	pthread_mutex_unlock(&mutex);
		printf("rufs_open FAIL\n");
		return -2;
	};
    pthread_mutex_unlock(&mutex);
	printf("rufs_open SUCCESS\n");
	return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path
	// Step 2: Based on size and offset, read its data blocks from disk
	// Step 3: copy the correct amount of data from offset to buffer
	// Note: this function should return the amount of bytes you copied to buffer

	printf("rufs_read START: path=%s size=%zu offset=%ld\n", path, size, offset); // linux cat
    pthread_mutex_lock(&mutex);
	if (get_node_by_path(path, 0, &curr_inode) != 0) {
    	pthread_mutex_unlock(&mutex);
		printf("rufs_read FAIL\n");
		return -2;
	};

	int start_block = offset / BLOCK_SIZE;
	int min_totalbytes = (curr_inode.size < offset + size) ? curr_inode.size : (offset + size);
    int num_blocks = (min_totalbytes + BLOCK_SIZE - 1) / BLOCK_SIZE;
	int curr_blkno;
    char* curr_faddr;
    char* curr_buffer = buffer;
    int curr_size = size;
	int curr_offset = offset % BLOCK_SIZE;
    for (int i = start_block; i < num_blocks; i++) {
		if (i < DP_ENTRIES) {
			curr_blkno = curr_inode.direct_ptr[i];
		} else if (i < DP_ENTRIES + INDIRECT_ENTRIES) {
			bio_read(curr_inode.indirect_ptr[0], curr_sipblock);
			int single_idx = i - DP_ENTRIES;
			curr_blkno = curr_sipblock[single_idx];
		} else if (i < DP_ENTRIES + INDIRECT_ENTRIES + INDIRECT_ENTRIES * INDIRECT_ENTRIES) {
			bio_read(curr_inode.indirect_ptr[1], curr_sipblock);
			int single_idx = (i - DP_ENTRIES - INDIRECT_ENTRIES) / INDIRECT_ENTRIES;
			bio_read(curr_sipblock[single_idx], curr_dipblock);
			int double_idx = (i - DP_ENTRIES - INDIRECT_ENTRIES) % INDIRECT_ENTRIES;
			curr_blkno = curr_dipblock[double_idx];
		}
		
		bio_read(curr_blkno, curr_fileblock);
    	curr_faddr = curr_fileblock + curr_offset;

        int bytes_to_copy = (curr_size > BLOCK_SIZE - curr_offset) ? (BLOCK_SIZE - curr_offset) : curr_size;
        memcpy(curr_buffer, curr_faddr, bytes_to_copy);

        curr_buffer += bytes_to_copy;
        curr_size -= bytes_to_copy;
        curr_offset = 0;
    }

	int bytes_copied = size - curr_size;
    pthread_mutex_unlock(&mutex);
	printf("rufs_read SUCCESS: bytes=%d\n", bytes_copied);
	return bytes_copied;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path
	// Step 2: Based on size and offset, read its data blocks from disk
	// Step 3: Write the correct amount of data from offset to disk
	// Step 4: Update the inode info and write it to disk
	// Note: this function should return the amount of bytes you write to disk

	printf("rufs_write START: path=%s size=%zu offset=%ld\n", path, size, offset); // linux echo >
    pthread_mutex_lock(&mutex);
	if (get_node_by_path(path, 0, &curr_inode) != 0) {
    	pthread_mutex_unlock(&mutex);
		printf("rufs_write FAIL\n");
		return -2;
	};

    int start_block = offset / BLOCK_SIZE;
    int num_blocks = (offset + size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	int curr_blkno;
    char* curr_faddr;
    char* curr_buffer = buffer;
    int curr_size = size;
	int curr_offset = offset % BLOCK_SIZE;
    for (int i = start_block; i < num_blocks; i++) {
		if (i < DP_ENTRIES) {
			if (curr_inode.direct_ptr[i] == 0) {
				curr_inode.direct_ptr[i] = superblock.d_start_blk + get_avail_blkno();
				memset(curr_fileblock, 0, BLOCK_SIZE);
				bio_write(curr_inode.direct_ptr[i], curr_fileblock);
			}
			curr_blkno = curr_inode.direct_ptr[i];
		} else if (i < DP_ENTRIES + INDIRECT_ENTRIES) {
			if (curr_inode.indirect_ptr[0] == 0) {
				curr_inode.indirect_ptr[0] = superblock.d_start_blk + get_avail_blkno();
				memset(curr_sipblock, 0, INDIRECT_ENTRIES * sizeof(int));
				bio_write(curr_inode.indirect_ptr[0], curr_sipblock);
			}
			bio_read(curr_inode.indirect_ptr[0], curr_sipblock);
			int single_idx = i - DP_ENTRIES;
			if (curr_sipblock[single_idx] == 0) {
				curr_sipblock[single_idx] = superblock.d_start_blk + get_avail_blkno();
				bio_write(curr_inode.indirect_ptr[0], curr_sipblock);
				memset(curr_fileblock, 0, BLOCK_SIZE);
				bio_write(curr_sipblock[single_idx], curr_fileblock);
			}
			curr_blkno = curr_sipblock[single_idx];
		} else if (i < DP_ENTRIES + INDIRECT_ENTRIES + INDIRECT_ENTRIES * INDIRECT_ENTRIES) {
			if (curr_inode.indirect_ptr[1] == 0) {
				curr_inode.indirect_ptr[1] = superblock.d_start_blk + get_avail_blkno();
				memset(curr_sipblock, 0, INDIRECT_ENTRIES * sizeof(int));
				bio_write(curr_inode.indirect_ptr[1], curr_sipblock);
			}
			bio_read(curr_inode.indirect_ptr[1], curr_sipblock);
			int single_idx = (i - DP_ENTRIES - INDIRECT_ENTRIES) / INDIRECT_ENTRIES;
			if (curr_sipblock[single_idx] == 0) {
				curr_sipblock[single_idx] = superblock.d_start_blk + get_avail_blkno();
				bio_write(curr_inode.indirect_ptr[1], curr_sipblock);
				memset(curr_dipblock, 0, INDIRECT_ENTRIES * sizeof(int));
				bio_write(curr_sipblock[single_idx], curr_dipblock);
			}
			bio_read(curr_sipblock[single_idx], curr_dipblock);
			int double_idx = (i - DP_ENTRIES - INDIRECT_ENTRIES) % INDIRECT_ENTRIES;
			if (curr_dipblock[double_idx] == 0) {
				curr_dipblock[double_idx] = superblock.d_start_blk + get_avail_blkno();
				bio_write(curr_sipblock[single_idx], curr_dipblock);
				memset(curr_fileblock, 0, BLOCK_SIZE);
				bio_write(curr_dipblock[double_idx], curr_fileblock);
			}
			curr_blkno = curr_dipblock[double_idx];
		}

		bio_read(curr_blkno, curr_fileblock);
    	curr_faddr = curr_fileblock + curr_offset;

        int bytes_to_copy = (curr_size > BLOCK_SIZE - curr_offset) ? (BLOCK_SIZE - curr_offset) : curr_size;
        memcpy(curr_faddr, curr_buffer, bytes_to_copy);
		bio_write(curr_blkno, curr_fileblock);

        curr_buffer += bytes_to_copy;
        curr_size -= bytes_to_copy;
        curr_offset = 0;
    }

	int bytes_copied = size - curr_size;
	curr_inode.size = (offset + bytes_copied > curr_inode.size) ? (offset + bytes_copied) : curr_inode.size;
	time(&curr_inode.vstat.st_mtime);
	time(&curr_inode.vstat.st_atime);
	writei(curr_inode.ino, &curr_inode);
    pthread_mutex_unlock(&mutex);
	printf("rufs_write SUCCESS: bytes=%d size=%u\n", bytes_copied, curr_inode.size);
	return bytes_copied;
}

static int rufs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name
	// Step 2: Call get_node_by_path() to get inode of target file
	// Step 3: Clear data block bitmap of target file
	// Step 4: Clear inode bitmap and its data block
	// Step 5: Call get_node_by_path() to get inode of parent directory
	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

	printf("rufs_unlink START: path=%s\n", path); // linux rm
    pthread_mutex_lock(&mutex);
	char* dir = dirname(strdup(path));
	char* base = basename(strdup(path));
	if (get_node_by_path(path, 0, &curr_inode) != 0) {
    	pthread_mutex_unlock(&mutex);
		printf("rufs_unlink FAIL\n");
		return -2;
	};

    int num_blocks = (curr_inode.size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	for (int i = 0; i < num_blocks; i++) {
		if (i < DP_ENTRIES) {
			release_blkno(curr_inode.direct_ptr[i] - superblock.d_start_blk);
			memset(curr_fileblock, 0, BLOCK_SIZE);
			bio_write(curr_inode.direct_ptr[i], curr_fileblock);
			curr_inode.direct_ptr[i] = 0;
		} else if (i < DP_ENTRIES + INDIRECT_ENTRIES) {
			bio_read(curr_inode.indirect_ptr[0], curr_sipblock);
			int single_idx = i - DP_ENTRIES;
			release_blkno(curr_sipblock[single_idx] - superblock.d_start_blk);
			memset(curr_fileblock, 0, BLOCK_SIZE);
			bio_write(curr_sipblock[single_idx], curr_fileblock);
			curr_sipblock[single_idx] = 0;
			if ((i + 1 == num_blocks) || (i + 1 == DP_ENTRIES + INDIRECT_ENTRIES)) {
				release_blkno(curr_inode.indirect_ptr[0] - superblock.d_start_blk);
				memset(curr_sipblock, 0, INDIRECT_ENTRIES * sizeof(int));
				bio_write(curr_inode.indirect_ptr[0], curr_sipblock);
				curr_inode.indirect_ptr[0] = 0;
			}
		} else if (i < DP_ENTRIES + INDIRECT_ENTRIES + INDIRECT_ENTRIES * INDIRECT_ENTRIES) {
			bio_read(curr_inode.indirect_ptr[1], curr_sipblock);
			int single_idx = (i - DP_ENTRIES - INDIRECT_ENTRIES) / INDIRECT_ENTRIES;
			bio_read(curr_sipblock[single_idx], curr_dipblock);
			int double_idx = (i - DP_ENTRIES - INDIRECT_ENTRIES) % INDIRECT_ENTRIES;
			release_blkno(curr_dipblock[double_idx] - superblock.d_start_blk);
			memset(curr_fileblock, 0, BLOCK_SIZE);
			bio_write(curr_dipblock[double_idx], curr_fileblock);
			curr_dipblock[double_idx] = 0;
			if ((i + 1 == num_blocks) || ((i + 1 - DP_ENTRIES - INDIRECT_ENTRIES) % INDIRECT_ENTRIES == 0)) {
				release_blkno(curr_sipblock[single_idx] - superblock.d_start_blk);
				memset(curr_dipblock, 0, INDIRECT_ENTRIES * sizeof(int));
				bio_write(curr_sipblock[single_idx], curr_dipblock);
				curr_sipblock[single_idx] = 0;
			}
			if ((i + 1 == num_blocks) || (i + 1 == DP_ENTRIES + INDIRECT_ENTRIES + INDIRECT_ENTRIES * INDIRECT_ENTRIES)) {
				release_blkno(curr_inode.indirect_ptr[1] - superblock.d_start_blk);
				memset(curr_sipblock, 0, INDIRECT_ENTRIES * sizeof(int));
				bio_write(curr_inode.indirect_ptr[1], curr_sipblock);
				curr_inode.indirect_ptr[1] = 0;
			}
		}
	}

	release_ino(curr_inode.ino);

	if (get_node_by_path(dir, 0, &curr_inode) != 0) {
    	pthread_mutex_unlock(&mutex);
		printf("rufs_unlink FAIL\n");
		return -2;
	};
	dir_remove(curr_inode, base, strlen(base));
    pthread_mutex_unlock(&mutex);
	printf("rufs_unlink SUCCESS\n");
	return 0;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}

