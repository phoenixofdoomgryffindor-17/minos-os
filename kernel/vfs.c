#include "vfs.h"
#include "ata.h"
#include "serial.h"

#define VFS_MAGIC 0x534F4E4DU
#define VFS_VERSION 2U
#define VFS_META_LBA 2048U
#define VFS_META_SECTORS 12U
#define VFS_DATA_LBA 2064U
#define VFS_META_BYTES (VFS_META_SECTORS * 512U)

struct vfs_record {
    char name[VFS_NAME_MAX + 1];
    int16_t parent;
    uint8_t type, used;
    uint32_t size, data_lba;
};
static struct vfs_record records[VFS_FILE_MAX];
static uint8_t allocation[VFS_FILE_MAX];
static struct block_device block;
static uint32_t generation;
static int disk_backed;
static struct vfs_handle handles[VFS_HANDLE_MAX];
static uint8_t volatile_data[VFS_FILE_MAX][4096];

static void zero(void *p, uint32_t n) { uint8_t *b = p; while (n--) *b++ = 0; }
static uint32_t slen(const char *s) { uint32_t n=0; while (s && s[n]) ++n; return n; }
static int eqi(const char *a,const char *b) {
    uint32_t i=0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        ++i;
    }
    return a[i] == b[i];
}
static void copy(char *d,const char *s,uint32_t cap) {
    uint32_t i=0; if(!cap)return; while(s&&s[i]&&i+1<cap)d[i]=s[i],++i; d[i]=0;
}

int vfs_normalize(const char *cwd,const char *path,char *out) {
    char work[VFS_PATH_MAX+1], parts[16][VFS_NAME_MAX+1]; uint32_t depth=0,i,n,len;
    if(!out||!path||!*path)return -1;
    if(path[0]!='/') {
        if(!cwd||vfs_normalize("/",cwd,work))return -1;
        len=slen(work);
        if(len+(len>1?1U:0U)+slen(path)>VFS_PATH_MAX)return -1;
        copy(out,work,VFS_PATH_MAX+1); len=slen(out); if(len>1)out[len++]='/';
        for(i=0;path[i]&&len<VFS_PATH_MAX;++i)out[len++]=path[i];
        out[len]=0;
        return vfs_normalize("/",out,work)==0?(copy(out,work,VFS_PATH_MAX+1),0):-1;
    }
    i=1; while(path[i]) { uint32_t start=i; while(path[i]&&path[i]!='/')++i; n=i-start;
        if(n) {
            if(n==1&&path[start]=='.') {}
            else if(n==2&&path[start]=='.'&&path[start+1]=='.') { if(depth)--depth; }
            else if(n<=VFS_NAME_MAX&&depth<16) { uint32_t j; for(j=0;j<n;++j)parts[depth][j]=path[start+j]; parts[depth][n]=0;++depth; }
            else return -1;
        } while(path[i]=='/')++i;
    }
    len=1; out[0]='/'; for(i=0;i<depth;++i){uint32_t j;if(len>1)out[len++]='/';for(j=0;parts[i][j];++j)out[len++]=parts[i][j];} out[len]=0; return 0;
}
static void path_of(uint32_t n,char *out) {
    uint32_t stack[16],d=0,a=n,j,l=1; out[0]='/'; out[1]=0;
    while(a&&d<16){stack[d++]=a;a=(uint32_t)records[a].parent;}
    while(d){uint32_t k=stack[--d];if(l>1)out[l++]='/';for(j=0;records[k].name[j]&&l<VFS_PATH_MAX;++j)out[l++]=records[k].name[j];out[l]=0;}
}
static int find(const char *path) {
    char norm[VFS_PATH_MAX+1],candidate[VFS_PATH_MAX+1];
    uint32_t i; if(vfs_normalize("/",path,norm))return -1; if(eqi(norm,"/"))return 0;
    for(i=1;i<VFS_FILE_MAX;++i) if(records[i].used) {
        path_of(i,candidate);
        if(eqi(norm,candidate)) return (int)i;
    }
    return -1;
}
static int parent_name(const char *path,int *parent,char *name) {
    char n[VFS_PATH_MAX+1],p[VFS_PATH_MAX+1];uint32_t i,split=0;
    if(vfs_normalize("/",path,n)||eqi(n,"/"))return -1;
    for(i=1;n[i];++i) if(n[i]=='/') split=i;
    copy(name,n+(split?split+1:1),VFS_NAME_MAX+1);if(!name[0])return -1;
    if(split){for(i=0;i<split;++i)p[i]=n[i];p[split]=0;}else copy(p,"/",sizeof(p));
    *parent=find(p);return *parent>=0&&records[*parent].type==VFS_DIRECTORY?0:-1;
}
static uint32_t sectors(uint32_t bytes){return bytes?((bytes+511U)/512U):0;}
static int range_free(uint32_t first,uint32_t count,int except) {
    uint32_t i,start,end; if(!count||first<VFS_DATA_LBA||first+count>block.block_count)return 0;
    for(i=1;i<VFS_FILE_MAX;++i)if(records[i].used&&records[i].type==VFS_FILE&&i!=(uint32_t)except){
        start=records[i].data_lba;end=start+sectors(records[i].size);
        if(start<first+count&&first<end)return 0;
    } return 1;
}
static int alloc_extent(uint32_t count,int except,uint32_t *first) {
    uint32_t p;if(!count){*first=0;return 0;} if(!disk_backed){*first=0;return 0;}
    for(p=VFS_DATA_LBA;p+count<=block.block_count;++p)
        if(range_free(p,count,except)){*first=p;return 0;}
    return -1;
}
static uint32_t checksum(const uint8_t *p,uint32_t n){uint32_t h=2166136261U,i;for(i=0;i<n;++i)h=(h^p[i])*16777619U;return h;}
static int persist(void) {
    uint8_t all[VFS_META_BYTES];uint32_t i,j,pos=0; if(!disk_backed)return 0;zero(all,sizeof(all));
    *(uint32_t*)(all)=VFS_MAGIC;*(uint32_t*)(all+4)=VFS_VERSION;*(uint32_t*)(all+8)=++generation;*(uint32_t*)(all+12)=VFS_FILE_MAX;
    for(i=0;i<VFS_FILE_MAX;++i)all[16+i]=allocation[i];
    for(i=0;i<VFS_FILE_MAX;++i){uint8_t *r=(uint8_t*)&records[i];for(j=0;j<sizeof(struct vfs_record);++j)all[16+VFS_FILE_MAX+pos++]=r[j];}
    *(uint32_t*)(all+VFS_META_BYTES-4U)=checksum(all,VFS_META_BYTES-4U);
    for(i=0;i<VFS_META_SECTORS;++i)if(block.write(&block,VFS_META_LBA+i,all+i*512U))return -1;
    return block.flush(&block);
}
int vfs_init(void) {
    uint8_t all[VFS_META_BYTES];uint32_t i,j,pos;int loaded=0;zero(records,sizeof(records));zero(allocation,sizeof(allocation));zero(handles,sizeof(handles));generation=0;
    disk_backed=ata_block_device_get(&block)==0;zero(volatile_data,sizeof(volatile_data));records[0].used=1;records[0].type=VFS_DIRECTORY;records[0].parent=-1;allocation[0]=1;copy(records[0].name,"/",sizeof(records[0].name));
    if(disk_backed){for(i=0;i<VFS_META_SECTORS;++i)if(block.read(&block,VFS_META_LBA+i,all+i*512U))break;
        if(i==VFS_META_SECTORS&&*(uint32_t*)all==VFS_MAGIC&&*(uint32_t*)(all+4)==VFS_VERSION&&*(uint32_t*)(all+12)==VFS_FILE_MAX&&
           *(uint32_t*)(all+VFS_META_BYTES-4U)==checksum(all,VFS_META_BYTES-4U)){pos=0;for(i=0;i<VFS_FILE_MAX;++i)allocation[i]=all[16+i];
            for(i=0;i<VFS_FILE_MAX;++i){uint8_t*r=(uint8_t*)&records[i];for(j=0;j<sizeof(struct vfs_record);++j)r[j]=all[16+VFS_FILE_MAX+pos++];}
            generation=*(uint32_t*)(all+8);loaded=records[0].used&&records[0].type==VFS_DIRECTORY&&records[0].parent==-1&&allocation[0];
            for(i=1;loaded&&i<VFS_FILE_MAX;++i)if(records[i].used!=allocation[i]||(records[i].used&&((records[i].parent<0)||records[i].parent>=VFS_FILE_MAX||!records[records[i].parent].used||!records[i].name[0]||(records[i].type!=VFS_FILE&&records[i].type!=VFS_DIRECTORY)||
                (records[i].type==VFS_FILE&&disk_backed&&(records[i].data_lba<VFS_DATA_LBA||records[i].data_lba+sectors(records[i].size)>block.block_count)))))loaded=0;
            for(i=1;loaded&&i<VFS_FILE_MAX;++i)if(records[i].used&&records[i].type==VFS_FILE) { uint32_t j; for(j=i+1;j<VFS_FILE_MAX;++j) if(records[j].used&&records[j].type==VFS_FILE&&records[i].data_lba<records[j].data_lba+sectors(records[j].size)&&records[j].data_lba<records[i].data_lba+sectors(records[i].size)) loaded=0; }
        }}
    serial_printf("[MinOS VFS] metadata %s, generation=%u\n",loaded?"loaded":"initialized",(uint64_t)generation);
    serial_puts("[MinOS VFS] mounted persistent metadata filesystem at /.\n");
    if(!loaded) {zero(records,sizeof(records));zero(allocation,sizeof(allocation));records[0].used=1;records[0].type=VFS_DIRECTORY;records[0].parent=-1;allocation[0]=1;copy(records[0].name,"/",sizeof(records[0].name));}
    return persist();
}
int vfs_stat_path(const char*p,struct vfs_stat*s){int n=find(p);if(n<0||!s)return -1;s->type=(enum vfs_type)records[n].type;s->size=records[n].size;return 0;}
int vfs_stat(const char*p,struct vfs_stat*s){return vfs_stat_path(p,s);}
int vfs_is_directory(const char*p){struct vfs_stat s;return vfs_stat_path(p,&s)==0&&s.type==VFS_DIRECTORY;}
static int create(const char*p,enum vfs_type type){int parent,n=find(p),slot=-1;char name[VFS_NAME_MAX+1];uint32_t i;if(n>=0||parent_name(p,&parent,name))return -1;for(i=1;i<VFS_FILE_MAX;++i)if(!records[i].used){slot=(int)i;break;}if(slot<0)return -1;zero(&records[slot],sizeof(records[slot]));records[slot].used=1;records[slot].type=type;records[slot].parent=(int16_t)parent;copy(records[slot].name,name,sizeof(records[slot].name));allocation[slot]=1;if(persist())return -1;return 0;}
int vfs_create(const char*p){return create(p,VFS_FILE);} int vfs_mkdir(const char*p){return create(p,VFS_DIRECTORY);}
int vfs_touch(const char*p){int n=find(p);return n>=0&&records[n].type==VFS_FILE?0:(n>=0?-1:create(p,VFS_FILE));}
int vfs_rmdir(const char*p){int n=find(p);uint32_t i;if(n<=0||records[n].type!=VFS_DIRECTORY)return -1;for(i=1;i<VFS_FILE_MAX;++i)if(records[i].used&&records[i].parent==n)return -1;zero(&records[n],sizeof(records[n]));allocation[n]=0;return persist();}
int vfs_remove(const char*p){int n=find(p);if(n<=0||records[n].type!=VFS_FILE)return -1;zero(&records[n],sizeof(records[n]));allocation[n]=0;return persist();}
int vfs_read(const char*p,char*b,uint32_t cap,uint32_t*size){int n=find(p);uint32_t i,want;uint8_t sec[512];if(n<0||records[n].type!=VFS_FILE||!b||!size)return -1;want=records[n].size<cap?records[n].size:cap;for(i=0;i<want;++i){uint32_t l=records[n].data_lba+i/512;if(!disk_backed)b[i]=(char)volatile_data[n][i];else{if(block.read(&block,l,sec))return -1;b[i]=sec[i%512];}}*size=want;return 0;}
int vfs_write(const char*p,const char*d,uint32_t size,int append){int n=find(p);uint32_t old,pos,newbytes,oldsec,newsec,first,i,j;uint8_t sec[512];if(n<0||records[n].type!=VFS_FILE||!d)return -1;old=records[n].size;pos=append?old:0;newbytes=pos+size;if(newbytes<pos||(!disk_backed&&newbytes>sizeof(volatile_data[n])))return -1;oldsec=sectors(old);newsec=sectors(newbytes);if(newsec!=oldsec){if(alloc_extent(newsec,n,&first))return -1;if(disk_backed){for(i=0;i<old;++i){if(block.read(&block,records[n].data_lba+i/512,sec))return -1;if(block.write(&block,first+i/512,sec))return -1;}}records[n].data_lba=first;}for(i=0;i<size;++i){uint32_t at=pos+i;if(!disk_backed)volatile_data[n][at]=(uint8_t)d[i];else{if(block.read(&block,records[n].data_lba+at/512,sec))return -1;sec[at%512]=(uint8_t)d[i];if(block.write(&block,records[n].data_lba+at/512,sec))return -1;}}records[n].size=newbytes;for(j=0;j<newsec;++j)if(disk_backed&&block.read(&block,records[n].data_lba+j,sec))return -1;return persist();}
int vfs_list(const char*p,struct vfs_dirent*e,uint32_t max,uint32_t*c){int n=find(p);uint32_t i,o=0;if(n<0||records[n].type!=VFS_DIRECTORY||!e||!c)return -1;for(i=1;i<VFS_FILE_MAX&&o<max;++i)if(records[i].used&&records[i].parent==n){copy(e[o].name,records[i].name,sizeof(e[o].name));e[o].type=(enum vfs_type)records[i].type;e[o].size=records[i].size;++o;}*c=o;return 0;}
int vfs_readdir(const char*p,struct vfs_dirent*e,uint32_t max,uint32_t*c){return vfs_list(p,e,max,c);}
int vfs_usage(struct vfs_usage*u){uint32_t i,used=0,total=0; if(!u)return -1;if(disk_backed){total=block.block_count>VFS_DATA_LBA?(block.block_count-VFS_DATA_LBA)*512U:0;for(i=1;i<VFS_FILE_MAX;++i)if(records[i].used&&records[i].type==VFS_FILE)used+=sectors(records[i].size)*512U;}else{total=0;for(i=1;i<VFS_FILE_MAX;++i)if(records[i].used&&records[i].type==VFS_FILE)used+=records[i].size;}u->total_bytes=total;u->used_bytes=used;u->free_bytes=total>used?total-used:0;return 0;}
int vfs_copy(const char*s,const char*d){int n=find(s);char b[4096];uint32_t at=0,got; if(n<0||records[n].type!=VFS_FILE)return -1;if(vfs_touch(d))return -1;while(at<records[n].size){uint32_t want=records[n].size-at;if(want>sizeof(b))want=sizeof(b);if(vfs_read(s,b,want,&got)||vfs_write(d,b,got,at!=0))return -1;at+=got;if(!got)break;}return at==records[n].size?0:-1;}
int vfs_rename(const char*s,const char*d){int n=find(s),p;char name[VFS_NAME_MAX+1];if(n<=0||parent_name(d,&p,name)||find(d)>=0)return -1;records[n].parent=(int16_t)p;copy(records[n].name,name,sizeof(records[n].name));return persist();}
int vfs_move(const char*s,const char*d){return vfs_rename(s,d);}
static int handle_valid(const struct vfs_handle *h) {
    uint32_t i;
    if (!h || !h->id || h->id > VFS_HANDLE_MAX || h->slot < 0 ||
        h->slot >= VFS_FILE_MAX || !records[h->slot].used) return 0;
    i = h->id - 1U;
    return handles[i].id == h->id && handles[i].slot == h->slot;
}
int vfs_open(const char*p,int w,struct vfs_handle*h){int n=find(p),i;if(n<0||records[n].type!=VFS_FILE||!h)return -1;for(i=0;i<VFS_HANDLE_MAX;++i)if(!handles[i].id){handles[i].slot=n;handles[i].offset=0;handles[i].id=(uint32_t)i+1U;handles[i].readable=1;handles[i].writable=(uint8_t)(w!=0);*h=handles[i];return i;}return -1;}
int vfs_close(struct vfs_handle*h){if(!handle_valid(h))return -1;zero(&handles[h->id-1U],sizeof(handles[0]));zero(h,sizeof(*h));return 0;}
int vfs_seek(struct vfs_handle*h,int32_t off,int whence){uint32_t base,n;if(!handle_valid(h))return -1;base=whence==0?0:(whence==1?h->offset:records[h->slot].size);if(off<0&&base<(uint32_t)(-off))return -1;n=off<0?base-(uint32_t)(-off):base+(uint32_t)off;if(n>records[h->slot].size)return -1;h->offset=n;handles[h->id-1U].offset=n;return 0;}
int vfs_read_handle(struct vfs_handle*h,void*b,uint32_t c,uint32_t*s){uint32_t i,want;uint8_t sec[512],*dst=(uint8_t*)b;if(!handle_valid(h)||!h->readable||!b||!s)return -1;want=records[h->slot].size-h->offset;if(want>c)want=c;for(i=0;i<want;++i){if(disk_backed){if(block.read(&block,records[h->slot].data_lba+(h->offset+i)/512,sec))return -1;dst[i]=sec[(h->offset+i)%512];}else dst[i]=volatile_data[h->slot][h->offset+i];}*s=want;h->offset+=want;handles[h->id-1U].offset=h->offset;return 0;}
int vfs_write_handle(struct vfs_handle*h,const void*d,uint32_t s){char path[VFS_PATH_MAX+1];if(!handle_valid(h)||!h->writable||!d)return -1;path_of((uint32_t)h->slot,path);if(vfs_write(path,d,s,1))return -1;h->offset=records[h->slot].size;handles[h->id-1U].offset=h->offset;return 0;}
