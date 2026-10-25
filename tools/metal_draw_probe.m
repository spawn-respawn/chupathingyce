/* Offscreen fixture for the actual backend, not a second draw implementation.
No game data/window/present path. Timing excludes compilation, clear, commit,
readback and GPU waits. Counters are collected in a separate untimed batch. */
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../port/macos/metal/renderer/gpu_metal.m"

static CAMetalLayer *probe_layer;

void *host_sdl_metal_layer(void) { return (__bridge void *)probe_layer; }
void host_config_string(const char *name, const char *fallback, char *out, size_t capacity)
{
    (void)name;
    snprintf(out, capacity, "%s", fallback);
}
void host_fatal(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    exit(1);
}
void platform_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
}
void metal_overlay_prepare(int width, int height, struct metal_overlay_frame *frame)
{
    (void)width; (void)height;
    memset(frame, 0, sizeof(*frame));
}

static const float vertices[12] = { -1,-1, 3,-1, -1,3, -1,-1, 3,-1, -1,3 };
static struct gpu_constant_store constants;
static struct gpu_uniforms uniforms;

enum { PROBE_DRAWS = 2048, PROBE_RUNS = 7, PROBE_WIDTH = 16, PROBE_POOL_DRAWS_MAX = 64 };
static unsigned pool_group_draws = 1;

/* Only the fixture may bypass the production pool wrapper. Every such call
must be inside a bounded group pool, drained before submission/readback. */
static uint32_t probe_submit_draw(const struct gpu_draw *draw)
{
    if (pool_group_draws > 1)
        return gpu_metal_draw_in_pool(draw, &constants, &uniforms);
    return gpu_metal_draw(draw, &constants, &uniforms);
}

static void probe_draw_one(struct gpu_draw *draw, int mode, unsigned index)
{
    draw->streams[0].offset = mode == 2 ? (index & 1)*24 : 0;
    if (mode == 1) {
        constants.c[0][0] = (index & 1) ? 0.25f : 0.125f;
        constants.serial++;
    }
    assert(probe_submit_draw(draw) == 1);
}

static struct gpu_draw make_draw(void)
{
    const char *vertex_source = "#include <metal_stdlib>\nusing namespace metal;\n"
        "struct A { uint format, stream, offset, stride; };\n"
        "struct T { A entries[16]; float4 constants[16]; };\n"
        "struct O { float4 p [[position]]; float4 color; };\n"
        "vertex O vertex_main(uint v [[vertex_id]], constant T &t [[buffer(2)]],\n"
        " device const uchar *data [[buffer(10)]], constant float4 *c [[buffer(0)]]) {\n"
        " O o; device const float2 *p = (device const float2 *)(data+t.entries[0].offset+v*t.entries[0].stride);\n"
        " o.p=float4(*p,0.5,1); o.color=c[0]; return o; }\n";
    const char *pixel_source = "#include <metal_stdlib>\nusing namespace metal;\n"
        "struct O { float4 p [[position]]; float4 color; };\n"
        "fragment float4 fragment_main(O i [[stage_in]], texture2d<float> t [[texture(0)]],\n"
        " sampler s [[sampler(0)]]) { return i.color*t.sample(s,float2(0.5)); }\n";
    struct gpu_texture_description target = { .type = GPU_TEXTURE_2D, .format = GPU_FORMAT_BGRA8,
        .usage = GPU_USAGE_RENDER_TARGET, .width = PROBE_WIDTH, .height = PROBE_WIDTH, .depth = 1, .levels = 1 };
    struct gpu_texture_description texel = { .type = GPU_TEXTURE_2D, .format = GPU_FORMAT_BGRA8,
        .usage = GPU_USAGE_UPLOAD, .width = 1, .height = 1, .depth = 1, .levels = 1 };
    struct gpu_draw draw = {0};
    uint32_t white = 0xffffffff;

    draw.color_target = gpu_metal_texture_create(&target);
    draw.vertex_shader = gpu_metal_shader_create(GPU_SHADER_VERTEX, vertex_source);
    draw.pixel_shader = gpu_metal_shader_create(GPU_SHADER_PIXEL, pixel_source);
    draw.viewport = (struct gpu_viewport){0, 0, PROBE_WIDTH, PROBE_WIDTH, 0, 1};
    draw.scissor = (struct gpu_rect){0, 0, PROBE_WIDTH, PROBE_WIDTH};
    draw.blend.color_write_mask = 15;
    draw.primitive = GPU_PRIMITIVE_TRIANGLES;
    draw.count = 3;
    draw.streams[0].buffer = gpu_metal_buffer_create(sizeof(vertices));
    draw.streams[0].stride = 8;
    gpu_metal_buffer_write(draw.streams[0].buffer, 0, sizeof(vertices), vertices, 0);
    for (int i = 0; i < GPU_ATTRIBUTE_COUNT; i++)
        draw.attributes[i] = (struct gpu_vertex_attribute){GPU_ATTRIBUTE_FLOAT4, GPU_STREAM_CONSTANT, 0};
    draw.attributes[0] = (struct gpu_vertex_attribute){GPU_ATTRIBUTE_FLOAT2, 0, 0};
    draw.stages[0].texture = gpu_metal_texture_create(&texel);
    draw.stages[0].type = GPU_TEXTURE_2D;
    draw.stages[0].sampler.min_filter = draw.stages[0].sampler.mag_filter = GPU_FILTER_POINT;
    gpu_metal_texture_upload(draw.stages[0].texture, 0, 0, &white, sizeof(white));
    constants.c[0][0] = 0.25f; constants.c[0][1] = 0.5f;
    constants.c[0][2] = 0.75f; constants.c[0][3] = 1;
    constants.serial = uniforms.serial = 1;
    return draw;
}

/* Commit after the timer, then read all pixels through an explicitly checked
command buffer. Backend frame serials/slots advance exactly as at Present. */
static uint64_t finish_batch(gpu_texture color, int split)
{
    id<MTLBuffer> readback = [device newBufferWithLength:PROBE_WIDTH*PROBE_WIDTH*4
        options:MTLResourceStorageModeShared];
    assert(readback);
    pass_end();
    id<MTLBlitCommandEncoder> blit = [command_buffer() blitCommandEncoder];
    assert(blit);
    [blit copyFromTexture:texture_record(color)->texture sourceSlice:0 sourceLevel:0
        sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(PROBE_WIDTH,PROBE_WIDTH,1)
        toBuffer:readback destinationOffset:0 destinationBytesPerRow:PROBE_WIDTH*4
        destinationBytesPerImage:PROBE_WIDTH*PROBE_WIDTH*4];
    [blit endEncoding];
    commit(YES);
    [last_committed waitUntilCompleted];
    assert(last_committed.status == MTLCommandBufferStatusCompleted);
    frames++;
    const unsigned char *pixels = readback.contents;
    uint64_t digest = 14695981039346656037ull;
    unsigned colored = 0;
    for (int y = 0; y < PROBE_WIDTH; y++) for (int x = 0; x < PROBE_WIDTH; x++) {
        const unsigned char *p = pixels + (y*PROBE_WIDTH+x)*4;
        if (p[0] || p[1] || p[2]) colored++;
        if (split > 0) {
            assert(p[0] == (x < PROBE_WIDTH/2 ? 0 : 255));
            assert(p[1] == 0 && p[2] == (x < PROBE_WIDTH/2 ? 255 : 0) && p[3] == 255);
        } else if (split == 0) {
            assert(p[0] >= 190 && p[0] <= 192 && p[1] >= 127 && p[1] <= 129);
            assert(p[2] >= 63 && p[2] <= 65 && p[3] == 255);
        }
        for (int c = 0; c < 4; c++) digest = (digest ^ p[c]) * 1099511628211ull;
    }
    if (split < 0) assert(colored != 0);
    return digest;
}

static double draw_batch(struct gpu_draw *draw, int mode, int counted, uint64_t *digest)
{
    struct gpu_clear clear = { .flags = GPU_CLEAR_COLOR, .color = 0xff000000 };
    metal_profile = (struct draw_profile){ .enabled = counted };
    prepared_native_reset();
    assert(pass_begin(draw->color_target, 0, &clear));
    double started = CACurrentMediaTime();
    assert(pool_group_draws >= 1);
    assert(pool_group_draws <= PROBE_POOL_DRAWS_MAX);
    assert(PROBE_DRAWS % pool_group_draws == 0);
    if (pool_group_draws == 1) {
        for (unsigned i = 0; i < PROBE_DRAWS; i++)
            probe_draw_one(draw, mode, i);
    } else {
        for (unsigned first = 0; first < PROBE_DRAWS; first += pool_group_draws) {
            @autoreleasepool {
                for (unsigned item = 0; item < pool_group_draws; item++)
                    probe_draw_one(draw, mode, first + item);
            }
        }
    }
    double ns = (CACurrentMediaTime() - started)*1e9/PROBE_DRAWS;
    *digest = finish_batch(draw->color_target, 0);
    return ns;
}

static void transition_test(struct gpu_draw draw)
{
    uint32_t red = 0xffff0000, blue = 0xff0000ff, white = 0xffffffff;
    struct gpu_clear clear = { .flags = GPU_CLEAR_COLOR, .color = 0xff000000 };
    metal_profile = (struct draw_profile){ .enabled = 1 };
    for (int c = 0; c < 4; c++) constants.c[0][c] = 1;
    constants.serial++;
    draw.streams[0].offset = 0;
    draw.viewport.width = draw.scissor.width = PROBE_WIDTH/2;
    gpu_metal_texture_upload(draw.stages[0].texture, 0, 0, &red, 4);
    assert(pass_begin(draw.color_target, 0, &clear));
    @autoreleasepool {
        assert(probe_submit_draw(&draw));
    } /* Drain before the old storage is renamed and before GPU submission. */
    /* Both storage writes happen before submission/completion. The left draw
    must retain the old texel/buffer, the right draw must see the new ones. */
    id<MTLBuffer> previous = buffer_record(draw.streams[0].buffer)->buffer;
    gpu_metal_buffer_write(draw.streams[0].buffer, 0, sizeof(vertices), vertices, 0);
    assert(buffer_record(draw.streams[0].buffer)->buffer != previous);
    gpu_metal_texture_upload(draw.stages[0].texture, 0, 0, &blue, 4);
    pass_end(); /* identical prepared state, new encoder must rebind */
    draw.viewport.x = draw.scissor.x = PROBE_WIDTH/2;
    draw.depth_stencil.stencil_reference = 7; /* dynamic, not a depth object key */
    @autoreleasepool {
        assert(probe_submit_draw(&draw));
    } /* Native resources must survive this drain through GPU completion. */
    finish_batch(draw.color_target, 1);
    assert(metal_profile.attribute_table_binds == 2);
    unsigned long expected = prepared_native_enabled ? 1 : 2;
    assert(metal_profile.pipeline_lookups == expected && metal_profile.depth_lookups == expected);
    gpu_metal_texture_upload(draw.stages[0].texture, 0, 0, &white, 4);
    constants.c[0][0] = 0.25f; constants.c[0][1] = 0.5f; constants.c[0][2] = 0.75f;
    constants.serial++;
}

/* Rejected/clipped draws must not disturb the caller-owned pool or prevent
valid work after its drain. No GPU encoding occurs for either early return. */
static void pool_early_return_test(struct gpu_draw draw)
{
    metal_profile = (struct draw_profile){ .enabled = 1 };
    struct gpu_draw invalid = draw;
    invalid.pixel_shader = 0;
    @autoreleasepool {
        assert(probe_submit_draw(&invalid) == 0);
        invalid = draw;
        invalid.scissor.x = PROBE_WIDTH * 2;
        assert(probe_submit_draw(&invalid) == 1);
    }
    assert(metal_profile.calls[DRAW_PROFILE_ENCODE] == 0);
    @autoreleasepool {
        assert(probe_submit_draw(&draw) == 1);
    }
    assert(metal_profile.calls[DRAW_PROFILE_ENCODE] == 1);
    finish_batch(draw.color_target, 0);
}

/* Match the frontend's rebased stream window with unmodified 16-bit indices.
Exercise signed vertex_id adjustment in both native and converted primitives. */
static void indexed_base_vertex_test(struct gpu_draw draw)
{
    const uint32_t primitives[] = { GPU_PRIMITIVE_TRIANGLES, GPU_PRIMITIVE_TRIANGLE_STRIP,
        GPU_PRIMITIVE_TRIANGLE_FAN, GPU_PRIMITIVE_LINE_LOOP, GPU_PRIMITIVE_LINE_STRIP };
    const int32_t bases[] = { 0, -65533, -32768, 3 };
    struct gpu_clear clear = { .flags = GPU_CLEAR_COLOR, .color = 0xff000000 };
    gpu_buffer indices = gpu_metal_buffer_create(8);
    const float line_vertices[] = { -.75f,-.75f, .75f,-.75f, -.75f,.75f,
        -.75f,-.75f, .75f,-.75f, -.75f,.75f };
    gpu_buffer lines = gpu_metal_buffer_create(sizeof(line_vertices));
    gpu_metal_buffer_write(lines, 0, sizeof(line_vertices), line_vertices, 0);
    draw.index_buffer = indices;
    draw.index_offset = 2;
    draw.streams[0].offset = 0;
    for (unsigned p = 0; p < sizeof(primitives)/sizeof(primitives[0]); p++) {
        draw.primitive = primitives[p];
        if (p >= 3) draw.streams[0].buffer = lines;
        uint64_t reference = 0;
        for (unsigned b = 0; b < sizeof(bases)/sizeof(bases[0]); b++) {
            uint16_t source[] = { 0xffff, 0, 1, 2 };
            int32_t base = bases[b];
            /* The frontend rebases strips containing Metal's restart sentinel. */
            if ((p == 1 || p == 4) && base == -65533) base = 0;
            for (unsigned i = 1; i < 4; i++)
                source[i] = (uint16_t)((int32_t)i - 1 - (base < 0 ? base : 0));
            gpu_metal_buffer_write(indices, 0, sizeof(source), source, 0);
            draw.base_vertex = base;
            /* Positive base selects the second identical triangle. */
            assert(pass_begin(draw.color_target, 0, &clear));
            assert(probe_submit_draw(&draw));
            uint64_t digest = finish_batch(draw.color_target, p >= 3 ? -1 : 0);
            if (!b) reference = digest;
            else assert(digest == reference);
        }
    }
    [buffers remove:indices];
    [buffers remove:lines];
}

static void buffer_cache_assert_accounting(void)
{
    NSUInteger bytes = 0;
    for (id item in buffers->objects) {
        if (![item isKindOfClass:[MetalBuffer class]]) continue;
        MetalBuffer *record = item;
        for (unsigned i = 0; i < BUFFER_RETIRED_MAX; i++)
            if (record->retired[i]) bytes += buffer_storage_bytes(record->retired[i]);
    }
    assert(bytes == buffer_retired_bytes);
    assert(bytes <= BUFFER_RETIRED_BYTES_MAX);
    assert(buffer_writes.cache_peak_bytes <= BUFFER_RETIRED_BYTES_MAX);
}

/* Fill the real global budget across distinct buffers, then test rejection,
recycling at capacity, record destruction, and a single oversized allocation. */
static void buffer_cache_budget_test(void)
{
    enum { BYTES = 4 * 1024 * 1024, PAGE = 4096,
        HANDLES = BUFFER_RETIRED_BYTES_MAX / BYTES + 1 };
    assert(buffer_retired_bytes == 0);
    @autoreleasepool {
        gpu_buffer handles[HANDLES];
        unsigned char patch[PAGE];
        memset(patch, 23, PAGE);
        uint64_t skipped = buffer_writes.cache_skipped;
        for (unsigned i = 0; i < HANDLES; i++) {
            handles[i] = gpu_metal_buffer_create(BYTES);
            MetalBuffer *record = buffer_record(handles[i]);
            memset(record->buffer.contents, 0, BYTES);
            id<MTLBuffer> old = record->buffer;
            use_buffer(record);
            gpu_metal_buffer_write(handles[i], 0, PAGE, patch, 0);
            assert(record->buffer != old && ((unsigned char *)old.contents)[0] == 0);
            assert(memcmp(record->buffer.contents, patch, PAGE) == 0);
            buffer_cache_assert_accounting();
        }
        assert(buffer_writes.cache_skipped > skipped);
        MetalBuffer *record = buffer_record(handles[0]);
        unsigned slot = 0;
        for (unsigned i = 0; i < BUFFER_RETIRED_MAX; i++)
            if (record->retired[i]) { slot = i; break; }
        assert(record->retired[slot]);
        NSUInteger before = buffer_retired_bytes;
        assert(buffer_storage_bytes(record->retired[slot]) == buffer_storage_bytes(record->buffer));
        record->retired_used[slot] = 0; /* only this version has completed */
        uint64_t reused = buffer_writes.reused;
        use_buffer(record);
        gpu_metal_buffer_write(handles[0], PAGE, PAGE, patch, 0);
        assert(buffer_writes.reused == reused + 1);
        assert(buffer_retired_bytes == before);
        assert(memcmp(record->buffer.contents, patch, PAGE) == 0);
        assert(memcmp((unsigned char *)record->buffer.contents + PAGE, patch, PAGE) == 0);
        buffer_cache_assert_accounting();
        for (unsigned i = 0; i < HANDLES; i++) [buffers remove:handles[i]];
    }
    assert(buffer_retired_bytes == 0); /* deallocation returns all charges */
    @autoreleasepool {
        gpu_buffer handle = gpu_metal_buffer_create(BUFFER_RETIRED_BYTES_MAX + PAGE);
        MetalBuffer *record = buffer_record(handle);
        uint64_t skipped = buffer_writes.cache_skipped;
        use_buffer(record);
        assert(buffer_rename(record, 0, record->buffer.length)); /* full overwrite: no copy */
        assert(buffer_writes.cache_skipped == skipped + 1);
        assert(buffer_retired_bytes == 0);
        [buffers remove:handle];
    }
    buffer_cache_assert_accounting();
}

/* GPU blits read both versions after the CPU has updated a page. Repeat over
completed frames so the real completion serial gates retired-storage reuse. */
static void buffer_gpu_lifetime_test(void)
{
    enum { BYTES = 16384, PAGE = 4096 };
    gpu_buffer handle = gpu_metal_buffer_create(BYTES);
    MetalBuffer *record = buffer_record(handle);
    unsigned char expected[BYTES], patch[PAGE];
    memset(expected, 17, BYTES);
    gpu_metal_buffer_write(handle, 0, BYTES, expected, 0);
    uint64_t reused = buffer_writes.reused;
    for (unsigned round = 0; round < 4; round++) {
        unsigned char before[BYTES];
        memcpy(before, expected, BYTES);
        id<MTLBuffer> readback = [device newBufferWithLength:BYTES * 2 options:MTLResourceStorageModeShared];
        assert(readback);
        pass_end();
        id<MTLBlitCommandEncoder> blit = [command_buffer() blitCommandEncoder];
        [blit copyFromBuffer:record->buffer sourceOffset:0 toBuffer:readback destinationOffset:0 size:BYTES];
        [blit endEncoding];
        use_buffer(record);
        memset(patch, (int)round + 40, PAGE);
        gpu_metal_buffer_write(handle, round * PAGE, PAGE, patch, 0);
        memcpy(expected + round * PAGE, patch, PAGE);
        blit = [command_buffer() blitCommandEncoder];
        [blit copyFromBuffer:record->buffer sourceOffset:0 toBuffer:readback destinationOffset:BYTES size:BYTES];
        [blit endEncoding];
        use_buffer(record);
        commit(YES);
        [last_committed waitUntilCompleted];
        dispatch_group_wait(completion_handlers, DISPATCH_TIME_FOREVER);
        assert(last_committed.status == MTLCommandBufferStatusCompleted);
        assert(memcmp(readback.contents, before, BYTES) == 0);
        assert(memcmp((unsigned char *)readback.contents + BYTES, expected, BYTES) == 0);
    }
    assert(buffer_writes.reused > reused);
    [buffers remove:handle];
}

static void buffer_busy_eviction_test(gpu_buffer handle, unsigned char *expected)
{
    enum { BYTES = 4 * 1024 * 1024, PAGE = 4096 };
    MetalBuffer *record = buffer_record(handle);
    unsigned char patch[PAGE];
    /* Exhaust the bounded cache without completing a serial. Never recycle
    busy storage, even if that means allocating and evicting a busy entry. */
    for (unsigned i = 0; i < BUFFER_RETIRED_MAX; i++)
        if (record->retired[i]) record->retired_used[i] = current_serial;
    uint64_t reused = buffer_writes.reused;
    uint64_t allocated = buffer_writes.allocated;
    NSMutableArray<id<MTLBuffer>> *pending = [NSMutableArray array];
    for (unsigned step = 0; step < BUFFER_RETIRED_MAX + 1; step++) {
        [pending addObject:record->buffer];
        use_buffer(record);
        memset(patch, (int)step + 90, PAGE);
        gpu_metal_buffer_write(handle, 0, PAGE, patch, 0);
        memcpy(expected, patch, PAGE);
        for (id<MTLBuffer> old in pending) assert(record->buffer != old);
        assert(buffer_writes.reused == reused);
        assert(buffer_writes.allocated == allocated + step + 1);
        assert(memcmp(record->buffer.contents, expected, BYTES) == 0);
    }
    /* Full overwrite and zero/invalid writes do no preservation work. */
    uint64_t copied = buffer_writes.copied_bytes;
    use_buffer(record);
    gpu_metal_buffer_write(handle, 0, BYTES, expected, 0);
    assert(buffer_writes.copied_bytes == copied);
    id<MTLBuffer> first = record->buffer;
    use_buffer(record);
    gpu_metal_buffer_write(handle, 0, 0, patch, 0);
    gpu_metal_buffer_write(handle, BYTES, PAGE, patch, 0);
    assert(record->buffer == first);
}

/* Controlled serials model draws of the CPU-written mirror without requiring
frame pacing. transition_test separately checks real queued-draw lifetimes. */
static void buffer_recycle_test(void)
{
    enum { BYTES = 4 * 1024 * 1024, PAGE = 4096 };
    gpu_buffer handle = gpu_metal_buffer_create(BYTES);
    MetalBuffer *record = buffer_record(handle);
    unsigned char *expected = calloc(1, BYTES);
    unsigned char patch[PAGE];
    assert(expected);
    gpu_metal_buffer_write(handle, 0, BYTES, expected, 0);
    id<MTLBuffer> first = record->buffer;
    uint64_t allocated = buffer_writes.allocated, reused = buffer_writes.reused;
    uint64_t copied = buffer_writes.copied_bytes;
    memset(patch, 1, PAGE);
    use_buffer(record);
    gpu_metal_buffer_write(handle, PAGE, PAGE, patch, 0);
    memset(expected + PAGE, 1, PAGE);
    assert(record->buffer != first);
    assert(buffer_writes.allocated == allocated + 1);
    assert(buffer_writes.copied_bytes == copied + BYTES - PAGE);
    assert(memcmp(first.contents, expected, PAGE * 2) != 0); /* old version untouched */
    assert(!((unsigned char *)first.contents)[PAGE]);
    /* Mark only the retired version completed. It must catch up with the
    previous patch, not copy four MiB, when a disjoint page is changed. */
    for (unsigned i = 0; i < BUFFER_RETIRED_MAX; i++) record->retired_used[i] = 0;
    use_buffer(record);
    copied = buffer_writes.copied_bytes;
    memset(patch, 2, PAGE);
    gpu_metal_buffer_write(handle, 3 * PAGE, PAGE, patch, 0);
    memset(expected + 3 * PAGE, 2, PAGE);
    assert(record->buffer == first);
    assert(buffer_writes.reused == reused + 1);
    assert(buffer_writes.copied_bytes == copied + PAGE);
    assert(memcmp(record->buffer.contents, expected, BYTES) == 0);
    /* UNUSED and idle writes still invalidate all retired copies. */
    for (unsigned step = 0; step < 64; step++) {
        uint32_t offset = (step * 7919U) % (BYTES - PAGE);
        memset(patch, (int)step + 3, PAGE);
        for (unsigned i = 0; i < BUFFER_RETIRED_MAX; i++) record->retired_used[i] = 0;
        record->used = step % 3 == 0 ? 0 : current_serial;
        gpu_metal_buffer_write(handle, offset, PAGE, patch, step % 3 == 1 ? GPU_WRITE_UNUSED : 0);
        memcpy(expected + offset, patch, PAGE);
        assert(memcmp(record->buffer.contents, expected, BYTES) == 0);
    }
    buffer_busy_eviction_test(handle, expected);
    [buffers remove:handle];
    free(expected);
}

static unsigned long expected_lookup(int changed)
{
    return prepared_native_enabled ? (unsigned long)changed : 1;
}

static void pipeline_transition_test(struct gpu_draw draw)
{
    struct draw_attributes attributes;
    draw_attributes_decide(&draw, &attributes);
    draw.blend.enable = 1;
    draw.blend.source = GPU_BLEND_ONE;
    draw.blend.destination = GPU_BLEND_ZERO;
    draw.blend.operation = GPU_BLEND_OP_ADD;
    struct gpu_draw cases[8];
    for (int i = 0; i < 8; i++) cases[i] = draw;
    cases[0].blend.source = GPU_BLEND_SOURCE_ALPHA;
    cases[1].blend.destination = GPU_BLEND_ONE;
    cases[2].blend.operation = GPU_BLEND_OP_REVERSE_SUBTRACT;
    cases[3].blend.color_write_mask = 7;
    cases[4].blend.alpha_separate = 1; cases[4].blend.alpha_source = GPU_BLEND_ZERO;
    cases[5].blend.alpha_separate = 1; cases[5].blend.alpha_source = GPU_BLEND_ONE;
    cases[5].blend.alpha_destination = GPU_BLEND_ONE;
    cases[6].pixel_shader = [shaders add:table_get(shaders, draw.pixel_shader)];
    cases[7].vertex_shader = [shaders add:table_get(shaders, draw.vertex_shader)];
    id base = draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid);
    assert(base);
    for (int i = 0; i < 8; i++) {
        unsigned long before = metal_profile.pipeline_lookups;
        assert(draw_pipeline(&cases[i], &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
        assert(metal_profile.pipeline_lookups == before + 1);
        before = metal_profile.pipeline_lookups;
        assert(draw_pipeline(&cases[i], &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
        assert(metal_profile.pipeline_lookups == before + expected_lookup(0));
        assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid) == base);
    }
    unsigned long before = metal_profile.pipeline_lookups;
    struct gpu_draw invalid = draw;
    invalid.pixel_shader = 0;
    assert(!draw_pipeline(&invalid, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
    assert(metal_profile.pipeline_lookups == before + 1);
    before = metal_profile.pipeline_lookups;
    draw.blend.color = 0xff123456; /* dynamic blend color is not a pipeline key */
    assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid) == base);
    assert(metal_profile.pipeline_lookups == before + expected_lookup(0));
    attributes.kinds[0] = GPU_ATTRIBUTE_FLOAT3;
    before = metal_profile.pipeline_lookups;
    assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
    assert(metal_profile.pipeline_lookups == before + 1);
    before = metal_profile.pipeline_lookups;
    assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatDepth32Float_Stencil8));
    assert(metal_profile.pipeline_lookups == before + 1);
    gpu_metal_warm_begin(); gpu_metal_warm_end();
    before = metal_profile.pipeline_lookups;
    assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatDepth32Float_Stencil8));
    assert(metal_profile.pipeline_lookups == before + 1);
    assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
    before = metal_profile.pipeline_lookups;
    assert(draw_pipeline(&draw, &attributes, 1, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
    assert(metal_profile.pipeline_lookups == before + 1); /* exact-border shader variant */
    assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
    if ([device supportsTextureSampleCount:2]) {
        struct gpu_texture_description target = texture_record(draw.color_target)->description;
        target.sample_count = 2;
        draw.color_target = gpu_metal_texture_create(&target);
        before = metal_profile.pipeline_lookups;
        assert(draw_pipeline(&draw, &attributes, 0, MTLPixelFormatBGRA8Unorm, MTLPixelFormatInvalid));
        assert(metal_profile.pipeline_lookups == before + 1); /* live MSAA storage changed */
        gpu_metal_texture_destroy(draw.color_target);
    }
}

static void depth_transition_test(void)
{
    struct gpu_depth_stencil_state base = { .depth_test = 1, .depth_function = GPU_COMPARE_ALWAYS,
        .stencil_test = 1, .stencil_function = GPU_COMPARE_ALWAYS,
        .stencil_read_mask = 255, .stencil_write_mask = 255 };
    struct gpu_depth_stencil_state cases[11];
    for (int i = 0; i < 11; i++) cases[i] = base;
    cases[0].depth_test = 0; cases[1].depth_write = 1;
    cases[2].depth_function = GPU_COMPARE_LESS; cases[3].stencil_test = 0;
    cases[4].stencil_function = GPU_COMPARE_EQUAL; cases[5].stencil_fail = GPU_STENCIL_REPLACE;
    cases[6].stencil_depth_fail = GPU_STENCIL_INCREMENT_CLAMP;
    cases[7].stencil_pass = GPU_STENCIL_INVERT;
    cases[8].stencil_read_mask = 127; cases[9].stencil_write_mask = 127;
    cases[10].stencil_function = GPU_COMPARE_NEVER;
    id original = depth_state(&base);
    assert(original);
    for (int i = 0; i < 11; i++) {
        unsigned long before = metal_profile.depth_lookups;
        assert(depth_state(&cases[i]));
        assert(metal_profile.depth_lookups == before + 1);
        before = metal_profile.depth_lookups;
        assert(depth_state(&cases[i]));
        assert(metal_profile.depth_lookups == before + expected_lookup(0));
        assert(depth_state(&base) == original);
    }
    unsigned long before = metal_profile.depth_lookups;
    base.stencil_reference = 42; base.stencil_read_mask |= 0xff00;
    assert(depth_state(&base) == original);
    assert(metal_profile.depth_lookups == before + expected_lookup(0));
}

int main(int argc, const char **argv)
{
    @autoreleasepool {
        if (!MTLCreateSystemDefaultDevice()) return 77;
        probe_layer = [CAMetalLayer layer];
        struct gpu_capabilities capabilities;
        gpu_metal_initialize(GPU_INITIALIZE_NO_PIPELINE_ARCHIVE | GPU_INITIALIZE_NO_SPECIALIZE, &capabilities);
        for (int argument = 1; argument < argc; argument++) {
            if (!strcmp(argv[argument], "--native-off"))
                prepared_native_enabled = NO;
            else if (!strcmp(argv[argument], "--pool-group=8"))
                pool_group_draws = 8;
            else if (!strcmp(argv[argument], "--pool-group=64"))
                pool_group_draws = 64;
            else
                host_fatal("Unknown probe option: %s\n", argv[argument]);
        }
        struct gpu_draw draw = make_draw();
        assert(draw.vertex_shader && draw.pixel_shader && draw.color_target);
        assert(capabilities.base_vertex);
        buffer_cache_budget_test();
        transition_test(draw);
        buffer_gpu_lifetime_test();
        buffer_recycle_test();
        indexed_base_vertex_test(draw);
        pool_early_return_test(draw);
        static const char *names[] = { "same-state", "changing-constants", "changing-ranges" };
        for (int mode = 0; mode < 3; mode++) {
            uint64_t digest = 0;
            for (int warm = 0; warm < 2; warm++) draw_batch(&draw, mode, 0, &digest);
            printf("{\"workload\":\"%s\",\"draws\":%u,\"ns_per_draw\":[", names[mode], PROBE_DRAWS);
            for (int run = 0; run < PROBE_RUNS; run++) {
                double ns = draw_batch(&draw, mode, 0, &digest);
                printf("%s%.3f", run ? "," : "", ns);
            }
            draw_batch(&draw, mode, 1, &digest); /* counters never contaminate timings */
            assert(metal_profile.calls[DRAW_PROFILE_ENCODE] == PROBE_DRAWS);
            unsigned long lookups = prepared_native_enabled ? 1 : PROBE_DRAWS;
            assert(metal_profile.pipeline_lookups == lookups && metal_profile.depth_lookups == lookups);
            assert(metal_profile.attribute_table_binds == (mode == 2 ? PROBE_DRAWS : 1));
            printf("],\"digest\":\"%016llx\",\"shader_lookups\":%lu,\"pipeline_lookups\":%lu,"
                "\"depth_lookups\":%lu,\"attribute_binds\":%lu,\"pool_group_draws\":%u,"
                "\"draw_pool_drains\":%u}\n", (unsigned long long)digest,
                metal_profile.shader_lookups, metal_profile.pipeline_lookups, metal_profile.depth_lookups,
                metal_profile.attribute_table_binds, pool_group_draws, PROBE_DRAWS / pool_group_draws);
        }
        pipeline_transition_test(draw);
        depth_transition_test();
        dispatch_group_wait(completion_handlers, DISPATCH_TIME_FOREVER);
        return 0;
    }
}
