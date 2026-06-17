# CPU convergence: new_dynarec hot-state migration map

Status: **design / Phase 1 (analysis only — no code changes yet)**
Scope of this document: the keystone change that the rest of the r4300/CPU
convergence to mupen64plus-next hangs off — migrating parallel-n64's flat-global
dynarec hot state into next's `struct new_dynarec_hot_state`, addressed through
`g_dev.r4300`.

This is the single highest-risk change in the codebase: it touches JIT machine-code
emission and hand-written assembly across four architectures. A wrong struct offset
produces a JIT that emits subtly-wrong code — no compile error, manifesting as random
crashes/corruption in specific games. Therefore this is mapped fully and implemented
**x86_64-first, in hardware-testable increments**, with arm/arm64 left on the flat
model behind `#ifdef` until they can be validated on real hardware.

--------------------------------------------------------------------------------
## 1. The core divergence

Both cores descend from the same Ari64 `new_dynarec` and share the x64/x86/arm/arm64
backend layout (`new_dynarec/<arch>/assem_<arch>.c` + `linkage_<arch>.{asm,S}`). The
JIT *architecture* is the same. The one structural difference that everything else
follows from is **where the hot state lives**:

* **parallel-n64 (current):** flat file-global symbols — `cycle_count`, `pcaddr`,
  `address`, `readmem_dword`, `memory_map[]`, `mini_ht[][]`, the register file via
  `r4300_regs()` / `g_cp0_regs[]`, etc. The codegen emits `(intptr_t)&cycle_count`
  and the hand-written asm uses `cextern cycle_count`.

* **mupen64plus-next (target):** one `struct new_dynarec_hot_state` embedded in
  `struct r4300_core` (itself inside `struct device g_dev`). The codegen emits
  `(intptr_t)&g_dev.r4300.new_dynarec_hot_state.cycle_count`; the asm reaches fields
  via generated `offsetof_*` defines: `g_dev + offsetof(device,r4300) +
  offsetof(r4300_core,new_dynarec_hot_state) + offsetof(hot_state,cycle_count)`.

Converging means: define the struct, move the globals into it, repoint the codegen
emit-sites and the asm symbol references, and (for the asm) generate the offset
defines via the `asm_defines` / `recomp_dbg` mechanism next already uses.

--------------------------------------------------------------------------------
## 2. Field mapping (pn64 flat global  ->  next hot_state field)

next's `struct new_dynarec_hot_state` (from new_dynarec.h), x64/arm64 variant:

| next field                | type            | pn64 flat global / source                         | notes |
|---------------------------|-----------------|---------------------------------------------------|-------|
| `dynarec_local[32]`       | uint64_t[32]    | (implicit; pn64 keeps host regs differently)      | see §5 |
| `cycle_count`             | int             | `cycle_count` (assem_x64.c:42)                    | CCREG spill target |
| `pending_exception`       | int             | `pending_exception` (assem_x64.c:41)              | |
| `pcaddr`                  | int             | `pcaddr` (assem_x64.c:40)                         | |
| `stop`                    | int             | `stop` (linkage cextern; r4300 stop flag)         | |
| `invc_ptr`                | char*           | (pn64: invalid_code path; see §5)                 | |
| `address`                 | uint32_t        | `address` (m64p_memory.c:53)                      | shared w/ memory layer |
| `rdword`                  | uint64_t        | `readmem_dword` (assem_x64.c:46)                  | **rename** |
| `wdword`                  | uint64_t        | `cpu_dword` (m64p_memory.c:61)                    | **rename** |
| `wword`                   | uint32_t        | `cpu_word` (m64p_memory.c:58)                     | **rename** |
| (no direct)               | uint8_t         | `cpu_byte` (m64p_memory.c:59)                     | pn64-extra, keep |
| (no direct)               | uint16_t        | `cpu_hword` (m64p_memory.c:60)                    | pn64-extra, keep |
| `cp1_fcr0` / `cp1_fcr31`  | uint32_t        | pn64 cp1 fcr0/31 (cp1.c)                          | |
| `regs[32]`                | int64_t[32]     | `reg[32]` via `r4300_regs()` (r4300_core.c:69)    | **accessor->member** |
| `hi` / `lo`               | int64_t         | `hi` / `lo` via `r4300_mult_hi/lo()`              | **accessor->member** |
| `cp0_regs[32]`            | uint32_t[32]    | `g_cp0_regs[32]` (cp0.c:38)                       | **global->member** |
| `cp0_latch`               | uint64_t        | (pn64 cp0 latch path)                             | |
| `cp1_regs_simple[32]`     | float*[32]      | pn64 cp1 simple regs                              | |
| `cp1_regs_double[32]`     | double*[32]     | pn64 cp1 double regs                              | |
| `cp2_latch`               | uint64_t        | (pn64 has no cp2 -> 0/unused)                     | see §5 |
| `rounding_modes[4]`       | uint32_t[4]     | `rounding_modes[4]` (assem_x64.c)                 | |
| `branch_target`           | int             | `branch_target` (assem_x64.c:44)                  | |
| `pc`                      | precomp_instr*  | pn64 pc pointer                                   | |
| `fake_pc`                 | precomp_instr   | `fake_pc` (assem_x64.c)                           | |
| `rs` / `rt` / `rd`        | int64_t         | pn64 staging (partly `ldlr_block`, multdiv)       | see §5 |
| `ram_offset`              | intptr_t        | `ram_offset` (assem_x64.c:45)                     | |
| `mini_ht[32][2]`          | uintptr_t[32][2]| `mini_ht[32][2]` (assem_x64.c:70)                 | |
| `memory_map[1048576]`     | uintptr_t[]     | `memory_map[1048576]` (assem_x64.c:69)            | 8MB; dominates struct size |

pn64-only globals that have **no** next hot_state equivalent and must be decided (§5):
`last_count` (15 codegen refs), `restore_candidate[512]`, `ldlr_block {rt,rs,shift}`,
`multdiv_rs_scratch` / `multdiv_rt_scratch` / `multdiv_fake_pc`, `extra_memory`
(the JIT code cache — next keeps this as a *separate* `r4300_core.extra_memory`
member, not inside hot_state).

--------------------------------------------------------------------------------
## 3. Codegen transformation (assem_x64.c)

Every emit-site that takes the address of a hot-state global changes from
`(intptr_t)&GLOBAL` to `(intptr_t)&g_dev.r4300.new_dynarec_hot_state.FIELD`.

Approximate emit-site counts in pn64 `x64/assem_x64.c` (refs, incl. defs):
`readmem_dword` 19, `memory_map` 19, `last_count` 15, `rounding_modes` 11,
`ram_offset` 6, `mini_ht` 5, `restore_candidate` 5, `pending_exception` 4,
`cycle_count` 3, `pcaddr` 3, `branch_target` 1; plus `g_cp0_regs[...]` (~7+,
e.g. CP0_COUNT_REG spill at lines 3186/3316/3320/...), and the register file
via `&g_cp0_regs[CP0_STATUS_REG]` (line 1029) and the `reg`/`hi`/`lo` address
arithmetic in `get_reg`-style helpers (lines ~929-945 in next, equivalent here).

Reference target pattern (from next x64/assem_x64.c):
```
addr=((intptr_t)g_dev.r4300.new_dynarec_hot_state.regs)+((r&63)<<3)+((r&64)>>4);
if(r==CCREG)  addr=(intptr_t)&g_dev.r4300.new_dynarec_hot_state.cycle_count;
if(r==CSREG)  addr=(intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp0_regs[CP0_STATUS_REG];
if(r==FSREG)  addr=(intptr_t)&g_dev.r4300.new_dynarec_hot_state.cp1_fcr31;
...
if(addr==(intptr_t)&g_dev.r4300.new_dynarec_hot_state.memory_map) ...
```

Risk: these addresses are baked into emitted machine code as RIP-relative
displacements. The struct must live in BSS within +/-2GB of the code cache
(next guarantees this by placing `extra_memory` adjacent — see §4). Getting the
field set right but the *placement* wrong = out-of-range rel32 = crash.

--------------------------------------------------------------------------------
## 4. extra_memory / placement constraint

pn64 currently: `ALIGN(4096, u_char extra_memory[1<<TARGET_SIZE_2])` in assem_x64.c,
and `base_addr` is the JIT code cache. All generated RIP-relative refs to hot state
must stay within +/-2GB of generated code.

next places `extra_memory[33554432]` (32MB) **and** `new_dynarec_hot_state` as
adjacent members of `struct r4300_core`, so the code cache and the hot state are
co-located in one BSS object and every intra-object displacement is small. The
`offsetof_struct_r4300_core_extra_memory` / `..._new_dynarec_hot_state` defines in
next's asm_defines encode these (e.g. extra_memory at 0x00901000, hot_state at
0x02901000 inside r4300_core).

**Migration requirement:** when the hot state moves into `g_dev.r4300`, the JIT code
cache (`extra_memory`) must move with it (also into `r4300_core`), preserving the
co-location guarantee. This is why the struct migration and the code-cache placement
are one atomic change, not separable.

--------------------------------------------------------------------------------
## 5. pn64-specific deltas (decisions required before implementation)

1. **`readmem_dword` -> `rdword`, `cpu_word/dword` -> `wword/wdword`:** pure rename +
   move into the struct. pn64 also has `cpu_byte`/`cpu_hword` (next lacks). Keep them
   as struct extensions appended *after* the next-matching fields so next's
   offsetof-derived layout for the shared fields is preserved.

2. **`ldlr_block` / `multdiv_*_scratch` / `multdiv_fake_pc`:** pn64 re-engineered
   LDL/LDR and 64-bit mult/div to stage operands in these C-side blocks and call void
   shims (deliberate, documented in assem_x64.c). next stages via hot_state `rs`/`rt`/`rd`.
   **Decision:** keep pn64's staging blocks as appended struct members rather than
   reverting to next's rs/rt/rd model — reverting would undo working pn64 dynarec
   engineering and rewrite the LDL/LDR + mult/div emit paths. The struct can carry
   BOTH next's rs/rt/rd (for shared code paths) and pn64's staging blocks.

3. **`cp2` / `cp2_latch`:** pn64 has no COP2 (RSP-only on N64; CP2 is next's RSP HLE
   hook). Include `cp2_latch` as a zero-initialised member for layout compatibility
   but it stays unused on pn64. Do NOT pull in next's cp2.{c,h}.

4. **`last_count` (15 refs), `restore_candidate[512]`:** pn64-specific, no next field.
   Append as struct members. They are hot (15 codegen refs) so they belong in the
   struct for locality, but their offset is pn64-private (not in next's asm_defines).

5. **`dynarec_local[32]` / `invc_ptr`:** **RESOLVED (Phase-1 finding).** pn64's **x64**
   backend does NOT use `dynarec_local` — it addresses hot state via flat RIP-relative
   globals (`base_addr` + direct `[rel SYMBOL]`), with a `new_dyna_start` 5-register
   prologue. `dynarec_local` appears ONLY in pn64's **arm64** backend, where it is
   `RECOMPILER_MEMORY->rml_dynarec_local` (an FP-relative struct base — a *different*,
   pn64-specific hot-state model from x64). next's x64 linkage likewise does not
   FP-base on dynarec_local. **Implication:** for the x86_64 migration, `dynarec_local`
   is not a porting concern — the x64 hot-state fields are addressed individually and
   map directly to struct members. arm64 already has a struct-base model (closer to
   next's shape but pn64-private via RECOMPILER_MEMORY), which is a separate reconciliation
   deferred to the arm/arm64 phase. This removes the top Phase-2a risk for x86_64:
   `invc_ptr`/`dynarec_local` need not be reconciled to start.

--------------------------------------------------------------------------------
## 6. Assembly transformation (linkage_x64.asm)

pn64 `linkage_x64.asm` has 23 hot-state `cextern` symbols:
`pcaddr, pending_exception, cycle_count, last_count, next_interrupt, stop, frame_break,
dyna_entry_rsp, g_cp0_regs, reg, hi, lo, memory_map, restore_candidate, address,
readmem_dword, cpu_word, cpu_dword, cpu_hword, cpu_byte, invalid_code, ram_offset,
hash_table` (plus non-state externs: base_addr, the get_addr/interrupt helpers).

Each hot-state symbol's `[rel SYMBOL]` access becomes
`[rel g_dev_r4300_new_dynarec_hot_state_SYMBOL]`, where that define expands (in
asm_defines_nasm.h) to `g_dev + offsetof_struct_device_r4300 +
offsetof_struct_r4300_core_new_dynarec_hot_state + offsetof_struct_new_dynarec_hot_state_SYMBOL`.

The non-state helpers (`new_recompile_block`, `gen_interrupt`, `check_interrupt`, ...)
stay as plain `cextern` — they are functions, not hot state.

--------------------------------------------------------------------------------
## 7. asm_defines generation (recomp_dbg.c)

next generates `asm_defines_{nasm,gas}.h` per arch by compiling a small program
(recomp_dbg.c path) that emits `%define offsetof_struct_X_Y (0x...)` lines via
`offsetof`. pn64 currently hardcodes nothing (flat symbols need no offsets).

**Migration requirement:** import next's `recomp_dbg.c` offset-emitting mechanism (or a
minimal pn64 equivalent) and wire it into the build so `asm_defines_*.h` are generated
from the actual `struct new_dynarec_hot_state` layout — never hand-written, or they
drift from the C struct and silently corrupt the JIT.

--------------------------------------------------------------------------------
## 8. Staged implementation plan (Phase 2, x86_64-first)

Each step must compile clean (full sweep) AND be smoke-testable on the 9950X3D
before the next. arm/arm64 stay on flat globals behind `#ifdef` until a later,
hardware-validated phase.

* **2a — struct definition + layout validation (no behaviour change). [DONE]**
  Defined `struct new_dynarec_hot_state` in pn64's new_dynarec.h matching next's
  field order for the shared region (dynarec_local..memory_map), with pn64-extras
  (cpu_byte/hword, last_count, ldlr_block, multdiv staging, restore_candidate)
  appended AFTER. The 23 shared-field offsets are pinned to next's known x64
  values by negative-array-bound static asserts in new_dynarec_64.c (MSVC-C89-safe);
  all pass, so the layout is byte-identical to next for the shared region. The
  struct is DORMANT — defined and validated, but nothing references it yet, and the
  flat globals remain the live state. NOTE: embedding the struct + extra_memory into
  struct r4300_core was deliberately deferred to 2c (rather than done here), because
  embedding while the flat globals are still live would bloat g_dev by ~40MB of dead
  weight and perturb savestate/layout for a dormant struct; per §4 the embed must be
  atomic with the codegen repoint anyway. (Open question §5.5 — dynarec_local /
  host-reg save — was RESOLVED in Phase 1: not an x64 concern.)

* **2b — asm_defines generation.** Wire recomp_dbg offset emission; produce
  `x64/asm_defines_{nasm,gas}.h` from the struct. Verify the generated offsets match
  the static-assert expectations from 2a. Pure build-tooling; no runtime change yet.

* **2c — codegen repoint (assem_x64.c).** Change every hot-state emit-site from
  `&GLOBAL` to `&g_dev.r4300.new_dynarec_hot_state.FIELD`. Remove the flat global
  definitions (now struct members). This is the behavioural switchover for the C side.
  **Heavy smoke test required.**

* **2d — linkage repoint (linkage_x64.asm).** Switch the 23 hot-state `cextern`s to
  `[rel g_dev_r4300_new_dynarec_hot_state_*]` using the 2b defines. This is the
  behavioural switchover for the asm side. **Heaviest smoke test — broad game matrix.**

* **2e — memory-layer reconciliation.** `address`/`cpu_word`/`cpu_dword` are shared
  with m64p_memory.c. Repoint those to the hot_state members so the memory layer and
  JIT agree on one location. (This is the seam to the *other* big convergence —
  memory.c — and may be where the two efforts eventually meet.)

* **(later) 2f+ — arm/arm64.** Only once x86_64 is hardware-validated. Requires an
  environment that can assemble/run those targets; not attempted blind.

--------------------------------------------------------------------------------
## 9. Verification strategy

* Per step: full-core syntax sweep (74 TUs) + object-level link check of the
  new_dynarec objects.
* 2a/2b: static_assert the C struct offsets equal the generated/asm offsets — a
  mismatch here is the single most dangerous failure mode and must be caught at
  compile time, not runtime.
* 2c/2d: cannot be verified here beyond compile + offset asserts. Requires the
  9950X3D smoke test across a broad game matrix (different MBC/save types, TLB-heavy
  titles, FPU-heavy titles, and a few known dynarec edge-case ROMs).
* Bit-exact-or-revert does not apply cleanly (JIT output legitimately changes); the
  invariant is *behavioural* equivalence (same game behaviour), validated on hardware.

--------------------------------------------------------------------------------
## 10. Honest risk statement

This migration cannot be fully validated in the build/CI environment used to author
it (no arm/arm64 assembler; "smoke test" for a JIT = broad game-matrix runtime
testing, not one ROM). x86_64 steps are compile- and offset-assert-verifiable here
and runtime-verifiable on the maintainer's hardware. arm/arm64 are explicitly deferred.
A wrong offset is silent. Therefore every step is gated on hardware smoke-testing
before the next, and the struct-offset static asserts (2a/2b) are mandatory, not
optional.
