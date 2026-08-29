/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 *   Mupen64plus-rsp-hle - hle.c                                           *
 *   Mupen64Plus homepage: https://mupen64plus.org/                        *
 *   Copyright (C) 2012 Bobby Smiles                                       *
 *   Copyright (C) 2009 Richard Goedeken                                   *
 *   Copyright (C) 2002 Hacktarux                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.          *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#include <stdio.h>
#include <stdlib.h>
#include <boolean.h>
#include <stdint.h>

#ifdef ENABLE_TASK_DUMP
#include <stdio.h>
#include <stdlib.h>
#endif

#include "hle_external.h"
#include "hle_internal.h"
#include "memory.h"
#include "ucodes.h"

#define min(a,b) (((a) < (b)) ? (a) : (b))

/* some rdp status flags */
#define DP_STATUS_FREEZE            0x2



/* helper functions prototypes */
static unsigned int sum_bytes(const unsigned char *bytes, unsigned int size);
static bool is_task(struct hle_t* hle);
static void send_dlist_to_gfx_plugin(struct hle_t* hle);
static ucode_func_t try_audio_task_detection(struct hle_t* hle);
static ucode_func_t try_normal_task_detection(struct hle_t* hle);
static ucode_func_t non_task_detection(struct hle_t* hle);
static ucode_func_t task_detection(struct hle_t* hle);

#ifdef ENABLE_TASK_DUMP
static void dump_binary(struct hle_t* hle, const char *const filename,
                        const unsigned char *const bytes, unsigned int size);
static void dump_task(struct hle_t* hle, const char *const filename);
static void dump_unknown_task(struct hle_t* hle, unsigned int uc_start);
static void dump_unknown_non_task(struct hle_t* hle, unsigned int uc_start);
#endif

/* Global functions */
/* streaming graphics task state (see streaming_gfx_task) */
static int l_streaming_gfx_running;
/* Rogue Squadron streaming graphics task state (see rs_gfx_task) */
static int l_rs_gfx_running;
/* ZSortBOSS task state (see zboss_gfx_task) */
static int l_zboss_wait;
static int l_zboss_running;

void hle_init(struct hle_t* hle,
    unsigned char* dram,
    unsigned char* dmem,
    unsigned char* imem,
    unsigned int* mi_intr,
    unsigned int* sp_mem_addr,
    unsigned int* sp_dram_addr,
    unsigned int* sp_rd_length,
    unsigned int* sp_wr_length,
    unsigned int* sp_status,
    unsigned int* sp_dma_full,
    unsigned int* sp_dma_busy,
    unsigned int* sp_pc,
    unsigned int* sp_semaphore,
    unsigned int* dpc_start,
    unsigned int* dpc_end,
    unsigned int* dpc_current,
    unsigned int* dpc_status,
    unsigned int* dpc_clock,
    unsigned int* dpc_bufbusy,
    unsigned int* dpc_pipebusy,
    unsigned int* dpc_tmem,
    void* user_defined)
{
    hle->dram         = dram;
    hle->dmem         = dmem;
    hle->imem         = imem;
    hle->mi_intr      = mi_intr;
    hle->sp_mem_addr  = sp_mem_addr;
    hle->sp_dram_addr = sp_dram_addr;
    hle->sp_rd_length = sp_rd_length;
    hle->sp_wr_length = sp_wr_length;
    hle->sp_status    = sp_status;
    hle->sp_dma_full  = sp_dma_full;
    hle->sp_dma_busy  = sp_dma_busy;
    hle->sp_pc        = sp_pc;
    hle->sp_semaphore = sp_semaphore;
    hle->dpc_start    = dpc_start;
    hle->dpc_end      = dpc_end;
    hle->dpc_current  = dpc_current;
    hle->dpc_status   = dpc_status;
    hle->dpc_clock    = dpc_clock;
    hle->dpc_bufbusy  = dpc_bufbusy;
    hle->dpc_pipebusy = dpc_pipebusy;
    hle->dpc_tmem     = dpc_tmem;
    hle->user_defined = user_defined;

    /* a streaming graphics task cannot span a plugin re-init (ROM
     * restart, savestate load): drop any suspended walk */
    l_streaming_gfx_running = 0;
    l_zboss_running = 0;
    l_rs_gfx_running = 0;
}

void rs_set_fog_block(unsigned int rdram_addr);

#include "hle_audit_capture.h"

void hle_execute(struct hle_t* hle)
{
    audit_capture_task(hle);

    uint32_t uc_start = *dmem_u32(hle, TASK_UCODE);
    uint32_t uc_dstart = *dmem_u32(hle, TASK_UCODE_DATA);
    uint32_t uc_dsize = *dmem_u32(hle, TASK_UCODE_DATA_SIZE);

    /* Rogue Squadron runs a fog/geometry setup overlay (ucode text word0
     * 0x40065800) immediately before its graphics task. That overlay's
     * ucode_data pointer is the RDRAM fog block the graphics task's terrain
     * needs; publish it to the angrylion RS walker so rs_seed_fog_row can read
     * the coefficients from fog_block + 0x160. Without this the graphics task
     * seeds fog from its own (zeroed) DMEM row and every terrain vertex renders
     * with alpha 0 -- the blown-out sand cells. */
    if ((*dram_u32(hle, uc_start) & 0xffffffffu) == 0x40065800u)
        rs_set_fog_block(uc_dstart);

    bool match = false;
    struct cached_ucodes_t * cached_ucodes = &hle->cached_ucodes;
    struct ucode_info_t *info = NULL;
    if (cached_ucodes->count > 0)
        info = &cached_ucodes->infos[cached_ucodes->count-1];
    for (int i = 0; i < cached_ucodes->count; i++)
    {
        if (info->uc_start == uc_start && info->uc_dstart == uc_dstart && info->uc_dsize == uc_dsize)
        {
            match = true;
            break;
        }
        info--;
    }

    if (!match)
    {
        /* wrap around when needed */
        if (cached_ucodes->count >= CACHED_UCODES_MAX_SIZE)
            cached_ucodes->count = 0;

        info = &cached_ucodes->infos[cached_ucodes->count];
        info->uc_start = uc_start;
        info->uc_dstart = uc_dstart;
        info->uc_dsize = uc_dsize;
        info->uc_pfunc = task_detection(hle);

        /* ensure we stay within bounds */
        if (cached_ucodes->count < CACHED_UCODES_MAX_SIZE)
            cached_ucodes->count++;

        assert(info->uc_pfunc != NULL);
    }

    info->uc_pfunc(hle);
}

/* local functions */
static unsigned int sum_bytes(const unsigned char *bytes, unsigned int size)
{
    unsigned int sum = 0;
    const unsigned char *const bytes_end = bytes + size;

    while (bytes != bytes_end)
        sum += *bytes++;

    return sum;
}

/**
 * Try to figure if the RSP was launched using osSpTask* functions
 * and not run directly (in which case DMEM[0xfc0-0xfff] is meaningless).
 *
 * Previously, the ucode_size field was used to determine this,
 * but it is not robust enough (hi Pokemon Stadium !) because games could write anything
 * in this field : most ucode_boot discard the value and just use 0xf7f anyway.
 *
 * Using ucode_boot_size should be more robust in this regard.
 **/
static bool is_task(struct hle_t* hle)
{
    return (*dmem_u32(hle, TASK_UCODE_BOOT_SIZE) <= 0x1000);
}

void rsp_break(struct hle_t* hle, unsigned int setbits)
{
    *hle->sp_status |= setbits | SP_STATUS_BROKE | SP_STATUS_HALT;

    if ((*hle->sp_status & SP_STATUS_INTR_ON_BREAK)) {
        *hle->mi_intr |= MI_INTR_SP;
        HleCheckInterrupts(hle->user_defined);
    }
}

static void send_alist_to_audio_plugin(struct hle_t* hle)
{
    HleProcessAlistList(hle->user_defined);
    rsp_break(hle, SP_STATUS_TASKDONE);
}

static void send_dlist_to_gfx_plugin(struct hle_t* hle)
{
    /* Since GFX_INFO version 2, these bits are set before calling the ProcessDlistList function.
     * And the GFX plugin is responsible to unset them if needed.
     * For GFX_INFO version < 2, the GFX plugin didn't have access to sp_status so
     * it doesn't matter if we set these bits before calling ProcessDlistList function.
     */
    *hle->sp_status |= SP_STATUS_TASKDONE | SP_STATUS_BROKE | SP_STATUS_HALT;

    HleProcessDlistList(hle->user_defined);

    if ((*hle->sp_status & SP_STATUS_INTR_ON_BREAK) && (*hle->sp_status & (SP_STATUS_TASKDONE | SP_STATUS_BROKE | SP_STATUS_HALT))) {
        *hle->mi_intr |= MI_INTR_SP;
        HleCheckInterrupts(hle->user_defined);
    }
}

static void task_done(struct hle_t* hle)
{
    rsp_break(hle, SP_STATUS_TASKDONE);
}

/* Persistent (streaming) gfx microcodes: the task is a long-lived server
 * the CPU talks to while it runs -- it yields (halts without BREAK) when
 * it needs the CPU, is re-dispatched by the core, and consumes commands
 * the CPU appends to a ring buffer live, with flow control through the
 * SP_STATUS signal bits. A one-shot dlist walk with a forced TASKDONE
 * ends the server after its first slice, so the game's render loop
 * deadlocks. These tasks must run on the LLE fallback, which reproduces
 * the full yield/signal protocol. If no fallback is linked, degrade to
 * the plain dlist forward: it cannot animate these titles but keeps the
 * task-done signalling flowing. */
static void forward_gfx_task_to_lle(struct hle_t* hle)
{
    if (HleForwardTask(hle->user_defined) != 0)
        send_dlist_to_gfx_plugin(hle);
}

/* Streaming graphics microcode (Gauntlet Legends): an F3DEX2 2.0xH
 * derivative whose display list the CPU extends while the task runs.
 * The list's live tail is a G_DL branch to its own address, patched
 * forward by the CPU chunk by chunk; the microcode also honors the
 * standard libultra yield protocol (CPU sets SIG0; the task saves its
 * state and breaks with SIG1|SIG2, and a relaunch with OS_TASK_YIELDED
 * set in the task flags resumes it).
 *
 * The angrylion HLE walker services the list in slices: it walks until
 * the list completes or its live tail is reached, keeping its position
 * and DL stack across calls. While the list is incomplete this task
 * function returns without setting HALT, BROKE or TASKDONE -- the core
 * then re-dispatches the task after letting the CPU run, exactly as it
 * does when the LLE interpreter hands a long-running task back -- so
 * the CPU keeps extending the list between slices. A pending SIG0 at a
 * slice boundary is answered with the microcode's yield status.
 *
 * The renderer and this plugin are statically linked into one core;
 * the direct call mirrors the existing cxd4 forward. If the walker
 * cannot service the task, fall back to the LLE forward. */
int angrylion_streaming_dlist(int resume);
int angrylion_rs_dlist(int resume);
int angrylion_zboss_dlist(int resume, unsigned int *sp_status);

/* ZSortBOSS (World Driver Championship, Stunt Racer 64): the task
 * carries two display lists (main at DMEM 0xff0, sub at 0xff8) and the
 * microcode is a command server with two host handshakes:
 *   WAITSIGNAL  raise SIG3, suspend until the CPU clears it,
 *   ENDMAINDL   suspend until the CPU raises SIG0, then clear SIG0 and
 *               continue on the sub list; ENDSUBDL completes the task.
 * The task raises SIG4 at launch and keeps it through completion
 * (status 0xa43 observed at task end under the LLE interpreter).
 *
 * The microcode also polls SIG0 once per object while the main list is
 * still walking (IMEM 0x85c) and interleaves the sub list into the walk
 * at the next sync object, so the walker takes the SP status pointer
 * and consumes the grant itself; the ENDMAINDL wait below only covers
 * the case where the main list finishes before the CPU grants.
 *
 * Between handshakes the walk runs in bounded slices; while a wait or
 * a pacing yield is pending this function returns the task incomplete
 * (no HALT, BROKE or TASKDONE) so the core re-dispatches after the CPU
 * has run, checking the condition each slice. */
static void zboss_gfx_task(struct hle_t* hle)
{
    int r;


    if (!l_zboss_running) {
        /* task launch: the microcode clears SIG1|SIG2 and raises SIG4 */
        *hle->sp_status &= ~(SP_STATUS_SIG1 | SP_STATUS_TASKDONE);
        *hle->sp_status |= SP_STATUS_SIG4;
        l_zboss_wait = 0;
        r = angrylion_zboss_dlist(0, hle->sp_status);
    }
    else if (l_zboss_wait == 1) {
        /* WAITSIGNAL: the microcode proceeds once the CPU clears SIG3 */
        if (*hle->sp_status & SP_STATUS_SIG3)
            return;     /* still waiting: incomplete, core re-dispatches */
        r = angrylion_zboss_dlist(1, hle->sp_status);
    }
    else {
        /* ENDMAINDL: the microcode waits for SIG0, then clears it */
        if (!(*hle->sp_status & SP_STATUS_SIG0))
            return;
        *hle->sp_status &= ~SP_STATUS_SIG0;
        r = angrylion_zboss_dlist(1, hle->sp_status);
    }

    if (r < 0) {
        l_zboss_running = 0;
        forward_gfx_task_to_lle(hle);
        return;
    }
    if (r == 0) {
        l_zboss_running = 0;
        rsp_break(hle, SP_STATUS_TASKDONE);
        return;
    }
    l_zboss_running = 1;
    if (r == 1) {
        /* WAITSIGNAL (microcode IMEM 0xfbc): the status write is
         * 0x12810 -- set SIG3, clear SIG1|SIG2, and raise the SP
         * interrupt so the CPU services the handshake without
         * polling */
        l_zboss_wait = 1;
        *hle->sp_status &= ~SP_STATUS_SIG1;
        *hle->sp_status |= SP_STATUS_SIG3;
        *hle->mi_intr |= MI_INTR_SP;
        HleCheckInterrupts(hle->user_defined);
    }
    else
        l_zboss_wait = 2;
    /* incomplete return: the core re-dispatches after the CPU runs */
}

/* Rogue Squadron (Factor 5 custom microcode): one persistent graphics
 * task per frame whose display-list page ring the CPU extends while the
 * microcode walks it -- the terminating command is provisional until
 * the CPU stops appending. The walker suspends at the live tail and
 * the task is re-dispatched through the incomplete-return protocol
 * until the end survives a slice. */
int angrylion_naboo_dlist(int resume, int emit);
static int l_naboo_gfx_running;
static int l_naboo_emit;

/* Naboo-era Factor 5 streaming server (Battle for Naboo, Indiana
 * Jones): same libultra yield protocol as Rogue Squadron; the walker
 * is incremental and returns negative on commands it does not yet
 * implement, in which case the slice reruns on the LLE fallback. */
static void naboo_gfx_task(struct hle_t* hle)
{
    int resume;
    int r;

    resume = l_naboo_gfx_running
          || ((*dmem_u32(hle, TASK_FLAGS) & 1) != 0);

    if (!resume)
        *hle->sp_status &= ~(SP_STATUS_SIG1 | SP_STATUS_TASKDONE);

    r = angrylion_naboo_dlist(resume, l_naboo_emit);

    if (r < 0) {
        l_naboo_gfx_running = 0;
        forward_gfx_task_to_lle(hle);
        return;
    }

    if (r == 0) {
        l_naboo_gfx_running = 0;
        rsp_break(hle, SP_STATUS_TASKDONE);
        return;
    }

    l_naboo_gfx_running = 1;
    *hle->dpc_current = *hle->dpc_end;
    *hle->dpc_status &= ~0x600u;

    if (*hle->sp_status & SP_STATUS_SIG0) {
        rsp_break(hle, SP_STATUS_SIG1);
        return;
    }
}

static void rs_gfx_task(struct hle_t* hle)
{
    int resume;
    int r;

    /* a relaunch with OS_TASK_YIELDED set resumes a yielded walk */
    resume = l_rs_gfx_running
          || ((*dmem_u32(hle, TASK_FLAGS) & 1) != 0);

    if (!resume) {
        /* the microcode clears SIG1 and SIG2 at task start */
        *hle->sp_status &= ~(SP_STATUS_SIG1 | SP_STATUS_TASKDONE);
    }

    r = angrylion_rs_dlist(resume);

    if (r < 0) {
        l_rs_gfx_running = 0;
        forward_gfx_task_to_lle(hle);
        return;
    }

    if (r == 0) {
        /* list complete: the microcode's exit writes SET_SIG2 and
         * breaks */
        l_rs_gfx_running = 0;
        rsp_break(hle, SP_STATUS_TASKDONE);
        return;
    }

    /* suspended at the live tail */
    l_rs_gfx_running = 1;

    /* The microcode feeds the RDP through DPC_START/DPC_END from its
     * own output buffer and the CPU paces its display-list appends on
     * the RDP's consumption; the HLE renderer consumes synchronously,
     * so report the pipe as drained and idle. */
    *hle->dpc_current = *hle->dpc_end;
    *hle->dpc_status &= ~0x600u;    /* clear cmd/pipe busy */

    if (*hle->sp_status & SP_STATUS_SIG0) {
        /* The CPU requested a yield. The microcode's yield overlay
         * saves its state and breaks with SET_SIG1 only -- not SIG2,
         * so the OS sees a yielded task, not a finished one -- and the
         * relaunch arrives with OS_TASK_YIELDED set. Keep the walker's
         * saved position so that relaunch resumes instead of
         * re-walking (and re-drawing) the ring from the start. SIG0 is
         * left for the CPU to clear, as on the real microcode. */
        rsp_break(hle, SP_STATUS_SIG1);
        return;
    }

    /* plain incomplete return: leave HALT/BROKE/TASKDONE clear so the
     * core re-dispatches the task after the CPU has run */
}

static void streaming_gfx_task(struct hle_t* hle)
{
    int resume;
    int r;

    resume = l_streaming_gfx_running
          || ((*dmem_u32(hle, TASK_FLAGS) & 1) != 0);

    if (!l_streaming_gfx_running || (*dmem_u32(hle, TASK_FLAGS) & 1)) {
        /* every real launch of the task runs the microcode's boot, which
         * clears SIG1 and SIG2 -- the fresh start AND the relaunch of a
         * yielded task. Only the plain re-dispatch of a suspended walk
         * (l_streaming_gfx_running set, no launch in between) keeps the
         * signals untouched. Without the clear on the yielded relaunch,
         * SIG1 survives the yield, and once the resumed task completes,
         * osSpTaskYielded reads the stale SIG1 and the game files the
         * completion as another yield. */
        *hle->sp_status &= ~(SP_STATUS_SIG1 | SP_STATUS_TASKDONE);
    }

    r = angrylion_streaming_dlist(resume);

    if (r < 0) {
        /* renderer can't service it (not initialised, bad task):
         * run the task on the LLE fallback instead */
        l_streaming_gfx_running = 0;
        forward_gfx_task_to_lle(hle);
        return;
    }

    if (r == 0) {
        /* list complete */
        l_streaming_gfx_running = 0;
        rsp_break(hle, SP_STATUS_TASKDONE);
        return;
    }

    /* suspended at the live tail */
    l_streaming_gfx_running = 1;

    if (*hle->sp_status & SP_STATUS_SIG0) {
        /* the CPU requested a yield: answer with the microcode's yield
         * status (SET_SIG1|SET_SIG2, break). SIG0 stays set -- the
         * game's osSpTaskYielded still needs to see the request, and
         * the CPU clears it itself on the relaunch (osSpTaskLoad). The
         * walker state stays saved; the relaunch arrives with
         * OS_TASK_YIELDED set. */
        rsp_break(hle, SP_STATUS_SIG1 | SP_STATUS_TASKDONE);
        return;
    }

    /* plain incomplete return: leave HALT/BROKE/TASKDONE clear so the
     * core re-dispatches the task after the CPU has run */
}

static void unknown_ucode(struct hle_t* hle)
{
    /* Forward task to RSP Fallback.
     * If task is not forwarded, use the regular "unknown ucode" path */
    if (HleForwardTask(hle->user_defined) != 0) {

        uint32_t uc_start = *dmem_u32(hle, TASK_UCODE);
        HleWarnMessage(hle->user_defined, "unknown RSP code: uc_start: %x PC:%x", uc_start, *hle->sp_pc);
#ifdef ENABLE_TASK_DUMP
        dump_unknown_non_task(hle, uc_start);
#endif
    }
}

static void unknown_task(struct hle_t* hle)
{
    /* Forward task to RSP Fallback.
     * If task is not forwarded, use the regular "unknown task" path */
    if (HleForwardTask(hle->user_defined) != 0) {

        /* Send task_done signal for unknown ucodes to allow further processings */
        rsp_break(hle, SP_STATUS_TASKDONE);

        uint32_t uc_start = *dmem_u32(hle, TASK_UCODE);
        HleWarnMessage(hle->user_defined, "unknown OSTask: uc_start: %x PC:%x", uc_start, *hle->sp_pc);
#ifdef ENABLE_TASK_DUMP
        dump_unknown_task(hle, uc_start);
#endif
    }
}

static ucode_func_t try_audio_task_detection(struct hle_t* hle)
{
    /* identify audio ucode by using the content of ucode_data */
    uint32_t ucode_data = *dmem_u32(hle, TASK_UCODE_DATA);
    uint32_t v;

    if (*dram_u32(hle, ucode_data) == 0x00000001) {
        if (*dram_u32(hle, ucode_data + 0x30) == 0xf0000f00) {
            v = *dram_u32(hle, ucode_data + 0x28);
            switch(v)
            {
            case 0x1e24138c: /* audio ABI (most common) */
                return &alist_process_audio;
            case 0x1dc8138c: /* GoldenEye */
                return &alist_process_audio_ge;
            case 0x1e3c1390: /* BlastCorp, DiddyKongRacing */
                return &alist_process_audio_bc;
            default:
                HleWarnMessage(hle->user_defined, "ABI1 identification regression: v=%08x", v);
            }
        } else {
            v = *dram_u32(hle, ucode_data + 0x10);
            switch(v)
            {
            case 0x11181350: /* MarioKart, WaveRace (E) */
                return &alist_process_nead_mk;
            case 0x111812e0: /* StarFox (J) */
                return &alist_process_nead_sfj;
            case 0x110412ac: /* WaveRace (J RevB) */
                return &alist_process_nead_wrjb;
            case 0x110412cc: /* StarFox/LylatWars (except J) */
                return &alist_process_nead_sf;
            case 0x1cd01250: /* FZeroX */
                return &alist_process_nead_fz;
            case 0x1f08122c: /* YoshisStory */
                return &alist_process_nead_ys;
            case 0x1f38122c: /* 1080° Snowboarding */
                return &alist_process_nead_1080;
            case 0x1f681230: /* Zelda OoT / Zelda MM (J, J RevA) */
                return &alist_process_nead_oot;
            case 0x1f801250: /* Zelda MM (except J, J RevA, E Beta), PokemonStadium 2 */
                return &alist_process_nead_mm;
            case 0x109411f8: /* Zelda MM (E Beta) */
                return &alist_process_nead_mmb;
            case 0x1eac11b8: /* AnimalCrossing */
                return &alist_process_nead_ac;
            case 0x00010010: /* MusyX v2 (IndianaJones, BattleForNaboo) */
                return &musyx_v2_task;
            case 0x1f701238: /* Mario Artist Talent Studio */
                return &alist_process_nead_mats;
            case 0x1f4c1230: /* FZeroX Expansion */
                return &alist_process_nead_efz;
            default:
                HleWarnMessage(hle->user_defined, "ABI2 identification regression: v=%08x", v);
            }
        }
    } else {
        v = *dram_u32(hle, ucode_data + 0x10);
        switch(v)
        {
        case 0x00000001: /* MusyX v1
            RogueSquadron, ResidentEvil2, PolarisSnoCross,
            TheWorldIsNotEnough, RugratsInParis, NBAShowTime,
            HydroThunder, Tarzan, GauntletLegend, Rush2049 */
            return &musyx_v1_task;
        case 0x0000127c: /* naudio (many games) */
            return &alist_process_naudio;
        case 0x00001280: /* BanjoKazooie */
            return &alist_process_naudio_bk;
        case 0x1c58126c: /* DonkeyKong */
            return &alist_process_naudio_dk;
        case 0x1ae8143c: /* BanjoTooie, JetForceGemini, MickeySpeedWayUSA, PerfectDark */
            return &alist_process_naudio_mp3;
        case 0x1ab0140c: /* ConkerBadFurDay */
            return &alist_process_naudio_cbfd;

        default:
            HleWarnMessage(hle->user_defined, "ABI3 identification regression: v=%08x", v);
        }
    }

    return NULL;
}

static ucode_func_t try_normal_task_detection(struct hle_t* hle)
{
    unsigned int sum =
        sum_bytes((void*)dram_u32(hle, *dmem_u32(hle, TASK_UCODE)), min(*dmem_u32(hle, TASK_UCODE_SIZE), 0xf80) >> 1);

    {
        const char *tp = getenv("HLE_UCODE_DUMP");
        if (tp) {
            FILE *tf = fopen(tp, "wb");
            if (tf) {
                fwrite((void*)dram_u32(hle, *dmem_u32(hle, TASK_UCODE)), 1, 0x1000, tf);
                fwrite((void*)dram_u32(hle, *dmem_u32(hle, TASK_UCODE_DATA)), 1, 0x800, tf);
                fclose(tf);
            }
        }
    }

    switch (sum) {
    /* StoreVe12: found in Zelda Ocarina of Time [misleading task->type == 4] */
    case 0x278:
        /* Nothing to emulate */
        return &task_done;

    /* GFX: Twintris [misleading task->type == 0] */
    case 0x212ee:
        if (hle->hle_gfx) {
            return &send_dlist_to_gfx_plugin;
        }
        return NULL;

    /* JPEG: found in Pokemon Stadium J */
    case 0x2c85a:
        return &jpeg_decode_PS0;

    /* JPEG: found in Zelda Ocarina of Time, Pokemon Stadium 1, Pokemon Stadium 2 */
    case 0x2caa6:
        return &jpeg_decode_PS;

    /* JPEG: found in Ogre Battle, Bottom of the 9th */
    case 0x130de:
    case 0x278b0:
        return &jpeg_decode_OB;

    /* Persistent streaming gfx microcodes (see forward_gfx_task_to_lle):
     * Gauntlet Legends' custom F3DEX2 derivative, and the BOSS Game
     * Studios microcode (World Driver Championship, Stunt Racer 64). */
    case 0x28b9e:
        return &streaming_gfx_task;
    /* Star Wars: Rogue Squadron (Factor 5 custom graphics microcode) */
    case 0x2095b:
        return &rs_gfx_task;
    case 0x1f7bb:
        return &zboss_gfx_task;
    /* Factor 5's later engine revisions (Battle for Naboo, Indiana
     * Jones and the Infernal Machine): the same persistent streaming
     * server family as Rogue Squadron. The naboo walker services what
     * it implements and reruns anything else on the LLE fallback --
     * never the one-shot dlist forward, whose forced TASKDONE kills
     * the server after its first slice and deadlocks the game. */
    case 0x25c16:       /* Battle for Naboo */
        l_naboo_emit = 1;
        return &naboo_gfx_task;
    case 0x25c53:       /* Indiana Jones and the Infernal Machine */
        l_naboo_emit = 1;
        return &naboo_gfx_task;
    }

    /* Resident Evil 2 */
    sum = sum_bytes((void*)dram_u32(hle, *dmem_u32(hle, TASK_UCODE)), 256);
    switch (sum) {

    case 0x450f:
        return &resize_bilinear_task;

    case 0x3b44:
        return &decode_video_frame_task;

    case 0x3d84:
        return &fill_video_double_buffer_task;
    }

    /* HVQM */
    sum = sum_bytes((void*)dram_u32(hle, *dmem_u32(hle, TASK_UCODE)), 1488);
    switch (sum) {
    case 0x19495:
        return &hvqm2_decode_sp1_task;

    case 0x19728:
        return &hvqm2_decode_sp2_task;
    }

    return NULL;
}

static ucode_func_t non_task_detection(struct hle_t* hle)
{
    const unsigned int sum = sum_bytes(hle->imem, 44);

    if (sum == 0x9e2)
    {
        /* CIC x105 ucode (used during boot of CIC x105 games) */
        return &cicx105_ucode;
    }
    return &unknown_ucode;
}

static ucode_func_t task_detection(struct hle_t* hle)
{
    if (is_task(hle)) {
        ucode_func_t uc_pfunc;
        uint32_t type = *dmem_u32(hle, TASK_TYPE);

        if (type == 2) {
            if (hle->hle_aud) {
                return &send_alist_to_audio_plugin;
            }
            uc_pfunc = try_audio_task_detection(hle);
            if (uc_pfunc)
                return uc_pfunc;
        }

        uc_pfunc = try_normal_task_detection(hle);
        if (uc_pfunc)
            return uc_pfunc;
        
        if (type == 1) {
            if (hle->hle_gfx) {
                return &send_dlist_to_gfx_plugin;
            }
        }

        return &unknown_task;
    }
    else {
        return non_task_detection(hle);
    }
}

#ifdef ENABLE_TASK_DUMP
static void dump_unknown_task(struct hle_t* hle, unsigned int uc_start)
{
    char filename[256];
    uint32_t ucode = *dmem_u32(hle, TASK_UCODE);
    uint32_t ucode_data = *dmem_u32(hle, TASK_UCODE_DATA);
    uint32_t data_ptr = *dmem_u32(hle, TASK_DATA_PTR);

    sprintf(&filename[0], "task_%x.log", uc_start);
    dump_task(hle, filename);

    /* dump ucode_boot */
    sprintf(&filename[0], "ucode_boot_%x.bin", uc_start);
    dump_binary(hle, filename, (void*)dram_u32(hle, *dmem_u32(hle, TASK_UCODE_BOOT)), *dmem_u32(hle, TASK_UCODE_BOOT_SIZE));

    /* dump ucode */
    if (ucode != 0) {
        sprintf(&filename[0], "ucode_%x.bin", uc_start);
        dump_binary(hle, filename, (void*)dram_u32(hle, ucode), 0xf80);
    }

    /* dump ucode_data */
    if (ucode_data != 0) {
        sprintf(&filename[0], "ucode_data_%x.bin", uc_start);
        dump_binary(hle, filename, (void*)dram_u32(hle, ucode_data), *dmem_u32(hle, TASK_UCODE_DATA_SIZE));
    }

    /* dump data */
    if (data_ptr != 0) {
        sprintf(&filename[0], "data_%x.bin", uc_start);
        dump_binary(hle, filename, (void*)dram_u32(hle, data_ptr), *dmem_u32(hle, TASK_DATA_SIZE));
    }
}

static void dump_unknown_non_task(struct hle_t* hle, unsigned int uc_start)
{
    char filename[256];

    /* dump IMEM & DMEM for further analysis */
    sprintf(&filename[0], "imem_%x.bin", uc_start);
    dump_binary(hle, filename, hle->imem, 0x1000);

    sprintf(&filename[0], "dmem_%x.bin", uc_start);
    dump_binary(hle, filename, hle->dmem, 0x1000);
}

static void dump_binary(struct hle_t* hle, const char *const filename,
                        const unsigned char *const bytes, unsigned int size)
{
    FILE *f;

    /* if file already exists, do nothing */
    f = fopen(filename, "r");
    if (f == NULL) {
        /* else we write bytes to the file */
        f = fopen(filename, "wb");
        if (f != NULL) {
            if (fwrite(bytes, 1, size, f) != size)
                HleErrorMessage(hle->user_defined, "Writing error on %s", filename);
            fclose(f);
        } else
            HleErrorMessage(hle->user_defined, "Couldn't open %s for writing !", filename);
    } else
        fclose(f);
}

static void dump_task(struct hle_t* hle, const char *const filename)
{
    FILE *f;

    f = fopen(filename, "r");
    if (f == NULL) {
        f = fopen(filename, "w");
        fprintf(f,
                "type = %d\n"
                "flags = %d\n"
                "ucode_boot  = %#08x size  = %#x\n"
                "ucode       = %#08x size  = %#x\n"
                "ucode_data  = %#08x size  = %#x\n"
                "dram_stack  = %#08x size  = %#x\n"
                "output_buff = %#08x *size = %#x\n"
                "data        = %#08x size  = %#x\n"
                "yield_data  = %#08x size  = %#x\n",
                *dmem_u32(hle, TASK_TYPE),
                *dmem_u32(hle, TASK_FLAGS),
                *dmem_u32(hle, TASK_UCODE_BOOT),     *dmem_u32(hle, TASK_UCODE_BOOT_SIZE),
                *dmem_u32(hle, TASK_UCODE),          *dmem_u32(hle, TASK_UCODE_SIZE),
                *dmem_u32(hle, TASK_UCODE_DATA),     *dmem_u32(hle, TASK_UCODE_DATA_SIZE),
                *dmem_u32(hle, TASK_DRAM_STACK),     *dmem_u32(hle, TASK_DRAM_STACK_SIZE),
                *dmem_u32(hle, TASK_OUTPUT_BUFF),    *dmem_u32(hle, TASK_OUTPUT_BUFF_SIZE),
                *dmem_u32(hle, TASK_DATA_PTR),       *dmem_u32(hle, TASK_DATA_SIZE),
                *dmem_u32(hle, TASK_YIELD_DATA_PTR), *dmem_u32(hle, TASK_YIELD_DATA_SIZE));
        fclose(f);
    } else
        fclose(f);
}
#endif
