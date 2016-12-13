#include "../../../../main/main.h"
#include "../../../../main/rom.h"
#include "../../../../memory/memory.h"
#include "../../../../rsp/rsp_core.h"
#include "../../../cached_interp.h"
#include "../../../cp0_private.h"
#include "../../../cp1_private.h"
#include "../../../interupt.h"
#include "../../../ops.h"
#include "../../../r4300.h"
#include "../../../recomp.h"
#include "../../../recomph.h" //include for function prototypes
#include "../../../tlb.h"
#include "../../new_dynarec.h"
#include "n64.h"

#ifdef __cplusplus
extern "C" {
#endif

extern u_int memory_map[1048576];

#ifdef __cplusplus
}
#endif

void tlb_hacks(void)
{
   // Goldeneye hack
   if (strncmp((char *) ROM_HEADER.Name, "GOLDENEYE",9) == 0)
   {
      u_int addr;
      int n;
      switch (ROM_HEADER.destination_code&0xFF) 
      {
         case 0x45: // U
            addr=0x34b30;
            break;                   
         case 0x4A: // J 
            addr=0x34b70;    
            break;    
         case 0x50: // E 
            addr=0x329f0;
            break;                        
         default: 
            // Unknown country code
            addr=0;
            break;
      }
      u_int rom_addr=(u_int)g_rom;
#ifdef ROM_COPY
      // Since memory_map is 32-bit, on 64-bit systems the rom needs to be
      // in the lower 4G of memory to use this hack.  Copy it if necessary.
      if((void *)g_rom>(void *)0xffffffff) {
         munmap(ROM_COPY, 67108864);
         if(mmap(ROM_COPY, 12582912,
                  PROT_READ | PROT_WRITE,
                  MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS,
                  -1, 0) <= 0) {DebugMessage(M64MSG_ERROR, "mmap() failed");}
         memcpy(ROM_COPY,g_rom,12582912);
         rom_addr=(u_int)ROM_COPY;
      }
#endif
      if(addr) {
         for(n=0x7F000;n<0x80000;n++) {
            memory_map[n]=(((u_int)(rom_addr+addr-0x7F000000))>>2)|0x40000000;
         }
      }
   }
}
