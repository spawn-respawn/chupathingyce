"""Exercise the actual indexed frontend with mocked GPU/mirror operations.

No game data or Metal device required. Covers mirror reuse, streaming fallback,
quad conversion and Metal's strip restart sentinel with ASan/UBSan enabled.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from metal_paths import include_flags, read

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "gpu.h"
#include "upload_bounds.h"
typedef int BOOL;
typedef uint16_t WORD;
typedef unsigned int UINT;
typedef unsigned int D3DPRIMITIVETYPE;
#define CONST const
#define TRUE 1
#define FALSE 0
#define XBOX_ADDRESS(p) ((uintptr_t)(p))
enum { D3DPT_TRIANGLELIST, D3DPT_TRIANGLESTRIP, D3DPT_LINESTRIP, D3DPT_QUADLIST };
enum { _index_scratch_rebased };
static struct gpu_draw draw_packet, submitted;
static struct gpu_capabilities device_capabilities;
static struct { uint32_t base_vertex_index; } device;
static struct { unsigned long streamed_bytes; } stats;
static int target_hud_measured;
static unsigned long mirror_generation = 1, stream_first, stream_count;
static unsigned uploads, submissions, mirror_calls;
static BOOL mirror_allowed = TRUE;
static WORD uploaded[64], scratch[64], quads[64];
static void hud_draw_extent(const float *vertices, unsigned long count)
{ (void)vertices; (void)count; assert(0); }
#define trace_draw(...) ((void)0)
static BOOL prepare_draw_profiled(struct gpu_draw *draw, BOOL immediate)
{ assert(!immediate); memset(draw, 0, sizeof(*draw)); return TRUE; }
static BOOL setup_streams_profiled(struct gpu_draw *draw, unsigned long first, unsigned long count)
{ (void)draw; stream_first = first; stream_count = count; return TRUE; }
static BOOL mirror_range(unsigned long address, unsigned long size, gpu_buffer *buffer,
    unsigned long *offset, unsigned long *generation)
{
    assert(address && size);
    mirror_calls++;
    if (!mirror_allowed) return FALSE;
    *buffer = 99; *offset = 32; *generation = mirror_generation;
    return TRUE;
}
static uint32_t packet_primitive(D3DPRIMITIVETYPE primitive)
{ return primitive == D3DPT_TRIANGLESTRIP ? GPU_PRIMITIVE_TRIANGLE_STRIP : GPU_PRIMITIVE_TRIANGLES; }
static WORD *index_scratch(int kind, unsigned long count)
{ (void)kind; assert(count <= 64); return scratch; }
static WORD *quad_indices(const WORD *source, unsigned long count, unsigned long *result_count)
{
    assert(count == 4);
    const unsigned order[] = {0, 1, 2, 0, 2, 3};
    for (unsigned i = 0; i < 6; i++) quads[i] = source[order[i]];
    *result_count = 6;
    return quads;
}
static uint32_t index_upload(const void *data, unsigned long bytes, gpu_buffer *buffer)
{
    assert(bytes <= sizeof(uploaded));
    memcpy(uploaded, data, bytes);
    *buffer = 77;
    uploads++;
    return 2;
}
static void submit_draw(const struct gpu_draw *draw, BOOL immediate)
{ assert(!immediate); submitted = *draw; submissions++; }
/* IMPLEMENTATION */
static void reset(void)
{
    uploads = submissions = mirror_calls = 0;
    device_capabilities.base_vertex = 1;
    device.base_vertex_index = 7;
    mirror_allowed = TRUE;
    mirror_generation++;
}
int main(void)
{
    WORD source[] = {100, 103, 102, 101};
    reset();
    D3DDevice_DrawIndexedVertices_unprofiled(D3DPT_TRIANGLELIST, 3, source);
    assert(submissions == 1 && uploads == 0 && mirror_calls == 1);
    assert(submitted.index_buffer == 99 && submitted.index_offset == 32);
    assert(submitted.base_vertex == -100 && submitted.count == 3);
    assert(stream_first == 107 && stream_count == 4);
    /* Stable generation hits the range cache; a newer generation rescans. */
    source[0] = 99;
    D3DDevice_DrawIndexedVertices_unprofiled(D3DPT_TRIANGLELIST, 3, source);
    assert(submitted.base_vertex == -100);
    mirror_generation++;
    D3DDevice_DrawIndexedVertices_unprofiled(D3DPT_TRIANGLELIST, 3, source);
    assert(submitted.base_vertex == -99 && uploads == 0);
    source[0] = 100;
    reset();
    mirror_allowed = FALSE;
    D3DDevice_DrawIndexedVertices_unprofiled(D3DPT_TRIANGLELIST, 3, source);
    assert(uploads == 1 && submitted.index_buffer == 77 && submitted.base_vertex == -100);
    assert(memcmp(uploaded, source, 6) == 0);
    reset();
    device_capabilities.base_vertex = 0;
    D3DDevice_DrawIndexedVertices_unprofiled(D3DPT_TRIANGLELIST, 3, source);
    assert(uploads == 1 && mirror_calls == 0 && submitted.base_vertex == 0);
    assert(uploaded[0] == 0 && uploaded[1] == 3 && uploaded[2] == 2);
    WORD high[] = {65533, 65534, 65535};
    for (unsigned primitive = D3DPT_TRIANGLESTRIP; primitive <= D3DPT_LINESTRIP; primitive++) {
        reset();
        D3DDevice_DrawIndexedVertices_unprofiled(primitive, 3, high);
        assert(uploads == 1 && submitted.base_vertex == 0);
        assert(uploaded[0] == 0 && uploaded[1] == 1 && uploaded[2] == 2);
    }
    reset();
    D3DDevice_DrawIndexedVertices_unprofiled(D3DPT_TRIANGLELIST, 3, high);
    assert(uploads == 0 && submitted.base_vertex == -65533);
    reset();
    D3DDevice_DrawIndexedVertices_unprofiled(D3DPT_QUADLIST, 4, source);
    assert(mirror_calls == 0 && uploads == 1 && submitted.count == 6);
    assert(submitted.base_vertex == -100 && submitted.primitive == GPU_PRIMITIVE_TRIANGLES);
    assert(uploaded[0] == 100 && uploaded[3] == 100 && uploaded[5] == 101);
    reset();
    device.base_vertex_index = UINT32_MAX;
    D3DDevice_DrawIndexedVertices_unprofiled(D3DPT_TRIANGLELIST, 3, source);
    assert(submissions == 0 && uploads == 0);
    reset();
    D3DDevice_DrawIndexedVertices_unprofiled(D3DPT_TRIANGLELIST, 0, source);
    D3DDevice_DrawIndexedVertices_unprofiled(D3DPT_TRIANGLELIST, 3, NULL);
    D3DDevice_DrawIndexedVertices_unprofiled(D3DPT_TRIANGLELIST, METAL_DRAW_VERTICES_MAX + 1, source);
    assert(submissions == 0 && mirror_calls == 0);
    return 0;
}
'''


class IndexReuseTests(unittest.TestCase):
    def test_actual_frontend(self):
        compiler = shutil.which("clang")
        if not compiler:
            self.skipTest("clang required")
        source = read("xbox_device.c")
        start = source.index("#define INDEX_RANGE_SLOTS")
        end = source.index("/* ---------- vertex data */", start)
        extent = source[start:end]
        start = source.index("static void D3DDevice_DrawIndexedVertices_unprofiled(")
        start = source.index("static void D3DDevice_DrawIndexedVertices_unprofiled(", start + 1)
        end = source.index("/* ---------- immediate mode */", start)
        implementation = extent + source[start:end]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            test = path / "indices.c"
            test.write_text(HARNESS.replace("/* IMPLEMENTATION */", implementation))
            executable = path / "indices"
            subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined", *include_flags(),
                            str(test), "-o", str(executable)], check=True, capture_output=True)
            subprocess.run([str(executable)], check=True, capture_output=True)
