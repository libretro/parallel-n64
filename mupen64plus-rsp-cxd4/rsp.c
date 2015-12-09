/******************************************************************************\
* Authors:  Iconoclast                                                         *
* Release:  2013.12.12                                                         *
* License:  CC0 Public Domain Dedication                                       *
*                                                                              *
* To the extent possible under law, the author(s) have dedicated all copyright *
* and related and neighboring rights to this software to the public domain     *
* worldwide. This software is distributed without any warranty.                *
*                                                                              *
* You should have received a copy of the CC0 Public Domain Dedication along    *
* with this software.                                                          *
* If not, see <http://creativecommons.org/publicdomain/zero/1.0/>.             *
\******************************************************************************/

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#define MINIMUM_MESSAGE_PRIORITY    1
#define EXTERN_COMMAND_LIST_GBI
#define EXTERN_COMMAND_LIST_ABI
#define SEMAPHORE_LOCK_CORRECTIONS
#define WAIT_FOR_CPU_HOST
#define EMULATE_STATIC_PC

#include "config.h"

#include "Rsp_#1.1.h"
#include "rsp.h"
#include "m64p_plugin.h"

/* N:  number of processor elements in SIMD processor */
#define N      8

/*
 * RSP virtual registers (of vector unit)
 * The most important are the 32 general-purpose vector registers.
 * The correct way to accurately store these is using big-endian vectors.
 *
 * For ?WC2 we may need to do byte-precision access just as directly.
 * This is amended by using the `VU_S` and `VU_B` macros defined in `rsp.h`.
 */
static ALIGNED short VR[32][N];
static ALIGNED short VACC[3][N];

#if defined(ARCH_MIN_SSE2)
#include <emmintrin.h>

#define _mm_cmple_epu16(dst, src) _mm_cmpeq_epi16(_mm_subs_epu16(dst, src), _mm_setzero_si128())
#define _mm_cmpgt_epu16(dst, src) _mm_andnot_si128(_mm_cmpeq_epi16(dst, src), _mm_cmple_epu16(src, dst))
#define _mm_cmplt_epu16(dst, src) _mm_cmpgt_epu16(src, dst)
#define _mm_mullo_epu16(dst, src) _mm_mullo_epi16(dst, src)
#endif

/*
 * accumulator-indexing macros (inverted access dimensions, suited for SSE)
 */
#define HI      00
#define MD      01
#define LO      02

#define VACC_L      (VACC[LO])
#define VACC_M      (VACC[MD])
#define VACC_H      (VACC[HI])

#define ACC_L(i)    (VACC_L[i])
#define ACC_M(i)    (VACC_M[i])
#define ACC_H(i)    (VACC_H[i])

#include "vu/shuffle.h"
#include "vu/clamp.h"
#include "vu/cf.h"

static void res_V(int vd, int vs, int vt, int e)
{
   register int i;

   vs = vt = e = 0;
   if (vs != vt || vt != e)
      return;
   /* C2, RESERVED */
   /* uncertain how to handle reserved, untested */
   for (i = 0; i < N; i++)
      VR[vd][i] = 0x0000; /* override behavior (bpoint) */
}

static void res_M(int vd, int vs, int vt, int e)
{
   /* VMUL IQ */
   res_V(vd, vs, vt, e);
   /* Ultra64 OS did have these, so one could implement this ext. */
}

#include "vu/add.h"
#include "vu/logical.h"
#include "vu/divide.h"
#include "vu/select.h"
#include "vu/multiply.h"

static void (*COP2_C2[64])(int, int, int, int) = {
    VMULF  ,VMULU  ,res_M  ,res_M  ,VMUDL  ,VMUDM  ,VMUDN  ,VMUDH  , /* 000 */
    VMACF  ,VMACU  ,res_M  ,res_M  ,VMADL  ,VMADM  ,VMADN  ,VMADH  , /* 001 */
    VADD   ,VSUB   ,res_V  ,VABS   ,VADDC  ,VSUBC  ,res_V  ,res_V  , /* 010 */
    res_V  ,res_V  ,res_V  ,res_V  ,res_V  ,VSAW   ,res_V  ,res_V  , /* 011 */
    VLT    ,VEQ    ,VNE    ,VGE    ,VCL    ,VCH    ,VCR    ,VMRG   , /* 100 */
    VAND   ,VNAND  ,VOR    ,VNOR   ,VXOR   ,VNXOR  ,res_V  ,res_V  , /* 101 */
    VRCP   ,VRCPL  ,VRCPH  ,VMOV   ,VRSQ   ,VRSQL  ,VRSQH  ,VNOP   , /* 110 */
    res_V  ,res_V  ,res_V  ,res_V  ,res_V  ,res_V  ,res_V  ,res_V  , /* 111 */
}; /* 000     001     010     011     100     101     110     111 */

#include "matrix.h"

unsigned char conf[32];

#ifndef EMULATE_STATIC_PC
static int stage;
#endif
static int temp_PC;
#ifdef WAIT_FOR_CPU_HOST
static short MFC0_count[32];
/* Keep one C0 MF status read count for each scalar register. */
#endif

static RCPREG* CR[16];

#if (0)
#define SP_EXECUTE_LOG
#define VU_EMULATE_SCALAR_ACCUMULATOR_READ
#endif

/*
 * Most of the point behind this config system is to let users use HLE video
 * or audio plug-ins.  The other task types are used less than 1% of the time
 * and only in a few games.  They require simulation from within the RSP
 * internally, which I have no intention to ever support.  Some good research
 * on a few of these special task types was done by Hacktarux in the MUPEN64
 * HLE RSP plug-in, so consider using that instead for complete HLE.
 */

/*
 * Schedule binary dump exports to the DllConfig schedule delay queue.
 */
#define CFG_QUEUE_E_DRAM    (*(int *)(conf + 0x04))
#define CFG_QUEUE_E_DMEM    (*(int *)(conf + 0x08))
#define CFG_QUEUE_E_IMEM    (*(int *)(conf + 0x0C))
/*
 * Note:  This never actually made it into the configuration system.
 * Instead, DMEM and IMEM are always exported on every call to DllConfig().
 */

/*
 * Special switches.
 * (generally for correcting RSP clock behavior on Project64 2.x)
 * Also includes RSP register states debugger.
 */
#define CFG_WAIT_FOR_CPU_HOST       (*(int *)(conf + 0x10))
#define CFG_MEND_SEMAPHORE_LOCK     (*(int *)(conf + 0x14))
#define CFG_TRACE_RSP_REGISTERS     (*(int *)(conf + 0x18))


#define RSP_CXD4_VERSION 0x0101

#define RSP_PLUGIN_API_VERSION 0x020000
#define CONFIG_API_VERSION       0x020100
#define CONFIG_PARAM_VERSION     1.00

static int l_PluginInit = 0;
static m64p_handle l_ConfigRsp;
extern RSP_INFO rsp_info;

#define VERSION_PRINTF_SPLIT(x) (((x) >> 16) & 0xffff), (((x) >> 8) & 0xff), ((x) & 0xff)

NOINLINE void update_conf(void)
{
   memset(conf, 0, sizeof(conf));

   CFG_HLE_GFX = ConfigGetParamBool(l_ConfigRsp, "DisplayListToGraphicsPlugin");
   CFG_HLE_AUD = ConfigGetParamBool(l_ConfigRsp, "AudioListToAudioPlugin");
   CFG_WAIT_FOR_CPU_HOST = ConfigGetParamBool(l_ConfigRsp, "WaitForCPUHost");
   CFG_MEND_SEMAPHORE_LOCK = ConfigGetParamBool(l_ConfigRsp, "SupportCPUSemaphoreLock");
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                     void (*DebugCallback)(void *, int, const char *))
{
    float fConfigParamsVersion = 0.0f;

    if (l_PluginInit)
        return M64ERR_ALREADY_INIT;

    /* first thing is to set the callback function for debug info */

    /* set the default values for this plugin */
    ConfigSetDefaultFloat(l_ConfigRsp, "Version", CONFIG_PARAM_VERSION,  "Mupen64Plus cxd4 RSP Plugin config parameter version number");
    ConfigSetDefaultBool(l_ConfigRsp, "DisplayListToGraphicsPlugin", 0, "Send display lists to the graphics plugin");
    ConfigSetDefaultBool(l_ConfigRsp, "AudioListToAudioPlugin", 0, "Send audio lists to the audio plugin");
    ConfigSetDefaultBool(l_ConfigRsp, "WaitForCPUHost", 0, "Force CPU-RSP signals synchronization");
    ConfigSetDefaultBool(l_ConfigRsp, "SupportCPUSemaphoreLock", 0, "Support CPU-RSP semaphore lock");

    l_PluginInit = 1;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL PluginShutdown(void)
{
    if (!l_PluginInit)
        return M64ERR_NOT_INIT;

    l_PluginInit = 0;
    return M64ERR_SUCCESS;
}

EXPORT m64p_error CALL cxd4PluginGetVersion(m64p_plugin_type *PluginType, int *PluginVersion, int *APIVersion, const char **PluginNamePtr, int *Capabilities)
{
    /* set version info */
    if (PluginType != NULL)
        *PluginType = M64PLUGIN_RSP;

    if (PluginVersion != NULL)
        *PluginVersion = RSP_CXD4_VERSION;

    if (APIVersion != NULL)
        *APIVersion = RSP_PLUGIN_API_VERSION;

    if (Capabilities != NULL)
        *Capabilities = 0;

    return M64ERR_SUCCESS;
}

EXPORT int CALL RomOpen(void)
{
    if (!l_PluginInit)
        return 0;

    update_conf();
    return 1;
}



EXPORT void CALL cxd4InitiateRSP(RSP_INFO Rsp_Info, unsigned int *CycleCount)
{
    if (CycleCount != NULL) /* cycle-accuracy not doable with today's hosts */
        *CycleCount = 0x00000000;

    if (Rsp_Info.DMEM == Rsp_Info.IMEM) /* usually dummy RSP data for testing */
        return; /* DMA is not executed just because plugin initiates. */

#if 0
    while (Rsp_Info.IMEM != Rsp_Info.DMEM + 4096)
        message("Virtual host map noncontiguity.", 3);
#endif

    RSP = Rsp_Info;
    *RSP.SP_PC_REG = 0x04001000 & 0x00000FFF; /* task init bug on Mupen64 */
    CR[0x0] = RSP.SP_MEM_ADDR_REG;
    CR[0x1] = RSP.SP_DRAM_ADDR_REG;
    CR[0x2] = RSP.SP_RD_LEN_REG;
    CR[0x3] = RSP.SP_WR_LEN_REG;
    CR[0x4] = RSP.SP_STATUS_REG;
    CR[0x5] = RSP.SP_DMA_FULL_REG;
    CR[0x6] = RSP.SP_DMA_BUSY_REG;
    CR[0x7] = RSP.SP_SEMAPHORE_REG;
    CR[0x8] = RSP.DPC_START_REG;
    CR[0x9] = RSP.DPC_END_REG;
    CR[0xA] = RSP.DPC_CURRENT_REG;
    CR[0xB] = RSP.DPC_STATUS_REG;
    CR[0xC] = RSP.DPC_CLOCK_REG;
    CR[0xD] = RSP.DPC_BUFBUSY_REG;
    CR[0xE] = RSP.DPC_PIPEBUSY_REG;
    CR[0xF] = RSP.DPC_TMEM_REG;
}

EXPORT void CALL cxd4RomClosed(void)
{
    *RSP.SP_PC_REG = 0x00000000;
    return;
}

/*
 * RSP virtual registers (of scalar unit)
 * The most important are the 32 general-purpose scalar registers.
 * We have the convenience of using a 32-bit machine (Win32) to emulate
 * another 32-bit machine (MIPS/N64), so the most natural way to accurately
 * emulate the scalar GPRs is to use the standard `int` type.  Situations
 * specifically requiring sign-extension or lack thereof are forcibly
 * applied as defined in the MIPS quick reference card and user manuals.
 * Remember that these are not the same "GPRs" as in the MIPS ISA and totally
 * abandon their designated purposes on the master CPU host (the VR4300),
 * hence most of the MIPS names "k0, k1, t0, t1, v0, v1 ..." no longer apply.
 */
static int SR[32];

#include "rsp.h"

NOINLINE static void res_S(void)
{
}

#ifdef EMULATE_STATIC_PC
#define BASE_OFF    0x000
#else
#define BASE_OFF    0x004
#endif

#define SLOT_OFF    (BASE_OFF + 0x000)
#define LINK_OFF    (BASE_OFF + 0x004)

#define MF_SP_STATUS_TIMEOUT 8192

void set_PC(int address)
{
    temp_PC = 0x04001000 + (address & 0xFFC);
#ifndef EMULATE_STATIC_PC
    stage = 1;
#endif
}

/*
 * If the client CPU's shift amount is exactly 5 bits for a 32-bit source,
 * then omit emulating (sa & 31) in the SLL/SRL/SRA interpreter steps.
 * (Additionally, omit doing (GPR[rs] & 31) in SLLV/SRLV/SRAV.)
 *
 * As C pre-processor logic seems incapable of interpreting type storage,
 * stuff like #if (1U << 31 == 1U << ~0U) will generally just fail.
 */
#if defined(ARCH_MIN_SSE2)
#define MASK_SA(sa) (sa)
#else
#define MASK_SA(sa) ((sa) & 31)
#endif

#if (0)
#define ENDIAN   0
#else
#define ENDIAN  ~0
#endif
#define BES(address)    ((address) ^ ((ENDIAN) & 03))
#define HES(address)    ((address) ^ ((ENDIAN) & 02))
#define MES(address)    ((address) ^ ((ENDIAN) & 01))
#define WES(address)    ((address) ^ ((ENDIAN) & 00))
#define SR_B(s, i)      (*(byte *)(((byte *)(SR + s)) + BES(i)))
#define SR_S(s, i)      (*(short *)(((byte *)(SR + s)) + HES(i)))
#define SE(x, b)        (-(x & (1 << b)) | (x & ~(~0 << b)))
#define ZE(x, b)        (+(x & (1 << b)) | (x & ~(~0 << b)))

static union {
    unsigned char B[4];
    signed char SB[4];
    unsigned short H[2];
    signed short SH[2];
    unsigned W:  32;
} SR_temp;

static void ULW(int rd, uint32_t addr);
static void USW(int rs, uint32_t addr);

/*
 * All other behaviors defined below this point in the file are specific to
 * the SGI N64 extension to the MIPS R4000 and are not entirely implemented.
 */

/*** Scalar, Coprocessor Operations (system control) ***/
static void SP_DMA_READ(void);
static void SP_DMA_WRITE(void);

static void MFC0(int rt, int rd)
{
    SR[rt] = *(CR[rd]);
    SR[0] = 0x00000000;
    if (rd == 0x7) /* SP_SEMAPHORE_REG */
    {
        if (CFG_MEND_SEMAPHORE_LOCK == 0)
            return;
        *RSP.SP_SEMAPHORE_REG = 0x00000001;
        *RSP.SP_STATUS_REG |= 0x00000001; /* temporary bit to break CPU */
        return;
    }
#ifdef WAIT_FOR_CPU_HOST
    if (rd == 0x4) /* SP_STATUS_REG */
    {
        ++MFC0_count[rt];
        if (MFC0_count[rt] >= MF_SP_STATUS_TIMEOUT)
            *RSP.SP_STATUS_REG |= 0x00000001; /* Let OS restart the task. */
    }
#endif
}

static void MT_DMA_CACHE(int rt)
{
    *RSP.SP_MEM_ADDR_REG = SR[rt] & 0xFFFFFFF8; /* & 0x00001FF8 */
    /* Reserved upper bits are ignored during DMA R/W. */
}
static void MT_DMA_DRAM(int rt)
{
    *RSP.SP_DRAM_ADDR_REG = SR[rt] & 0xFFFFFFF8; /* & 0x00FFFFF8 */
    /* Let the reserved bits get sent, but the pointer is 24-bit. */
}
static void MT_DMA_READ_LENGTH(int rt)
{
    *RSP.SP_RD_LEN_REG = SR[rt] | 07;
    SP_DMA_READ();
}
static void MT_DMA_WRITE_LENGTH(int rt)
{
    *RSP.SP_WR_LEN_REG = SR[rt] | 07;
    SP_DMA_WRITE();
}

static void MT_SP_STATUS(int rt)
{
#if 0
    if (SR[rt] & 0xFE000040)
        message("MTC0\nSP_STATUS", 2);
#endif
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000001) <<  0);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00000002) <<  0);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000004) <<  1);
    *RSP.MI_INTR_REG &= ~((SR[rt] & 0x00000008) >> 3); /* SP_CLR_INTR */
    *RSP.MI_INTR_REG |=  ((SR[rt] & 0x00000010) >> 4); /* SP_SET_INTR */
    *RSP.SP_STATUS_REG |= (SR[rt] & 0x00000010) >> 4; /* int set halt */
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000020) <<  5);
 /* *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00000040) <<  5); */
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000080) <<  6);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00000100) <<  6);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000200) <<  7);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00000400) <<  7);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00000800) <<  8);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00001000) <<  8);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00002000) <<  9);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00004000) <<  9);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00008000) << 10);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00010000) << 10);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00020000) << 11);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00040000) << 11);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00080000) << 12);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00100000) << 12);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00200000) << 13);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x00400000) << 13);
    *RSP.SP_STATUS_REG &= ~(!!(SR[rt] & 0x00800000) << 14);
    *RSP.SP_STATUS_REG |=  (!!(SR[rt] & 0x01000000) << 14);
}
static void MT_SP_RESERVED(int rt)
{
    const uint32_t source = SR[rt] & 0x00000000; /* forced (zilmar, dox) */

    *RSP.SP_SEMAPHORE_REG = source;
}
static void MT_CMD_START(int rt)
{
    const uint32_t source = SR[rt] & 0xFFFFFFF8; /* Funnelcube demo */

#if 0
    if (*RSP.DPC_BUFBUSY_REG) /* lock hazards not implemented */
        message("MTC0\nCMD_START", 0);
#endif
    *RSP.DPC_END_REG = *RSP.DPC_CURRENT_REG = *RSP.DPC_START_REG = source;
}
static void MT_CMD_END(int rt)
{
#if 0
    if (*RSP.DPC_BUFBUSY_REG)
        message("MTC0\nCMD_END", 0); /* This is just CA-related. */
#endif
    *RSP.DPC_END_REG = SR[rt] & 0xFFFFFFF8;
    if (RSP.ProcessRdpList == NULL) /* zilmar GFX #1.2 */
        return;
    RSP.ProcessRdpList();
}
static void MT_CMD_STATUS(int rt)
{
#if 0
    if (SR[rt] & 0xFFFFFD80) /* unsupported or reserved bits */
        message("MTC0\nCMD_STATUS", 2);
#endif
    *RSP.DPC_STATUS_REG &= ~(!!(SR[rt] & 0x00000001) << 0);
    *RSP.DPC_STATUS_REG |=  (!!(SR[rt] & 0x00000002) << 0);
    *RSP.DPC_STATUS_REG &= ~(!!(SR[rt] & 0x00000004) << 1);
    *RSP.DPC_STATUS_REG |=  (!!(SR[rt] & 0x00000008) << 1);
    *RSP.DPC_STATUS_REG &= ~(!!(SR[rt] & 0x00000010) << 2);
    *RSP.DPC_STATUS_REG |=  (!!(SR[rt] & 0x00000020) << 2);
/* Some NUS-CIC-6105 SP tasks try to clear some zeroed DPC registers. */
    *RSP.DPC_TMEM_REG     &= !(SR[rt] & 0x00000040) * -1;
 /* *RSP.DPC_PIPEBUSY_REG &= !(SR[rt] & 0x00000080) * -1; */
 /* *RSP.DPC_BUFBUSY_REG  &= !(SR[rt] & 0x00000100) * -1; */
    *RSP.DPC_CLOCK_REG    &= !(SR[rt] & 0x00000200) * -1;
}

static void MT_CMD_CLOCK(int rt)
{
#if 0
    message("MTC0\nCMD_CLOCK", 1); /* read-only?? */
#endif
    *RSP.DPC_CLOCK_REG = SR[rt];
    /* Appendix says this is RW; elsewhere it says R. */
}

static void MT_READ_ONLY(int rt)
{
#if 0
    char text[64];

    sprintf(text, "MTC0\nInvalid write attempt.\nSR[%i] = 0x%08X", rt, SR[rt]);
    message(text, 2);
#endif
}

static void (*MTC0[16])(int) = {
MT_DMA_CACHE       ,MT_DMA_DRAM        ,MT_DMA_READ_LENGTH ,MT_DMA_WRITE_LENGTH,
MT_SP_STATUS       ,MT_READ_ONLY       ,MT_READ_ONLY       ,MT_SP_RESERVED,
MT_CMD_START       ,MT_CMD_END         ,MT_READ_ONLY       ,MT_CMD_STATUS,
MT_CMD_CLOCK       ,MT_READ_ONLY       ,MT_READ_ONLY       ,MT_READ_ONLY
}; 

static void SP_DMA_READ(void)
{
    register unsigned int length = ((*RSP.SP_RD_LEN_REG & 0x00000FFF) >>  0) + 1;
    register unsigned int count  = ((*RSP.SP_RD_LEN_REG & 0x000FF000) >> 12) + 1;
    register unsigned int skip   = ((*RSP.SP_RD_LEN_REG & 0xFFF00000) >> 20) + length;

    do
    {
       /* `count` always starts > 0, so we begin with `do` instead of `while`. */
       register unsigned int i = 0;

       --count;
       do
       {
          unsigned int offC = (count*length + *RSP.SP_MEM_ADDR_REG + i) & 0x00001FF8; /* SP cache and dynamic DMA pointers */
          unsigned int offD = (count*skip + *RSP.SP_DRAM_ADDR_REG + i) & 0x00FFFFF8;
          memcpy(RSP.DMEM + offC, RSP.RDRAM + offD, 8);
          i += 0x008;
       } while (i < length);
    } while (count);
    *RSP.SP_DMA_BUSY_REG = 0x00000000;
    *RSP.SP_STATUS_REG &= ~0x00000004; /* SP_STATUS_DMABUSY */
}

static void SP_DMA_WRITE(void)
{
    register unsigned int length = ((*RSP.SP_WR_LEN_REG & 0x00000FFF) >>  0) + 1;
    register unsigned int count  = ((*RSP.SP_WR_LEN_REG & 0x000FF000) >> 12) + 1;
    register unsigned int skip   = ((*RSP.SP_WR_LEN_REG & 0xFFF00000) >> 20) + length;

    do
    {
       /* `count` always starts > 0, so we begin with `do` instead of `while`. */
       register unsigned int i = 0;

       --count;
       do
       {
          /* SP cache and dynamic DMA pointers */
          unsigned int offC = (count*length + *RSP.SP_MEM_ADDR_REG + i) & 0x00001FF8;
          unsigned int offD = (count*skip + *RSP.SP_DRAM_ADDR_REG + i) & 0x00FFFFF8;
          memcpy(RSP.RDRAM + offD, RSP.DMEM + offC, 8);
          i += 0x000008;
       } while (i < length);
    } while (count);
    *RSP.SP_DMA_BUSY_REG = 0x00000000;
    *RSP.SP_STATUS_REG &= ~0x00000004; /* SP_STATUS_DMABUSY */
}

/*** Scalar, Coprocessor Operations (vector unit) ***/


/*
 * Since RSP vectors are stored 100% accurately as big-endian arrays for the
 * proper vector operation math to be done, LWC2 and SWC2 emulation code will
 * have to look a little different.  zilmar's method is to distort the endian
 * using an array of unions, permitting hacked byte- and halfword-precision.
 */

/*
 * Universal byte-access macro for 16*8 halfword vectors.
 * Use this macro if you are not sure whether the element is odd or even.
 */
#define VR_B(vt,element)    (*(byte *)((byte *)(VR[vt]) + MES(element)))

/*
 * Optimized byte-access macros for the vector registers.
 * Use these ONLY if you know the element is even (or odd in the second).
 */
#define VR_A(vt,element)    (*(byte *)((byte *)(VR[vt]) + element + MES(0x0)))
#define VR_U(vt,element)    (*(byte *)((byte *)(VR[vt]) + element - MES(0x0)))

/*
 * Optimized halfword-access macro for indexing eight-element vectors.
 * Use this ONLY if you know the element is even, not odd.
 *
 * If the four-bit element is odd, then there is no solution in one hit.
 */
#define VR_S(vt,element)    (*(short *)((byte *)(VR[vt]) + element))

extern unsigned short get_VCO(void);
extern unsigned short get_VCC(void);
extern unsigned char get_VCE(void);
extern void set_VCO(unsigned short VCO);
extern void set_VCC(unsigned short VCC);
extern void set_VCE(unsigned char VCE);
extern short vce[8];

unsigned short rwR_VCE(void)
{ /* never saw a game try to read VCE out to a scalar GPR yet */
    register unsigned short ret_slot = 0x00 | (unsigned short)get_VCE();
    return (ret_slot);
}

void rwW_VCE(unsigned short VCE)
{ /* never saw a game try to write VCE using a scalar GPR yet */
    register int i;

    VCE = 0x00 | (VCE & 0xFF);
    for (i = 0; i < 8; i++)
        vce[i] = (VCE >> i) & 1;
}

static unsigned short (*R_VCF[32])(void) = {
    get_VCO,get_VCC,rwR_VCE,rwR_VCE,
/* Hazard reaction barrier:  RD = (UINT16)(inst) >> 11, without &= 3. */
    get_VCO,get_VCC,rwR_VCE,rwR_VCE,
    get_VCO,get_VCC,rwR_VCE,rwR_VCE,
    get_VCO,get_VCC,rwR_VCE,rwR_VCE,
    get_VCO,get_VCC,rwR_VCE,rwR_VCE,
    get_VCO,get_VCC,rwR_VCE,rwR_VCE,
    get_VCO,get_VCC,rwR_VCE,rwR_VCE,
    get_VCO,get_VCC,rwR_VCE,rwR_VCE
};
static void (*W_VCF[32])(unsigned short) = {
    set_VCO,set_VCC,rwW_VCE,rwW_VCE,
/* Hazard reaction barrier:  RD = (UINT16)(inst) >> 11, without &= 3. */
    set_VCO,set_VCC,rwW_VCE,rwW_VCE,
    set_VCO,set_VCC,rwW_VCE,rwW_VCE,
    set_VCO,set_VCC,rwW_VCE,rwW_VCE,
    set_VCO,set_VCC,rwW_VCE,rwW_VCE,
    set_VCO,set_VCC,rwW_VCE,rwW_VCE,
    set_VCO,set_VCC,rwW_VCE,rwW_VCE,
    set_VCO,set_VCC,rwW_VCE,rwW_VCE
};

static void MFC2(int rt, int vs, int e)
{
    SR_B(rt, 2) = VR_B(vs, e);
    e = (e + 0x1) & 0xF;
    SR_B(rt, 3) = VR_B(vs, e);
    SR[rt] = (signed short)(SR[rt]);
    SR[0] = 0x00000000;
}

static void MTC2(int rt, int vd, int e)
{
   VR_B(vd, e+0x0) = SR_B(rt, 2);
   VR_B(vd, e+0x1) = SR_B(rt, 3);
   /* If element == 0xF, it does not matter; loads do not wrap over. */
}

static void CFC2(int rt, int rd)
{
    SR[rt] = (signed short)R_VCF[rd]();
    SR[0] = 0x00000000;
}

static void CTC2(int rt, int rd)
{
    W_VCF[rd](SR[rt] & 0x0000FFFF);
}

/*** Scalar, Coprocessor Operations (vector unit, scalar cache transfers) ***/
static void LBV(int vt, int element, int offset, int base)
{
   const int e = element;
   register uint32_t addr = (SR[base] + 1*offset) & 0x00000FFF;
   VR_B(vt, e) = RSP.DMEM[BES(addr)];
}

static void LSV(int vt, int element, int offset, int base)
{
   int correction;
   register uint32_t addr;
   const int e = element;

   if (e & 0x1) /* LSV, illegal element */
      return;

   addr = (SR[base] + 2*offset) & 0x00000FFF;
   correction = addr % 0x004;
   if (correction == 0x003) /* LSV, weird address */
      return;

   VR_S(vt, e) = *(short *)(RSP.DMEM + addr - HES(0x000)*(correction - 1));
}

static void LLV(int vt, int element, int offset, int base)
{
    int correction;
    register uint32_t addr;
    const int e = element;

    if (e & 0x1) /* LLV, Odd element */
        return;

    /* Illegal (but still even) elements are used by Boss Game Studios. */
    addr = (SR[base] + 4*offset) & 0x00000FFF;
    if (addr & 0x00000001) /* LLV, Odd address */
    {
       /* branch very unlikely:  "Star Wars:  Battle for Naboo" unaligned addr */
       VR_A(vt, e+0x0) = RSP.DMEM[BES(addr)];
       addr = (addr + 0x00000001) & 0x00000FFF;
       VR_U(vt, e+0x1) = RSP.DMEM[BES(addr)];
       addr = (addr + 0x00000001) & 0x00000FFF;
       VR_A(vt, e+0x2) = RSP.DMEM[BES(addr)];
       addr = (addr + 0x00000001) & 0x00000FFF;
       VR_U(vt, e+0x3) = RSP.DMEM[BES(addr)];
       return;
    }

    correction = HES(0x000)*(addr%0x004 - 1);
    VR_S(vt, e+0x0) = *(short *)(RSP.DMEM + addr - correction);
    addr = (addr + 0x00000002) & 0x00000FFF; /* F3DLX 1.23:  addr%4 is 0x002. */
    VR_S(vt, e+0x2) = *(short *)(RSP.DMEM + addr + correction);
}

static void LDV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    const int e = element;

    if (e & 0x1) /* LDV, Odd element */
        return;

    /* Illegal (but still even) elements are used by Boss Game Studios. */
    addr = (SR[base] + 8*offset) & 0x00000FFF;

    switch (addr & 07)
    {
       case 00:
          VR_S(vt, e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x000));
          VR_S(vt, e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x002));
          VR_S(vt, e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x004));
          VR_S(vt, e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x006));
          break;
       case 01: /* standard ABI ucodes (unlike e.g. MusyX w/ even addresses) */
          VR_S(vt, e+0x0) = *(short *)(RSP.DMEM + addr + 0x000);
          VR_A(vt, e+0x2) = RSP.DMEM[addr + 0x002 - BES(0x000)];
          VR_U(vt, e+0x3) = RSP.DMEM[addr + 0x003 + BES(0x000)];
          VR_S(vt, e+0x4) = *(short *)(RSP.DMEM + addr + 0x004);
          VR_A(vt, e+0x6) = RSP.DMEM[addr + 0x006 - BES(0x000)];
          addr += 0x007 + BES(00);
          addr &= 0x00000FFF;
          VR_U(vt, e+0x7) = RSP.DMEM[addr];
          break;
       case 02:
          VR_S(vt, e+0x0) = *(short *)(RSP.DMEM + addr + 0x000 - HES(0x000));
          VR_S(vt, e+0x2) = *(short *)(RSP.DMEM + addr + 0x002 + HES(0x000));
          VR_S(vt, e+0x4) = *(short *)(RSP.DMEM + addr + 0x004 - HES(0x000));
          addr += 0x006 + HES(00);
          addr &= 0x00000FFF;
          VR_S(vt, e+0x6) = *(short *)(RSP.DMEM + addr);
          break;
       case 03: /* standard ABI ucodes (unlike e.g. MusyX w/ even addresses) */
          VR_A(vt, e+0x0) = RSP.DMEM[addr + 0x000 - BES(0x000)];
          VR_U(vt, e+0x1) = RSP.DMEM[addr + 0x001 + BES(0x000)];
          VR_S(vt, e+0x2) = *(short *)(RSP.DMEM + addr + 0x002);
          VR_A(vt, e+0x4) = RSP.DMEM[addr + 0x004 - BES(0x000)];
          addr += 0x005 + BES(00);
          addr &= 0x00000FFF;
          VR_U(vt, e+0x5) = RSP.DMEM[addr];
          VR_S(vt, e+0x6) = *(short *)(RSP.DMEM + addr + 0x001 - BES(0x000));
          break;
       case 04:
          VR_S(vt, e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x000));
          VR_S(vt, e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x002));
          addr += 0x004 + WES(00);
          addr &= 0x00000FFF;
          VR_S(vt, e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x000));
          VR_S(vt, e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x002));
          break;
       case 05: /* standard ABI ucodes (unlike e.g. MusyX w/ even addresses) */
          VR_S(vt, e+0x0) = *(short *)(RSP.DMEM + addr + 0x000);
          VR_A(vt, e+0x2) = RSP.DMEM[addr + 0x002 - BES(0x000)];
          addr += 0x003;
          addr &= 0x00000FFF;
          VR_U(vt, e+0x3) = RSP.DMEM[addr + BES(0x000)];
          VR_S(vt, e+0x4) = *(short *)(RSP.DMEM + addr + 0x001);
          VR_A(vt, e+0x6) = RSP.DMEM[addr + BES(0x003)];
          VR_U(vt, e+0x7) = RSP.DMEM[addr + BES(0x004)];
          break;
       case 06:
          VR_S(vt, e+0x0) = *(short *)(RSP.DMEM + addr - HES(0x000));
          addr += 0x002;
          addr &= 0x00000FFF;
          VR_S(vt, e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x000));
          VR_S(vt, e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x002));
          VR_S(vt, e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x004));
          break;
       case 07: /* standard ABI ucodes (unlike e.g. MusyX w/ even addresses) */
          VR_A(vt, e+0x0) = RSP.DMEM[addr - BES(0x000)];
          addr += 0x001;
          addr &= 0x00000FFF;
          VR_U(vt, e+0x1) = RSP.DMEM[addr + BES(0x000)];
          VR_S(vt, e+0x2) = *(short *)(RSP.DMEM + addr + 0x001);
          VR_A(vt, e+0x4) = RSP.DMEM[addr + BES(0x003)];
          VR_U(vt, e+0x5) = RSP.DMEM[addr + BES(0x004)];
          VR_S(vt, e+0x6) = *(short *)(RSP.DMEM + addr + 0x005);
          break;
    }
}

static void SBV(int vt, int element, int offset, int base)
{
   const int e = element;
   register uint32_t addr = (SR[base] + 1*offset) & 0x00000FFF;

   RSP.DMEM[BES(addr)] = VR_B(vt, e);
}

static void SSV(int vt, int element, int offset, int base)
{
   const int e = element;
   register uint32_t addr = (SR[base] + 2*offset) & 0x00000FFF;

   RSP.DMEM[BES(addr)] = VR_B(vt, (e + 0x0));
   addr = (addr + 0x00000001) & 0x00000FFF;
   RSP.DMEM[BES(addr)] = VR_B(vt, (e + 0x1) & 0xF);
}

static void SLV(int vt, int element, int offset, int base)
{
   int correction;
   register uint32_t addr;
   const int e = element;

   if ((e & 0x1) || e > 0xC) /* must support illegal even elements in F3DEX2 */
      return;

   addr = (SR[base] + 4*offset) & 0x00000FFF;
   if (addr & 0x00000001) /* SLV, Odd address */
      return;

   correction = HES(0x000)*(addr%0x004 - 1);
   *(short *)(RSP.DMEM + addr - correction) = VR_S(vt, e+0x0);
   addr = (addr + 0x00000002) & 0x00000FFF; /* F3DLX 0.95:  "Mario Kart 64" */
   *(short *)(RSP.DMEM + addr + correction) = VR_S(vt, e+0x2);
}

static void SDV(int vt, int element, int offset, int base)
{
   register uint32_t addr;
   const int e = element;

   addr = (SR[base] + 8*offset) & 0x00000FFF;
   if (e > 0x8 || (e & 0x1))
   { /* Illegal elements with Boss Game Studios publications. */
      register int i;

      for (i = 0; i < 8; i++)
         RSP.DMEM[BES(addr++ & 0x00000FFF)] = VR_B(vt, (e+i)&0xF);
      return;
   }

   switch (addr & 07)
   {
      case 00:
         *(short *)(RSP.DMEM + addr + HES(0x000)) = VR_S(vt, e+0x0);
         *(short *)(RSP.DMEM + addr + HES(0x002)) = VR_S(vt, e+0x2);
         *(short *)(RSP.DMEM + addr + HES(0x004)) = VR_S(vt, e+0x4);
         *(short *)(RSP.DMEM + addr + HES(0x006)) = VR_S(vt, e+0x6);
         break;
      case 01: /* "Tetrisphere" audio ucode */
         *(short *)(RSP.DMEM + addr + 0x000) = VR_S(vt, e+0x0);
         RSP.DMEM[addr + 0x002 - BES(0x000)] = VR_A(vt, e+0x2);
         RSP.DMEM[addr + 0x003 + BES(0x000)] = VR_U(vt, e+0x3);
         *(short *)(RSP.DMEM + addr + 0x004) = VR_S(vt, e+0x4);
         RSP.DMEM[addr + 0x006 - BES(0x000)] = VR_A(vt, e+0x6);
         addr += 0x007 + BES(0x000);
         addr &= 0x00000FFF;
         RSP.DMEM[addr] = VR_U(vt, e+0x7);
         break;
      case 02:
         *(short *)(RSP.DMEM + addr + 0x000 - HES(0x000)) = VR_S(vt, e+0x0);
         *(short *)(RSP.DMEM + addr + 0x002 + HES(0x000)) = VR_S(vt, e+0x2);
         *(short *)(RSP.DMEM + addr + 0x004 - HES(0x000)) = VR_S(vt, e+0x4);
         addr += 0x006 + HES(0x000);
         addr &= 0x00000FFF;
         *(short *)(RSP.DMEM + addr) = VR_S(vt, e+0x6);
         break;
      case 03: /* "Tetrisphere" audio ucode */
         RSP.DMEM[addr + 0x000 - BES(0x000)] = VR_A(vt, e+0x0);
         RSP.DMEM[addr + 0x001 + BES(0x000)] = VR_U(vt, e+0x1);
         *(short *)(RSP.DMEM + addr + 0x002) = VR_S(vt, e+0x2);
         RSP.DMEM[addr + 0x004 - BES(0x000)] = VR_A(vt, e+0x4);
         addr += 0x005 + BES(0x000);
         addr &= 0x00000FFF;
         RSP.DMEM[addr] = VR_U(vt, e+0x5);
         *(short *)(RSP.DMEM + addr + 0x001 - BES(0x000)) = VR_S(vt, 0x6);
         break;
      case 04:
         *(short *)(RSP.DMEM + addr + HES(0x000)) = VR_S(vt, e+0x0);
         *(short *)(RSP.DMEM + addr + HES(0x002)) = VR_S(vt, e+0x2);
         addr = (addr + 0x004) & 0x00000FFF;
         *(short *)(RSP.DMEM + addr + HES(0x000)) = VR_S(vt, e+0x4);
         *(short *)(RSP.DMEM + addr + HES(0x002)) = VR_S(vt, e+0x6);
         break;
      case 05: /* "Tetrisphere" audio ucode */
         *(short *)(RSP.DMEM + addr + 0x000) = VR_S(vt, e+0x0);
         RSP.DMEM[addr + 0x002 - BES(0x000)] = VR_A(vt, e+0x2);
         addr = (addr + 0x003) & 0x00000FFF;
         RSP.DMEM[addr + BES(0x000)] = VR_U(vt, e+0x3);
         *(short *)(RSP.DMEM + addr + 0x001) = VR_S(vt, e+0x4);
         RSP.DMEM[addr + BES(0x003)] = VR_A(vt, e+0x6);
         RSP.DMEM[addr + BES(0x004)] = VR_U(vt, e+0x7);
         break;
      case 06:
         *(short *)(RSP.DMEM + addr - HES(0x000)) = VR_S(vt, e+0x0);
         addr = (addr + 0x002) & 0x00000FFF;
         *(short *)(RSP.DMEM + addr + HES(0x000)) = VR_S(vt, e+0x2);
         *(short *)(RSP.DMEM + addr + HES(0x002)) = VR_S(vt, e+0x4);
         *(short *)(RSP.DMEM + addr + HES(0x004)) = VR_S(vt, e+0x6);
         break;
      case 07: /* "Tetrisphere" audio ucode */
         RSP.DMEM[addr - BES(0x000)] = VR_A(vt, e+0x0);
         addr = (addr + 0x001) & 0x00000FFF;
         RSP.DMEM[addr + BES(0x000)] = VR_U(vt, e+0x1);
         *(short *)(RSP.DMEM + addr + 0x001) = VR_S(vt, e+0x2);
         RSP.DMEM[addr + BES(0x003)] = VR_A(vt, e+0x4);
         RSP.DMEM[addr + BES(0x004)] = VR_U(vt, e+0x5);
         *(short *)(RSP.DMEM + addr + 0x005) = VR_S(vt, e+0x6);
         break;
   }
}

/*
 * Group II vector loads and stores:
 * PV and UV (As of RCP implementation, XV and ZV are reserved opcodes.)
 */
static void LPV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    register int b;
    const int e = element;

    if (e != 0x0) /* Illegal element */
        return;

    addr = (SR[base] + 8*offset) & 0x00000FFF;
    b = addr & 07;
    addr &= ~07;
    switch (b)
    {
        case 00:
            VR[vt][07] = RSP.DMEM[addr + BES(0x007)] << 8;
            VR[vt][06] = RSP.DMEM[addr + BES(0x006)] << 8;
            VR[vt][05] = RSP.DMEM[addr + BES(0x005)] << 8;
            VR[vt][04] = RSP.DMEM[addr + BES(0x004)] << 8;
            VR[vt][03] = RSP.DMEM[addr + BES(0x003)] << 8;
            VR[vt][02] = RSP.DMEM[addr + BES(0x002)] << 8;
            VR[vt][01] = RSP.DMEM[addr + BES(0x001)] << 8;
            VR[vt][00] = RSP.DMEM[addr + BES(0x000)] << 8;
            break;
        case 01: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            VR[vt][00] = RSP.DMEM[addr + BES(0x001)] << 8;
            VR[vt][01] = RSP.DMEM[addr + BES(0x002)] << 8;
            VR[vt][02] = RSP.DMEM[addr + BES(0x003)] << 8;
            VR[vt][03] = RSP.DMEM[addr + BES(0x004)] << 8;
            VR[vt][04] = RSP.DMEM[addr + BES(0x005)] << 8;
            VR[vt][05] = RSP.DMEM[addr + BES(0x006)] << 8;
            VR[vt][06] = RSP.DMEM[addr + BES(0x007)] << 8;
            addr += BES(0x008);
            addr &= 0x00000FFF;
            VR[vt][07] = RSP.DMEM[addr] << 8;
            break;
        case 02: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            VR[vt][00] = RSP.DMEM[addr + BES(0x002)] << 8;
            VR[vt][01] = RSP.DMEM[addr + BES(0x003)] << 8;
            VR[vt][02] = RSP.DMEM[addr + BES(0x004)] << 8;
            VR[vt][03] = RSP.DMEM[addr + BES(0x005)] << 8;
            VR[vt][04] = RSP.DMEM[addr + BES(0x006)] << 8;
            VR[vt][05] = RSP.DMEM[addr + BES(0x007)] << 8;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][06] = RSP.DMEM[addr + BES(0x000)] << 8;
            VR[vt][07] = RSP.DMEM[addr + BES(0x001)] << 8;
            break;
        case 03: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            VR[vt][00] = RSP.DMEM[addr + BES(0x003)] << 8;
            VR[vt][01] = RSP.DMEM[addr + BES(0x004)] << 8;
            VR[vt][02] = RSP.DMEM[addr + BES(0x005)] << 8;
            VR[vt][03] = RSP.DMEM[addr + BES(0x006)] << 8;
            VR[vt][04] = RSP.DMEM[addr + BES(0x007)] << 8;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][05] = RSP.DMEM[addr + BES(0x000)] << 8;
            VR[vt][06] = RSP.DMEM[addr + BES(0x001)] << 8;
            VR[vt][07] = RSP.DMEM[addr + BES(0x002)] << 8;
            break;
        case 04: /* "Resident Evil 2" in-game 3-D, F3DLX 2.08--"WWF No Mercy" */
            VR[vt][00] = RSP.DMEM[addr + BES(0x004)] << 8;
            VR[vt][01] = RSP.DMEM[addr + BES(0x005)] << 8;
            VR[vt][02] = RSP.DMEM[addr + BES(0x006)] << 8;
            VR[vt][03] = RSP.DMEM[addr + BES(0x007)] << 8;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][04] = RSP.DMEM[addr + BES(0x000)] << 8;
            VR[vt][05] = RSP.DMEM[addr + BES(0x001)] << 8;
            VR[vt][06] = RSP.DMEM[addr + BES(0x002)] << 8;
            VR[vt][07] = RSP.DMEM[addr + BES(0x003)] << 8;
            break;
        case 05: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            VR[vt][00] = RSP.DMEM[addr + BES(0x005)] << 8;
            VR[vt][01] = RSP.DMEM[addr + BES(0x006)] << 8;
            VR[vt][02] = RSP.DMEM[addr + BES(0x007)] << 8;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][03] = RSP.DMEM[addr + BES(0x000)] << 8;
            VR[vt][04] = RSP.DMEM[addr + BES(0x001)] << 8;
            VR[vt][05] = RSP.DMEM[addr + BES(0x002)] << 8;
            VR[vt][06] = RSP.DMEM[addr + BES(0x003)] << 8;
            VR[vt][07] = RSP.DMEM[addr + BES(0x004)] << 8;
            break;
        case 06: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            VR[vt][00] = RSP.DMEM[addr + BES(0x006)] << 8;
            VR[vt][01] = RSP.DMEM[addr + BES(0x007)] << 8;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][02] = RSP.DMEM[addr + BES(0x000)] << 8;
            VR[vt][03] = RSP.DMEM[addr + BES(0x001)] << 8;
            VR[vt][04] = RSP.DMEM[addr + BES(0x002)] << 8;
            VR[vt][05] = RSP.DMEM[addr + BES(0x003)] << 8;
            VR[vt][06] = RSP.DMEM[addr + BES(0x004)] << 8;
            VR[vt][07] = RSP.DMEM[addr + BES(0x005)] << 8;
            break;
        case 07: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            VR[vt][00] = RSP.DMEM[addr + BES(0x007)] << 8;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][01] = RSP.DMEM[addr + BES(0x000)] << 8;
            VR[vt][02] = RSP.DMEM[addr + BES(0x001)] << 8;
            VR[vt][03] = RSP.DMEM[addr + BES(0x002)] << 8;
            VR[vt][04] = RSP.DMEM[addr + BES(0x003)] << 8;
            VR[vt][05] = RSP.DMEM[addr + BES(0x004)] << 8;
            VR[vt][06] = RSP.DMEM[addr + BES(0x005)] << 8;
            VR[vt][07] = RSP.DMEM[addr + BES(0x006)] << 8;
            break;
    }
}
static void LUV(int vt, int element, int offset, int base)
{
    register int b;
    int e = element;
    register uint32_t addr = (SR[base] + 8*offset) & 0x00000FFF;

    if (e != 0x0)
    { /* "Mia Hamm Soccer 64" SP exception override (zilmar) */
        addr += -e & 0xF;
        for (b = 0; b < 8; b++)
        {
            VR[vt][b] = RSP.DMEM[BES(addr &= 0x00000FFF)] << 7;
            --e;
            addr -= 16 * (e == 0x0);
            ++addr;
        }
        return;
    }
    b = addr & 07;
    addr &= ~07;
    switch (b)
    {
        case 00:
            VR[vt][07] = RSP.DMEM[addr + BES(0x007)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][00] = RSP.DMEM[addr + BES(0x000)] << 7;
            break;
        case 01: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += BES(0x008);
            addr &= 0x00000FFF;
            VR[vt][07] = RSP.DMEM[addr] << 7;
            break;
        case 02: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][06] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x001)] << 7;
            break;
        case 03: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][05] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x002)] << 7;
            break;
        case 04: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][04] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x003)] << 7;
            break;
        case 05: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][03] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x004)] << 7;
            break;
        case 06: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x006)] << 7;
            VR[vt][01] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][02] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x005)] << 7;
            break;
        case 07: /* PKMN Puzzle League HVQM decoder */
            VR[vt][00] = RSP.DMEM[addr + BES(0x007)] << 7;
            addr += 0x008;
            addr &= 0x00000FFF;
            VR[vt][01] = RSP.DMEM[addr + BES(0x000)] << 7;
            VR[vt][02] = RSP.DMEM[addr + BES(0x001)] << 7;
            VR[vt][03] = RSP.DMEM[addr + BES(0x002)] << 7;
            VR[vt][04] = RSP.DMEM[addr + BES(0x003)] << 7;
            VR[vt][05] = RSP.DMEM[addr + BES(0x004)] << 7;
            VR[vt][06] = RSP.DMEM[addr + BES(0x005)] << 7;
            VR[vt][07] = RSP.DMEM[addr + BES(0x006)] << 7;
            break;
    }
}
static void SPV(int vt, int element, int offset, int base)
{
    register int b;
    register uint32_t addr;
    const int e = element;

    if (e != 0x0) /* SPV, illegal element */
        return;

    addr = (SR[base] + 8*offset) & 0x00000FFF;
    b = addr & 07;
    addr &= ~07;
    switch (b)
    {
        case 00:
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][07] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][00] >> 8);
            return;
        case 01: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][06] >> 8);
            addr += BES(0x008);
            addr &= 0x00000FFF;
            RSP.DMEM[addr] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 02: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][05] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 03: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][04] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 04: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][03] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 05: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][02] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 06: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][00] >> 8);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][01] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][07] >> 8);
            return;
        case 07: /* F3DZEX 2.08J "Doubutsu no Mori" (Animal Forest) CFB layer */
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][00] >> 8);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][01] >> 8);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][02] >> 8);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][03] >> 8);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][04] >> 8);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][05] >> 8);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][06] >> 8);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][07] >> 8);
            return;
    }
}
static void SUV(int vt, int element, int offset, int base)
{
    register int b;
    register uint32_t addr;
    const int e = element;

    if (e != 0x0) /* SUV, Illegal element */
        return;

    addr = (SR[base] + 8*offset) & 0x00000FFF;
    b = addr & 07;
    addr &= ~07;
    switch (b)
    {
        case 00:
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][07] >> 7);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][06] >> 7);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][05] >> 7);
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][04] >> 7);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][03] >> 7);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][02] >> 7);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][01] >> 7);
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][00] >> 7);
            return;
        case 04: /* "Indiana Jones and the Infernal Machine" in-game */
            RSP.DMEM[addr + BES(0x004)] = (unsigned char)(VR[vt][00] >> 7);
            RSP.DMEM[addr + BES(0x005)] = (unsigned char)(VR[vt][01] >> 7);
            RSP.DMEM[addr + BES(0x006)] = (unsigned char)(VR[vt][02] >> 7);
            RSP.DMEM[addr + BES(0x007)] = (unsigned char)(VR[vt][03] >> 7);
            addr += 0x008;
            addr &= 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = (unsigned char)(VR[vt][04] >> 7);
            RSP.DMEM[addr + BES(0x001)] = (unsigned char)(VR[vt][05] >> 7);
            RSP.DMEM[addr + BES(0x002)] = (unsigned char)(VR[vt][06] >> 7);
            RSP.DMEM[addr + BES(0x003)] = (unsigned char)(VR[vt][07] >> 7);
            return;
        default: /* Completely legal, just never seen it be done. */
            /* SUV, weird address */
            return;
    }
}

/*
 * Group III vector loads and stores:
 * HV, FV, and AV (As of RCP implementation, AV opcodes are reserved.)
 */
static void LHV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    const int e = element;

    if (e != 0x0) /* LHV, illegal element */
        return;

    addr = (SR[base] + 16*offset) & 0x00000FFF;
    if (addr & 0x0000000E) /* LHV, illegal address */
        return;

    addr ^= MES(00);
    VR[vt][07] = RSP.DMEM[addr + HES(0x00E)] << 7;
    VR[vt][06] = RSP.DMEM[addr + HES(0x00C)] << 7;
    VR[vt][05] = RSP.DMEM[addr + HES(0x00A)] << 7;
    VR[vt][04] = RSP.DMEM[addr + HES(0x008)] << 7;
    VR[vt][03] = RSP.DMEM[addr + HES(0x006)] << 7;
    VR[vt][02] = RSP.DMEM[addr + HES(0x004)] << 7;
    VR[vt][01] = RSP.DMEM[addr + HES(0x002)] << 7;
    VR[vt][00] = RSP.DMEM[addr + HES(0x000)] << 7;
    return;
}
static void SHV(int vt, int element, int offset, int base)
{
   register uint32_t addr;
   const int e = element;

   if (e != 0x0) /* SHV, illegal element */
      return;

   addr = (SR[base] + 16*offset) & 0x00000FFF;
   if (addr & 0x0000000E) /* SHV, illegal address */
      return;

   addr ^= MES(00);
   RSP.DMEM[addr + HES(0x00E)] = (unsigned char)(VR[vt][07] >> 7);
   RSP.DMEM[addr + HES(0x00C)] = (unsigned char)(VR[vt][06] >> 7);
   RSP.DMEM[addr + HES(0x00A)] = (unsigned char)(VR[vt][05] >> 7);
   RSP.DMEM[addr + HES(0x008)] = (unsigned char)(VR[vt][04] >> 7);
   RSP.DMEM[addr + HES(0x006)] = (unsigned char)(VR[vt][03] >> 7);
   RSP.DMEM[addr + HES(0x004)] = (unsigned char)(VR[vt][02] >> 7);
   RSP.DMEM[addr + HES(0x002)] = (unsigned char)(VR[vt][01] >> 7);
   RSP.DMEM[addr + HES(0x000)] = (unsigned char)(VR[vt][00] >> 7);
}

static void SFV(int vt, int element, int offset, int base)
{
    const int e = element;
    register uint32_t addr = ((SR[base] + 16*offset) & 0x00000FFF) & 0x00000FF3;
    addr ^= BES(00);

    switch (e)
    {
        case 0x0:
            RSP.DMEM[addr + 0x000] = (unsigned char)(VR[vt][00] >> 7);
            RSP.DMEM[addr + 0x004] = (unsigned char)(VR[vt][01] >> 7);
            RSP.DMEM[addr + 0x008] = (unsigned char)(VR[vt][02] >> 7);
            RSP.DMEM[addr + 0x00C] = (unsigned char)(VR[vt][03] >> 7);
            break;
        case 0x8:
            RSP.DMEM[addr + 0x000] = (unsigned char)(VR[vt][04] >> 7);
            RSP.DMEM[addr + 0x004] = (unsigned char)(VR[vt][05] >> 7);
            RSP.DMEM[addr + 0x008] = (unsigned char)(VR[vt][06] >> 7);
            RSP.DMEM[addr + 0x00C] = (unsigned char)(VR[vt][07] >> 7);
            break;
        default:
            /* SFV, illegal element */
            break;
    }
}

/*
 * Group IV vector loads and stores:
 * QV and RV
 */
static void LQV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    register int b;
    const int e = element; /* Boss Game Studios illegal elements */

    if (e & 0x1) /* LQV, odd element */
        return;

    addr = (SR[base] + 16*offset) & 0x00000FFF;
    if (addr & 0x00000001) /* LQV, odd address */
        return;

    b = addr & 0x0000000F;
    addr &= ~0x0000000F;
    switch (b/2) /* mistake in SGI patent regarding LQV */
    {
        case 0x0/2:
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR_S(vt,e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR_S(vt,e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR_S(vt,e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x006));
            VR_S(vt,e+0x8) = *(short *)(RSP.DMEM + addr + HES(0x008));
            VR_S(vt,e+0xA) = *(short *)(RSP.DMEM + addr + HES(0x00A));
            VR_S(vt,e+0xC) = *(short *)(RSP.DMEM + addr + HES(0x00C));
            VR_S(vt,e+0xE) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            break;
        case 0x2/2:
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR_S(vt,e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR_S(vt,e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x006));
            VR_S(vt,e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x008));
            VR_S(vt,e+0x8) = *(short *)(RSP.DMEM + addr + HES(0x00A));
            VR_S(vt,e+0xA) = *(short *)(RSP.DMEM + addr + HES(0x00C));
            VR_S(vt,e+0xC) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            break;
        case 0x4/2:
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR_S(vt,e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x006));
            VR_S(vt,e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x008));
            VR_S(vt,e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x00A));
            VR_S(vt,e+0x8) = *(short *)(RSP.DMEM + addr + HES(0x00C));
            VR_S(vt,e+0xA) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            break;
        case 0x6/2:
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x006));
            VR_S(vt,e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x008));
            VR_S(vt,e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x00A));
            VR_S(vt,e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x00C));
            VR_S(vt,e+0x8) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            break;
        case 0x8/2: /* "Resident Evil 2" cinematics and Boss Game Studios */
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x008));
            VR_S(vt,e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x00A));
            VR_S(vt,e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x00C));
            VR_S(vt,e+0x6) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            break;
        case 0xA/2: /* "Conker's Bad Fur Day" audio microcode by Rareware */
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x00A));
            VR_S(vt,e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x00C));
            VR_S(vt,e+0x4) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            break;
        case 0xC/2: /* "Conker's Bad Fur Day" audio microcode by Rareware */
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x00C));
            VR_S(vt,e+0x2) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            break;
        case 0xE/2: /* "Conker's Bad Fur Day" audio microcode by Rareware */
            VR_S(vt,e+0x0) = *(short *)(RSP.DMEM + addr + HES(0x00E));
            break;
    }
}
static void LRV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    register int b;
    const int e = element;

    if (e != 0x0) /* LVR, illegal element */
        return;

    addr = (SR[base] + 16*offset) & 0x00000FFF;
    if (addr & 0x00000001) /* LRV, odd address */
        return;

    b = addr & 0x0000000F;
    addr &= ~0x0000000F;
    switch (b/2)
    {
        case 0xE/2:
            VR[vt][01] = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR[vt][02] = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR[vt][03] = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR[vt][04] = *(short *)(RSP.DMEM + addr + HES(0x006));
            VR[vt][05] = *(short *)(RSP.DMEM + addr + HES(0x008));
            VR[vt][06] = *(short *)(RSP.DMEM + addr + HES(0x00A));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + HES(0x00C));
            return;
        case 0xC/2:
            VR[vt][02] = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR[vt][03] = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR[vt][04] = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR[vt][05] = *(short *)(RSP.DMEM + addr + HES(0x006));
            VR[vt][06] = *(short *)(RSP.DMEM + addr + HES(0x008));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + HES(0x00A));
            return;
        case 0xA/2:
            VR[vt][03] = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR[vt][04] = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR[vt][05] = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR[vt][06] = *(short *)(RSP.DMEM + addr + HES(0x006));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + HES(0x008));
            return;
        case 0x8/2:
            VR[vt][04] = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR[vt][05] = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR[vt][06] = *(short *)(RSP.DMEM + addr + HES(0x004));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + HES(0x006));
            return;
        case 0x6/2:
            VR[vt][05] = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR[vt][06] = *(short *)(RSP.DMEM + addr + HES(0x002));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + HES(0x004));
            return;
        case 0x4/2:
            VR[vt][06] = *(short *)(RSP.DMEM + addr + HES(0x000));
            VR[vt][07] = *(short *)(RSP.DMEM + addr + HES(0x002));
            return;
        case 0x2/2:
            VR[vt][07] = *(short *)(RSP.DMEM + addr + HES(0x000));
            return;
        case 0x0/2:
            return;
    }
}

static void SQV(int vt, int element, int offset, int base)
{
    register int b;
    const unsigned int e = element;
    register uint32_t addr = (SR[base] + 16*offset) & 0x00000FFF;

    if (e != 0x0)
    { /* happens with "Mia Hamm Soccer 64" */
        register unsigned int i;

        for (i = 0; i < 16 - addr%16; i++)
            RSP.DMEM[BES((addr + i) & 0xFFF)] = VR_B(vt, (e + i) & 0xF);
        return;
    }
    b = addr & 0x0000000F;
    addr &= ~0x0000000F;
    switch (b)
    {
        case 00:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][00];
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][01];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x00C)) = VR[vt][06];
            *(short *)(RSP.DMEM + addr + HES(0x00E)) = VR[vt][07];
            break;
        case 02:
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][00];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][01];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x00C)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x00E)) = VR[vt][06];
            break;
        case 04:
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][00];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][01];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x00C)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x00E)) = VR[vt][05];
            break;
        case 06:
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][00];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][01];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x00C)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x00E)) = VR[vt][04];
            break;
        default:
            /* SQV, weird address */
            break;
    }
}
static void SRV(int vt, int element, int offset, int base)
{
    register uint32_t addr;
    register int b;
    const int e = element;

    if (e != 0x0) /* SRV, illegal element */
        return;

    addr = (SR[base] + 16*offset) & 0x00000FFF;

    if (addr & 0x00000001) /* SRV, odd address */
        return;

    b = addr & 0x0000000F;
    addr &= ~0x0000000F;
    switch (b/2)
    {
        case 0xE/2:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][01];
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][06];
            *(short *)(RSP.DMEM + addr + HES(0x00C)) = VR[vt][07];
            break;
        case 0xC/2:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][02];
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][06];
            *(short *)(RSP.DMEM + addr + HES(0x00A)) = VR[vt][07];
            break;
        case 0xA/2:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][03];
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][06];
            *(short *)(RSP.DMEM + addr + HES(0x008)) = VR[vt][07];
            break;
        case 0x8/2:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][04];
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][06];
            *(short *)(RSP.DMEM + addr + HES(0x006)) = VR[vt][07];
            break;
        case 0x6/2:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][05];
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][06];
            *(short *)(RSP.DMEM + addr + HES(0x004)) = VR[vt][07];
            break;
        case 0x4/2:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][06];
            *(short *)(RSP.DMEM + addr + HES(0x002)) = VR[vt][07];
            break;
        case 0x2/2:
            *(short *)(RSP.DMEM + addr + HES(0x000)) = VR[vt][07];
            break;
        case 0x0/2:
            break;
    }
}

/*
 * Group V vector loads and stores
 * TV and SWV (As of RCP implementation, LTWV opcode was undesired.)
 */
static void LTV(int vt, int element, int offset, int base)
{
   register int i;
   register uint32_t addr;
   const int e = element;

   if (e & 1) /* LTV, illegal element */
      return;

   if (vt & 07) /* LTV, uncertain case */
      return; /* For LTV I am not sure; for STV I have an idea. */

   addr = (SR[base] + 16*offset) & 0x00000FFF;
   if (addr & 0x0000000F) /* LTV, illegal address */
      return;
   for (i = 0; i < 8; i++) /* SGI screwed LTV up on N64.  See STV instead. */
      VR[vt+i][(i - e/2) & 07] = *(int16_t*)(RSP.DMEM + addr + HES(2*i));
}

static void STV(int vt, int element, int offset, int base)
{
   register int i;
   register uint32_t addr;
   const int e = element;

   if (e & 1) /* STV, illegal element */
      return;

   if (vt & 07) /* STV, uncertain case */
      return; /* vt &= 030; */

   addr = (SR[base] + 16*offset) & 0x00000FFF;
   if (addr & 0x0000000F) /* STV, illegal address */
      return;

   for (i = 0; i < 8; i++)
      *(short *)(RSP.DMEM + addr + HES(2*i)) = VR[vt + (e/2 + i)%8][i];
}

/*
 * unused SGI opcodes for LWC2 on the RCP:
 * LAV, LXV, LZV, LTWV
 */
NOINLINE static void lwc_res(int vt, int element, signed offset, int base)
{
#if 0
    static char disasm[32];

    sprintf(
        disasm,
        "%cWC2    $v%d[0x%X], %i($%d)",

        'L',
        vt,
        element &= 0xF,
        offset,
        base
    );
#endif
}

/*
 * unused SGI opcodes for SWC2 on the RCP:
 * SAV, SXV, SZV
 */
NOINLINE static void swc_res(int vt, int element, signed offset, int base)
{
#if 0
    static char disasm[32];

    sprintf(
        disasm,
        "%cWC2    $v%d[0x%X], %i($%d)",

        'S',
        vt,
        element &= 0xF,
        offset,
        base
    );
#endif
}

static void LFV(int vt, int element, int offset, int base)
{ /* Dummy implementation only:  Do any games execute this? */
    lwc_res(vt, element, offset, base);
}

static void SWV(int vt, int element, int offset, int base)
{ /* Dummy implementation only:  Do any games execute this? */
    swc_res(vt, element, offset, base);
}

static void (*LWC2_op[1 << 5])(int, int, signed, int) = {
    LBV    ,LSV    ,LLV    ,LDV    ,LQV    ,LRV    ,LPV    ,LUV    ,
    LHV    ,LFV    ,lwc_res,LTV    ,lwc_res,lwc_res,lwc_res,lwc_res,
    lwc_res,lwc_res,lwc_res,lwc_res,lwc_res,lwc_res,lwc_res,lwc_res,
    lwc_res,lwc_res,lwc_res,lwc_res,lwc_res,lwc_res,lwc_res,lwc_res,
}; /* 000  |  001  |  010  |  011  |  100  |  101  |  110  |  111 */
static void (*SWC2_op[1 << 5])(int, int, signed, int) = {
    SBV    ,SSV    ,SLV    ,SDV    ,SQV    ,SRV    ,SPV    ,SUV    ,
    SHV    ,SFV    ,SWV    ,STV    ,swc_res,swc_res,swc_res,swc_res,
    swc_res,swc_res,swc_res,swc_res,swc_res,swc_res,swc_res,swc_res,
    swc_res,swc_res,swc_res,swc_res,swc_res,swc_res,swc_res,swc_res,
}; /* 000  |  001  |  010  |  011  |  100  |  101  |  110  |  111 */

/*** Modern pseudo-operations (not real instructions, but nice shortcuts) ***/

static void ULW(int rd, uint32_t addr)
{ /* "Unaligned Load Word" */
   if (addr & 0x00000001)
   {
      SR_temp.B[03] = RSP.DMEM[BES(addr)];
      addr = (addr + 0x001) & 0xFFF;
      SR_temp.B[02] = RSP.DMEM[BES(addr)];
      addr = (addr + 0x001) & 0xFFF;
      SR_temp.B[01] = RSP.DMEM[BES(addr)];
      addr = (addr + 0x001) & 0xFFF;
      SR_temp.B[00] = RSP.DMEM[BES(addr)];
   }
   else /* addr & 0x00000002 */
   {
      SR_temp.H[01] = *(short *)(RSP.DMEM + addr - HES(0x000));
      addr = (addr + 0x002) & 0xFFF;
      SR_temp.H[00] = *(short *)(RSP.DMEM + addr + HES(0x000));
   }
   SR[rd] = SR_temp.W;
   /* SR[0] = 0x00000000; */
}

static void USW(int rs, uint32_t addr)
{ /* "Unaligned Store Word" */
   SR_temp.W = SR[rs];
   if (addr & 0x00000001)
   {
      RSP.DMEM[BES(addr)] = SR_temp.B[03];
      addr = (addr + 0x001) & 0xFFF;
      RSP.DMEM[BES(addr)] = SR_temp.B[02];
      addr = (addr + 0x001) & 0xFFF;
      RSP.DMEM[BES(addr)] = SR_temp.B[01];
      addr = (addr + 0x001) & 0xFFF;
      RSP.DMEM[BES(addr)] = SR_temp.B[00];
   }
   else /* addr & 0x00000002 */
   {
      *(short *)(RSP.DMEM + addr - HES(0x000)) = SR_temp.H[01];
      addr = (addr + 0x002) & 0xFFF;
      *(short *)(RSP.DMEM + addr + HES(0x000)) = SR_temp.H[00];
   }
}

/* Allocate the RSP CPU loop to its own functional space. */
#define FIT_IMEM(PC)    (PC & 0xFFF & 0xFFC)

NOINLINE void run_task(void)
{
   register uint32_t PC;
   register unsigned int i;

#ifdef WAIT_FOR_CPU_HOST
   for (i = 0; i < 32; i++)
      MFC0_count[i] = 0;
#endif

   PC = FIT_IMEM(*RSP.SP_PC_REG);
   while ((*RSP.SP_STATUS_REG & 0x00000001) == 0x00000000)
   {
      register uint32_t inst;
      register int rd, rs, rt;
      register int base;

      inst = *(uint32_t *)(RSP.IMEM + FIT_IMEM(PC));
#ifdef EMULATE_STATIC_PC
      PC = (PC + 0x004);
EX:
#endif
#ifdef SP_EXECUTE_LOG
      step_SP_commands(inst);
#endif
      if (inst >> 25 == 0x25) /* is a VU instruction */
      {
         const int opcode = inst % 64; /* inst.R.func */
         const int vd = (inst & 0x000007FF) >> 6; /* inst.R.sa */
         const int vs = (unsigned short)(inst) >> 11; /* inst.R.rd */
         const int vt = (inst >> 16) & 31; /* inst.R.rt */
         const int e  = (inst >> 21) & 0xF; /* rs & 0xF */

         COP2_C2[opcode](vd, vs, vt, e);
      }
      else
      {
         const int op = inst >> 26;
         const int element = (inst & 0x000007FF) >> 7;

         switch (op)
         {
            int16_t offset;
            register uint32_t addr;

            case 000: /* SPECIAL */
#if (1u >> 1 == 0)
               rd = (inst & 0x0000FFFFu) >> 11;
               /* rs = inst >> 21; // Primary op is 0, so we don't need &= 31. */
#else
               rd = (inst >> 11) % 32;
               /* rs = (inst >> 21) % 32; */
#endif
               rt = (inst >> 16) % (1 << 5);

               switch (inst % 64)
               {
                  case 000: /* SLL */
                     SR[rd] = SR[rt] << MASK_SA(inst >> 6);
                     SR[0] = 0x00000000;
                     continue;
                  case 002: /* SRL */
                     SR[rd] = (unsigned)(SR[rt]) >> MASK_SA(inst >> 6);
                     SR[0] = 0x00000000;
                     continue;
                  case 003: /* SRA */
                     SR[rd] = (signed)(SR[rt]) >> MASK_SA(inst >> 6);
                     SR[0] = 0x00000000;
                     continue;
                  case 004: /* SLLV */
                     SR[rd] = SR[rt] << MASK_SA(SR[rs = inst >> 21]);
                     SR[0] = 0x00000000;
                     continue;
                  case 006: /* SRLV */
                     SR[rd] = (unsigned)(SR[rt]) >> MASK_SA(SR[rs = inst >> 21]);
                     SR[0] = 0x00000000;
                     continue;
                  case 007: /* SRAV */
                     SR[rd] = (signed)(SR[rt]) >> MASK_SA(SR[rs = inst >> 21]);
                     SR[0] = 0x00000000;
                     continue;
                  case 011: /* JALR */
                     SR[rd] = (PC + LINK_OFF) & 0x00000FFC;
                     SR[0] = 0x00000000;
                  case 010: /* JR */
                     set_PC(SR[rs = inst >> 21]);
                     goto BRANCH;
                  case 015: /* BREAK */
                     *RSP.SP_STATUS_REG |= 0x00000003; /* BROKE | HALT */
                     if (*RSP.SP_STATUS_REG & 0x00000040)
                     { /* SP_STATUS_INTR_BREAK */
                        *RSP.MI_INTR_REG |= 0x00000001;
                        RSP.CheckInterrupts();
                     }
                     continue;
                  case 040: /* ADD */
                  case 041: /* ADDU */
                     rs = inst >> 21;
                     SR[rd] = SR[rs] + SR[rt];
                     SR[0] = 0x00000000; /* needed for Rareware ucodes */
                     continue;
                  case 042: /* SUB */
                  case 043: /* SUBU */
                     rs = inst >> 21;
                     SR[rd] = SR[rs] - SR[rt];
                     SR[0] = 0x00000000;
                     continue;
                  case 044: /* AND */
                     rs = inst >> 21;
                     SR[rd] = SR[rs] & SR[rt];
                     SR[0] = 0x00000000; /* needed for Rareware ucodes */
                     continue;
                  case 045: /* OR */
                     rs = inst >> 21;
                     SR[rd] = SR[rs] | SR[rt];
                     SR[0] = 0x00000000;
                     continue;
                  case 046: /* XOR */
                     rs = inst >> 21;
                     SR[rd] = SR[rs] ^ SR[rt];
                     SR[0] = 0x00000000;
                     continue;
                  case 047: /* NOR */
                     rs = inst >> 21;
                     SR[rd] = ~(SR[rs] | SR[rt]);
                     SR[0] = 0x00000000;
                     continue;
                  case 052: /* SLT */
                     rs = inst >> 21;
                     SR[rd] = ((signed)(SR[rs]) < (signed)(SR[rt]));
                     SR[0] = 0x00000000;
                     continue;
                  case 053: /* SLTU */
                     rs = inst >> 21;
                     SR[rd] = ((unsigned)(SR[rs]) < (unsigned)(SR[rt]));
                     SR[0] = 0x00000000;
                     continue;
                  default:
                     res_S();
                     continue;
               }
               continue;
            case 001: /* REGIMM */
               rs = (inst >> 21) & 31;
               rt = (inst >> 16) & 31;
               switch (rt)
               {
                  case 020: /* BLTZAL */
                     SR[31] = (PC + LINK_OFF) & 0x00000FFC;
                  case 000: /* BLTZ */
                     if (!(SR[rs] < 0))
                        continue;
                     set_PC(PC + 4*inst + SLOT_OFF);
                     goto BRANCH;
                  case 021: /* BGEZAL */
                     SR[31] = (PC + LINK_OFF) & 0x00000FFC;
                  case 001: /* BGEZ */
                     if (!(SR[rs] >= 0))
                        continue;
                     set_PC(PC + 4*inst + SLOT_OFF);
                     goto BRANCH;
                  default:
                     res_S();
                     continue;
               }
               continue;
            case 003: /* JAL */
               SR[31] = (PC + LINK_OFF) & 0x00000FFC;
            case 002: /* J */
               set_PC(4*inst);
               goto BRANCH;
            case 004: /* BEQ */
               rs = (inst >> 21) & 31;
               rt = (inst >> 16) & 31;
               if (!(SR[rs] == SR[rt]))
                  continue;
               set_PC(PC + 4*inst + SLOT_OFF);
               goto BRANCH;
            case 005: /* BNE */
               rs = (inst >> 21) & 31;
               rt = (inst >> 16) & 31;
               if (!(SR[rs] != SR[rt]))
                  continue;
               set_PC(PC + 4*inst + SLOT_OFF);
               goto BRANCH;
            case 006: /* BLEZ */
               if (!((signed)SR[rs = (inst >> 21) & 31] <= 0x00000000))
                  continue;
               set_PC(PC + 4*inst + SLOT_OFF);
               goto BRANCH;
            case 007: /* BGTZ */
               if (!((signed)SR[rs = (inst >> 21) & 31] >  0x00000000))
                  continue;
               set_PC(PC + 4*inst + SLOT_OFF);
               goto BRANCH;
            case 010: /* ADDI */
            case 011: /* ADDIU */
               rs = (inst >> 21) & 31;
               rt = (inst >> 16) & 31;
               SR[rt] = SR[rs] + (signed short)(inst);
               SR[0] = 0x00000000;
               continue;
            case 012: /* SLTI */
               rs = (inst >> 21) & 31;
               rt = (inst >> 16) & 31;
               SR[rt] = ((signed)(SR[rs]) < (signed short)(inst));
               SR[0] = 0x00000000;
               continue;
            case 013: /* SLTIU */
               rs = (inst >> 21) & 31;
               rt = (inst >> 16) & 31;
               SR[rt] = ((unsigned)(SR[rs]) < (unsigned short)(inst));
               SR[0] = 0x00000000;
               continue;
            case 014: /* ANDI */
               rs = (inst >> 21) & 31;
               rt = (inst >> 16) & 31;
               SR[rt] = SR[rs] & (unsigned short)(inst);
               SR[0] = 0x00000000;
               continue;
            case 015: /* ORI */
               rs = (inst >> 21) & 31;
               rt = (inst >> 16) & 31;
               SR[rt] = SR[rs] | (unsigned short)(inst);
               SR[0] = 0x00000000;
               continue;
            case 016: /* XORI */
               rs = (inst >> 21) & 31;
               rt = (inst >> 16) & 31;
               SR[rt] = SR[rs] ^ (unsigned short)(inst);
               SR[0] = 0x00000000;
               continue;
            case 017: /* LUI */
               SR[rt = (inst >> 16) & 31] = inst << 16;
               SR[0] = 0x00000000;
               continue;
            case 020: /* COP0 */
               rd = (inst & 0x0000F800u) >> 11;
               rs = (inst >> 21) & 31;
               rt = (inst >> 16) & 31;
               switch (rs)
               {
                  case 000: /* MFC0 */
                     MFC0(rt, rd & 0xF);
                     continue;
                  case 004: /* MTC0 */
                     MTC0[rd & 0xF](rt);
                     continue;
                  default:
                     res_S();
                     continue;
               }
               continue;
            case 022: /* COP2 */
               rd = (inst & 0x0000F800u) >> 11;
               rs = (inst >> 21) & 31;
               rt = (inst >> 16) & 31;
               switch (rs)
               {
                  case 000: /* MFC2 */
                     MFC2(rt, rd, element);
                     continue;
                  case 002: /* CFC2 */
                     CFC2(rt, rd);
                     continue;
                  case 004: /* MTC2 */
                     MTC2(rt, rd, element);
                     continue;
                  case 006: /* CTC2 */
                     CTC2(rt, rd);
                     continue;
                  default:
                     res_S();
                     continue;
               }
               continue;
            case 040: /* LB */
               rt = (inst >> 16) % (1 << 5);
               offset = (signed short)(inst);
               addr = (SR[base = (inst >> 21) & 31] + offset) & 0x00000FFF;
               SR[rt] = RSP.DMEM[BES(addr)];
               SR[rt] = (signed char)(SR[rt]);
               SR[0] = 0x00000000;
               continue;
            case 041: /* LH */
               rt = (inst >> 16) % (1 << 5);
               offset = (signed short)(inst);
               addr = (SR[base = (inst >> 21) & 31] + offset) & 0x00000FFF;
               if (addr%0x004 == 0x003)
               {
                  SR_B(rt, 2) = RSP.DMEM[addr - BES(0x000)];
                  addr = (addr + 0x00000001) & 0x00000FFF;
                  SR_B(rt, 3) = RSP.DMEM[addr + BES(0x000)];
                  SR[rt] = (signed short)(SR[rt]);
               }
               else
               {
                  addr -= HES(0x000)*(addr%0x004 - 1);
                  SR[rt] = *(signed short *)(RSP.DMEM + addr);
               }
               SR[0] = 0x00000000;
               continue;
            case 043: /* LW */
               rt = (inst >> 16) % (1 << 5);
               offset = (signed short)(inst);
               addr = (SR[base = (inst >> 21) & 31] + offset) & 0x00000FFF;
               if (addr%0x004 != 0x000)
                  ULW(rt, addr);
               else
                  SR[rt] = *(int32_t *)(RSP.DMEM + addr);
               SR[0] = 0x00000000;
               continue;
            case 044: /* LBU */
               rt = (inst >> 16) % (1 << 5);
               offset = (signed short)(inst);
               addr = (SR[base = (inst >> 21) & 31] + offset) & 0x00000FFF;
               SR[rt] = RSP.DMEM[BES(addr)];
               SR[rt] = (unsigned char)(SR[rt]);
               SR[0] = 0x00000000;
               continue;
            case 045: /* LHU */
               rt = (inst >> 16) % (1 << 5);
               offset = (signed short)(inst);
               addr = (SR[base = (inst >> 21) & 31] + offset) & 0x00000FFF;
               if (addr%0x004 == 0x003)
               {
                  SR_B(rt, 2) = RSP.DMEM[addr - BES(0x000)];
                  addr = (addr + 0x00000001) & 0x00000FFF;
                  SR_B(rt, 3) = RSP.DMEM[addr + BES(0x000)];
                  SR[rt] = (unsigned short)(SR[rt]);
               }
               else
               {
                  addr -= HES(0x000)*(addr%0x004 - 1);
                  SR[rt] = *(unsigned short *)(RSP.DMEM + addr);
               }
               SR[0] = 0x00000000;
               continue;
            case 050: /* SB */
               rt = (inst >> 16) % (1 << 5);
               offset = (signed short)(inst);
               addr = (SR[base = (inst >> 21) & 31] + offset) & 0x00000FFF;
               RSP.DMEM[BES(addr)] = (unsigned char)(SR[rt]);
               continue;
            case 051: /* SH */
               rt = (inst >> 16) % (1 << 5);
               offset = (signed short)(inst);
               addr = (SR[base = (inst >> 21) & 31] + offset) & 0x00000FFF;
               if (addr%0x004 == 0x003)
               {
                  RSP.DMEM[addr - BES(0x000)] = SR_B(rt, 2);
                  addr = (addr + 0x00000001) & 0x00000FFF;
                  RSP.DMEM[addr + BES(0x000)] = SR_B(rt, 3);
                  continue;
               }
               addr -= HES(0x000)*(addr%0x004 - 1);
               *(short *)(RSP.DMEM + addr) = (short)(SR[rt]);
               continue;
            case 053: /* SW */
               rt = (inst >> 16) % (1 << 5);
               offset = (signed short)(inst);
               addr = (SR[base = (inst >> 21) & 31] + offset) & 0x00000FFF;
               if (addr%0x004 != 0x000)
                  USW(rt, addr);
               else
                  *(int32_t *)(RSP.DMEM + addr) = SR[rt];
               continue;
            case 062: /* LWC2 */
               rt = (inst >> 16) % (1 << 5);
               offset = (signed short)(inst & 0x0000FFFFu);
#if defined(ARCH_MIN_SSE2)
               offset <<= 5 + 4; /* safe on x86, skips 5-bit rd, 4-bit element */
               offset >>= 5 + 4;
#else
               offset = SE(offset, 6);
#endif
               base = (inst >> 21) & 31;
               LWC2_op[rd = (inst & 0xF800u) >> 11](rt, element, offset, base);
               continue;
            case 072: /* SWC2 */
               rt = (inst >> 16) % (1 << 5);
               offset = (signed short)(inst & 0x0000FFFFu);
#if defined(ARCH_MIN_SSE2)
               offset <<= 5 + 4; /* safe on x86, skips 5-bit rd, 4-bit element */
               offset >>= 5 + 4;
#else
               offset = SE(offset, 6);
#endif
               base = (inst >> 21) & 31;
               SWC2_op[rd = (inst & 0xF800u) >> 11](rt, element, offset, base);
               continue;
            default:
               res_S();
               continue;
         }
      }
#ifndef EMULATE_STATIC_PC
      if (stage == 2) /* branch phase of scheduler */
      {
         stage = 0*stage;
         PC = temp_PC & 0x00000FFC;
         *RSP.SP_PC_REG = temp_PC;
      }
      else
      {
         stage = 2*stage; /* next IW in branch delay slot? */
         PC = (PC + 0x004) & 0xFFC;
         *RSP.SP_PC_REG = 0x04001000 + PC;
      }
      continue;
#else
      continue;
BRANCH:
      inst = *(uint32_t *)(RSP.IMEM + FIT_IMEM(PC));
      PC = temp_PC & 0x00000FFC;
      goto EX;
#endif
   }
   *RSP.SP_PC_REG = 0x04001000 | FIT_IMEM(PC);
   if (*RSP.SP_STATUS_REG & 0x00000002) /* normal exit, from executing BREAK */
      return;
   else if (*RSP.MI_INTR_REG & 0x00000001) /* interrupt set by MTC0 to break */
      RSP.CheckInterrupts();
   else if (*RSP.SP_SEMAPHORE_REG != 0x00000000) /* semaphore lock fixes */
   {}
#ifndef WAIT_FOR_CPU_HOST
   else /* ??? unknown, possibly external intervention from CPU memory map */
      return; /* SP_SET_HALT */
#endif
   *RSP.SP_STATUS_REG &= ~0x00000001; /* CPU restarts with the correct SIGs. */
}

EXPORT unsigned int CALL cxd4DoRspCycles(unsigned int cycles)
{
   OSTask_type task_type;

   if (*RSP.SP_STATUS_REG & 0x00000003) /* SP_STATUS_HALT */
      return 0x00000000;

   task_type = 0x00000000
#ifdef MSB_FIRST
      | (uint32_t)RSP.DMEM[0xFC0] << 24
      | (uint32_t)RSP.DMEM[0xFC1] << 16
      | (uint32_t)RSP.DMEM[0xFC2] <<  8
      | (uint32_t)RSP.DMEM[0xFC3] <<  0;
#else
      | *((int32_t*)(RSP.DMEM + 0x000FC0U));
#endif

   switch (task_type)
   {
      /* Simulation barrier to redirect processing externally. */
#ifdef EXTERN_COMMAND_LIST_GBI
      case M_GFXTASK:
         if (CFG_HLE_GFX == 0)
            break;

         if (*(int32_t*)(RSP.DMEM + 0xFF0) == 0x00000000)
            break; /* Resident Evil 2, null task pointers */

         if (rsp_info.ProcessDlistList != NULL)
            rsp_info.ProcessDlistList();

         *RSP.SP_STATUS_REG |= 0x00000203;

         if (*RSP.SP_STATUS_REG & 0x00000040) /* SP_STATUS_INTR_BREAK */
         {
            *RSP.MI_INTR_REG |= 0x00000001; /* VR4300 SP interrupt */
            rsp_info.CheckInterrupts();
         }
         if (*RSP.DPC_STATUS_REG & 0x00000002) /* DPC_STATUS_FREEZE */
         {
            /* DPC_CLR_FREEZE */
            *RSP.DPC_STATUS_REG &= ~0x00000002;
         }
         return 0;
#endif
#ifdef EXTERN_COMMAND_LIST_ABI
      case M_AUDTASK:
         if (CFG_HLE_AUD == 0)
            break;

         if (rsp_info.ProcessAlistList != 0)
            rsp_info.ProcessAlistList();

         *RSP.SP_STATUS_REG |= 0x00000203;

         if (*RSP.SP_STATUS_REG & 0x00000040) /* SP_STATUS_INTR_BREAK */
         {
            *RSP.MI_INTR_REG |= 0x00000001; /* VR4300 SP interrupt */
            rsp_info.CheckInterrupts();
         }
         return 0;
#endif
      case M_VIDTASK:
         /* stub */
         break;
      case M_NJPEGTASK:
         /* Zelda, Pokemon, others */
         break;
      case M_NULTASK:
         break;
      case M_HVQMTASK:
         if (rsp_info.ShowCFB != 0)
            rsp_info.ShowCFB(); /* forced FB refresh in case gfx plugin skip */
         break;
   }

   run_task();

   /*
    * An optional EMMS when compiling with Intel SIMD or MMX support.
    *
    * Whether or not MMX has been executed in this emulator, here is a good time
    * to finally empty the MM state, at the end of a long interpreter loop.
    */
#ifdef ARCH_MIN_SSE2
      _mm_empty();
#endif
   return (cycles);
}
