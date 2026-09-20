#ifndef MINOS_VFS_H
#define MINOS_VFS_H

#include <stdint.h>

#define VFS_PATH_MAX 127
#define VFS_NAME_MAX 31
#define VFS_FILE_MAX 128
#define VFS_HANDLE_MAX 32

enum vfs_type { VFS_DIRECTORY = 1, VFS_FILE = 2 };
struct vfs_stat { enum vfs_type type; uint32_t size; };
struct vfs_dirent { char name[VFS_NAME_MAX + 1]; enum vfs_type type; uint32_t size; };
struct vfs_usage { uint32_t total_bytes; uint32_t used_bytes; uint32_t free_bytes; };
struct vfs_handle { int slot; uint32_t offset; uint32_t id; uint8_t readable; uint8_t writable; };

int vfs_init(void);
int vfs_normalize(const char *cwd, const char *path, char *out);
int vfs_stat_path(const char *path, struct vfs_stat *st);
int vfs_stat(const char *path, struct vfs_stat *st);
int vfs_list(const char *path, struct vfs_dirent *entries, uint32_t max, uint32_t *count);
int vfs_readdir(const char *path, struct vfs_dirent *entries, uint32_t max, uint32_t *count);
int vfs_mkdir(const char *path);
int vfs_rmdir(const char *path);
int vfs_touch(const char *path);
int vfs_remove(const char *path);
int vfs_read(const char *path, char *buffer, uint32_t capacity, uint32_t *size);
int vfs_write(const char *path, const char *data, uint32_t size, int append);
int vfs_copy(const char *source, const char *destination);
int vfs_move(const char *source, const char *destination);
int vfs_is_directory(const char *path);
int vfs_usage(struct vfs_usage *usage);
int vfs_open(const char *path, int writeable, struct vfs_handle *handle);
int vfs_close(struct vfs_handle *handle);
int vfs_seek(struct vfs_handle *handle, int32_t offset, int whence);
int vfs_read_handle(struct vfs_handle *handle, void *buffer, uint32_t capacity, uint32_t *size);
int vfs_write_handle(struct vfs_handle *handle, const void *data, uint32_t size);
int vfs_create(const char *path);
int vfs_rename(const char *source, const char *destination);

#endif
