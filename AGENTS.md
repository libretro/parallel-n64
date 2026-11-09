# AGENTS.md

Rules for anyone, human or agent, changing this core. Accuracy comes first:
the same guest-visible bytes, order and timing. Within that, the goal is that
a game never hitches and no work is done that the hardware did not need done.

## First rule: never compile a pipeline on the draw path

A shader, program or pipeline is never compiled because a draw needs it. That
holds on the emulation thread, the RDP command thread, the GL context thread and
anywhere a draw waits for the result, on a cold cache as much as a warm one.
"Only the first time" is the hitch this rule exists to stop.

- Everything a game can reach is built at content load, or by a worker or the
  driver's own threads without anyone waiting for it.
- On a miss, draw with something already built that renders the same. If nothing
  can, skip the draw; never wait.
- A new shader variant, pipeline key or special case joins the load-time build in
  the same change.
- Content load is the only place that may wait for a compile.

Not there yet, most urgent first. These are debts, not precedent:

1. GLideN64 on GLES2, which has no ubershader, compiles on the draw path.
2. `gles2n64`, `gles2rice` and `glide2gl` build what earlier sessions met at load,
   but what they have never met still compiles: they have no ubershader or
   parallel compile to stand in, so a cold first session cannot avoid it.
3. paraLLEl-RDP still builds a specialised path it has no entry for synchronously
   (the temporal upscaler's own passes); everything else is built at load.

## Method: the speed of light

Compare a path with what the hardware must physically do, not with the last
build. For the path you touch, write down the least it could cost: bytes
moved, conversions, driver calls, waits. Everything above that is a defect to
remove or a cost with a named reason to exist.

Work in this order:

1. Remove the work: no representation, copy or wait without a consumer.
2. Keep data where it is: mutate in place, write the final form once, lend it.
3. Prepare ahead what cannot be removed, off the frame path.
4. Only then schedule what is left better.

Moving a copy to another thread is not removing it. Removing a required wait
is not throughput. Any measured or provable gain is welcome however small; if
part of an optimization is risky, do the safe parts first and then the rest.

## The frame path

The frame path is `retro_run` and everything under it: the emulation thread,
the RDP command thread (`command_ring.c`), the timeline worker, the GL context
thread, the RSP JIT and the angrylion workers (`parallel_al.c`). On it:

- No shader or pipeline compile, no JIT growth that can be done earlier.
- No heap allocation. Allocate at init or on a resolution change.
- No file I/O, no logging, no profiling counters in release builds.
- No lock of any kind, no sleep, no device or queue idle.
- No wall-clock reads to decide anything. Host time never drives guest time.
- The emulation thread never waits on a worker. The exceptions are the guest
  dependencies below, and they are named, not added to.

Required waits are the ones a guest or an API contract demands: the
`synchronous` RDP option at `SyncFull`, CPU reads of GPU-written RDRAM,
savestates, teardown, `wait_sync_index`. Name the producer and the consumer of
any wait before keeping or adding one. Only `build_pipelines_ahead` may wait
on the pipeline worker, and only at content load.

These also still break the rules: the pipeline compiles listed under the first
rule, and the paraLLEl RSP JIT, which compiles blocks on first use and allocates
`cached_blocks` nodes.

## Shaders and pipelines

Know which layer you are changing. Having one does not give you the next.

| Layer | Where | Scope |
| --- | --- | --- |
| Prebuilt SPIR-V | `shaders/slangmosh.hpp`, `upscaler_shaders.hpp` | every device |
| Recipes (what to build) | `parallel_rdp_pipelines.bin`, `pipeline_record.h` | portable |
| Driver blob | `parallel_rdp_pipeline_cache.bin` | one device and driver |
| Live `VkPipeline` | `Program::get_pipeline` | this device |
| GL binaries / keys | `glsl_ShaderStorage.cpp` | GL vendor and version / portable |

### parallel-RDP

- Shaders stay prebuilt SPIR-V. No runtime GLSL compiler, ever. After a
  shader change regenerate `slangmosh.hpp` (see the parallel-rdp README),
  run `tools/rdp_no_storage_8bit_bank.py` and then its `--check`.
- A draw never waits for a variant. Specialised rasterizer variant ready:
  use it. Missing: `flush_pipeline_state_without_blocking` fails, the variant
  goes to the "RDP Pipelines" worker, and this dispatch runs the generic
  variant (spec mask 7), which produces the same pixels. Never skip a draw,
  never substitute an approximation, never wait.
- Every new pipeline goes through the records (`recordable`) so a warm run
  builds it ahead. A warm run must build none on the frame's threads.
- A new compute or VI pipeline joins `queue_known_pipelines` in the same change,
  through the state setter its draw uses, so a cold run builds it at load too.
  `tools/ci/parallel_rdp_pipelines.py` holds a first run to none on the frame's
  threads. The other lanes skip this: it takes a software driver most of a minute.
- `wait_pipelines_ahead` is for load time. Specialised rasterizer variants are
  never urgent: the generic variant covers them.
- Each bit in the specialization constants multiplies the variants. Fewer
  permutations beat compiling them faster. Prefer state read from buffers
  unless a specialisation is a measured win.
- `normalize_static_state` and `normalize_combiner` may drop a field only when
  no shader path reads it in that mode. Prove it: identical frames and a
  passing `rdp-validate-dump` (`DUMPING.md`), including `--sync-only`.
- Pipeline identity is the complete recipe: program and shader hashes, static
  state, spec constants, render pass. Zero padding before hashing. A hash-only
  lookup may lose speculative work, never make a draw wrong.
- When the meaning of a record field changes, bump
  `PIPELINE_RECORD_LIST_VERSION`, even if the size did not change.
- A foreign, damaged or short cache file is a miss, never a crash or an abort.
  The list only grows by union, capped at `PIPELINE_RECORD_LIST_MAX`, oldest
  out. Save only after no thread builds pipelines, and only when it changed.
- A finished pipeline publishes in its own entry at once. Never hold it behind
  an older unfinished job.
- Do not add workers or capacity because a queue looks idle or busy. Measure
  miss-to-adoption time first. Admission counts results not yet consumed.
- Publish complete jobs: fill, then release-store. Nothing in a queued job may
  point at a stack or at storage the producer can reuse.

### Compile cost

- Smaller SPIR-V does not mean faster driver compile. Bytes and instruction
  counts are not the result; the first-use driver time of the worst variant
  is. Reject a change that is smaller but compiles slower, and say so in the
  commit message.
- A validator pass does not prove correctness. Valid SPIR-V can compute the
  wrong value; only pixel results decide. Add a negative control for new
  checks: break the code on purpose and see the check fail.
- Do not relax precision, reassociate, or fuse arithmetic for compile speed
  unless the result is identical on every input the RDP can produce.

### GLideN64 and the other GL plugins

- GL calls stay on the context thread. `glsm` state must be restored for the
  frontend; do not move a compile to a worker without a shared context and
  explicit ownership.
- On a combiner miss, where the driver compiles on its own threads
  (`GL_KHR_parallel_shader_compile`), the ubershader
  (`CombinerProgramBuilder::buildCombinerProgramAsync`) draws while the key's
  own program compiles. A driver without the extension stays on the ubershader
  until the next load builds the keys from the journal. It must draw the same:
  anything added to `compileCombiner` goes into `_uberCombiner` too, and
  `tools/ci/gliden64_ubershader.py` holds the two to the same frames, with
  `GLIDEN64_GL_STRICT` aborting on a draw that has to compile.
- Every ubershader variant is finished at load, and every copy and fill key is
  built at load (`_buildFixedKeys`). Nothing else may be waited for.
- GLES2 has no ubershader and still compiles on a miss, so a miss must be correct
  first. Cut misses through `CombinerKey` canonicalization and shader storage.
- Bump `m_formatVersion` when generated shaders or the binary layout change
  and `m_keysFormatVersion` when a key's meaning changes.
- Poll `GL_COMPLETION_STATUS_KHR` before asking anything of a program being
  compiled. Asking for link status or uniform locations early serializes the
  driver.
- New file access uses libretro-common `file_stream`, not iostreams.

### GLideN64 on Vulkan

- Written from the ground up in MSVC C89 under
  `src/Graphics/VulkanContext/` (`vk_spirv.c`, `vk_shaders.c`, `vk_renderer.c`,
  `vk_device.c`); `vk_ContextImpl.cpp` is the C++ boundary and holds no logic
  of its own. New code here stays C89.
- The rest of GLideN64 moves to C89 a file at a time, leaf files first. A moved
  file goes on `GLIDEN64_C89` so it stays moved; its public functions take a
  `gli_` prefix, because the other plugins export the same C names.
- There is no GLSL and no compiler. Shaders are written directly as SPIR-V;
  every module goes through `test_gliden64_spirv`. One writer gives both the
  generic shader (combiner read from push constants) and each combiner's own,
  so a known condition writes only the arm that runs.
- A draw never waits and never compiles: without its own pipeline it runs the
  generic one, which draws the same, and the job goes to the worker. `prewarm`
  builds every generic and special pipeline at load, the only wait there is.
  A new blend pair or special goes into `prewarm` too; the lane's strict mode
  aborts on one it missed.
- Blending and depth clamp are dynamic state where the device allows
  (`VK_EXT_extended_dynamic_state3`): a state left out of the pipeline key is
  a pipeline never built. `GLIDEN64_VULKAN_NO_EDS3` runs the other path.
- Anything added to `compileCombiner` goes into `vkr_write_combiner` too.
  `tools/ci/gliden64_vulkan.py` holds Vulkan to GL frame for frame.
- Vertices and combiner values are written once, final, into the frame's
  ring. Values are the `CombinerValues` the GL backend also reads. Textures
  decode straight into the ring (`lendTextureData`) unless something has to
  read them back, which leaves no copy, and upload ahead of the render pass
  unless the image was already used in the commands being recorded.

### paraLLEl RSP JIT

Blocks are keyed by a hash of IMEM and built on first use on the emulation
thread. Treat this as shader stutter: the key must cover everything the code
depends on, invalidation must stay exact, and code memory must not move or
grow under a running block.

## Stutter

Attribute a hitch before fixing it. It can be a pipeline, an RSP block, an
upscaling allocation, an RDRAM readback, a GL compile or the frontend, and
they look alike. A cause needs direct evidence at that frame: the "Stalled
compile" warning, `PARALLEL_RDP_TIMELINE_TRACE`, `PARALLEL_RDP_BENCH`.

Never hide a stall with a skipped draw, a no-op fallback, a sleep, deeper
audio buffering or less guest work.

## Zero-copy and borrowed lends

Look for a copy to remove on every data path you touch. Say exactly what was
removed. "No CPU copy" does not mean no GPU transfer; "no roundtrip" means no
readback followed by re-upload of the same content.

Classify every copy before touching it: format conversion, immutable
snapshot, ownership handoff, API staging, or redundant duplicate. Only the
last is simply deleted. Find out why the others exist first.

A pointer is not a version. A consumer may reuse data only if producer,
content version, interpretation and lifetime all still match. Handles and
addresses get recycled, so a cache key needs a generation.

- A lend must outlive its last CPU and GPU consumer. State who ends it.
- Never hand a pointer to guest RDRAM to anything that outlives the guest's
  next write, unless the write is ordered against it. A bounded snapshot of
  mutable guest memory can be the correct and fastest design.
- Write the final representation once, directly into its destination: the
  ring slot, staging span, or frontend buffer.
- Reset counts rather than clearing payloads. Do not clear bytes about to be
  fully overwritten, but keep the zeros a hash or file format depends on.
- Prove full overwrite before skipping initialization.
- Copy or hash only the bytes the consumer reads. Use half-open, overflow-safe
  ranges; a pitch is a row stride, not an offset.

Lends in place, which must stay:

- RDRAM imported with `VK_EXT_external_memory_host`, with the page-diff path
  as the fallback (`PARALLEL_RDP_VERIFY_UPLOADS` checks it).
- RDP command lists run from where they are in `process_commands`.
- angrylion drawing into the frontend's lent software framebuffer.
- GLideN64's persistently mapped vertex rings and deferred readback slots.
- The VI downscale and deinterlace folded into the scale pass.

Keep rendered data on the GPU until a real CPU reader needs it, and batch that
readback at the dependency, in writer order. Synchronize the dependency, not
the device. Size barriers and flushes to the bytes concerned.

## Memory and GPU resources

- Capacity comes from the largest live set or the guest address space, not a
  number chosen to hide an allocation. Define exhaustion for every pool.
- Keep high-water storage; do not free and refetch per frame.
- Size caches from the device's real limits and budget. Never page.
- Use the smallest exact formats and no oversized targets. On tile GPUs: no
  needless clears, no mid-pass readback, discard what is not needed.
- Retire a resource only after every reader has finished, the frontend
  included. A display can outlive the submission that drew it.
- Warn-and-lower is correct where a device cannot fit a mode
  (`parallel_rdp_binding_fit.py`); crashing is not.

## Threading

- Lock-free always: `retro_atomic`, `command_ring.c` (SPSC), `rdp_worker.c`
  and `mpsc_stack` (MPSC), eventcounts for parking. No new mutex, no
  OS-specific atomics.
- Prefer one owner over any synchronization at all.
- Park idle workers with prepare, recheck, commit. Never spin indefinitely and
  never `sleep(1)` poll. A lock-free queue does not make the system wait-free.
- A full queue defers speculative work; it never blocks a draw or loses
  required work.
- Stop admitting, join, drain, then destroy, and retain results until then.
- Be conservative with core counts. Past 8 workers, parallel-RDP's replay
  costs more than it saves.

## Guest progress and accuracy

- Faster by doing less is a regression. Frame count, VI cadence, interrupts
  and audio totals must match.
- DP, SP and VI interrupts stay where the hardware puts them.
- parallel-RDP stays bit-exact with angrylion, hidden RDRAM bits included.
- Savestate load, reset and Run-Ahead must invalidate derived state.
- Timing and motion code goes by real time and gets smoother with refresh
  rate; nothing is tuned to 60 Hz.

## Measuring

- Do the measuring yourself. Keep counters and timers in `tools/` or
  uncommitted, never as runtime logging.
- Cold and warm are different experiments. Use
  `tools/ci/parallel_rdp_pipelines.py` for its cold, warm, warm-cache, foreign,
  damaged and cut cases. Never delete a user's caches to get a cold run.
- Time separately from correctness. Frame hashes and dumps
  (`LRHOST_HASHLOG`, `LRHOST_DUMPDIR`) cause the readbacks you are timing.
- Report median, p99 and max, not averages. Alternate baseline and candidate
  (ABBA) with nothing else running. Keep raw samples.
- Give the workload and denominator. A byte count is not bandwidth, and a
  microbenchmark is not FPS. Percentages from different runs do not add up.
- Keep bad results visible, including a regression inside a tolerance.
- lavapipe and llvmpipe prove correctness of a path, not performance, and not
  behaviour on real GPUs. Call GPU work unverified on hardware until it runs
  on some.

## What does not carry over from other cores

The disciplines above transfer from the Xbox and 360 work; constants and
mechanisms do not. The RDP has no guest shaders, so there is no microcode to
scan offline: its states exist only once the RSP produces them, and the
record list is the equivalent. Graphics pipeline libraries and multi-draw do
not fit a renderer that is mostly compute dispatches. The generic rasterizer variant
already is the fallback; do not add another.

## Before delivering

Build, with `make -j` matched to `nproc`:

    make -j$(nproc) platform=unix WITH_DYNAREC=x86_64 HAVE_THR_AL=1 HAVE_PARALLEL=1 HAVE_PARALLEL_RSP=1
    make platform=unix HAVE_PARALLEL=1 lrhost_vk
    make platform=unix lrhost_gl lrhost

Run what the change touches:

    make platform=unix check-core check-angrylion
    python3 tools/ci/check_cxx_runtime.py
    python3 tools/ci/check_vtables.py
    python3 tools/gen_upscaler_shaders.py --check
    python3 tools/rdp_no_storage_8bit_bank.py --check
    python3 tools/ci/parallel_rdp_regress.py --core ./parallel_n64_libretro.so --host ./lrhost_vk
    python3 tools/ci/parallel_rdp_syncval.py --core ./parallel_n64_libretro.so --host ./lrhost_vk
    python3 tools/ci/parallel_rdp_pipelines.py --core ./parallel_n64_libretro.so --host ./lrhost_vk
    python3 tools/ci/parallel_rdp_rdram.py --core ./parallel_n64_libretro.so --host ./lrhost_vk
    python3 tools/ci/gl_renderers.py --core ./parallel_n64_libretro.so --host ./lrhost_gl
    make platform=unix test_gliden64_spirv && ./test_gliden64_spirv
    python3 tools/ci/gliden64_vulkan.py --core ./parallel_n64_libretro.so --gl ./lrhost_gl --vk ./lrhost_vk
    python3 tools/ci/angrylion_lend.py --core ./parallel_n64_libretro.so --host ./lrhost

Threading changes also go through the ThreadSanitizer lane in
`.gitlab-ci.yml`, and memory changes through `SANITIZER=address,undefined`.
A bug fix ships with a regression test in the same patch, shown failing on the
old code. Tests must build on Windows and Linux.

## Code and patches

- parallel-RDP and its glue use no C++ runtime (`check_cxx_runtime.py`). New
  self-contained modules are MSVC-compatible C89, like `rdp_worker.c`.
- Use libretro-common and its VFS rather than shims. Its files stay identical
  to RetroArch's; what a core needs goes into RetroArch first.
- Keep comments short and commit messages concise.
- Deliver `git format-patch` mailbox patches, cut after fetching upstream and
  checked with `git am` on a clean checkout. Author:
  `LibretroAdmin <libretroadmin@users.noreply.github.com>`. No AI attribution
  trailers.
