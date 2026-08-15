/* Audio task capture for the cxd4 replay oracle.
 *
 * The HLE audio microcodes are verified by replaying captured tasks
 * against the cxd4 interpreter and requiring both to write the same
 * bytes.  A capture is the machine state at the moment a task starts:
 * all of RDRAM followed by DMEM.  Replaying one seeds both
 * implementations from that image, runs the task, and diffs the regions
 * the interpreter DMA'd out.
 *
 * Compiled out entirely unless HLE_AUDIT_CAPTURE is defined, so an
 * ordinary build carries no cost - not even the getenv - and the call in
 * hle_execute() disappears with it:
 *
 *   make HAVE_HLE_AUDIT=1
 *
 * Then, under a frontend:
 *
 *   AUDIT_DUMP=1      enable capture
 *   AUDIT_SKIP=N      ignore the first N audio tasks.  Boot and menu
 *                     tasks render silence and match trivially, which
 *                     proves nothing; this is how a capture slot is kept
 *                     for a task that actually mixes voices.
 *   AUDIT_MINCMDS=N   ignore tasks with fewer than N commands
 *   AUDIT_VOICES=1    ignore musyx tasks whose first subframe declares no
 *                     voices - same reason
 *   AUDIT_ANY=1       keep tasks carrying no op7/op8.  The musyx family
 *                     carries neither, so its tasks are invisible without
 *                     this.
 *   AUDIT_TASKLOG=1   print every task's signature and command count,
 *                     which is how an in-game task is told from a menu
 *                     one before a capture slot is spent on it
 *
 * Three captures are kept per microcode revision, written to
 * /tmp/ucodes/mp3scene_<abi>_<n>.bin.  The revision is keyed on the ABI
 * identity word at ucode_data+0x10, the same word the dispatcher keys on:
 * a checksum over the ucode text is not stable, since a few words in it
 * are patched per task.
 */

#ifndef HLE_AUDIT_CAPTURE_H
#define HLE_AUDIT_CAPTURE_H

#ifdef HLE_AUDIT_CAPTURE

#include <stdio.h>
#include <stdlib.h>

#define AUDIT_SCENES_PER_UCODE 3
#define AUDIT_MAX_UCODES       16

static void audit_capture_task(struct hle_t* hle)
{
    static uint32_t seen[AUDIT_MAX_UCODES];
    static int      nscenes[AUDIT_MAX_UCODES];
    static int      nseen;
    static unsigned taskno;

    uint32_t uc_dstart, da, sz, sig;
    int slot = -1, keep = 0, i;

    if (getenv("AUDIT_DUMP") == NULL)
        return;

    /* hle_execute runs for every task type.  Only the audio ones are
     * wanted; a graphics task would otherwise take a capture slot and
     * push a real one out. */
    if (*dmem_u32(hle, TASK_TYPE) != 2)
        return;

    uc_dstart = *dmem_u32(hle, TASK_UCODE_DATA);
    da        = *dmem_u32(hle, TASK_DATA_PTR);
    sz        = *dmem_u32(hle, TASK_DATA_SIZE);
    sig       = *dram_u32(hle, uc_dstart + 0x10);

    for (i = 0; i < nseen; ++i)
        if (seen[i] == sig)
            slot = i;
    if (slot < 0) {
        if (nseen >= AUDIT_MAX_UCODES)
            return;
        slot = nseen;
        seen[nseen++] = sig;
    }

    ++taskno;

    /* An alist task carries op7 or op8 somewhere in its command list;
     * musyx carries neither, which is what AUDIT_ANY is for. */
    if (sz >= 8 && sz < 0x4000) {
        uint32_t k;
        for (k = 0; k + 8 <= sz; k += 8) {
            uint32_t op = (*dram_u32(hle, da + k) >> 24) & 0xff;
            if (op == 7 || op == 8) { keep = 1; break; }
        }
    }
    if (getenv("AUDIT_ANY"))
        keep = 1;

    if (getenv("AUDIT_TASKLOG"))
        fprintf(stderr, "TASK %u sig=%08x cmds=%u\n",
                taskno, sig, (unsigned)(sz / 8));

    {
        const char* s = getenv("AUDIT_SKIP");
        if (s != NULL && taskno < (unsigned)atoi(s))
            keep = 0;
    }
    {
        const char* m = getenv("AUDIT_MINCMDS");
        if (m != NULL && (sz / 8) < (uint32_t)atoi(m))
            keep = 0;
    }
    if (getenv("AUDIT_VOICES") && *dram_u16(hle, da) == 0)
        keep = 0;

    if (!keep || nscenes[slot] >= AUDIT_SCENES_PER_UCODE)
        return;

    {
        char  path[256];
        FILE* f;

        snprintf(path, sizeof(path), "/tmp/ucodes/mp3scene_%08x_%d.bin",
                 sig, nscenes[slot]);
        f = fopen(path, "wb");
        if (f != NULL) {
            fwrite(hle->dram, 1, 0x800000, f);
            fwrite(hle->dmem, 1, 0x1000, f);
            fclose(f);
            ++nscenes[slot];
        }
    }
}

#else   /* !HLE_AUDIT_CAPTURE */

#define audit_capture_task(hle) ((void)0)

#endif  /* HLE_AUDIT_CAPTURE */

#endif  /* HLE_AUDIT_CAPTURE_H */
