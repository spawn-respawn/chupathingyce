"""Offscreen checks of actual opt-in texture, wait and clipping telemetry."""
import os
import platform
import runpy
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from metal_paths import ROOT, include_flags

summarize = runpy.run_path(str(ROOT / "tools/metal_work_profile.py"))["summarize"]

HARNESS = r'''
#define main original_probe_main
#include "PROBE_PATH"
#undef main

static void profile_test(void)
{
    struct gpu_draw draw = make_draw();
    unsigned char pixels[PROBE_WIDTH * PROBE_WIDTH * 4];
    struct gpu_texture_description mip_description = {
        .type = GPU_TEXTURE_2D, .format = GPU_FORMAT_BGRA8, .usage = GPU_USAGE_UPLOAD,
        .width = 2, .height = 2, .depth = 1, .levels = 2
    };
    uint32_t white[4] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
    gpu_texture mip = gpu_metal_texture_create(&mip_description);
    gpu_metal_texture_upload(mip, 0, 0, white, sizeof(white));
    gpu_metal_texture_upload(mip, 0, 1, white, 4);
    draw.stages[0].texture = mip;
    memset(&work_profile, 0, sizeof(work_profile));
    work_profile.enabled = YES;
    assert(gpu_metal_draw(&draw, &constants, &uniforms));
    /* Updating a busy nonzero mip preserves both existing levels. */
    gpu_metal_texture_upload(mip, 0, 1, white, 4);
    assert(work_profile.texture_uploads == 1 && work_profile.texture_renames == 1);
    assert(work_profile.preserved_levels == 2 && work_profile.preserved_bytes == 20);
    assert(gpu_metal_texture_read(draw.color_target, pixels, sizeof(pixels)));
    assert(gpu_metal_texture_read(mip, pixels, sizeof(pixels)));
    assert(!gpu_metal_texture_read(draw.color_target, pixels, 1));
    assert(work_profile.readbacks == 2 && work_profile.readback_waits == 1);
    assert(work_profile.readback_bytes == sizeof(pixels) + 16);
    assert(work_profile.readback_wait_seconds >= 0.0);
    struct gpu_texture_description depth_description = {
        .type = GPU_TEXTURE_2D, .format = GPU_FORMAT_DEPTH_STENCIL,
        .usage = GPU_USAGE_RENDER_TARGET, .width = PROBE_WIDTH, .height = PROBE_WIDTH,
        .depth = 1, .levels = 1
    };
    gpu_texture depth = gpu_metal_texture_create(&depth_description);
    struct gpu_clear clear = { .depth_target = depth, .flags = GPU_CLEAR_DEPTH, .depth = 0.5f };
    float depths[PROBE_WIDTH * PROBE_WIDTH];
    struct gpu_rect whole = { 0, 0, PROBE_WIDTH, PROBE_WIDTH };
    gpu_metal_clear(&clear, &whole, 1);
    assert(gpu_metal_texture_read(depth, depths, sizeof(depths)));
    for (unsigned i = 0; i < PROBE_WIDTH * PROBE_WIDTH; i++) assert(depths[i] == 0.5f);
    assert(work_profile.readbacks == 3 && work_profile.readback_waits == 2);
    assert(work_profile.readback_bytes == sizeof(pixels) + sizeof(depths) + 16);
    draw.scissor.x = PROBE_WIDTH + 1;
    assert(gpu_metal_draw(&draw, &constants, &uniforms));
    assert(work_profile.draws == 2 && work_profile.clipped == 1);
    assert(work_profile.slot_requests <= 1);
    /* A report drains counters, not GPU resources or enabled state. */
    work_profile_report();
    assert(work_profile.enabled && !work_profile.draws && !work_profile.readbacks);
    work_profile.enabled = NO;
    work_readback(100, YES, 1.0);
    assert(!work_profile.readbacks && !work_profile.readback_waits);
    assert(gpu_metal_texture_read(draw.color_target, pixels, sizeof(pixels)));
    assert(!work_profile.readbacks);
    pass_end();
    commit(YES);
    [last_committed waitUntilCompleted];
    assert(last_committed.status == MTLCommandBufferStatusCompleted);
}

int main(void)
{
    @autoreleasepool {
        if (!MTLCreateSystemDefaultDevice()) return 77;
        probe_layer = [CAMetalLayer layer];
        struct gpu_capabilities capabilities;
        gpu_metal_initialize(GPU_INITIALIZE_NO_PIPELINE_ARCHIVE | GPU_INITIALIZE_NO_SPECIALIZE,
            &capabilities);
        assert(!work_profile.enabled);
        profile_test();
        dispatch_group_wait(completion_handlers, DISPATCH_TIME_FOREVER);
    }
    return 0;
}
'''


class WorkProfileTests(unittest.TestCase):
    def test_parser_sums_complete_windows_and_separates_waits(self):
        window = ("Metal work: draws 100 clipped 2\n"
                  "Metal work: texture uploads 8 renames 1 preserved levels 3 bytes 4096\n"
                  "Metal work: readbacks 2 bytes 1024 waits 1 wait_ms 1.500\n"
                  "Metal work: drawable requests 600 wait_ms 120.000 slot requests 600 wait_ms 6.000\n")
        report = summarize("Metal: work profiling enabled (diagnostic counters)\n" + window * 2)
        self.assertEqual(report["windows"], 2)
        self.assertEqual(report["totals"]["preserved_bytes"], 8192)
        self.assertEqual(report["clipped_draw_fraction"], 0.02)
        self.assertEqual(report["mean_drawable_wait_ms"], 0.2)
        self.assertEqual(report["mean_slot_wait_ms"], 0.01)
        with self.assertRaisesRegex(ValueError, "not enabled"):
            summarize(window)
        with self.assertRaisesRegex(ValueError, "incomplete"):
            summarize("work profiling enabled (diagnostic counters)\n" + window.split("Metal work: readbacks")[0])
        with self.assertRaisesRegex(ValueError, "invalid"):
            summarize("work profiling enabled (diagnostic counters)\n" + window.replace("clipped 2", "clipped 101"))

    def test_actual_backend_counters_and_disabled_path(self):
        if platform.system() != "Darwin" or not shutil.which("xcrun"):
            self.skipTest("macOS/Metal required")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / "work.m").write_text(HARNESS.replace("PROBE_PATH", str(ROOT / "tools/metal_draw_probe.m")))
            command = ["xcrun", "clang", "-std=gnu11", "-fobjc-arc", "-O2", "-g", "-Wall",
                       "-Wno-unused-function", "-mmacosx-version-min=13.0", *include_flags(),
                       "-idirafter", "/opt/homebrew/include", str(path / "work.m"),
                       str(ROOT / "port/macos/metal/renderer/metal_report.c"),
                       "-framework", "Metal", "-framework", "Foundation", "-framework", "QuartzCore",
                       "-framework", "MetalFX", "-Wl,-dead_strip", "-o", str(path / "work")]
            subprocess.run(command, check=True)
            run = subprocess.run([str(path / "work")],
                                 env={**os.environ, "MTL_DEBUG_LAYER": "1", "HALO_METAL_WORK_PROFILE": "0"},
                                 capture_output=True, text=True, timeout=120)
            if run.returncode == 77:
                self.skipTest("no Metal device")
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("Metal API Validation Enabled", run.stderr)
            self.assertIn("texture uploads 1 renames 1 preserved levels 2 bytes 20", run.stderr)
            self.assertIn("draws 2 clipped 1", run.stderr)
