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

//To check data:
//*(fat+)
//*((struct JFile *)(data_blocks + BLOCK_SIZE*0 + sizeof(struct JFile)*))
//((char *)(data_blocks + 128*))

int create_jfs_image(char *name, char *inst_name, char *src_path, uint32_t block_size, uint32_t data_blocks_count)
{
    FILE *jfs_image;
    uint8_t *data_blocks, *system_data;
    uint32_t system_data_size, data_blocks_size;
    struct JSuper *sb;
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

    sb = (struct JSuper *)system_data;
    fat = (int32_t *)(system_data + sizeof(struct JSuper));
    data_blocks = system_data + system_data_size;

    ///init
    //superblock
    sb->block_size = block_size;
    sb->blocks_count = data_blocks_count;
    sb->system_bytes = system_data_size;
    sb->total_bytes = data_blocks_count*block_size + system_data_size;

    ///FAT
    for (int ii=0; ii<(int)(data_blocks_count-1); ii++)
    {
        fat[ii] = ii + 1;
    }
    fat[data_blocks_count-1] = -1; // -1 is for no next
    sb->first_free_block = 0;

    //Root
    //int32_t root_block = get_free_block(fat, sb);
    //strcpy(sb->root.name, inst_name);
    //sb->root.size = files_of_dir(name);
    //sb->root.first_data_block_idx = 0;
    //sb->root.flags = 1;
    sb->root.coord.my_jfile_block = -1;
    sb->root.coord.my_jfile_offset = 0;
    sb->root.coord.parent_jfile_block = -1;
    sb->root.coord.parent_jfile_offset = 0;

    ///fill
    int32_t ret = fill_jfs_image(src_path, fat, sb, data_blocks, &(sb->root), NULL);
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

    explore_image(jfs_get_root_dir(sb), sb);
    fat_dump(sb);

    printf("System data size: %d, JSuper block size: %lu, JFile size: %lu\n", system_data_size, sizeof(struct JSuper), sizeof(struct JFile));
    //hexdump(system_data, system_data_size);

    printf("\n------------------------------------------\n\n");
    struct JFile *file, *parent;
    jfs_read_dir(jfs_get_root_dir(sb), sb, 2, &file);
    jfs_read_dir(jfs_get_root_dir(sb), sb, 1, &parent);
    jfs_move_file(file, sb, parent);
    explore_image(jfs_get_root_dir(sb), sb);
    fat_dump(sb);

    /*for (int ii = 0; ii < 6; ii++)
    {
        printf("\n------------------------------------------\n\n");
        struct JFile *file;
        jfs_read_dir(jfs_get_root_dir(sb), sb, 0, &file);
        jfs_remove_file(file, sb);
        printf("\n------------------------------------------\n\n");
        explore_image(jfs_get_root_dir(sb), sb);
        fat_dump(sb);
    }*/

    free(system_data);
    fclose(jfs_image);
    return 0;
}

//TODO: Check unique, check correctness
int32_t write_file_name(char *path, struct JFile *meta)
{
    int32_t path_len = strlen(path);
    int32_t begin = 0, end = path_len - 1;

    /// get name
    for (int ii = path_len; ii >= 0; ii--)
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
    meta->name[end - begin + 1] = '\0';

    ///tolower
    for (int ii; ii<strlen(meta->name); ii++)
        meta->name[ii] = tolower(meta->name[ii]);

    //printf("%s\n",  meta->name);

    return 0;
}

int fill_jfs_image(char *path, int32_t *fat, struct JSuper *sb, uint8_t *data, struct JFile *meta, struct JCoord *parent)
{
    ///init metadata
    meta->size = 0;
    meta->first_data_block_idx = -1;
    meta->flags = 1;

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
        if (S_ISDIR(buf.st_mode)) ///Is directory
        {
            int ret = -1;
            //printf("handle dir:  '%s'\n", newp);
            struct JFile *new_dir = jfs_create_file(meta, sb, NULL, 1); //name will be filled later
            if (new_dir != NULL)
                ret = fill_jfs_image(newp, fat, sb, data, new_dir, &(meta->coord));
            if (NULL == new_dir || 0 != ret)
            {
                printf("Can't create new directory!\n");
                return -1;
            }
        }
        else if (S_ISREG(buf.st_mode)) ///Is file
        {
            //printf("handle file: '%s'\n", newp);
            struct JFile *new_file = jfs_create_file(meta, sb, NULL, 0);
            if (NULL == new_file)
            {
                printf("Can't create new file!\n");
                return -1;
            }

            write_file_name(newp, new_file);

            uint8_t *data = malloc(sb->block_size * sizeof(uint8_t));
            if (NULL == data)
            {
                printf("Can't alloc memory for file input!\n");
                return -1;
            }
            size_t ret_read;
            uint32_t was_written = 0;
            FILE *input_file = fopen(newp, "rb");
            if (NULL == input_file)
            {
                printf("Can't open data file!\n");
                free(data);
                return -1;
            }

            while ((ret_read = fread(data, sizeof(uint8_t), sb->block_size, input_file)))
            {
                int ret = jfs_write_file(new_file, sb, was_written, data, ret_read);//(new_file, sb, 0, NULL, 0);
                if (ret < 0)
                {
                    printf("Can't write file data!\n");
                    free(data);
                    return -1;
                }
                was_written += ret_read;
            }
            free(data);
        }
        else
        {
            continue;
        }
    }
    //printf("Add: %s\n", meta->name);
    return 0;
}

void explore_image(struct JFile *file, struct JSuper *sb)
{
    if (file->flags)
    {
        struct JFile *subdir;
        int32_t offset = 0;
        while (!jfs_read_dir(file, sb, offset, &subdir) && NULL != subdir)
        {
            printf("Name: %s, offset: %d, type: %d, size: %d\n", subdir->name, offset, subdir->flags, subdir->size);
            printf("Coord: my block %d, offset %d, parent block %d, offset %d\n",
                subdir->coord.my_jfile_block, subdir->coord.my_jfile_offset, subdir->coord.parent_jfile_block, subdir->coord.parent_jfile_offset);/**/
            explore_image(subdir, sb);
            offset++;
        }
    }
    else
    {
        uint8_t *read_data = malloc(file->size);
        if (NULL == read_data)
        {
            printf("Cant't read file!\n");
            return;
        }

        uint32_t read_count = 0;
        jfs_read_file(file, sb, 0, read_data, file->size, &read_count);
        if (0 == read_count)
        {
            printf("Nothing was read!\n");
        }
        else
        {
            printf("Blocks: ");
            int32_t block = file->first_data_block_idx;
            int32_t *fat = jfs_get_fat_ptr(sb);
            while (-1 != fat[block])
            {
                printf("%d, ", block);
                block = fat[block];
            }
            printf("%d\n", block);

            for (int32_t ii = 0; ii < file->size; ii++)
                printf("%c", read_data[ii]);
            printf("\n");
        }
        free(read_data);
    }

    return;
}

void fat_dump(struct JSuper *sb)
{
    int32_t *fat = jfs_get_fat_ptr(sb);

    printf("FAT:\n");
    printf("\t-1: %d\n", sb->first_free_block);
    for (int ii = 0; ii < 11/*sb->blocks_count*/; ii++)
        printf("\t%d: %d\n", ii, fat[ii]);
    return;
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
