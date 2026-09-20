#ifndef MINOS_VFS_H
#define MINOS_VFS_H

#include <stdint.h>

#define VFS_PATH_MAX 127
#define VFS_NAME_MAX 31
#define VFS_FILE_MAX 64

enum vfs_type { VFS_DIRECTORY = 1, VFS_FILE = 2 };
struct vfs_stat { enum vfs_type type; uint32_t size; };
struct vfs_dirent { char name[VFS_NAME_MAX + 1]; enum vfs_type type; uint32_t size; };

int vfs_init(void);
int vfs_normalize(const char *cwd, const char *path, char *out);
int vfs_stat_path(const char *path, struct vfs_stat *st);
int vfs_list(const char *path, struct vfs_dirent *entries, uint32_t max, uint32_t *count);
int vfs_mkdir(const char *path);
int vfs_rmdir(const char *path);
int vfs_touch(const char *path);
int vfs_remove(const char *path);
int vfs_read(const char *path, char *buffer, uint32_t capacity, uint32_t *size);
int vfs_write(const char *path, const char *data, uint32_t size, int append);
int vfs_copy(const char *source, const char *destination);
int vfs_move(const char *source, const char *destination);
int vfs_is_directory(const char *path);

#endif
