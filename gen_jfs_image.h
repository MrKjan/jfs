#ifndef __GEN_JFS_IMAGE_H__
#define __GEN_JFS_IMAGE_H__

#include <stdint.h>
#include "jfs.h"

#define BLOCK_SIZE 128
#define BLOCKS_CNT 20

struct Dir_explore
{
    uint16_t files;
    uint16_t dirs;
    uint16_t file_blocks;
    uint16_t dir_blocks;
};

//Should set up BLOCK_SIZE, BLOCKS_CNT instead of block_size, data_blocks_count
int create_jfs_image(char *file_name, char *inst_name, char *src_path, uint32_t block_size, uint32_t data_blocks_count);
struct Dir_explore explore_dir(char *pth, uint32_t block_size);
//void hexdump(const void* addr, int len);
uint32_t blocks_of_dir(uint32_t block_size, uint32_t files_cnt);
uint32_t files_of_dir(char *name);
int fill_jfs_image(char *path, int32_t *fat, struct JSuper *sb, uint8_t *data, struct JFile *meta);
int32_t write_file_name(char *path, struct JFile *meta);
void explore_image(struct JFile *dir, struct JSuper *sb);
#endif

