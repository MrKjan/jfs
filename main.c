#include <stdio.h>
#include "jfs.h"
#include "gen_jfs_image.h"
#include <stdint.h>

int main(int argc, char **argv)
{
    /*int ret = */create_jfs_image("fs_files/jfs_instance", "jfs", "data", BLOCK_SIZE, BLOCKS_CNT);

    //~ printf("%d\n", files_of_dir("data"));

    //~ struct Dir_explore get = explore_dir("data", 100);
    //~ printf("\nFiles: %d, Dirs: %d, FBlocks: %d, DBlocks: %d\n", get.files, get.dirs, get.file_blocks, get.dir_blocks);

    return 0;
}
