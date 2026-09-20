#include "vfs.h"
#include "ata.h"
#include "serial.h"

#define VFS_MAGIC 0x534F4E4DU
#define VFS_META_LBA 2048U
#define VFS_DATA_LBA 2064U
#define VFS_CONTENT_MAX 4096U

struct vfs_record {
    char name[VFS_NAME_MAX + 1];
    int16_t parent;
    uint8_t type;
    uint8_t used;
    uint32_t size;
    uint32_t data_lba;
};
static struct vfs_record records[VFS_FILE_MAX];
static uint8_t contents[VFS_FILE_MAX][VFS_CONTENT_MAX];
static uint32_t generation;
static int disk_backed;

static void zero(void *p, uint32_t n) {
    uint8_t *b = (uint8_t *)p;
    while (n--) *b++ = 0;
}
static uint32_t slen(const char *s) {
    uint32_t n = 0; while (s && s[n]) ++n; return n;
}
static int eq(const char *a, const char *b) {
    uint32_t i = 0; while (a[i] && b[i] && a[i] == b[i]) ++i;
    return a[i] == b[i];
}
static void copy(char *d, const char *s, uint32_t max) {
    uint32_t i = 0; if (!max) return;
    while (s && s[i] && i + 1 < max) { d[i] = s[i]; ++i; }
    d[i] = 0;
}

/* Normalize absolute and relative paths, collapsing '.', '..', and repeats. */
int vfs_normalize(const char *cwd, const char *path, char *out) {
    char work[VFS_PATH_MAX + 1], parts[16][VFS_NAME_MAX + 1];
    uint32_t depth = 0, i = 0, length;
    if (!out || !path || !*path) return -1;
    out[0] = 0;
    if (path[0] != '/') {
        if (!cwd || vfs_normalize("/", cwd, work) != 0) return -1;
        length = slen(work);
        if (length + 1 + slen(path) > VFS_PATH_MAX) return -1;
        for (i = 0; i < length; ++i) out[i] = work[i];
        if (length > 1) out[length++] = '/';
        for (i = 0; path[i] && length < VFS_PATH_MAX; ++i) out[length++] = path[i];
        out[length] = 0;
        return vfs_normalize("/", out, work) == 0 ? (copy(out, work, VFS_PATH_MAX + 1), 0) : -1;
    }
    i = 1;
    while (path[i]) {
        uint32_t start = i, n;
        while (path[i] && path[i] != '/') ++i;
        n = i - start;
        if (n) {
            if (n == 1 && path[start] == '.') { /* ignore */ }
            else if (n == 2 && path[start] == '.' && path[start + 1] == '.') {
                if (depth) --depth;
            } else if (n <= VFS_NAME_MAX && depth < 16) {
                uint32_t j; for (j = 0; j < n; ++j) parts[depth][j] = path[start + j];
                parts[depth][n] = 0; ++depth;
            } else return -1;
        }
        while (path[i] == '/') ++i;
    }
    length = 0; out[length++] = '/';
    for (i = 0; i < depth; ++i) {
        uint32_t j;
        if (length > 1) out[length++] = '/';
        for (j = 0; parts[i][j]; ++j) out[length++] = parts[i][j];
    }
    out[length] = 0;
    return 0;
}

static int find(const char *path) {
    char normalized[VFS_PATH_MAX + 1];
    uint32_t i;
    if (vfs_normalize("/", path, normalized) != 0) return -1;
    if (eq(normalized, "/")) return 0;
    for (i = 1; i < VFS_FILE_MAX; ++i) {
        char parent[VFS_PATH_MAX + 1], candidate[VFS_PATH_MAX + 1];
        int p;
        if (!records[i].used) continue;
        p = records[i].parent;
        if (p < 0) continue;
        if (p == 0) copy(parent, "/", sizeof(parent));
        else {
            /* Build parent path by recursively walking the small table. */
            uint32_t stack[16], depth = 0, at = (uint32_t)p, j;
            while (at && depth < 16) { stack[depth++] = at; at = records[at].parent; }
            copy(parent, "/", sizeof(parent));
            while (depth) {
                uint32_t k = stack[--depth];
                uint32_t l = slen(parent);
                if (l > 1) parent[l++] = '/';
                for (j = 0; records[k].name[j] && l < VFS_PATH_MAX; ++j) parent[l++] = records[k].name[j];
                parent[l] = 0;
            }
        }
        copy(candidate, parent, sizeof(candidate));
        { uint32_t l = slen(candidate), j;
          if (l > 1) candidate[l++] = '/';
          for (j = 0; records[i].name[j] && l < VFS_PATH_MAX; ++j) candidate[l++] = records[i].name[j];
          candidate[l] = 0; }
        if (eq(candidate, normalized)) return (int)i;
    }
    return -1;
}

static int parent_and_name(const char *path, int *parent, char *name) {
    char normalized[VFS_PATH_MAX + 1], parent_path[VFS_PATH_MAX + 1];
    uint32_t i, split = 0;
    if (vfs_normalize("/", path, normalized) != 0 || eq(normalized, "/")) return -1;
    for (i = 1; normalized[i]; ++i) if (normalized[i] == '/') split = i;
    copy(name, normalized + (split ? split + 1U : 1U), VFS_NAME_MAX + 1);
    if (!split) copy(parent_path, "/", sizeof(parent_path));
    else { for (i = 0; i < split; ++i) parent_path[i] = normalized[i]; parent_path[split] = 0; }
    *parent = find(parent_path);
    return *parent >= 0 && records[*parent].type == VFS_DIRECTORY ? 0 : -1;
}

static int persist(void) {
    uint8_t all[4096]; uint32_t i, offset = 0;
    if (!disk_backed) return 0;
    zero(all, sizeof(all));
    *(uint32_t *)all = VFS_MAGIC; *(uint32_t *)(all + 4) = ++generation;
    for (i = 0; i < VFS_FILE_MAX; ++i) {
        uint8_t *raw = (uint8_t *)&records[i];
        uint32_t j;
        for (j = 0; j < sizeof(struct vfs_record); ++j) all[8 + offset++] = raw[j];
    }
    for (i = 0; i < 8; ++i) if (ata_write28(VFS_META_LBA + i, all + i * 512U) != 0) return -1;
    return 0;
}

int vfs_init(void) {
    uint8_t sector[512]; uint32_t i;
    int loaded = 0;
    zero(records, sizeof(records)); generation = 0;
    disk_backed = ata_primary_master_present();
    records[0].used = 1; records[0].type = VFS_DIRECTORY; records[0].parent = -1;
    copy(records[0].name, "/", sizeof(records[0].name));
    if (disk_backed && ata_read28(VFS_META_LBA, sector) == 0 &&
        *(uint32_t *)sector == VFS_MAGIC) {
        /* Metadata is intentionally bounded and fixed-size for crash-safe reads. */
        uint8_t all[4096]; uint32_t pos = 0;
        for (i = 0; i < 8; ++i) if (ata_read28(VFS_META_LBA + i, all + i * 512U) != 0) break;
        if (i == 8) {
            for (i = 0; i < VFS_FILE_MAX; ++i) {
                uint8_t *raw = (uint8_t *)&records[i]; uint32_t j;
                for (j = 0; j < sizeof(struct vfs_record); ++j) raw[j] = all[8 + pos++];
            }
            generation = *(uint32_t *)(all + 4);
            loaded = 1;
        }
    }
    serial_printf("[MinOS VFS] metadata %s, generation=%u\n",
                  loaded ? "loaded" : "initialized",
                  (uint64_t)generation);
    for (i = 1; i < VFS_FILE_MAX; ++i)
        if (records[i].used)
            serial_printf("[MinOS VFS] record=%u name=%s parent=%d size=%u\n",
                          (uint64_t)i, records[i].name, records[i].parent,
                          (uint64_t)records[i].size);
    serial_puts("[MinOS VFS] mounted persistent metadata filesystem at /.\n");
    return persist();
}

int vfs_stat_path(const char *path, struct vfs_stat *st) {
    int n = find(path); if (n < 0 || !st) return -1;
    st->type = (enum vfs_type)records[n].type; st->size = records[n].size; return 0;
}
int vfs_is_directory(const char *path) { struct vfs_stat s; return vfs_stat_path(path, &s) == 0 && s.type == VFS_DIRECTORY; }

static int create(const char *path, enum vfs_type type) {
    int parent, n = find(path), slot = -1; char name[VFS_NAME_MAX + 1]; uint32_t i;
    if (n >= 0 || parent_and_name(path, &parent, name) != 0 || !name[0]) return -1;
    for (i = 1; i < VFS_FILE_MAX; ++i) if (!records[i].used) { slot = (int)i; break; }
    if (slot < 0) return -1;
    zero(&records[slot], sizeof(records[slot])); records[slot].used = 1; records[slot].type = type;
    records[slot].parent = (int16_t)parent; copy(records[slot].name, name, sizeof(records[slot].name));
    records[slot].data_lba = VFS_DATA_LBA + (uint32_t)slot * 8U;
    return persist();
}
int vfs_mkdir(const char *p) { return create(p, VFS_DIRECTORY); }
int vfs_touch(const char *p) { int n = find(p); return n >= 0 ? 0 : create(p, VFS_FILE); }
int vfs_rmdir(const char *p) { int n = find(p); uint32_t i; if (n <= 0 || records[n].type != VFS_DIRECTORY) return -1; for (i=1;i<VFS_FILE_MAX;++i) if(records[i].used&&records[i].parent==n)return -1; records[n].used=0; return persist(); }
int vfs_remove(const char *p) { int n=find(p); if(n<=0||records[n].type!=VFS_FILE)return -1; records[n].used=0; return persist(); }
int vfs_read(const char *p, char *b, uint32_t cap, uint32_t *size) {
    int n=find(p); uint32_t i, want; uint8_t sector[512];
    if(n<0||records[n].type!=VFS_FILE||!b||!size)return -1;
    if (disk_backed) for (i = 0; i < 8; ++i) {
        if (ata_read28(records[n].data_lba + i, sector) != 0) {
            serial_printf("[MinOS VFS] read failed lba=%u\n",
                          (uint64_t)(records[n].data_lba + i));
            return -1;
        }
        { uint32_t j; for (j = 0; j < 512; ++j) contents[n][i * 512U + j] = sector[j]; }
    }
    want=records[n].size<cap?records[n].size:cap; for(i=0;i<want;++i)b[i]=contents[n][i]; *size=want; return 0;
}
int vfs_write(const char *p, const char *d, uint32_t size, int append) {
    int n=find(p); uint32_t pos, i; if(n<0||records[n].type!=VFS_FILE||!d)return -1;
    pos=append?records[n].size:0; if(pos>VFS_CONTENT_MAX)return -1;
    if (size > VFS_CONTENT_MAX - pos) size = VFS_CONTENT_MAX - pos;
    for (i = 0; i < size; ++i) contents[n][pos + i] = (uint8_t)d[i];
    records[n].size = pos + size;
    if (disk_backed) {
        uint8_t sector[512];
        for (i = 0; i < 8; ++i) {
            uint32_t j; for (j = 0; j < 512; ++j) sector[j] = contents[n][i * 512U + j];
            if (ata_write28(records[n].data_lba + i, sector) != 0) return -1;
        }
    }
    return persist();
}
int vfs_copy(const char *s,const char *d){char b[VFS_CONTENT_MAX];uint32_t n; if(vfs_read(s,b,sizeof(b),&n)||vfs_touch(d)||vfs_write(d,b,n,0))return -1;return 0;}
int vfs_move(const char *s,const char *d){if(vfs_copy(s,d))return -1;return vfs_remove(s);}
int vfs_list(const char *p, struct vfs_dirent *e,uint32_t max,uint32_t *count){int n=find(p);uint32_t i,out=0;if(n<0||records[n].type!=VFS_DIRECTORY||!e||!count)return -1;for(i=1;i<VFS_FILE_MAX&&out<max;++i)if(records[i].used&&records[i].parent==n){copy(e[out].name,records[i].name,sizeof(e[out].name));e[out].type=(enum vfs_type)records[i].type;e[out].size=records[i].size;++out;}*count=out;return 0;}
