#ifndef __JFS_H__
#define __JFS_H__

#include <stdint.h>

#define JFS_FILE_NAME_SIZE  64
#define JFS_FAT_EOF         -1
#define FILL_CHAR           '\0'
//#define JFS_BLOCK_SIZE 128

enum JFileType
{
    JFS_FILE,
    JFS_DIR
};

struct JFile
{
    char name[JFS_FILE_NAME_SIZE];
    uint32_t size; //if is dir, size is cnt of files in
    int32_t first_data_block_idx;
    uint8_t flags; //0 - is file, 1 - is dir
    //enum JFileType type; //TODO: Causes crash. Explore why
    //create_time
    //update_time
    //int32_t parent;
};

struct JSuper
{
    uint32_t block_size;
    uint32_t blocks_count;
    uint32_t system_bytes; //Bytes before 1st data block
    uint32_t total_bytes;
    int32_t first_free_block;
    struct JFile root;
};

struct JCoord
{
    int32_t my_jfile_block;
    uint32_t my_jfile_offset;
    int32_t parent_jfile_block;
    uint32_t parent_jfile_offset;
}

int32_t jfs_get_free_block(int32_t *fat, struct JSuper *sb);
void jfs_return_free_block(struct JSuper *sb, int32_t free_block);
void jfs_add_new_block(struct JFile *file, struct JSuper *sb, int32_t new_block_idx);
struct JFile *jfs_create_file(struct JFile *parent, struct JSuper *sb, char *name, uint8_t flags);
int32_t *jfs_get_fat_ptr(struct JSuper *sb);
uint8_t *jfs_get_data_ptr(struct JSuper *sb);
uint8_t *jfs_block_idx_to_ptr(int32_t block_idx, struct JSuper *sb);
int32_t jfs_read_dir(struct JFile *dir, struct JSuper *sb, uint32_t offset, struct JFile **ret);
struct JFile *jfs_get_root_dir(struct JSuper *sb);
int32_t jfs_files_fit_in_block(struct JSuper *sb);
int8_t jfs_is_dir(struct JFile *file);
int8_t jfs_is_file(struct JFile *file);
int32_t jfs_write_file(struct JFile *file, struct JSuper *sb, uint32_t offset, uint8_t *data, uint32_t data_size);
int32_t jfs_read_file(struct JFile *file, struct JSuper *sb, uint32_t offset, uint8_t *dst, uint32_t size, uint32_t *ret_size);
int32_t jfs_rename_file(struct JFile *file, struct JSuper *sb, char *new_name);
int32_t jfs_resize_file(struct JFile *file, struct JSuper *sb, uint32_t new_size);

//TODO: Delete when merge with Jetos
#ifndef FS_H
struct file
{
    long int                f_pos;          /* Положение файлового указателя */
    struct dentry           *f_path;        /* Содержит объект dentry */
    unsigned int            f_flags;        /* Флаги, указанные при вызове функции open */
    struct file             *f_free_list;   /* Для списка свободных элементов */
};

struct directory_file
{
    struct dentry *df_source;   // Директория, для которой эта структура является файлом
    unsigned int  df_seek;      // Положение файлового указателя
    struct directory_file *df_free_list;   /* Для списка свободных элементов */
};
#endif //FS_H

#endif //__JFS_H__
