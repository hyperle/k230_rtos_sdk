"""Run real display handoff functions with separate CPU-cache/device buffers."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
SOURCE = ROOT / 'src/canmv/port/modules/modmedia.display.c'


def function(source, name):
    start = source.index('static int ' + name + '(')
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


preamble = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kd_display.h"
#include "mpi_sys_api.h"
#include "mpi_vb_api.h"
/* Only the image fields accessed by these two production functions are modeled. */
typedef struct { int w, h; void *data; k_u64 phy_addr; k_u32 pool_id; } image_t;
static size_t image_size(image_t *image) { return (size_t)image->w * image->h * 2; }
static int copies, flushes, pushes, flush_error;
static unsigned char physical[16], source_pixels[8], cached[16];
static void *hal_rvv_memcpy(void *dst, const void *src, size_t size) {
    ++copies; return memcpy(dst, src, size);
}
k_s32 kd_mpi_sys_mmz_flush_cache(k_u64 pa, void *va, k_u32 size) {
    ++flushes;
    assert(pa == (uintptr_t)physical && size <= sizeof(physical));
    if (flush_error) return flush_error;
    memcpy(physical, va, size);
    return 0;
}
k_s32 kd_mpi_vo_insert_frame(k_vo_layer_id layer, const k_video_frame_info *frame) {
    (void)layer; ++pushes;
    assert(frame->v_frame.phys_addr[0] == (uintptr_t)physical);
    assert(memcmp(physical, source_pixels, sizeof(source_pixels)) == 0);
    return 0;
}
'''
checks = r'''
int main(int argc, char **argv) {
    assert(argc == 2);
    int which = atoi(argv[1]);
    memset(physical, 0x81, sizeof(physical));
    memset(cached, 0x31, sizeof(cached));
    for (size_t i = 0; i < sizeof(source_pixels); ++i) source_pixels[i] = i + 1;
    image_t image = {2, 2, source_pixels, (uintptr_t)physical, 7};
    py_display_buffer_t buffer = {0};
    buffer.valid = 1;
    buffer.buffer_pool_id = 7;
    buffer.buffer_addr = cached;
    buffer.buffer_size = sizeof(source_pixels);
    buffer.vf_info.v_frame.phys_addr[0] = (uintptr_t)physical;
    int ret;
    switch (which) {
    case 0: /* Copy, flush, and publish a fully synchronized image. */
        ret = py_display_show_image_to_buffer(K_VO_LAYER_OSD0, &image, &buffer, PIXEL_FORMAT_RGB_565, 2);
        assert(!ret && copies == 1 && flushes == 1 && pushes == 1);
        assert(cached[sizeof(source_pixels)] == 0x31);
        break;
    case 1: /* Direct display also synchronizes before submission. */
        ret = py_display_show_image_direct(K_VO_LAYER_OSD0, &image, PIXEL_FORMAT_RGB_565, 2);
        assert(!ret && !copies && flushes == 1 && pushes == 1);
        break;
    case 2: /* Failed copy-path flush must never publish stale device memory. */
        flush_error = -41;
        ret = py_display_show_image_to_buffer(K_VO_LAYER_OSD0, &image, &buffer, PIXEL_FORMAT_RGB_565, 2);
        assert(ret && copies == 1 && flushes == 1 && !pushes);
        break;
    case 3: /* Same rule for direct display. */
        flush_error = -42;
        ret = py_display_show_image_direct(K_VO_LAYER_OSD0, &image, PIXEL_FORMAT_RGB_565, 2);
        assert(ret && !copies && flushes == 1 && !pushes);
        break;
    case 4: /* A reused buffer too small for this image must not publish a prefix. */
        buffer.buffer_size = sizeof(source_pixels) - 1;
        ret = py_display_show_image_to_buffer(K_VO_LAYER_OSD0, &image, &buffer, PIXEL_FORMAT_RGB_565, 2);
        assert(ret && !copies && !flushes && !pushes);
        break;
    case 5: /* Invalid borrowed state never reaches hardware. */
        buffer.valid = 0;
        ret = py_display_show_image_to_buffer(K_VO_LAYER_OSD0, &image, &buffer, PIXEL_FORMAT_RGB_565, 2);
        assert(ret && !copies && !flushes && !pushes);
        break;
    case 6: /* A failed publication can be retried with current pixels. */
        flush_error = -43;
        ret = py_display_show_image_to_buffer(K_VO_LAYER_OSD0, &image, &buffer, PIXEL_FORMAT_RGB_565, 2);
        assert(ret && !pushes);
        flush_error = 0;
        ret = py_display_show_image_to_buffer(K_VO_LAYER_OSD0, &image, &buffer, PIXEL_FORMAT_RGB_565, 2);
        assert(!ret && copies == 2 && flushes == 2 && pushes == 1);
        break;
    default: return 2;
    }
    return 0;
}
'''
source = SOURCE.read_text()
start = source.index('typedef struct _py_display_buffer {')
end = source.index('} py_display_buffer_t;', start) + len('} py_display_buffer_t;')
with tempfile.TemporaryDirectory(prefix='display-buffer-test-') as directory:
    cfile, binary = Path(directory) / 'test.c', Path(directory) / 'test'
    cfile.write_text(preamble + source[start:end] + '\n' +
                     function(source, 'py_display_show_image_to_buffer') + '\n' +
                     function(source, 'py_display_show_image_direct') + '\n' + checks)
    includes = ('include', 'src/rtsmart/libs/rtsmart_hal/utils', 'src/rtsmart/mpp/userapps/api',
                'src/rtsmart/mpp/userapps/api/framework', 'src/rtsmart/mpp/include',
                'src/rtsmart/mpp/include/comm')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-Wno-int-conversion', *['-I' + str(ROOT / p) for p in includes],
                    str(cfile), '-o', str(binary)], check=True)
    failed = []
    for case in range(7):
        result = subprocess.run([str(binary), str(case)], capture_output=True, text=True)
        if result.returncode:
            failed.append(case)
            print(f'FAIL display case {case}: {result.stderr.strip()}')
    assert not failed, f'display buffer regression cases failed: {failed}'
    print('Display CPU/device-cache handoff, capacity and retry: 7 cases PASS')
