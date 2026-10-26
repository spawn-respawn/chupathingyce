"""Native D3DCOLOR routing and real Metal vertex-pulling image/lifetime parity."""
import ast
import os
import platform
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from metal_paths import ROOT, include_flags, read

FRONTEND = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "gpu.h"
#include "draw_profile.h"
#include "upload_bounds.h"
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define METAL_REPORT(...) ((void)0)
#define XGPU_VERTEX_ATTRIBUTE_COUNT 16
#define PLATFORM_CONTIGUOUS_BASE 0x10000U
#define PLATFORM_CONTIGUOUS_SIZE 0x800000U
static unsigned char guest[512], uploaded[64];
#define XBOX_ADDRESS(p) (PLATFORM_CONTIGUOUS_BASE + (uint32_t)((const unsigned char *)(p) - guest))
#define PLATFORM_PHYSICAL_TO_VIRTUAL(p) (guest + ((p) - PLATFORM_CONTIGUOUS_BASE))
enum { D3DVSDT_NONE, D3DVSDT_FLOAT1, D3DVSDT_FLOAT2, D3DVSDT_FLOAT3, D3DVSDT_FLOAT4,
 D3DVSDT_D3DCOLOR, D3DVSDT_SHORT1, D3DVSDT_SHORT2, D3DVSDT_SHORT3, D3DVSDT_SHORT4,
 D3DVSDT_NORMSHORT1, D3DVSDT_NORMSHORT2, D3DVSDT_NORMSHORT3, D3DVSDT_NORMSHORT4,
 D3DVSDT_FLOAT2H, D3DVSDT_NORMPACKED3, D3DVSDT_PBYTE1, D3DVSDT_PBYTE2, D3DVSDT_PBYTE3, D3DVSDT_PBYTE4 };
struct vertex_element { unsigned char reg, stream, type, bytes; unsigned short offset; };
struct vertex_shader_object {
 struct vertex_element elements[16]; unsigned long element_count, packed_mask;
 struct gpu_vertex_attribute attribute_plan[16];
 uint32_t plan_streams, plan_constants; BOOL plan_valid, plan_bgra;
};
static struct gpu_capabilities device_capabilities;
static struct { struct vertex_shader_object *vertex_shader;
 struct { uint32_t data, stride; } streams[16]; float attributes[16][4]; } device;
static struct { unsigned long streamed_bytes; } stats;
static struct draw_profile draw_profile;
static unsigned mirrors, uploads, reservations;
static BOOL mirror_allowed;
static uint32_t uploaded_bytes;
static BOOL mirror_range(unsigned long base, unsigned long bytes, gpu_buffer *buffer,
 unsigned long *offset, unsigned long *generation)
{
 assert(base == PLATFORM_CONTIGUOUS_BASE + 32 && bytes == 48); (void)generation;
 mirrors++; if (!mirror_allowed) return FALSE; *buffer = 99; *offset = 32; return TRUE;
}
void gpu_stream_reserve(uint32_t bytes, uint32_t indices)
{ assert(indices == 0); reservations++; uploaded_bytes = bytes; }
static unsigned long stream_upload(const void *data, unsigned long size, gpu_buffer *buffer)
{ assert(data && size == 48); memcpy(uploaded, data, size); uploads++; *buffer = 77; return 16; }
/* IMPLEMENTATION */
int main(void)
{
 struct vertex_shader_object declaration = {0}; struct gpu_draw draw = {0};
 device.vertex_shader = &declaration;
 declaration.element_count = 3;
 declaration.elements[0] = (struct vertex_element){0, 2, D3DVSDT_FLOAT3, 12, 0};
 declaration.elements[1] = (struct vertex_element){3, 2, D3DVSDT_D3DCOLOR, 4, 12};
 declaration.elements[2] = (struct vertex_element){4, 2, D3DVSDT_D3DCOLOR, 4, 12};
 device.streams[2].data = PLATFORM_CONTIGUOUS_BASE; device.streams[2].stride = 16;
 for (unsigned i = 0; i < sizeof(guest); i++) guest[i] = (unsigned char)i;
 device_capabilities.vertex_bgra = 1; mirror_allowed = TRUE;
 assert(setup_streams(&draw, 2, 3));
 assert(mirrors == 1 && uploads == 0 && uploaded_bytes == 0 && reservations == 1);
 assert(draw.streams[2].buffer == 99 && draw.streams[2].offset == 32);
 assert(draw.attributes[3].format == GPU_ATTRIBUTE_BGRA8);
 assert(draw.attributes[4].format == GPU_ATTRIBUTE_BGRA8);
 mirror_allowed = FALSE;
 assert(setup_streams(&draw, 2, 3));
 assert(mirrors == 2 && uploads == 1 && uploaded_bytes == 48 && stats.streamed_bytes == 48);
 assert(draw.streams[2].buffer == 77 && draw.streams[2].offset == 16);
 assert(draw.attributes[3].format == GPU_ATTRIBUTE_BGRA8);
 assert(memcmp(uploaded, guest + 32, 48) == 0); /* No CPU color conversion. */
 /* Old capability keeps its CPU-swizzle fallback and never asks the mirror. */
 device_capabilities.vertex_bgra = 0; mirror_allowed = TRUE;
 declaration.element_count = 2;
 assert(setup_streams(&draw, 2, 3));
 assert(mirrors == 2 && uploads == 2 && draw.attributes[3].format == GPU_ATTRIBUTE_RGBA8);
 for (unsigned i = 0; i < 48; i++) {
  unsigned component = i % 16;
  unsigned source = component == 12 ? i + 2 : component == 14 ? i - 2 : i;
  assert(uploaded[i] == guest[32 + source]);
 }
 for (unsigned i = 0; i < sizeof(guest); i++) assert(guest[i] == (unsigned char)i);
 declaration.elements[1].offset = 14;
 assert(!setup_streams(&draw, 2, 3));
 return 0;
}
'''

GPU = r'''
#define main original_probe_main
#include "PROBE_PATH"
#undef main
#include "fetch.h"
_Static_assert(GPU_ATTRIBUTE_BGRA8 == 5 && GPU_ATTRIBUTE_RGBA8 == 6, "MSL format numbers");
static void color_readback(gpu_texture target, const unsigned char expected[4])
{
 unsigned char pixels[PROBE_WIDTH*PROBE_WIDTH*4];
 assert(gpu_metal_texture_read(target, pixels, sizeof(pixels)));
 for (unsigned i = 0; i < sizeof(pixels); i++) assert(pixels[i] == expected[i % 4]);
}
static void bgra_test(void)
{
 struct gpu_draw draw = make_draw();
 const char *suffix = "\nstruct O { float4 p [[position]]; float4 color; };\n"
 "vertex O vertex_main(uint v [[vertex_id]], constant AttributeTable &t [[buffer(2)]],"
 "device const uchar *positions [[buffer(10)]], device const uchar *colors [[buffer(13)]]) {"
 "O o; o.p=fetch_attribute(0,v,t,positions,attribute_kind0);"
 "o.p.z=0.5; o.p.w=1; o.color=fetch_attribute(3,v,t,colors,attribute_kind3); return o; }";
 NSString *source = [NSString stringWithFormat:@"#include <metal_stdlib>\nusing namespace metal;\n%s%s", actual_fetch, suffix];
 unsigned char colors[25] = {0};
 /* Deliberately unaligned offset, distinct R/B and nonopaque alpha. */
 const unsigned char expected[] = {17, 81, 203, 129};
 for (unsigned i = 0; i < 3; i++) memcpy(colors + 1 + i*8, expected, 4);
 draw.streams[2].buffer = gpu_metal_buffer_create(sizeof(colors));
 draw.streams[2].stride = 8; draw.streams[2].offset = 1;
 for (unsigned specialized = 0; specialized < 2; specialized++) {
  specialize_off = !specialized;
  draw.vertex_shader = gpu_metal_shader_create(GPU_SHADER_VERTEX, source.UTF8String);
  assert(draw.vertex_shader);
  for (unsigned bgra = 0; bgra < 2; bgra++) {
   unsigned char input[25]; memcpy(input, colors, sizeof(input));
   if (!bgra) for (unsigned i = 0; i < 3; i++) {
    unsigned char *p = input + 1 + i*8; unsigned char b = p[0]; p[0] = p[2]; p[2] = b;
   }
   gpu_metal_buffer_write(draw.streams[2].buffer, 0, sizeof(input), input, 0);
   draw.attributes[3] = (struct gpu_vertex_attribute){bgra ? GPU_ATTRIBUTE_BGRA8 : GPU_ATTRIBUTE_RGBA8, 2, 0};
   assert(gpu_metal_draw(&draw, &constants, &uniforms));
   color_readback(draw.color_target, expected);
   /* Zero stride selects the format's packed size (4), not a constant. */
   unsigned char packed[25] = {0};
   for (unsigned i = 0; i < 3; i++) memcpy(packed + 1 + i*4, input + 1, 4);
   gpu_metal_buffer_write(draw.streams[2].buffer, 0, sizeof(packed), packed, 0);
   draw.streams[2].stride = 0;
   assert(gpu_metal_draw(&draw, &constants, &uniforms));
   color_readback(draw.color_target, expected);
   draw.streams[2].stride = 8;
  }
  draw.attributes[3].stream = GPU_STREAM_CONSTANT;
  for (unsigned c = 0; c < 4; c++) draw.constant_values[3][c] = expected[c == 0 ? 2 : c == 2 ? 0 : c]/255.0f;
  assert(gpu_metal_draw(&draw, &constants, &uniforms));
  color_readback(draw.color_target, expected);
  draw.attributes[3].stream = 2;
  MetalShader *record = table_get(shaders, draw.vertex_shader);
  if (specialized) {
   assert(record->specialized.count >= 2);
   for (id function in record->specialized.allValues) assert(function != record->function);
  }
  /* Old red and new blue versions coexist before submission. */
  unsigned char red[25] = {0}, blue[25] = {0};
  for (unsigned i = 0; i < 3; i++) {
   red[1+i*8+2] = red[1+i*8+3] = 255;
   blue[1+i*8] = blue[1+i*8+3] = 255;
  }
  draw.attributes[3].format = GPU_ATTRIBUTE_BGRA8;
  draw.scissor.width = PROBE_WIDTH/2;
  gpu_metal_buffer_write(draw.streams[2].buffer, 0, sizeof(red), red, 0);
  assert(gpu_metal_draw(&draw, &constants, &uniforms));
  @autoreleasepool { gpu_metal_buffer_write(draw.streams[2].buffer, 0, sizeof(blue), blue, 0); }
  draw.scissor.x = PROBE_WIDTH/2;
  assert(gpu_metal_draw(&draw, &constants, &uniforms));
  finish_batch(draw.color_target, 1);
  draw.scissor = (struct gpu_rect){0,0,PROBE_WIDTH,PROBE_WIDTH};
 }
}
int main(void)
{
 @autoreleasepool {
  if (!MTLCreateSystemDefaultDevice()) return 77;
  probe_layer = [CAMetalLayer layer]; struct gpu_capabilities capabilities;
  gpu_metal_initialize(GPU_INITIALIZE_NO_PIPELINE_ARCHIVE, &capabilities);
  assert(capabilities.vertex_bgra);
  bgra_test(); dispatch_group_wait(completion_handlers, DISPATCH_TIME_FOREVER);
 }
 return 0;
}
'''


class VertexBgraTests(unittest.TestCase):
    def test_frontend_mirror_and_stream_routing(self):
        compiler = shutil.which("clang")
        if not compiler:
            self.skipTest("clang required")
        source = read("xbox_device.c")
        start = source.index("static unsigned long stream_upload_swizzled(")
        end = source.index("static uint32_t packet_primitive(", start)
        implementation = source[start:end]
        # Native uploads preserve guest bytes; legacy conversion must not mutate them.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            test = path / "routing.c"
            test.write_text(FRONTEND.replace("/* IMPLEMENTATION */", implementation))
            subprocess.run([compiler, "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-fsanitize=address,undefined", *include_flags(),
                            str(test), "-o", str(path / "routing")], check=True)
            subprocess.run([str(path / "routing")], check=True)

    def test_metal_decode_and_live_writes(self):
        if platform.system() != "Darwin" or not shutil.which("xcrun"):
            self.skipTest("macOS/Metal required")
        source = read("nv2a_msl.c")
        start = source.index("static const char msl_vertex_fetch[] =")
        end = source.index("static const char *const varyings", start)
        # Compile the production fetch text, not a second implementation of decoding.
        literals = re.findall(r'"(?:[^"\\]|\\.)*"', re.sub(r"/\*.*?\*/", "", source[start:end], flags=re.S))
        self.assertTrue(literals)
        self.assertIn("case 5:", "".join(ast.literal_eval(s) for s in literals))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "fetch.h").write_text("static const char actual_fetch[] =\n" + "\n".join(literals) + ";\n")
            (path / "bgra.m").write_text(GPU.replace("PROBE_PATH", str(ROOT / "tools/metal_draw_probe.m")))
            command = ["xcrun", "clang", "-std=gnu11", "-fobjc-arc", "-O2", "-g", "-Wall",
                       "-Wno-unused-function", "-mmacosx-version-min=13.0", *include_flags(),
                       "-idirafter", "/opt/homebrew/include", str(path / "bgra.m"),
                       str(ROOT / "port/macos/metal/renderer/metal_report.c"),
                       "-framework", "Metal", "-framework", "Foundation", "-framework", "QuartzCore",
                       "-framework", "MetalFX", "-Wl,-dead_strip", "-o", str(path / "bgra")]
            subprocess.run(command, check=True)
            run = subprocess.run([str(path / "bgra")], env={**os.environ, "MTL_DEBUG_LAYER": "1"},
                                 capture_output=True, text=True, timeout=120)
            if run.returncode == 77:
                self.skipTest("no Metal device")
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("Metal API Validation Enabled", run.stderr)
