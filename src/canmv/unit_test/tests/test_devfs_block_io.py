"""Compile real devfs read/write functions with sector-counting device mocks."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / 'src/rtsmart/rtsmart/kernel/rt-thread/components/dfs/filesystems/devfs/devfs.c'


def function(source, name):
    start = source.index('int ' + name + '(')
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


preamble = r'''
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#define RT_NULL NULL
#define RT_ASSERT(x) assert(x)
#define RT_ENOSYS ENOSYS
#define RT_Device_Class_Block 1
#define RT_Device_Class_Char 2
struct device { int type; };
typedef struct device *rt_device_t;
struct fnode { const char *path; void *data; };
struct dfs_fd { struct fnode *fnode; off_t pos; };
static int calls, result;
static size_t count_seen;
static off_t pos_seen;
static int rt_device_read(rt_device_t dev, off_t pos, void *buf, size_t count) {
    ++calls; count_seen=count; pos_seen=pos;
    // A block driver would write count * 512 bytes, not count bytes.
    assert(dev->type != RT_Device_Class_Block);
    if (result > 0) memset(buf, 0x5a, result);
    return result;
}
static int rt_device_write(rt_device_t dev, off_t pos, const void *buf, size_t count) {
    (void)buf; ++calls; count_seen=count; pos_seen=pos;
    assert(dev->type != RT_Device_Class_Block);
    return result;
}
'''
checks = r'''
int main(void) {
    struct device dev={RT_Device_Class_Block};
    struct fnode node={"/sd0",&dev};
    struct dfs_fd file={&node,0};
    unsigned char buf[520];
    const size_t sizes[]={0,1,511,512,513,4096,SIZE_MAX};
    const off_t offsets[]={0,1,512,513,10*1024*1024};
    for(size_t p=0;p<sizeof(offsets)/sizeof(offsets[0]);p++) {
        for(size_t n=0;n<sizeof(sizes)/sizeof(sizes[0]);n++) {
            memset(buf,0xa5,sizeof(buf)); file.pos=offsets[p]; calls=0;
            int expected=sizes[n]?-ENOSYS:0;
            assert(dfs_device_fs_read(&file,buf+4,sizes[n])==expected);
            assert(dfs_device_fs_write(&file,buf+4,sizes[n])==expected);
            assert(!calls && file.pos==offsets[p]);
            for(size_t i=0;i<sizeof(buf);i++) assert(buf[i]==0xa5);
        }
    }
    dev.type=RT_Device_Class_Char; file.pos=7; calls=0; result=3;
    assert(dfs_device_fs_read(&file,buf,5)==3);
    assert(calls==1 && count_seen==5 && pos_seen==7 && file.pos==10);
    assert(dfs_device_fs_write(&file,buf,5)==3);
    assert(calls==2 && count_seen==5 && pos_seen==10 && file.pos==13);
    result=-EIO;
    assert(dfs_device_fs_read(&file,buf,5)==-EIO && file.pos==13);
    assert(dfs_device_fs_write(&file,buf,5)==-EIO && file.pos==13);
    node.path="/"; calls=0;
    assert(dfs_device_fs_read(&file,buf,5)==-ENOSYS && calls==0);
    puts("devfs block byte/sector boundaries and character partial/error I/O: PASS");
    return 0;
}
'''
source = SOURCE.read_text()
with tempfile.TemporaryDirectory(prefix='devfs-block-test-') as directory:
    cfile = Path(directory)/'test.c'
    binary = Path(directory)/'test'
    cfile.write_text(preamble+'\n'+function(source,'dfs_device_fs_read')+'\n'+function(source,'dfs_device_fs_write')+'\n'+checks)
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',str(cfile),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
