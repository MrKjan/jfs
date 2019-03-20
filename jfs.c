#include "jfs.h"
#include <stdio.h>
#include <string.h>

int32_t jfs_get_free_block(int32_t *fat, struct JSuper *sb)
{
    int32_t ret = sb->first_free_block;

    if (0 <= ret)
        sb->first_free_block = fat[ret];

    return ret;
}

void jfs_return_free_block(int32_t *fat, struct JSuper *sb, int32_t free_block)
{
    fat[free_block] = sb->first_free_block;
    sb->first_free_block = free_block;
}

inline int32_t *jfs_get_fat_ptr(struct JSuper *sb)
{
    return (int32_t *)(sb + 1);
}

inline uint8_t *jfs_get_data_ptr(struct JSuper *sb)
{
    return (uint8_t *)(sb) + sb->system_bytes;
}

inline uint8_t *jfs_block_idx_to_ptr(int32_t block_idx, struct JSuper *sb)
{
    return jfs_get_data_ptr(sb) + block_idx * sb->block_size;
}

inline int32_t jfs_files_fit_in_block(struct JSuper *sb)
{
    return sb->block_size / sizeof(struct JFile);
}

void jfs_add_new_block(struct JFile *file, struct JSuper *sb, int32_t new_block_idx)
{
    int32_t *fat = jfs_get_fat_ptr(sb);
    int32_t last_file_block = file->first_data_block_idx;

    if (-1 == file->first_data_block_idx)
    {
        file->first_data_block_idx = new_block_idx;
        fat[new_block_idx] = -1;
        return;
    }

    while (fat[last_file_block] != -1)
    {
        last_file_block = fat[last_file_block];
    }

    fat[last_file_block] = new_block_idx;
    fat[new_block_idx] = -1;

    return;
}

struct JFile *jfs_create_file(struct JFile *parent, struct JSuper *sb, char *name, uint8_t flags)
{
    uint8_t *where_to_add = NULL; //В какое место добавить новую запись
    int32_t *fat = jfs_get_fat_ptr(sb);

    int32_t files_fit_in_block = jfs_files_fit_in_block(sb);
    if (0 == files_fit_in_block)
    {
        printf("Too small block size - can't create any file!\n");
        return NULL;
    }

    if (0 == parent->size % files_fit_in_block) //need new block
    {
        int32_t new_block = jfs_get_free_block(fat, sb);
        if (-1 == new_block)
        {
            printf("No free blocks left!\n");
            return NULL;
        }

        jfs_add_new_block(parent, sb, new_block);

        where_to_add = jfs_get_data_ptr(sb) + new_block * sb->block_size;
    }
    else //last block has enough free space
    {
        int32_t last_file_block = parent->first_data_block_idx;

        while (last_file_block != -1)
        {
            last_file_block = fat[last_file_block];
        }

        where_to_add = jfs_get_data_ptr(sb) + last_file_block * sb->block_size +
                       (parent->size % files_fit_in_block) * sizeof(struct JFile);
    }

    struct JFile *new_file = (struct JFile *)where_to_add;
    if (name != NULL)
    {
        if (strlen(name) > 63)
            printf("Too long file name! Only 63 bytes will be written!\n");
        strcpy(new_file->name, name);
    }
    else
        new_file->name[0] = '\0';
    new_file->size = 0;
    new_file->first_data_block_idx = -1;
    new_file->flags = flags;

    parent->size++;

    return new_file;
}

struct JFile *jfs_get_root_dir(struct JSuper *sb)
{
    return &(sb->root);
}

int32_t jfs_read_dir(struct JFile *dir, struct JSuper *sb, uint32_t offset, struct JFile **ret)
{
    int32_t block_pos = dir->first_data_block_idx;
    int32_t loop_offset = 0;
    int32_t *fat = jfs_get_fat_ptr(sb);

    if (offset >= dir->size)
    {
        *ret = NULL;
        return 0;
    }

    while (offset - loop_offset >= jfs_files_fit_in_block(sb))
    {
        block_pos = fat[block_pos];
        loop_offset += jfs_files_fit_in_block(sb);
    }

    uint8_t *global_pos = jfs_get_data_ptr(sb) +
                          block_pos * sb->block_size +
                          (offset - loop_offset) * sizeof(struct JFile);


    *ret = (struct JFile *)global_pos;

    return 0;
}

int32_t jfs_write_file(struct JFile *file, struct JSuper *sb, uint32_t offset, char *data, uint32_t data_size)
{
    printf("\tWrite file %s!\n", file->name);
    int32_t *fat = jfs_get_fat_ptr(sb);

    ///Error handle
    if (!jfs_is_file(file))
    {
        printf("Eww, it is not a file!\n");
        return -1;
    }

    if (offset < 0 || offset > file->size)
    {
        printf("Bad offset value!\n");
        return -1;
    }

    ///Set pointers
    uint8_t *write_ptr = NULL;
    int32_t cnt_to_write = data_size;
    int32_t curr_block = -1;
    int32_t ret = 0;

    ///Write data
    for(;;)
    {
        ///Error happens
        if (ret < 0)
        {
            printf("Error while write in file!\n");
            return ret;
        }
        ///All done!
        else if (0 == cnt_to_write)
        {
            break;
        }
        ///File is empty
        else if (file->first_data_block_idx < 0)
        {
            int32_t new_block = jfs_get_free_block(fat, sb);
            if (0 >= new_block)
            {
                ret = -1;
                continue;
            }

            jfs_add_new_block(file, sb, new_block);
            curr_block = new_block;
            write_ptr = jfs_block_idx_to_ptr(curr_block, sb);
        }
        ///file is not empty, block and place to write is unset
        else if (NULL == write_ptr)
        {
            uint32_t ii;
            curr_block = file->first_data_block_idx;

            for (ii = offset; ii > sb->block_size; ii -= sb->block_size)
            {
                curr_block = fat[curr_block];
            }

            write_ptr = jfs_block_idx_to_ptr(curr_block, sb) + ii;
        }
        ///Reach the end of the block
        else if (jfs_block_idx_to_ptr(curr_block, sb) + sb->block_size == write_ptr)
        {
            ///Two cases: EOF or not
            if (fat[curr_block] >= 0)
            {
                curr_block = fat[curr_block];
                write_ptr = jfs_block_idx_to_ptr(curr_block, sb);
            }
            else
            {
                int32_t new_block = jfs_get_free_block(fat, sb);
                if (0 >= new_block)
                {
                    ret = -1;
                    continue;
                }

                jfs_add_new_block(file, sb, new_block);
                curr_block = new_block;
                write_ptr = jfs_block_idx_to_ptr(curr_block, sb);
            }
        }
        ///All is set, can write
        else
        {
            uint32_t left_in_block = sb->block_size - (write_ptr - jfs_block_idx_to_ptr(curr_block, sb));
            uint32_t write_in_block = cnt_to_write > left_in_block ? left_in_block : cnt_to_write;

            memcpy(write_ptr, data - cnt_to_write + data_size, write_in_block);
            write_ptr += write_in_block;
            cnt_to_write -= write_in_block;
        }
    }

    return ret;
}

//TODO: should be implemented other way, if another flags fill appear
int8_t jfs_is_dir(struct JFile *file)
{
    return file->flags;
}

//TODO: should be implemented other way, if another flags fill appear
int8_t jfs_is_file(struct JFile *file)
{
    return !file->flags;
}
