# Native Metal BGRA vertex colors

Metal now advertises `vertex_bgra`. The existing frontend emits
`GPU_ATTRIBUTE_BGRA8` for D3DCOLOR, bypassing CPU R/B swizzling and allowing
eligible colored streams to use the geometry mirror. Volatile geometry still
streams, without color conversion. No shader, pipeline-key, mirror-policy or
resource-lifetime implementation changes are needed: dynamic and specialized
MSL vertex fetching already decode BGRA bytes as normalized RGBA.

## Verification

On Apple M2 Pro, macOS 26.3.1:

```sh
python3 -m unittest discover -s port/macos/metal/tests -p 'test_metal_vertex_bgra.py'
python3 -m unittest discover -s port/macos/metal/tests -p 'test_metal_mirror_policy.py'
python3 -m unittest discover -s port/macos/metal/tests -p 'test_metal_prepared_state.py'
python3 -m unittest discover -s port/macos/metal/tests -p 'test_metal_index_reuse.py'
```

All six tests pass. The new frontend fixture uses the actual extracted routing
code with ASan/UBSan. It checks mirror hits without transient uploads, shared
stream declarations, unchanged native upload bytes, legacy CPU-swizzle fallback,
and invalid declarations. The GPU fixture compiles the production MSL fetch text
and requires Metal API Validation. It checks exact channel/alpha parity against
CPU-swizzled RGBA, unaligned offsets, explicit/format-derived strides, constant
attributes, genuinely specialized and dynamic functions, and old/new vertex
buffer versions drawn before the same submission.

Clean game executables built from `232e6ba`, differing only in the BGRA capability,
also pass the existing image-comparison limits (mean RGB error <= 3/255,
bad-pixel fraction <= 3%) with Metal API Validation on both sides:

| Case | Mean RGB error / 255 | Bad-pixel fraction |
| --- | ---: | ---: |
| Menu | 0.002 | 0.00% |
| Gameplay, AA off | 0.451 | 0.80% |
| Gameplay, 4x MSAA | 1.117 | 2.08% |
| Gameplay, per-pixel lighting | 0.771 | 1.30% |

These are fixed-seed idle captures with matching camera positions, not exact
animation replays. The original noninterpolated capture fixture stalls in the
unchanged reference's `D3DDevice_Present` pending-flips condition-variable wait.
The successful comparison uses visible windows and enables interpolation
identically on both sides; this is a fixture workaround, not a pacing fix.
No image tolerances were relaxed.

Local artifacts: `/tmp/metal-round2-bgra-interpolated-parity/` contains commands,
original/difference images, logs and results; `/tmp/metal-round2-builds/` contains
clean baseline/BGRA executable and source hashes and build commands.
`/tmp/metal-round2-visible-parity.py` runs the existing comparison harness with
the two explicit capture-setting overrides.

The historical `ef0e0ee` versus `232e6ba` gameplay baseline is complete. The
independent BGRA gameplay performance comparison is still in progress at this
checkpoint. **No FPS, GPU-time or whole-process-memory improvement is claimed.**
Long-run memory stability and broader moving-scene parity remain follow-up gates.
