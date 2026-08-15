# Verifying the HLE audio microcodes against cxd4

The audio microcodes here are checked by replaying real captured tasks
against the cxd4 interpreter and requiring both to write the same bytes.
The interpreter runs the actual RSP instructions, so where the two agree
the HLE implementation is doing what the hardware does; where they
differ, one of them is wrong and it is nearly always this one.

Everything below is reproducible from the tree. The two pieces of
instrumentation are compiled out unless asked for.

## Taking a capture

    make HAVE_HLE_AUDIT=1

A capture is the machine state at the moment an audio task starts: 8 MiB
of RDRAM followed by 4 KiB of DMEM, written to
`/tmp/ucodes/mp3scene_<abi>_<n>.bin`. Three are kept per microcode
revision. See `src/hle_audit_capture.h` for the environment switches.

The switches exist because most tasks are worthless as evidence. A game
sitting in its title menu emits tasks that render silence, and silence
matches silence, so the capture passes while testing nothing. `AUDIT_SKIP`
and `AUDIT_MINCMDS` are how the three slots get spent on a task that is
actually mixing voices; `AUDIT_TASKLOG` prints each task's signature and
command count so you can see which those are before spending a slot.
`AUDIT_ANY` is needed for the musyx family, whose tasks carry none of the
commands the default filter looks for and are otherwise invisible.

A revision is identified by the ABI word at `ucode_data+0x10` - the same
word the dispatcher keys on. A checksum over the ucode text is not
stable: a few words in it are patched per task.

## Replaying one

Seed both implementations from the capture, run the task, compare. How
the microcode is seeded depends on the family:

  - the `audio` family (ABI `1118xxxx`) boots through the ucode boot
    stub, so IMEM gets the three-instruction stub and the ucode text
    lands at IMEM `0x80`
  - the `nead` revisions (ABI `1exxxxxx`, `1fxxxxxx`, `04xxxxxx`) are
    loaded directly, ucode text at IMEM 0

Getting this wrong does not produce a subtle difference: the task does
not run and every region mismatches. If a capture that ought to work
fails completely, check the seeding before suspecting the microcode.

## Comparing the right thing

Do not diff whole RDRAM against the pre-task image. A task that rewrites
the same samples it already wrote - which any looping engine does, frame
after frame - shows no difference from its input and reads as a pass
while proving nothing. Mickey's Speedway looked like a vacuous capture
for exactly that reason until the comparison moved; it then turned out to
be one of the largest verifications in the corpus.

Instead, record what the interpreter DMA'd out and diff precisely those
regions:

    make HAVE_RSP_DMA_TRACE=1
    DMALOG=/tmp/dma.log ...

The trace writes `RD`/`WR` lines with the DMEM address, DRAM address,
length and count. The `WR` extents are the task's real output, and a
capture is only meaningful in proportion to how many bytes it covers: a
few hundred bytes across five regions is weak evidence even when it
passes, while sixty kilobytes across three hundred is strong.

## What is covered

At the time of writing the corpus is around thirty games spanning all six
microcode families and every ABI revision encountered so far, each
byte-exact over the regions its tasks write. Two paths remain unverified
for want of content that exercises them:

  - the `naudio_mp3` revision's MP3 bank window (`0x800` bytes at DMEM
    `0x800`). The Conker window is transcribed and verified; this one is
    deliberately left unbanked rather than guessed. Five games on
    revisions that map the MP3 commands have been captured and only
    Conker issues any, so this needs a savestate taken while one of the
    others is streaming music.
  - `NAUDIO_0000`, opcode `0x07`/`0x08` in the generic `naudio` and
    `naudio_bk` tables. Roughly fifteen hundred commands captured across
    two games on those revisions include not one invocation. A command no
    game issues cannot be reverse-engineered, and the stub may well be
    right.
