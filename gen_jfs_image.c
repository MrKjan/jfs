#include "jfs.h"
#include "gen_jfs_image.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>

//TODO: Use inst_name

int create_jfs_image(char *name, char *inst_name, char *src_path, uint32_t block_size, uint32_t data_blocks_count)
{
    FILE *jfs_image;
    uint8_t *data_blocks, *system_data;
    uint32_t system_data_size, data_blocks_size;
    struct JSuper *super_block;
    int32_t *fat;

    size_t write_size;

    jfs_image = fopen(name, "wb");
    if (NULL == jfs_image)
    {
        printf("Can't create data file!\n");
        return -1;
    }

    ///alloc

    system_data_size = sizeof(struct JSuper) +
                       data_blocks_count * sizeof(uint32_t); //FAT
    data_blocks_size = data_blocks_count * block_size;

    system_data = (uint8_t *)calloc(system_data_size + data_blocks_size, sizeof(uint8_t));
    if (NULL == system_data)
    {
        printf("Can't alloc memory for jfs!\n");
        return -1;
    }

    super_block = (struct JSuper *)system_data;
    fat = (int32_t *)(system_data + sizeof(struct JSuper));
    data_blocks = system_data + system_data_size;

    ///init
    //superblock
    super_block->block_size = block_size;
    super_block->blocks_count = data_blocks_count;
    super_block->system_bytes = system_data_size;
    super_block->total_bytes = data_blocks_count*block_size + system_data_size;

    //FAT
    for (int ii=0; ii<(int)(data_blocks_count-1); ii++)
    {
        fat[ii] = ii + 1;
    }
    fat[data_blocks_count-1] = -1; // -1 is for no next
    super_block->first_free_block = 0;

    //Root
    //int32_t root_block = get_free_block(fat, super_block);
    //strcpy(super_block->root.name, inst_name);
    //super_block->root.size = files_of_dir(name);
    //super_block->root.first_data_block_idx = 0;
    //super_block->root.flags = 1;

    ///fill
    int32_t ret = fill_jfs_image(src_path, fat, super_block, data_blocks, &(super_block->root));
    if (0 != ret)
    {
        printf("Image cannot be created, see comments above!\n");
        free(system_data);
        return -1;
    }

    ///copy to file
    write_size = fwrite(system_data, sizeof(uint8_t), system_data_size, jfs_image);
    if (write_size < system_data_size * sizeof(uint8_t))
    {
        printf("Can't write system data to file!\n");
        free(system_data);
        return -1;
    }

    write_size = fwrite(data_blocks, sizeof(uint8_t), block_size * data_blocks_count, jfs_image);
    if (write_size < system_data_size * sizeof(uint8_t))
    {
        printf("Can't write blocks to file!\n");
        free(system_data);
        return -1;
    }

    struct JFile *subdir;
    int32_t offset = 0;

    while (!jfs_read_dir(jfs_get_root_dir(super_block), super_block, offset, &subdir) && NULL != subdir)
    {
        printf("Name: %s, offset: %lu, type: %d\n", subdir->name, offset, subdir->flags);
        offset++;
    }

    //printf("System data size: %d\nJSuper block size: %d\n", system_data_size, sizeof(struct JSuper));
    //hexdump(system_data, system_data_size);

    free(system_data);
    fclose(jfs_image);
    return 0;
}

int32_t write_file_name(char *path, struct JFile *meta)
{
    int32_t path_len = strlen(path);
    int32_t begin = 0, end = path_len - 1;

    /// get name
    for (int ii=path_len; ii >= 0; ii--)
    {
        if (path[ii] == '/')
        {
            if (ii == path_len - 1)
            {
                end--;
                continue;
            }
            else
            {
                begin = ii + 1;
                break;
            }
        }
    }

    ///check
    if (end - begin < 0)
        return -1;

    if (end - begin >= 64)
        return -2;

    ///copy
    for (int ii = begin; ii<=end; ii++)
    {
        meta->name[ii - begin] = path[ii];
    }
    meta->name[begin - end + 1] = '\0';

    ///tolower
    for (int ii; ii<strlen(meta->name); ii++)
        meta->name[ii] = tolower(meta->name[ii]);

    ///Check unique?
    //TODO

    //printf("%s\n",  meta->name);

    return 0;
}

int fill_jfs_image(char *path, int32_t *fat, struct JSuper *sb, uint8_t *data, struct JFile *meta)
{
    ///init metadata
    meta->size = 0;
    meta->first_data_block_idx = -1;
    meta->flags = 1;

    //for debug
    //int32_t fat[BLOCKS_CNT] = (int32_t *)(sb+1);

    int32_t ret = write_file_name(path, meta);
    if (ret < 0)
    {
        printf("Incorrect file name!\n");
        return -1;
    }

    ///explore content
    struct dirent *files;
    DIR *dp = opendir(path);
    if (NULL == dp)
    {
        printf("Cant open directory: %s", path);
        return -1;
    }

    while (NULL != (files = readdir(dp)))
    {
        char newp[1000];
        struct stat buf;
        if (!strcmp(files->d_name, ".") || !strcmp(files->d_name, ".."))
        {
            continue;
        }

        ///new path
        if (strlen(path) + 1 + strlen(files->d_name) >= 1000)
        {
            printf("Reach the end of the buffer size!\n");
            return -1;
        }
        strcpy(newp, path);
        strcat(newp, "/");
        strcat(newp, files->d_name);

        ///stat
        if (-1 == stat(newp, &buf))
        {
            perror("Can't get stat!");
            return -1;
        }

        ///process child
        if (S_ISDIR(buf.st_mode))
        {
            int ret = -1;
            printf("handle dir:  '%s'\n", newp);
            //jfs_create_file(meta, files->d_name, uint32_t size, int32_t first_data_block_idx, uint8_t flags)
            struct JFile *new_dir = jfs_create_file(meta, sb, NULL, 1); //name will be filled later
            if (new_dir != NULL)
                ret = fill_jfs_image(newp, fat, sb, data, new_dir);
            if (NULL == new_dir || 0 != ret)
            {
                printf("Can't create new directory!\n");
                return -1;
            }
        }
        else if (S_ISREG(buf.st_mode))
        {
            printf("handle file: '%s'\n", newp);
            struct JFile *new_file = jfs_create_file(meta, sb, NULL, 0);
            if (NULL == new_file)
            {
                printf("Can't create new file!\n");
                return -1;
            }
            write_file_name(newp, new_file);
        }
        else
        {
            continue;
        }
    }
    //printf("Add: %s\n", meta->name);
    return 0;
}

/*
inline uint32_t blocks_of_dir(uint32_t block_size, uint32_t files_cnt)
{
    return sizeof(struct JFile) * files_cnt / block_size +
           ((sizeof(struct JFile) * files_cnt) % block_size != 0);
}

uint32_t files_of_dir(char *path)
{
    struct dirent *files;
    DIR *dp = opendir(path);
    uint32_t ret = 0;

    while (NULL != (files = readdir(dp)))
    {
        char newp[1000];
        struct stat buf;
        if (!strcmp(files->d_name, ".") || !strcmp(files->d_name, ".."))
        {
            continue;
        }

        strcpy(newp, path);
        strcat(newp, "/");
        strcat(newp, files->d_name);

        if (-1 == stat(newp, &buf))
        {
            perror("Can't get stat!");
            return -1;
        }
        if (S_ISDIR(buf.st_mode) || S_ISREG(buf.st_mode))
            ret++;
    }

    return ret;
}

struct Dir_explore explore_dir(char *pth, uint32_t block_size)
{
    char path[1000];
    strcpy(path, pth);
    DIR *dp;
    struct dirent *files;
    struct Dir_explore ret;
    uint32_t dir_file_size, total = 0;

    ret.files = 0;
    ret.dirs = 0;
    ret.file_blocks = 0;
    ret.dir_blocks = 0;

    if (NULL == (dp = opendir(path)))
    {
        perror("Can't open directory!");
    }

    char newp[1000];
    struct stat buf;
    while (NULL != (files = readdir(dp)))
    {
        if (!strcmp(files->d_name, ".") || !strcmp(files->d_name, ".."))
        {
            continue;
        }

        strcpy(newp, path);
        strcat(newp, "/");
        strcat(newp, files->d_name);
        //~ printf("%s\n", newp);

        if (-1 == stat(newp, &buf))
        {
            perror("Can't get stat!");
        }

        if (S_ISDIR(buf.st_mode))
        {
            struct Dir_explore sub_ret;
            ret.dirs++;
            total++;

            // if directory, then add a "/" to current path
            strcat(path,"/");
            strcat(path,files->d_name);
            sub_ret = explore_dir(path, block_size);
            strcpy(path, pth);

            ret.files += sub_ret.files;
            ret.dirs += sub_ret.dirs;
            ret.file_blocks += sub_ret.file_blocks;
            ret.dir_blocks += sub_ret.dir_blocks;
        }
        else if (S_ISREG(buf.st_mode))
        {
            total++;
            ret.files++;
            ret.file_blocks += buf.st_size / block_size +
                               (buf.st_size % block_size != 0);
            //~ printf("size: %d\n", buf.st_size);
        }
        else
        {
            printf("Not a file!\n");
        }
    }

    //~ printf("total = %d, size = %d, blocks = %d\n", total,
                //~ dir_file_size, blocks_of_dir(block_size, total));
    ret.dir_blocks += blocks_of_dir(block_size, total);

    closedir(dp);
    return ret;
}

void hexdump(const void* addr, int len)
{
    int                  i;
    unsigned char        buff[17];
    const unsigned char* pc = (const unsigned char*) addr;

    if (len == 0)
    {
        printf("Len is zero\n");
        return;
    }

    // Process every byte in the data.
    for (i = 0; i < len; i++)
    {
        // Multiple of 16 means new line (with line offset).

        if ((i % 16) == 0)
        {
            // Just don't print ASCII for the zeroth line.
            if (i != 0)
            {
                printf("  %s\n", buff);
            }

            // Output the offset.
            printf("  %04x ", i);
        }

        // Now the hex code for the specific character.
        printf(" %02x", pc[i]);

        // And store a printable ASCII character for later.
        if ((pc[i] < 0x20) || (pc[i] > 0x7e))
        {
            buff[i % 16] = '.';
        }
        else
        {
            buff[i % 16] = pc[i];
        }
        buff[(i % 16) + 1] = '\0';
    }

    // Pad out last line if not exactly 16 characters.
    while ((i % 16) != 0)
    {
        printf("   ");
        i++;
    }

    // And print the final ASCII bit.
    printf("  %s\n", buff);
}
*/
