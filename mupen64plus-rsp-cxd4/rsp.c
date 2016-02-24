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

unsigned char conf[32];

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "Rsp_#1.1.h"
#include "rsp.h"
#include "m64p_plugin.h"
#include "su.h"
#include "vu/vu.h"

#define RSP_CXD4_VERSION 0x0101

#include <stdarg.h>

#define RSP_PLUGIN_API_VERSION 0x020000
#define CONFIG_API_VERSION       0x020100
#define CONFIG_PARAM_VERSION     1.00

static void (*l_DebugCallback)(void *, int, const char *) = NULL;
static void *l_DebugCallContext = NULL;
static int l_PluginInit = 0;
static m64p_handle l_ConfigRsp;
extern RSP_INFO rsp_info;

#define VERSION_PRINTF_SPLIT(x) (((x) >> 16) & 0xffff), (((x) >> 8) & 0xff), ((x) & 0xff)

#define FIT_IMEM(PC)    (PC & 0xFFF & 0xFFC)

static INLINE unsigned SPECIAL(uint32_t inst, uint32_t PC)
{
   unsigned int rs;
#if (1u >> 1 == 0)
   unsigned int rd = (inst & 0x0000FFFFu) >> 11;
   /* rs = inst >> 21; // Primary op is 0, so we don't need &= 31. */
#else
   unsigned int rd = (inst >> 11) % 32;
   /* rs = (inst >> 21) % 32; */
#endif
   unsigned int rt = (inst >> 16) % (1 << 5);

   switch (inst % 64)
   {
      case 000: /* SLL */
         SR[rd] = SR[rt] << MASK_SA(inst >> 6);
         SR[0] = 0x00000000;
         break;
      case 002: /* SRL */
         SR[rd] = (unsigned)(SR[rt]) >> MASK_SA(inst >> 6);
         SR[0] = 0x00000000;
         break;
      case 003: /* SRA */
         SR[rd] = (signed)(SR[rt]) >> MASK_SA(inst >> 6);
         SR[0] = 0x00000000;
         break;
      case 004: /* SLLV */
         SR[rd] = SR[rt] << MASK_SA(SR[rs = inst >> 21]);
         SR[0] = 0x00000000;
         break;
      case 006: /* SRLV */
         SR[rd] = (unsigned)(SR[rt]) >> MASK_SA(SR[rs = inst >> 21]);
         SR[0] = 0x00000000;
         break;
      case 007: /* SRAV */
         SR[rd] = (signed)(SR[rt]) >> MASK_SA(SR[rs = inst >> 21]);
         SR[0] = 0x00000000;
         break;
      case 011: /* JALR */
         SR[rd] = (PC + LINK_OFF) & 0x00000FFC;
         SR[0] = 0x00000000;
      case 010: /* JR */
         SET_PC(SR[rs = inst >> 21]);
         return 1;
      case 015: /* BREAK */
         *RSP.SP_STATUS_REG |= SP_STATUS_BROKE | SP_STATUS_HALT;
         if (*RSP.SP_STATUS_REG & SP_STATUS_INTR_BREAK)
         { /* SP_STATUS_INTR_BREAK */
            *RSP.MI_INTR_REG |= 0x00000001;
            RSP.CheckInterrupts();
         }
         break;
      case 040: /* ADD */
      case 041: /* ADDU */
         rs = inst >> 21;
         SR[rd] = SR[rs] + SR[rt];
         SR[0] = 0x00000000; /* needed for Rareware ucodes */
         break;
      case 042: /* SUB */
      case 043: /* SUBU */
         rs = inst >> 21;
         SR[rd] = SR[rs] - SR[rt];
         SR[0] = 0x00000000;
         break;
      case 044: /* AND */
         rs = inst >> 21;
         SR[rd] = SR[rs] & SR[rt];
         SR[0] = 0x00000000; /* needed for Rareware ucodes */
         break;
      case 045: /* OR */
         rs = inst >> 21;
         SR[rd] = SR[rs] | SR[rt];
         SR[0] = 0x00000000;
         break;
      case 046: /* XOR */
         rs = inst >> 21;
         SR[rd] = SR[rs] ^ SR[rt];
         SR[0] = 0x00000000;
         break;
      case 047: /* NOR */
         rs = inst >> 21;
         SR[rd] = ~(SR[rs] | SR[rt]);
         SR[0] = 0x00000000;
         break;
      case 052: /* SLT */
         rs = inst >> 21;
         SR[rd] = ((signed)(SR[rs]) < (signed)(SR[rt]));
         SR[0] = 0x00000000;
         break;
      case 053: /* SLTU */
         rs = inst >> 21;
         SR[rd] = ((unsigned)(SR[rs]) < (unsigned)(SR[rt]));
         SR[0] = 0x00000000;
         break;
      default:
         res_S();
   }
   return 0;
}

static int PC;

/* Allocate the RSP CPU loop to its own functional space. */
static unsigned int run_task_opcode(uint32_t inst, const int opcode)
{
   register int base;
   register int rd, rs, rt;
   const unsigned int element = (inst & 0x000007FF) >> 7;

   switch (opcode)
   {
      int16_t offset;
      register uint32_t addr;

      case 000: /* SPECIAL */
         if (SPECIAL(inst, PC) != 0)
            return 1; /* JR and JALR should return a non-zero value. */
         break;
      case 001: /* REGIMM */
         rs = (inst >> 21) & 31;
         rt = (inst >> 16) & 31;
         switch (rt)
         {
            case 020: /* BLTZAL */
               SR[31] = (PC + LINK_OFF) & 0x00000FFC;
            case 000: /* BLTZ */
               if (!(SR[rs] < 0))
                  break;
               SET_PC(PC + 4*inst + SLOT_OFF);
               return 1;
            case 021: /* BGEZAL */
               SR[31] = (PC + LINK_OFF) & 0x00000FFC;
            case 001: /* BGEZ */
               if (!(SR[rs] >= 0))
                  break;
               SET_PC(PC + 4*inst + SLOT_OFF);
               return 1;
            default:
               res_S();
               break;
         }
         break;
      case 003: /* JAL */
         SR[31] = (PC + LINK_OFF) & 0x00000FFC;
      case 002: /* J */
         SET_PC(4*inst);
         return 1;
      case 004: /* BEQ */
         rs = (inst >> 21) & 31;
         rt = (inst >> 16) & 31;
         if (!(SR[rs] == SR[rt]))
            break;
         SET_PC(PC + 4*inst + SLOT_OFF);
         return 1;
      case 005: /* BNE */
         rs = (inst >> 21) & 31;
         rt = (inst >> 16) & 31;
         if (!(SR[rs] != SR[rt]))
            break;
         SET_PC(PC + 4*inst + SLOT_OFF);
         return 1;
      case 006: /* BLEZ */
         if (!((signed)SR[rs = (inst >> 21) & 31] <= 0x00000000))
            break;
         SET_PC(PC + 4*inst + SLOT_OFF);
         return 1;
      case 007: /* BGTZ */
         if (!((signed)SR[rs = (inst >> 21) & 31] >  0x00000000))
            break;
         SET_PC(PC + 4*inst + SLOT_OFF);
         return 1;
      case 010: /* ADDI */
      case 011: /* ADDIU */
         rs = (inst >> 21) & 31;
         rt = (inst >> 16) & 31;
         SR[rt] = SR[rs] + (signed short)(inst);
         SR[0] = 0x00000000;
         break;
      case 012: /* SLTI */
         rs = (inst >> 21) & 31;
         rt = (inst >> 16) & 31;
         SR[rt] = ((signed)(SR[rs]) < (signed short)(inst));
         SR[0] = 0x00000000;
         break;
      case 013: /* SLTIU */
         rs = (inst >> 21) & 31;
         rt = (inst >> 16) & 31;
         SR[rt] = ((unsigned)(SR[rs]) < (unsigned short)(inst));
         SR[0] = 0x00000000;
         break;
      case 014: /* ANDI */
         rs = (inst >> 21) & 31;
         rt = (inst >> 16) & 31;
         SR[rt] = SR[rs] & (unsigned short)(inst);
         SR[0] = 0x00000000;
         break;
      case 015: /* ORI */
         rs = (inst >> 21) & 31;
         rt = (inst >> 16) & 31;
         SR[rt] = SR[rs] | (unsigned short)(inst);
         SR[0] = 0x00000000;
         break;
      case 016: /* XORI */
         rs = (inst >> 21) & 31;
         rt = (inst >> 16) & 31;
         SR[rt] = SR[rs] ^ (unsigned short)(inst);
         SR[0] = 0x00000000;
         break;
      case 017: /* LUI */
         SR[rt = (inst >> 16) & 31] = inst << 16;
         SR[0] = 0x00000000;
         break;
      case 020: /* COP0 */
         rd = (inst & 0x0000F800u) >> 11;
         rs = (inst >> 21) & 31;
         rt = (inst >> 16) & 31;
         switch (rs)
         {
            case 000: /* MFC0 */
               SP_CP0_MF(rt, rd);
               break;
            case 004: /* MTC0 */
               MTC0[rd & 0xF](rt);
               break;
            default:
               res_S();
         }
         break;
      case 022: /* COP2 */
         rd = (inst & 0x0000F800u) >> 11;
         rs = (inst >> 21) & 31;
         rt = (inst >> 16) & 31;
         switch (rs)
         {
            case 000: /* MFC2 */
               MFC2(rt, rd, element);
               break;
            case 002: /* CFC2 */
               CFC2(rt, rd);
               break;
            case 004: /* MTC2 */
               MTC2(rt, rd, element);
               break;
            case 006: /* CTC2 */
               CTC2(rt, rd);
               break;
            default:
               res_S();
         }
         break;
      case 040: /* LB */
         rt = (inst >> 16) % (1 << 5);
         offset = (signed short)(inst);
         addr = (SR[base = (inst >> 21) & 31] + offset) & 0x00000FFF;
         SR[rt] = RSP.DMEM[BES(addr)];
         SR[rt] = (signed char)(SR[rt]);
         SR[0] = 0x00000000;
         break;
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
         break;
      case 043: /* LW */
         rt = (inst >> 16) % (1 << 5);
         offset = (signed short)(inst);
         addr = (SR[base = (inst >> 21) & 31] + offset) & 0x00000FFF;
         if (addr%0x004 != 0x000)
            ULW(rt, addr);
         else
            SR[rt] = *(int32_t *)(RSP.DMEM + addr);
         SR[0] = 0x00000000;
         break;
      case 044: /* LBU */
         rt = (inst >> 16) % (1 << 5);
         offset = (signed short)(inst);
         addr = (SR[base = (inst >> 21) & 31] + offset) & 0x00000FFF;
         SR[rt] = RSP.DMEM[BES(addr)];
         SR[rt] = (unsigned char)(SR[rt]);
         SR[0] = 0x00000000;
         break;
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
         break;
      case 050: /* SB */
         rt = (inst >> 16) % (1 << 5);
         offset = (signed short)(inst);
         addr = (SR[base = (inst >> 21) & 31] + offset) & 0x00000FFF;
         RSP.DMEM[BES(addr)] = (unsigned char)(SR[rt]);
         break;
      case 051: /* SH */
         rt = (inst >> 16) % (1 << 5);
         offset = (signed short)(inst);
         addr = (SR[base = (inst >> 21) & 31] + offset) & 0x00000FFF;
         if (addr%0x004 == 0x003)
         {
            RSP.DMEM[addr - BES(0x000)] = SR_B(rt, 2);
            addr = (addr + 0x00000001) & 0x00000FFF;
            RSP.DMEM[addr + BES(0x000)] = SR_B(rt, 3);
            break;
         }
         addr -= HES(0x000)*(addr%0x004 - 1);
         *(short *)(RSP.DMEM + addr) = (short)(SR[rt]);
         break;
      case 053: /* SW */
         rt = (inst >> 16) % (1 << 5);
         offset = (signed short)(inst);
         addr = (SR[base = (inst >> 21) & 31] + offset) & 0x00000FFF;
         if (addr%0x004 != 0x000)
            USW(rt, addr);
         else
            *(int32_t *)(RSP.DMEM + addr) = SR[rt];
         break;
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
         break;
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
         break;
      default:
         res_S();
   }

   return 0;
}

NOINLINE void run_task(void)
{
    PC = FIT_IMEM(*RSP.SP_PC_REG);

    stale_signals = 0;

    while ((*RSP.SP_STATUS_REG & 0x00000001) == 0x00000000)
    {
       register uint32_t inst = *(uint32_t *)(RSP.IMEM + FIT_IMEM(PC));
#ifdef EMULATE_STATIC_PC
       PC = (PC + 0x004);
EX:
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
          int task_ran = run_task_opcode(inst, inst >> 26);
          if (task_ran)
          {
#ifdef EMULATE_STATIC_PC
             inst = *(uint32_t *)(RSP.IMEM + FIT_IMEM(PC));
             PC = temp_PC & 0x00000FFC;
             goto EX;
#else
             break;
#endif
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
#endif
       continue;
    }
    *RSP.SP_PC_REG = 0x04001000 | FIT_IMEM(PC);
}

static void DebugMessage(int level, const char *message, ...)
{
  char msgbuf[1024];
  va_list args;

  if (l_DebugCallback == NULL)
      return;

  va_start(args, message);
  vsprintf(msgbuf, message, args);

  (*l_DebugCallback)(l_DebugCallContext, level, msgbuf);

  va_end(args);
}

EXPORT m64p_error CALL PluginStartup(m64p_dynlib_handle CoreLibHandle, void *Context,
                                     void (*DebugCallback)(void *, int, const char *))
{
    float fConfigParamsVersion = 0.0f;

    if (l_PluginInit)
        return M64ERR_ALREADY_INIT;

    /* first thing is to set the callback function for debug info */
    l_DebugCallback = DebugCallback;
    l_DebugCallContext = Context;

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

    memset(conf, 0, sizeof(conf));

    CFG_HLE_GFX = ConfigGetParamBool(l_ConfigRsp, "DisplayListToGraphicsPlugin");
    CFG_HLE_AUD = ConfigGetParamBool(l_ConfigRsp, "AudioListToAudioPlugin");
    CFG_WAIT_FOR_CPU_HOST = ConfigGetParamBool(l_ConfigRsp, "WaitForCPUHost");
    CFG_MEND_SEMAPHORE_LOCK = ConfigGetParamBool(l_ConfigRsp, "SupportCPUSemaphoreLock");
    return 1;
}

EXPORT unsigned int CALL cxd4DoRspCycles(unsigned int cycles)
{
   unsigned int i;
   if (*RSP.SP_STATUS_REG & 0x00000003)
   {
      message("SP_STATUS_HALT", 3);
      return 0x00000000;
   }
   switch (*(unsigned int *)(RSP.DMEM + 0xFC0))
   { /* Simulation barrier to redirect processing externally. */
#ifdef EXTERN_COMMAND_LIST_GBI
      case 0x00000001:
         if (CFG_HLE_GFX == 0)
            break;
         if (*(unsigned int *)(RSP.DMEM + 0xFF0) == 0x00000000)
            break; /* Resident Evil 2 */
         if (rsp_info.ProcessDlistList == NULL) {/*branch next*/} else
            rsp_info.ProcessDlistList();
         *RSP.SP_STATUS_REG |= 0x00000203;
         if (*RSP.SP_STATUS_REG & 0x00000040) /* SP_STATUS_INTR_BREAK */
         {
            *RSP.MI_INTR_REG |= 0x00000001; /* VR4300 SP interrupt */
            rsp_info.CheckInterrupts();
         }
         if (*RSP.DPC_STATUS_REG & 0x00000002) /* DPC_STATUS_FREEZE */
         {
            message("DPC_CLR_FREEZE", 2);
            *RSP.DPC_STATUS_REG &= ~0x00000002;
         }
         return 0;
#endif
#ifdef EXTERN_COMMAND_LIST_ABI
      case 0x00000002: /* OSTask.type == M_AUDTASK */
         if (CFG_HLE_AUD == 0)
            break;
         if (rsp_info.ProcessAlistList == 0) {} else
            rsp_info.ProcessAlistList();
         *RSP.SP_STATUS_REG |= 0x00000203;
         if (*RSP.SP_STATUS_REG & 0x00000040) /* SP_STATUS_INTR_BREAK */
         {
            *RSP.MI_INTR_REG |= 0x00000001; /* VR4300 SP interrupt */
            rsp_info.CheckInterrupts();
         }
         return 0;
#endif
   }

   for (i = 0; i < 32; i++)
      MFC0_count[i] = 0;
   run_task();

   if (*RSP.SP_STATUS_REG & SP_STATUS_BROKE) /* normal exit, from executing BREAK */
      return (cycles);
   else if (*RSP.MI_INTR_REG & 0x00000001) /* interrupt set by MTC0 to break */
      RSP.CheckInterrupts();
   else if (*RSP.SP_SEMAPHORE_REG != 0x00000000) /* semaphore lock fixes */
   {}
#ifdef WAIT_FOR_CPU_HOST
   else if (stale_signals != 0) /* too many iterations of MFC0:  timed out */
      MF_SP_STATUS_TIMEOUT = 16384; /* This is slow:  Make 16 if it works. */
#else
   else /* ??? unknown, possibly external intervention from CPU memory map */
   {
      message("SP_SET_HALT", 3);
      return (cycles);
   }
#endif
   *RSP.SP_STATUS_REG &= ~SP_STATUS_HALT; /* CPU restarts with the correct SIGs. */

   return (cycles);
}
EXPORT void CALL GetDllInfo(PLUGIN_INFO *PluginInfo)
{
    PluginInfo -> Version = 0x0101; /* zilmar #1.1 (only standard RSP spec) */
    PluginInfo -> Type = PLUGIN_TYPE_RSP;
    PluginInfo -> NormalMemory = 0;
    PluginInfo -> MemoryBswaped = 1;
    return;
}

RCPREG* CR[16];
int MF_SP_STATUS_TIMEOUT;
int stale_signals;
EXPORT void CALL cxd4InitiateRSP(RSP_INFO Rsp_Info, unsigned int *CycleCount)
{
    if (CycleCount != NULL) /* cycle-accuracy not doable with today's hosts */
        *CycleCount = 0x00000000;

    if (Rsp_Info.DMEM == Rsp_Info.IMEM) /* usually dummy RSP data for testing */
        return; /* DMA is not executed just because plugin initiates. */
    while (Rsp_Info.IMEM != Rsp_Info.DMEM + 4096)
        message("Virtual host map noncontiguity.", 3);

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
    MF_SP_STATUS_TIMEOUT = 16384;
    stale_signals = 0;
    return;
}
EXPORT void CALL cxd4RomClosed(void)
{
    *RSP.SP_PC_REG = 0x00000000;
    return;
}
