# Metal optimization round 2

## Scope and isolation

Baseline: `ef0e0ee1f` versus `232e6ba09`. Experiment: `232e6ba09`
versus the same source with native BGRA vertex attributes enabled. The existing
uncommitted GLB/gameplay work is excluded from the performance executables.

The clean `232e6ba09` worktree is `/tmp/chupa-metal-round2-clean`, configured
with `python3 configure.py --macos-metal --bots` and built with `ninja -j8 macos`.
Only `gpu_metal.m` and `xbox_device.c` differ in production between the two
baseline commits. Both are rebuilt from each commit and linked against the
same clean shared objects. Build commands and source/executable SHA-256s are
in `/tmp/metal-round2-builds/build.json`; the build script is
`/tmp/build-metal-round2-reference.py`. This is a controlled source substitution,
not a comparison of the user's existing app bundles.

The gameplay harness now accepts `--window visible`, records the existing
combined frame-slot/drawable wait telemetry and attribution-mode geometry bytes,
and samples the actual Halo descendant rather than a shell wrapper. macOS
physical footprint and RSS are reported separately; neither measures GPU
residency, and sparse samples do not establish a lifetime memory bound.

Example (executables, not `.app` directories):

```sh
HALO_ALLOW_LAUNCH=1 python3 tools/metal_game_profile.py \
  --reference-app /tmp/metal-round2-builds/reference/halo \
  --candidate-app /tmp/metal-round2-builds/candidate/halo \
  --output /tmp/metal-round2-baseline-visible \
  --window visible --seconds 45 --repeats 2
```

Each comparison uses Blood Gulch and Hang 'Em High, 31 combat bots, isolated
configs and offline networking. Requested window size is 1280x720; actual
render size is retained in the JSON (Retina scaling can differ). Native
resolution, AA/PPL/vsync/frame pacing off, uncapped, validation off for timing.
Per scene and mode the order is reference/candidate/candidate/reference.
Attribution timers and throughput runs are separate. First-third reporting
windows are discarded independently per metric. Medians of window p95s are
**not pooled frame percentiles**. Combat is not deterministic replay; changing
camera content/draw counts, compilation and system load limit conclusions.

## Native BGRA implementation and correctness

`gpu_metal_initialize` advertises `vertex_bgra = 1`. No new shader decoding or
pipeline key scheme is introduced:

- `attribute_format` already selects `GPU_ATTRIBUTE_BGRA8` for D3DCOLOR when
  supported. The declaration cache already includes the capability in its key.
- `setup_streams` can now mirror colored streams. Volatile or segment-crossing
  streams still upload, but without `stream_upload_swizzled`'s scratch copy and
  per-vertex R/B exchange. This does not eliminate necessary transient copies.
- `nv2a_msl.c` case 5 reads BGRA bytes as normalized RGBA, including alpha.
  Function constants use the same enum values as the dynamic table path;
  layout changes already participate in pipeline compatibility.
- Constant/immediate attributes remain float values, not BGRA byte arrays.
  Live record resolution, `use_buffer`, rename tracking and mirror volatility
  rules are unchanged. Mirrored-reference bytes are not uploaded bytes.

`test_metal_vertex_bgra.py` exercises the actual extracted frontend with
ASan/UBSan and compiles the production MSL fetch text in a real offscreen backend:

- Shared-stream attributes, mirror hits without transient uploads, fallback
  uploads preserving source bytes, legacy CPU-swizzle behavior and validation.
- BGRA versus CPU-swizzled RGBA pixel parity with distinct red/blue and alpha,
  unaligned offsets, nonzero and zero strides, and constant transitions.
- Both unspecialized and genuinely specialized Metal functions.
- Old red/new blue buffer versions drawn before the same submission, with a
  pool drain between draws, checked pixel-by-pixel after GPU completion.
- Metal API Validation must report its enabled banner; no-device cases skip.

```sh
python3 -m unittest discover -s port/macos/metal/tests -p 'test_metal_vertex_bgra.py'
```

These fixtures do not prove all gameplay image parity or long-run memory
stability. Moving-game timings must not be interpreted as deterministic image
comparisons.

## Checklist verified against current code

| Candidate | Source evidence | Decision |
| --- | --- | --- |
| Native BGRA | CPU swizzle/mirror exclusion was real; both MSL decode modes already exist | Experiment implemented; assess CPU, GPU, bytes and footprint separately |
| Bounded production pools | `gpu_metal_draw` still wraps each draw; only the fixture groups draws | Keep separate; cold shader/pipeline/primitive-conversion paths can allocate variable bytes, so 64 draws alone is not a byte bound |
| Unique-stream traversal | `setup_streams` walks elements twice, validates **every** element, reserves aggregate fallback bytes before uploads; backend resolves/binds 16 attributes | Measure current stage cost first. A unique-stream plan cannot simply omit per-element validation; preserve shared-stream ordering and live resource-use tracking |
| Texture rename preservation | Partial face/level rename loops all faces/mips through temporary `NSMutableData`, even the level about to be overwritten | Confirmed opportunity, frequency unknown. Count renamed textures/preserved bytes before choosing blit or staging; retain version/dependency semantics |
| Readbacks | Three render-target paths commit and wait; shared upload textures use direct `getBytes` | Combined slot/drawable waits already logged; readback count/bytes/wait time still missing. Immediate screenshot/depth consumers cannot silently become asynchronous |
| Clipped draw rejection | Pipeline resolution and `pass_begin` precede `clamp_rect` | Count rejects first. Early return must preserve missing-shader errors, target/pass effects and query behavior. Nonpositive scissor extents currently mean full target, not an empty draw |
| HUD preload | One worker scans queued slots by asset index; `take` may block on queued/decoding assets | Queue insertion order is not worker priority. Measure first-use waits, fallback decodes, load duration and decoded-byte peak before adding priority or a second worker |

Do not add another attribute-table cache based on a repeated-state fixture.
Gameplay attribution records the bind/draw ratio directly. Similarly, do not
increase frames in flight or shader-warm concurrency merely because concurrency
is possible: establish slot/barrier/compile saturation costs first.

## Results

### Historical baseline (Apple M2 Pro, macOS 26.3.1)

Visible-window throughput, median of two independent run summaries per cell,
ms. CPU excludes the separately reported slot/drawable waits:

| Map / commit | CPU median | GPU median | CPU window p95 | Combined wait median |
| --- | ---: | ---: | ---: | ---: |
| Blood Gulch / ef0e0ee | 3.88 | 2.20 | 6.15 | 0.00 |
| Blood Gulch / 232e6ba | 3.80 | 2.53 | 6.05 | 0.00 |
| Hang 'Em High / ef0e0ee | 2.78 | 2.60 | 5.53 | 2.30 |
| Hang 'Em High / 232e6ba | 2.80 | 2.82 | 5.70 | 2.28 |

**No consistent whole-frame improvement is established.** GPU medians are
higher in this sample; moving combat and two short repeats cannot distinguish
scene variation from regression. The combined wait is material even in some
visible runs, so the old hidden-window explanation is not established here.
This telemetry cannot attribute the wait separately to slot versus drawable.

Separate attribution, ns/call (exploratory; short fixture compilations overlapped
some early Blood Gulch attribution windows, not throughput):

| Map / commit | Whole Draw* | prepare_draw | setup_streams | gpu_draw | Attribute binds/draw |
| --- | ---: | ---: | ---: | ---: | ---: |
| Blood Gulch / ef0e0ee | 1438 | 533 | 111 | 680 | 0.987 |
| Blood Gulch / 232e6ba | 1371 | 529 | 102 | 639 | 0.980 |
| Hang 'Em High / ef0e0ee | 1288 | 463 | 82 | 700 | 0.994 |
| Hang 'Em High / 232e6ba | 1278 | 459 | 82 | 703 | 0.993 |

Blood Gulch attribution draws/frame differ (1408 versus 1718); the stage figures
are not matched-scene measurements. Remaining stream traversal is small, and
another attribute-table cache is not justified by these bind rates.

Process physical footprint, MiB, **not additive to GPU storage accounting**:

| Map / commit | Warmed median | Highest sampled peak |
| --- | ---: | ---: |
| Blood Gulch / ef0e0ee | 556.3 | 572.3 |
| Blood Gulch / 232e6ba | 584.5 | 597.6 |
| Hang 'Em High / ef0e0ee | 579.8 | 593.4 |
| Hang 'Em High / 232e6ba | 598.7 | 612.4 |

The original baseline harness's wrapper RSS values are invalid and excluded.
A separate recursive process sampler collected physical footprint every five
seconds throughout all throughput runs (started during the third attribution
run). The first third of samples in each run is discarded for warmed medians;
peaks retain every sample. These are sparse samples, not allocation high-water
proof. The higher footprint is compatible with retained buffer reuse, but is
not attributable solely to that cache from whole-process measurements.

Offscreen backend-only V2 submission, median of five process medians, ns/draw:

| Workload | ef0e0ee | 232e6ba |
| --- | ---: | ---: |
| Same state | 230.7 | 224.9 |
| Changing constants | 405.8 | 407.4 |
| Changing ranges | 259.3 | 263.1 |

These sequential fixture runs do not exercise the frontend index-reuse path or
establish an FPS gain. The initial offscreen runs overlapped a full build and
are preliminary only; the table uses the separately rerun `*-final` artifacts
after gameplay finished. No outliers were trimmed within either retained run.

Artifacts:

- `/tmp/metal-round2-baseline-visible/results.json`: commands, app hashes,
  raw per-run stage/frame/geometry summaries and logs.
- `/tmp/metal-round2-baseline-visible/memory-results.json`: corrected external
  process samples; `/tmp/metal-round2-baseline-memory.json` retains the raw series.
- `/tmp/metal-round2-offscreen-{reference,candidate}-final/`: fixture commands,
  source hashes, all samples and image digests.

### Native BGRA experiment

The actual game passes the existing image-comparison thresholds for menu,
AA-off gameplay, 4x MSAA gameplay and per-pixel lighting, with Metal API
Validation enabled on both sides:

| Case | Mean RGB error / 255 | Bad-pixel fraction |
| --- | ---: | ---: |
| Menu | 0.002 | 0.00% |
| Gameplay / AA off | 0.451 | 0.80% |
| Gameplay / 4x MSAA | 1.117 | 2.08% |
| Gameplay / per-pixel lighting | 0.771 | 1.30% |

The existing mean <= 3/255 and bad-pixel fraction <= 3% thresholds were not
relaxed. These are idle, fixed-seed, same-camera captures, not exact animation
replays. The offscreen test separately asserts exact BGRA/RGBA channel/alpha
parity and in-flight-write colors.

**Capture fixture limitation discovered:** the original noninterpolated
capture stalls on the unchanged BGRA-off reference, both hidden and visible,
after saving frame 0. The stack sample locates the main thread in
`D3DDevice_Present`'s `pending_flips >= 2` condition-variable wait while loading
the UI, not inside `waitUntilCompleted` or `nextDrawable`. Enabling interpolation
on both sides lets all four cases complete. This is a capture-setting workaround,
not a fix or proof of correctness of the noninterpolated pacing path. Production
pacing code is unchanged. The reason the corresponding producer stops advancing
was not established; track this separately from texture/GPU waits.

Artifacts: `/tmp/metal-round2-bgra-interpolated-parity/{results,commands}.json`
and per-case original/difference images/logs. The external runner
`/tmp/metal-round2-visible-parity.py` reuses `metal_regression.run_cases`, appending
only `HALO_HIDDEN_WINDOW=0` and `HALO_INTERPOLATION=true` identically to both
capture commands. Failed original attempts remain in
`/tmp/metal-round2-bgra-parity` and `/tmp/metal-round2-bgra-visible-parity`;
`/tmp/metal-round2-reference-capture.sample` retains the stalled reference stack.

The clean BGRA executable differs from the clean `232e6ba` backend by the
capability assignment only. `/tmp/metal-round2-builds/bgra/build.json` records
its build commands/source/executable hashes. Independent gameplay timing
results are complete: **16/16 visible-window runs**, with two independent runs
per scene/mode/variant. A user-requested pause occurred after run 9. The
interrupted run 10 was excluded and restarted from scratch, not counted as a
completed sample. The seven resumed runs use SHA-256-verified identical saved
executables and the original settings/order. This interruption adds a possible
system/thermal-state confound, especially to the Hang 'Em High attribution pair
that straddles it.

Throughput mode (draw-stage timers off), median of run summaries, ms:

| Map | CPU BGRA off / on | GPU BGRA off / on | CPU window p95 off / on |
| --- | ---: | ---: | ---: |
| Blood Gulch | 2.40 / 3.43 | 2.00 / 2.28 | 4.30 / 5.40 |
| Hang 'Em High | 2.45 / 2.20 | 1.98 / 2.05 | 4.60 / 4.35 |

**No consistent whole-frame improvement is demonstrated.** Blood Gulch CPU and
GPU medians regress in this sample. Hang 'Em High CPU improves, but GPU does
not. These are small, moving-combat samples, not deterministic matched-frame
comparisons, and the result must not be presented as an FPS win. Both throughput
comparisons report combined slot/drawable wait medians of 0.00 ms.

Separate attribution, ns/call:

| Map | Whole Draw* off / on | setup_streams off / on | Stream-upload call off / on | Mirror-refresh call off / on |
| --- | ---: | ---: | ---: | ---: |
| Blood Gulch | 1379 / 1322 | 107 / 92 | 72 / 56 | 36 / 44 |
| Hang 'Em High | 1226 / 1236 | 81 / 81 | 76 / 59 | 33 / 49 |

The lower stream-upload call cost is encouraging but not an isolated measurement
of BGRA conversion. Mirror-refresh call cost increases, and `setup_streams`
improvement does not repeat on Hang 'Em High. Attribution draws/frame differ:
1387 / 1688 on Blood Gulch and 1212 / 1296 on Hang 'Em High (off / on).
Mirrored-reference / streamed KiB per frame also differ with scene content:
Blood Gulch 2340 / 176 versus 2615 / 203; Hang 'Em High 2181 / 126 versus
2337 / 135. **No reduction in total streamed bytes is established by these
unmatched gameplay samples.** The routing fixture independently proves that
eligible mirrored colored geometry avoids transient uploads.

Throughput physical footprint (MiB), median of per-run warmed samples:

| Map | BGRA off | BGRA on |
| --- | ---: | ---: |
| Blood Gulch | 588.7 | 585.4 |
| Hang 'Em High | 597.7 | 606.2 |

Memory is mixed too; sparse 15-second process samples do not establish a memory
improvement, GPU residency or a lifetime high-water bound.

All completed runs passed the harness checks for 31 bot joins, fixed-seed hook,
gameplay duration, rendering errors and absence of validation instrumentation.
The final combined report is
`/tmp/metal-round2-bgra-resumed/results.json`, retaining the nine original rows
unchanged and all seven resumed rows. It includes executable hashes, commands,
raw per-run summaries, process samples and pause provenance. Original logs remain
in `/tmp/metal-round2-bgra-visible`; resumed logs are under
`/tmp/metal-round2-bgra-resumed`. The external resume script is
`/tmp/resume-metal-round2.py`. Hidden-window comparison and the full unit suite
were not resumed as part of this 16-run completion; the six focused correctness
tests and the four image comparisons had already passed.

Claim supported: native BGRA removes the eligible CPU conversion path while
preserving tested rendering correctness. Claim **not** supported: a consistent
whole-game CPU/GPU/frame-time or memory improvement. Stronger performance claims
need longer, matched-workload comparisons; repeated-state backend fixtures alone
cannot resolve these mixed gameplay results.

## Follow-up diagnosis and candidate selection

### Why the Blood Gulch comparison increased

The unconditional backend counters were present even in throughput mode. Warmed
median draws/frame in the four original throughput runs were:

| Run | BGRA off | BGRA on |
| --- | ---: | ---: |
| First pair | 1136.3 | 1644.8 |
| Second pair | 1266.9 | 1644.2 |

Median reference/candidate workloads differ by approximately **37%**, alongside
the reported 43% CPU increase. Player-state logs explain a concrete source of
that mismatch: by tick 900 the local reference player has died and respawned
(`d1`) in both reference runs, while both candidate players still have `d0`.
Positions/view content differ substantially. The fixed random seed does not
make real-time bot combat or camera paths deterministic.

This establishes an important confound, **not** an exact causal allocation of
the extra millisecond to rendering, simulation or BGRA. Dividing independent
CPU/draw medians is not a controlled cost-per-draw experiment. In particular,
a dynamic colored-mirror cost is not ruled out by a static scene.

A new visible-window, fixed idle Blood Gulch comparison uses the same saved
executables, zero bots, no scripted input and interpolation enabled. Eight
35-second runs separate throughput from attribution and use ABBA order in
each mode. The runner rejects a draw-count mismatch greater than 2%; it passes,
with approximately 342 backend draws/frame on both sides.

| Fixed scene / BGRA off vs on | Off | On |
| --- | ---: | ---: |
| Throughput CPU median, ms | 1.0 | 1.0 |
| Throughput GPU median, ms | 0.9 | 0.9 |
| Attribution setup_streams, ns/call | 84.5 | 83.0 |
| Attribution stream upload, ns/call | 183.5 | 88.0 |

The old timing logger rounds frame times to 0.1 ms. This test does **not**
reproduce a 1 ms penalty, but cannot resolve a tiny frame-time gain or prove
matched dynamic-write performance. The upload-call population changes too, so
that stage's lower average is not an isolated CPU-swizzle benchmark.
Artifacts: `/tmp/metal-bgra-fixed-scene/{results.json,*/run.log}`;
runner `/tmp/metal-fixed-scene.py`.

### Production-safe diagnostic additions

`HALO_METAL_WORK_PROFILE=1` enables render-thread-only counters in the actual
backend. It is off by default. There are no new waits, copies, scheduling
changes or shader variants. Existing synchronous readback semantics remain.
Every complete 600-frame mono presentation window reports:

- Draw attempts and post-pipeline scissor rejects.
- Texture uploads, busy-storage renames, preserved face/mip levels and bytes.
- Valid readbacks and bytes, synchronous wait calls and CPU wait duration.
- Separate `nextDrawable` and frame-slot request counts and CPU wait duration.

Partial final windows are not emitted. Reports include loading if it falls in
the window. Direct shared-texture reads count as readbacks without an explicit
command-buffer wait; these timings do not measure GPU execution or preservation
copy time. Disabled collection adds predictable branches and no readback clocks.
Frame CPU/GPU/percentile/wait logs now retain three decimal places rather than
one, without changing the underlying timing calculations. The gameplay runner
explicitly disables work diagnostics in its timing presets.

```sh
# After an isolated diagnostic game run with HALO_METAL_WORK_PROFILE=1:
python3 tools/metal_work_profile.py /path/to/run.log --output /tmp/work.json
python3 -m unittest discover -s port/macos/metal/tests -p 'test_metal_work_profile.py'
```

Parse one `run.log` or `debug.txt`, not both (they duplicate the reports).

A 45-second visible Blood Gulch run with 31 bots, with diagnostics on, produced
20 complete windows (12,000 presented frames):

| Counter | Observed |
| --- | ---: |
| Draw attempts | 14,922,619 |
| Fully clipped draws | 0 |
| Texture uploads / busy renames | 2064 / 99 |
| Preserved mip/face levels / bytes | 0 / 0 |
| Readbacks / explicit readback waits | 0 / 0 |
| nextDrawable total / mean wait | 5110.518 ms / 0.426 ms |
| Frame-slot total / mean wait | 80.040 ms / 0.00667 ms |

These are diagnostic counts, not production-style performance timings. The
preservation, readback and clipped-draw optimizations have no demonstrated
opportunity in this workload. Slot waits are small; increasing frames in flight
is not justified here. Drawable waits exist in a visible window too, so they
cannot all be dismissed as hidden-window behavior. Their driver/compositor/GPU
cause remains unestablished. No asynchronous-readback or blit-preservation change
is promoted on zero observed calls.

Artifacts: `/tmp/metal-work-pool-results/work-bloodgulch/` and its combined
`results.json`; `/tmp/metal-work-pool-trial/build.json` records clean shared-object
provenance and exact diagnostic/trial source/executable hashes. The user's GLB
integration is excluded from these executables.

### Scoped production pool experiment: not promoted

A throwaway executable wraps the actual engine `render_frame` invocation in an
explicit owner scope, then groups only draws inside that scope by 8 or 64.
Draws outside it retain the original per-draw pool. A group drains at its count
limit and at the owner exit; it never crosses `render_frame` return or Present.
The production repository's draw wrapper and common engine code are unchanged.

Seven 35-second visible idle-game runs used one executable, diagnostic counters
and draw-stage timers off, and the same settings except the trial pool size.
No samples were discarded:

| Ordered run | Group size | CPU median, ms | GPU median, ms | Backend draws/frame |
| --- | ---: | ---: | ---: | ---: |
| 1 | 1 | 1.0305 | 0.8550 | 342.40 |
| 2 | 8 | 1.0120 | 0.8655 | 342.00 |
| 3 | 64 | 1.0085 | 0.8665 | 342.00 |
| 4 | 1 | 1.3320 | 1.3090 | 342.00 |
| 5 | 64 | 1.0275 | 0.8560 | 343.85 |
| 6 | 8 | 1.0665 | 0.9060 | 311.55 |
| 7 | 1 | 1.0765 | 0.8570 | 342.00 |

**The experiment fails its final 2% geometry-parity gate** because run 6 has a
different workload; its cause was not established. Run 4 additionally shows a
large CPU/GPU/system or presentation disturbance despite unchanged draw counts.
Do not select the fastest samples or aggregate these into a claimed win. Early
small reductions are not sufficient evidence to change resource lifetime policy.
Maximum observed group counts obey the requested limits, but sparse footprint
samples do not prove a hard autoreleased-byte bound or long-run leak freedom.
The trial has not met production image/lifetime/memory/p95 promotion gates.

The external build and run scripts are `/tmp/build-metal-work-pool-trial.py`
and `/tmp/run-metal-work-pool-trial.py`. All seven completed raw runs remain in
`/tmp/metal-work-pool-results/`; the runner's final assertion is intentionally
left failed rather than marking the comparison successful. No pool-batching
source or engine-scope hooks are installed in the repository.

### Close-out decisions and validation

- Keep BGRA's tested removal of the redundant conversion path; do not claim a
  general FPS improvement or attribute the old 43% difference solely to BGRA.
- Keep opt-in work counters, the parser, tests and higher-resolution timing logs
  as the low-risk measurement slice. These are not an FPS optimization.
- Do not promote grouped production pools from the rejected comparison.
- Defer texture preservation/readback scheduling and clipped-draw changes until
  a relevant workload actually exercises those counters.
- Defer a declaration-plan rewrite: `setup_streams` remains about 80–100 ns/call
  and the broader change has no demonstrated material payoff. Preserve complete
  per-element validation and live resource-use tracking.
- HUD concurrency/priority was not benchmarked this round; no load-bound evidence
  justifies changing its worker count. This checklist item remains open.

The actual-backend counter fixture passes Metal API Validation and checks
preservation bytes, ordinary/depth staged reads, direct reads, invalid requests,
clipped draws, reset behavior and collection disabled. The 115-test full suite
has two existing failures (one overlay harness compilation error and one settings
registry parity failure), both independently reproduced on the clean `232e6ba`
worktree; they are not caused by the diagnostic changes. One test is skipped.
The related BGRA/mirror/index/declaration tests remain separate correctness gates.

All experiment runners/game processes have exited. There is no background job
left to resume automatically. Follow-up diagnostics remain uncommitted; the
BGRA commit is still `8d819e679`.

### Pool experiment rerun (randomized, interleaved)

Twelve 35-second visible idle runs (4 rounds, shuffled group order 1/8/64, same
executable, seed 7). Runs whose backend draws/frame fell outside 338–346 were
excluded from medians (one per group excluded as unmatched or disturbed: e.g.
64 had 332.6 draws and 1.35 ms CPU; 8 had a 1.83 ms CPU outlier). Matched-run
medians (n=3 each):

| Group | CPU ms | GPU ms | CPU window p95 | Footprint MiB |
| --- | ---: | ---: | ---: | ---: |
| 1 (production) | 1.0005 | 0.861 | 1.160 | 553.3 |
| 8 | 1.010 | 0.864 | 1.184 | 553.4 |
| 64 | 1.0155 | 0.8635 | 1.198 | 554.0 |

**No benefit from grouped pools is demonstrated; differences are within noise
and nominally slightly worse.** The earlier 2–5% offscreen gain did not carry
over to the game. Decision: keep per-draw pools; abandon this candidate.
Artifacts: `/tmp/metal-pool-rerun/results.json`, runner `/tmp/run-pool-round2.py`.

### HUD preload (candidate 7): measured, not changed

Temporary timing (not kept) around `hud_preload_take` in three 30-second visible
Blood Gulch runs (`debug.texture_log`, high-res HUD on, no bots): 84 pictures are
queued per map load; 15 of 16 textures used in-game were taken from the preload
with ≤0.001 ms wait each, and one (not queued) was decoded at first draw in every
run. The single worker already finishes ahead of use, so prioritization or a
second worker would save nothing measurable on this hardware. Decision: no
change. Map-load duration and peak memory were not measured; revisit only if
load profiles show the decode thread is a bottleneck. Artifacts: `/tmp/hud-run-{1,2,3}`.
