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

struct JFile *jfs_get_children_dir(struct JFile parent, struct JSuper *sb, char *name)
{
    //TODO
    return NULL;
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

int32_t jfs_write_file(struct JFile *flie, struct JSuper *sb, uint32_t offset, char *data, uint32_t data_size)
{
    //~ ///get new block if needed
    //~ int32_t blocks_total = 0, last_block = meta->first_data_block_idx;
    //~ while (last_block >= 0)
        //~ blocks_total++;

    //~ if (size + sizeof(struct JFile) > blocks_total * sb->block_size)
    //~ {
        //~ fat[last_block] = get_free_block(fat, sb);
        //~ if (0 > fat[last_block])
        //~ {
            //~ printf("Can't allocate free block!");
            //~ return -1;
        //~ }
    //~ }
    return 0;
}
