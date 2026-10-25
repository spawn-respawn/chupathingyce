# Metal index and buffer-storage reuse

## Indexed draws

Metal now advertises `base_vertex`. The frontend's existing indexed path binds
vertex streams starting at `base_vertex_index + minimum_index`, then submits
unmodified indices with `baseVertex = -minimum_index`. Metal applies that signed
offset to `vertex_id`, including the backend's vertex-pulling shaders.

Eligible static index ranges use the existing mirror and generation-keyed extent
cache: no per-draw rebasing pass or transient index upload. Volatile or
segment-crossing ranges still stream. Quads still expand into triangle indices;
fans and line loops still convert inside Metal, with the base offset preserved.
Non-indexed primitive conversions keep a zero base offset.

Metal reserves `0xffff` as an indexed-strip restart marker **before** adding the
base vertex. Triangle/line strips whose maximum source index is `0xffff` retain
the old CPU-rebasing path rather than newly interpreting high indices as restart.
This preserves the previous behavior; it does not repair the pre-existing case
where a strip spans the full 0..65535 range and rebasing cannot remove the marker.

## Buffer renaming

`MetalBuffer` lazily retains at most two retired storage versions. Each tracks its
last GPU-use serial and the bounding interval of CPU writes since retirement.

- A retired version is reused only after its last serial completes.
- Before reuse, only its stale interval is copied from current storage, excluding
  bytes the incoming write replaces.
- Every write invalidates retired versions, including idle and `GPU_WRITE_UNUSED`
  writes. The latter still relies on the caller's existing unused-range contract.
- If no completed version exists, allocate as before. Preserve only the prefix
  and suffix outside the incoming write; a full overwrite needs no preservation.
- Busy cache evictions remain alive through Metal command-buffer retention. No
  new waits or render-pass breaks are introduced.

The 4 MiB mirror segmentation and page policy are unchanged. Retired storage now
has a **32 MiB global cache budget** (`BUFFER_RETIRED_BYTES_MAX`), in addition to
the two-version limit per buffer. Admission charges the larger of Metal's
`allocatedSize` and the buffer payload length, so allocation padding is included.
Busy versions retained by the cache count against the same budget. Replacing or
recycling an entry returns its old charge before admitting another; destroying a
buffer record returns all its charges.

If storage does not fit, it is not cached: command buffers keep any in-flight
reference alive, and renaming still allocates safely without new waits or render
pass breaks. A single allocation larger than 32 MiB is never cached. Admission
checks do not scan or evict other buffers, so cold cached entries can cause new
buffers to fall back to the original allocation/copy path. The budget was chosen
to cover the observed 16–20 MiB working set with headroom, not from an FPS claim.

This caps **cache-owned storage only**, not total process or Metal memory. Current
buffers, textures, transients and versions retained solely by in-flight commands
are outside it. Previously the per-buffer-only cache allowed a theoretical 1 GiB
of extra retained mirror storage; that cache ceiling is now replaced by 32 MiB.
Widely separated updates can still make the stale bounding interval large; this
is bounded bookkeeping, not a promise of page-sized copies for every workload.

Every 600 frames, buffer-write telemetry reports rename allocations, recycled
versions, bytes preserved and bytes uploaded. It also reports current/peak cache
bytes, the global budget and skipped admissions. These upload bytes cover
`gpu_metal_buffer_write`, not transient streaming copies. Correlate this with
mirror-refresh timing and gameplay frame-time/memory measurements.

## Verification

On Apple M2 Pro, the actual backend's offscreen probe passed with Metal API
Validation in native, native-off, pool8 and pool64 modes. Checks include:

- Image parity for zero, negative and positive base offsets; nonzero index-buffer
  offsets; 16-bit high indices; native strips; converted fans and loops.
- GPU blit snapshots of old and updated buffer versions across completion/reuse.
- Cache exhaustion without completion, in-place/UNUSED writes, invalid/empty
  writes, partial/full overwrites and byte-for-byte mirror coherence.
- Global-budget saturation across multiple buffers, busy-version immutability
  on rejected admission, reuse at capacity, oversized-buffer rejection and charge
  cleanup on record destruction. An independent table sum checks live accounting
  and the 32 MiB ceiling.
- In a deterministic 4 MiB buffer fixture, a recycled version needs only 4 KiB of
  preservation after a previous disjoint 4 KiB write, versus 4 MiB with the old
  whole-buffer copy. First-time allocations still preserve untouched contents.

The extracted frontend and mirror-policy tests also pass with ASan/UBSan. The
frontend test asserts zero uploads on mirror hits, generation-based extent-cache
invalidation, streaming/quad fallbacks and strip-sentinel rebasing. Production
`xbox_device.o` and `gpu_metal.o` compile through the existing Ninja build.

Reproduce (use fresh output directories). If the working tree also includes the
GLB renderer integration, set `CPATH=/opt/homebrew/include` for the standalone
probe so it can find the SDL3 headers; the production build already supplies that
include path:

```sh
python3 -m unittest discover -s port/macos/metal/tests -p 'test_metal_index_reuse.py'
python3 -m unittest discover -s port/macos/metal/tests -p 'test_metal_mirror_policy.py'
python3 tools/metal_draw_benchmark.py --output /tmp/metal-reuse-validation \
  --rounds 1 --validation --pool-experiment
python3 tools/metal_draw_benchmark.py --output /tmp/metal-reuse-timing --rounds 5
```

The timing fixture measures backend draw submission, not the frontend index path
or gameplay speedup. No reference/candidate FPS benchmark or map-transition memory
soak has been run. The short gameplay memory comparison below does not establish
long-run leak freedom or a total-memory improvement.

## Rat Race memory comparison (2026-10-24)

Historical comparison **before adding the global cache budget**: four valid
90-second offline runs, reference/candidate/candidate/reference, on
Apple M2 Pro. Each confirmed seven bot joins, eight named players, two teams in
`team_slayer`, the fixed regression seed and gameplay through tick 2550. Requested
1280x720 window, actual Retina drawable 2560x1440, 4x MSAA, 120 FPS cap,
per-pixel lighting off, Metal API Validation off. Moving bots are not a deterministic
replay; run-to-run variation and other applications' memory pressure still apply.

Both variants use the same bot-enabled shared objects and libraries. The reference
substitutes HEAD's pre-optimization frontend/backend; the candidate uses the working
copy's optimized files. Source/executable SHA-256 hashes are retained in `build.json`.
Temporary instrumentation, identical on both sides, samples Metal allocation and
buffer-table totals every 600 frames. It is not installed in production source.

The last two runs additionally sampled macOS `proc_pid_rusage(RUSAGE_INFO_V2)`
physical footprint once per second; warmed medians discard the first 30 seconds.
RSS varied sharply during these runs and should not be read as total owned memory.
The two accounting views below overlap and **must not be added together**.

| Metric, footprint-measured pair | Before | After |
| --- | ---: | ---: |
| Warmed median process physical footprint | 737.5 MiB | 780.1 MiB |
| Peak sampled process physical footprint | 765.6 MiB | 814.9 MiB |
| Peak sampled Metal `currentAllocatedSize` | 464.8 MiB | 488.7 MiB |
| Current non-transient buffer storage, last sample | 20 MiB | 24 MiB |
| Transient storage, last sample | 96 MiB | 96 MiB |
| Retained retired-buffer cache, last sample | 0 MiB | 20 MiB |

Across both candidate runs, retired storage ended at 16–20 MiB; across both
reference runs, sampled Metal peaks were 464.8–475.3 MiB versus 483.9–488.7 MiB
for the candidate. The footprint pair's roughly 43 MiB increase is whole-process
accounting, not a measurement attributable solely to the retired cache. Directly
counted buffer storage increased by 24 MiB in that pair (4 MiB current, 20 MiB
retired). No claim of reduced memory use follows from reducing copy bandwidth.

### What was bounded before?

- The mirror allocated lazily in 4 MiB segments, at most 512 MiB of current
  geometry-buffer storage over the desktop guest window. This is a capacity limit,
  not a statement that the game committed all 512 MiB.
- Old versions created by renaming were kept by queued/in-flight Metal commands
  and released after completion. There was no explicit byte limit or per-buffer
  version-count limit. Three frames in flight limits frame backlog, **not** how
  many renames one frame can record.
- Transient memory had 64 chunk slots per stream/snapshot pool per frame slot.
  Chunks are at least 16 MiB, can be larger for oversized requests, and are reused
  at their high-water mark. The scene used six ordinary chunks: 96 MiB total.
- Textures, pipeline caches and the rest of the game have their own allocations;
  there was no unified renderer/process memory budget.

Artifacts: `/tmp/chupa-ratrace-memory-audit/`, valid runs `2-reference-bots`,
`3-candidate-bots`, `4-candidate-footprint`, `5-reference-footprint`. Build/run
scripts: `/tmp/build-chupa-memory-audit.py`, `/tmp/run-chupa-memory-audit.py`.
The preliminary `1-reference` run was rejected: its executable lacked bot support
and its sampler followed a shell rather than the game. It is excluded above.
The valid runs use a separate bot-enabled Ninja graph and a recursive descendant
sampler that selects the actual `halo` process. User configs and maps were not
modified.

### Global-budget follow-up

A further 90-second Rat Race/team-slayer run with seven bots and the same display
settings passed after introducing the global budget. All eight players were
confirmed and gameplay reached tick 2523. Production cache telemetry peaked at
**20 MiB against the 32 MiB limit**, with no skipped admissions in this workload.
The offscreen tests above separately force saturation and rejected admissions.

Sampled warmed process footprint was 774.7 MiB, sampled peak footprint 798.1 MiB,
and sampled peak Metal allocation 487.3 MiB. These are one follow-up run, not a
new paired performance comparison or evidence of a hard total-memory cap.
Artifacts: `/tmp/chupa-ratrace-global-budget/1-budgeted-footprint/`; build and run
scripts: `/tmp/build-chupa-global-budget.py`, `/tmp/run-chupa-global-budget.py`.
