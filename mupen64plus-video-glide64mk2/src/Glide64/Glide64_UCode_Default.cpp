#include <stdint.h>
#include "Gfx_1.3.h"
#include "winlnxdefs.h"
#include "Ini.h"
#include "Config.h"
#include "Glide64_UCode.h"
#include "rdp.h"
#include "../../libretro/libretro.h"

extern uint8_t microcode[4096];
extern uint32_t uc_crc;
extern int old_ucode;
extern SETTINGS settings;

void microcheck(void)
{
  wxUint32 i;
  uc_crc = 0;

  // Check first 3k of ucode, because the last 1k sometimes contains trash
  for (i=0; i<3072>>2; i++)
  {
    uc_crc += ((wxUint32*)microcode)[i];
  }

  FRDP_E ("crc: %08lx\n", uc_crc);

#ifdef LOG_UCODE
  std::ofstream ucf;
  ucf.open ("ucode.txt", std::ios::out | std::ios::binary);
  char d;
  for (i=0; i<0x400000; i++)
  {
    d = ((char*)gfx.RDRAM)[i^3];
    ucf.write (&d, 1);
  }
  ucf.close ();
#endif

  FRDP("ucode = %08lx\n", uc_crc);

  Ini * ini = Ini::OpenIni();
  ini->SetPath("UCODE");
  char str[9];
  sprintf (str, "%08lx", (unsigned long)uc_crc);
  int uc = ini->Read(str, -2);

  if (uc == -2 && ucode_error_report)
  {
    settings.ucode = Config_ReadInt("ucode", "Force microcode", 0, TRUE, FALSE);

    ReleaseGfx ();
    ERRLOG("Error: uCode crc not found in INI, using currently selected uCode\n\n%08lx", (unsigned long)uc_crc);

    ucode_error_report = FALSE; // don't report any more ucode errors from this game
  }
  else if (uc == -1 && ucode_error_report)
  {
    settings.ucode = ini->Read(_T("/SETTINGS/ucode"), 0);

    ReleaseGfx ();
    ERRLOG("Error: Unsupported uCode!\n\ncrc: %08lx", (unsigned long)uc_crc);

    ucode_error_report = FALSE; // don't report any more ucode errors from this game
  }
  else
  {
    old_ucode = settings.ucode;
    settings.ucode = uc;
    FRDP("microcheck: old ucode: %d,  new ucode: %d\n", old_ucode, uc);
    if (uc_crc == 0x8d5735b2 || uc_crc == 0xb1821ed3 || uc_crc == 0x1118b3e0) //F3DLP.Rej ucode. perspective texture correction is not implemented
    {
      rdp.Persp_en = 1;
      rdp.persp_supported = FALSE;
    }
    else if (settings.texture_correction)
      rdp.persp_supported = TRUE;
  }
}
