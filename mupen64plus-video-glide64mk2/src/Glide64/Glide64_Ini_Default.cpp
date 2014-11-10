#include <stdint.h>
#include <string.h>
#include "Gfx_1.3.h"
#include "Ini.h"
#include "Config.h"
#include "winlnxdefs.h"
#include "Glide64_Ini.h"
#include "Glide64_UCode.h"
#include "DepthBufferRender.h"
#include "rdp.h"

extern uint8_t microcode[4096];
extern SETTINGS settings;

void ConfigWrapper()
{
  char strConfigWrapperExt[] = "grConfigWrapperExt";
  GRCONFIGWRAPPEREXT grConfigWrapperExt = (GRCONFIGWRAPPEREXT)grGetProcAddress(strConfigWrapperExt);
  if (grConfigWrapperExt)
    grConfigWrapperExt(settings.wrpResolution, settings.wrpVRAM * 1024 * 1024, settings.wrpFBO, settings.wrpAnisotropic);
}

void ReadSettings(void)
{
  //  LOG("ReadSettings\n");
  if (!Config_Open())
  {
    ERRLOG("Could not open configuration!");
    return;
  }

  settings.card_id = (uint8_t)Config_ReadInt ("card_id", "Card ID", 0, TRUE, FALSE);
  //settings.lang_id not needed
  // depth_bias = -Config_ReadInt ("depth_bias", "Depth bias level", 0, TRUE, FALSE);
  settings.res_data = 0;
  settings.scr_res_x = settings.res_x = Config_ReadScreenInt("ScreenWidth");
  settings.scr_res_y = settings.res_y = Config_ReadScreenInt("ScreenHeight");

  settings.vsync = (BOOL)Config_ReadInt ("vsync", "Vertical sync", 1);
  settings.ssformat = (BOOL)Config_ReadInt("ssformat", "TODO:ssformat", 0);
  //settings.fast_crc = (BOOL)Config_ReadInt ("fast_crc", "Fast CRC", 0);

  settings.show_fps = (uint8_t)Config_ReadInt ("show_fps", "Display performance stats (add together desired flags): 1=FPS counter, 2=VI/s counter, 4=% speed, 8=FPS transparent", 0, TRUE, FALSE);
  settings.clock = (BOOL)Config_ReadInt ("clock", "Clock enabled", 0);
  settings.clock_24_hr = (BOOL)Config_ReadInt ("clock_24_hr", "Clock is 24-hour", 1);
  // settings.advanced_options only good for GUI config
  // settings.texenh_options = only good for GUI config
  //settings.use_hotkeys = ini->Read(_T("hotkeys"), 1l);

  settings.wrpResolution = (uint8_t)Config_ReadInt ("wrpResolution", "Wrapper resolution", 0, TRUE, FALSE);
  settings.wrpVRAM = (uint8_t)Config_ReadInt ("wrpVRAM", "Wrapper VRAM", 0, TRUE, FALSE);
  settings.wrpFBO = (BOOL)Config_ReadInt ("wrpFBO", "Wrapper FBO", 1, TRUE, TRUE);
  settings.wrpAnisotropic = (BOOL)Config_ReadInt ("wrpAnisotropic", "Wrapper Anisotropic Filtering", 1, TRUE, TRUE);

#ifndef _ENDUSER_RELEASE_
  settings.autodetect_ucode = (BOOL)Config_ReadInt ("autodetect_ucode", "Auto-detect microcode", 1);
  settings.ucode = (uint32_t)Config_ReadInt ("ucode", "Force microcode", 2, TRUE, FALSE);
  settings.wireframe = (BOOL)Config_ReadInt ("wireframe", "Wireframe display", 0);
  settings.wfmode = (int)Config_ReadInt ("wfmode", "Wireframe mode: 0=Normal colors, 1=Vertex colors, 2=Red only", 1, TRUE, FALSE);

  settings.logging = (BOOL)Config_ReadInt ("logging", "Logging", 0);
  settings.log_clear = (BOOL)Config_ReadInt ("log_clear", "", 0);

  settings.run_in_window = (BOOL)Config_ReadInt ("run_in_window", "", 0);

  settings.elogging = (BOOL)Config_ReadInt ("elogging", "", 0);
  settings.filter_cache = (BOOL)Config_ReadInt ("filter_cache", "Filter cache", 0);
  settings.unk_as_red = (BOOL)Config_ReadInt ("unk_as_red", "Display unknown combines as red", 0);
  settings.log_unk = (BOOL)Config_ReadInt ("log_unk", "Log unknown combines", 0);
  settings.unk_clear = (BOOL)Config_ReadInt ("unk_clear", "", 0);
#else
  settings.autodetect_ucode = TRUE;
  settings.ucode = 2;
  settings.wireframe = FALSE;
  settings.wfmode = 0;
  settings.logging = FALSE;
  settings.log_clear = FALSE;
  settings.run_in_window = FALSE;
  settings.elogging = FALSE;
  settings.filter_cache = FALSE;
  settings.unk_as_red = FALSE;
  settings.log_unk = FALSE;
  settings.unk_clear = FALSE;
#endif

#ifdef TEXTURE_FILTER
  
  // settings.ghq_fltr range is 0 through 6
  // Filters:\nApply a filter to either smooth or sharpen textures.\nThere are 4 different smoothing filters and 2 different sharpening filters.\nThe higher the number, the stronger the effect,\ni.e. \"Smoothing filter 4\" will have a much more noticeable effect than \"Smoothing filter 1\".\nBe aware that performance may have an impact depending on the game and/or the PC.\n[Recommended: your preference]
  // _("None"),
  // _("Smooth filtering 1"),
  // _("Smooth filtering 2"),
  // _("Smooth filtering 3"),
  // _("Smooth filtering 4"),
  // _("Sharp filtering 1"),
  // _("Sharp filtering 2")

// settings.ghq_cmpr 0=S3TC and 1=FXT1

//settings.ghq_ent is ___
// "Texture enhancement:\n7 different filters are selectable here, each one with a distinctive look.\nBe aware of possible performance impacts.\n\nIMPORTANT: 'Store' mode - saves textures in cache 'as is'. It can improve performance in games, which load many textures.\nDisable 'Ignore backgrounds' option for better result.\n\n[Recommended: your preference]"



  settings.ghq_fltr = Config_ReadInt ("ghq_fltr", "Texture Enhancement: Smooth/Sharpen Filters", 0, TRUE, FALSE);
  settings.ghq_cmpr = Config_ReadInt ("ghq_cmpr", "Texture Compression: 0 for S3TC, 1 for FXT1", 0, TRUE, FALSE);
  settings.ghq_enht = Config_ReadInt ("ghq_enht", "Texture Enhancement: More filters", 0, TRUE, FALSE);
  settings.ghq_hirs = Config_ReadInt ("ghq_hirs", "Hi-res texture pack format (0 for none, 1 for Rice)", 0, TRUE, FALSE);
  settings.ghq_enht_cmpr  = Config_ReadInt ("ghq_enht_cmpr", "Compress texture cache with S3TC or FXT1", 0, TRUE, TRUE);
  settings.ghq_enht_tile = Config_ReadInt ("ghq_enht_tile", "Tile textures (saves memory but could cause issues)", 0, TRUE, FALSE);
  settings.ghq_enht_f16bpp = Config_ReadInt ("ghq_enht_f16bpp", "Force 16bpp textures (saves ram but lower quality)", 0, TRUE, TRUE);
  settings.ghq_enht_gz  = Config_ReadInt ("ghq_enht_gz", "Compress texture cache", 1, TRUE, TRUE);
  settings.ghq_enht_nobg  = Config_ReadInt ("ghq_enht_nobg", "Don't enhance textures for backgrounds", 0, TRUE, TRUE);
  settings.ghq_hirs_cmpr  = Config_ReadInt ("ghq_hirs_cmpr", "Enable S3TC and FXT1 compression", 0, TRUE, TRUE);
  settings.ghq_hirs_tile = Config_ReadInt ("ghq_hirs_tile", "Tile hi-res textures (saves memory but could cause issues)", 0, TRUE, TRUE);
  settings.ghq_hirs_f16bpp = Config_ReadInt ("ghq_hirs_f16bpp", "Force 16bpp hi-res textures (saves ram but lower quality)", 0, TRUE, TRUE);
  settings.ghq_hirs_gz  = Config_ReadInt ("ghq_hirs_gz", "Compress hi-res texture cache", 1, TRUE, TRUE);
  settings.ghq_hirs_altcrc = Config_ReadInt ("ghq_hirs_altcrc", "Alternative CRC calculation -- emulates Rice bug", 1, TRUE, TRUE);
  settings.ghq_cache_save = Config_ReadInt ("ghq_cache_save", "Save tex cache to disk", 1, TRUE, TRUE);
  settings.ghq_cache_size = Config_ReadInt ("ghq_cache_size", "Texture Cache Size (MB)", 128, TRUE, FALSE);
  settings.ghq_hirs_let_texartists_fly = Config_ReadInt ("ghq_hirs_let_texartists_fly", "Use full alpha channel -- could cause issues for some tex packs", 0, TRUE, TRUE);
  settings.ghq_hirs_dump = Config_ReadInt ("ghq_hirs_dump", "Dump textures", 0, FALSE, TRUE);
#endif

  settings.special_alt_tex_size = Config_ReadInt("alt_tex_size", "Alternate texture size method: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_use_sts1_only = Config_ReadInt("use_sts1_only", "Use first SETTILESIZE only: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_force_calc_sphere = Config_ReadInt("force_calc_sphere", "Use spheric mapping only: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_correct_viewport = Config_ReadInt("correct_viewport", "Force positive viewport: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_increase_texrect_edge = Config_ReadInt("increase_texrect_edge", "Force texrect size to integral value: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_decrease_fillrect_edge = Config_ReadInt("decrease_fillrect_edge", "Reduce fillrect size by 1: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_texture_correction = Config_ReadInt("texture_correction", "Enable perspective texture correction emulation: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_pal230 = Config_ReadInt("pal230", "Set special scale for PAL games: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_stipple_mode = Config_ReadInt("stipple_mode", "3DFX Dithered alpha emulation mode: -1=Game default, >=0=dithered alpha emulation mode", -1, TRUE, FALSE);
  settings.special_stipple_pattern = Config_ReadInt("stipple_pattern", "3DFX Dithered alpha pattern: -1=Game default, >=0=pattern used for dithered alpha emulation", -1, TRUE, FALSE);
  settings.special_force_microcheck = Config_ReadInt("force_microcheck", "Check microcode each frame: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_force_quad3d = Config_ReadInt("force_quad3d", "Force 0xb5 command to be quad, not line 3D: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_clip_zmin = Config_ReadInt("clip_zmin", "Enable near z clipping: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_clip_zmax = Config_ReadInt("clip_zmax", "Enable far plane clipping: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_fast_crc = Config_ReadInt("fast_crc", "Use fast CRC algorithm: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_adjust_aspect = Config_ReadInt("adjust_aspect", "Adjust screen aspect for wide screen mode: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_zmode_compare_less = Config_ReadInt("zmode_compare_less", "Force strict check in Depth buffer test: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_old_style_adither = Config_ReadInt("old_style_adither", "Apply alpha dither regardless of alpha_dither_mode: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_n64_z_scale = Config_ReadInt("n64_z_scale", "Scale vertex z value before writing to depth buffer: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_optimize_texrect = Config_ReadInt("optimize_texrect", "Fast texrect rendering with hwfbe: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_ignore_aux_copy = Config_ReadInt("ignore_aux_copy", "Do not copy auxiliary frame buffers: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_hires_buf_clear = Config_ReadInt("hires_buf_clear", "Clear auxiliary texture frame buffers: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_fb_read_alpha = Config_ReadInt("fb_read_alpha", "Read alpha from framebuffer: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_useless_is_useless = Config_ReadInt("useless_is_useless", "Handle unchanged fb: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_fb_crc_mode = Config_ReadInt("fb_crc_mode", "Set frambuffer CRC mode: -1=Game default, 0=disable CRC, 1=fast CRC, 2=safe CRC", -1, TRUE, FALSE);
  settings.special_filtering = Config_ReadInt("filtering", "Filtering mode: -1=Game default, 0=automatic, 1=force bilinear, 2=force point sampled", -1, TRUE, FALSE);
  settings.special_fog = Config_ReadInt("fog", "Fog: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_buff_clear = Config_ReadInt("buff_clear", "Buffer clear on every frame: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_swapmode = Config_ReadInt("swapmode", "Buffer swapping method: -1=Game default, 0=swap buffers when vertical interrupt has occurred, 1=swap buffers when set of conditions is satisfied. Prevents flicker on some games, 2=mix of first two methods", -1, TRUE, FALSE);
  settings.special_aspect = Config_ReadInt("aspect", "Aspect ratio: -1=Game default, 0=Force 4:3, 1=Force 16:9, 2=Stretch, 3=Original", 0, TRUE, FALSE);
  settings.special_lodmode = Config_ReadInt("lodmode", "LOD calculation: -1=Game default, 0=disable. 1=fast, 2=precise", -1, TRUE, FALSE);
  settings.special_fb_smart = Config_ReadInt("fb_smart", "Smart framebuffer: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_fb_hires = Config_ReadInt("fb_hires", "Hardware frame buffer emulation: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_fb_read_always = Config_ReadInt("fb_read_always", "Read framebuffer every frame (may be slow use only for effects that need it e.g. Banjo Kazooie, DK64 transitions): -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_read_back_to_screen = Config_ReadInt("read_back_to_screen", "Render N64 frame buffer as texture: -1=Game default, 0=disable, 1=mode1, 2=mode2", -1, TRUE, FALSE);
  settings.special_detect_cpu_write = Config_ReadInt("detect_cpu_write", "Show images written directly by CPU: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_fb_get_info = Config_ReadInt("fb_get_info", "Get frame buffer info: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);
  settings.special_fb_render = Config_ReadInt("fb_render", "Enable software depth render: -1=Game default, 0=disable. 1=enable", -1, TRUE, FALSE);

  //TODO-PORT: remove?
  ConfigWrapper();
}

void ReadSpecialSettings (const char * name)
{
  //  char buf [256];
  //  sprintf(buf, "ReadSpecialSettings. Name: %s\n", name);
  //  LOG(buf);
  settings.hacks = 0;

  //detect games which require special hacks
  if (strstr(name, (const char *)"ZELDA") || strstr(name, (const char *)"MASK"))
    settings.hacks |= hack_Zelda;
  else if (strstr(name, (const char *)"ROADSTERS TROPHY"))
    settings.hacks |= hack_Zelda;
  else if (strstr(name, (const char *)"Diddy Kong Racing"))
    settings.hacks |= hack_Diddy;
  else if (strstr(name, (const char *)"Tonic Trouble"))
    settings.hacks |= hack_Tonic;
  else if (strstr(name, (const char *)"All") && strstr(name, (const char *)"Star") && strstr(name, (const char *)"Baseball"))
    settings.hacks |= hack_ASB;
  else if (strstr(name, (const char *)"Beetle") || strstr(name, (const char *)"BEETLE") || strstr(name, (const char *)"HSV"))
    settings.hacks |= hack_BAR;
  else if (strstr(name, (const char *)"I S S 64") || strstr(name, (const char *)"J WORLD SOCCER3") || strstr(name, (const char *)"PERFECT STRIKER") || strstr(name, (const char *)"RONALDINHO SOCCER"))
    settings.hacks |= hack_ISS64;
  else if (strstr(name, (const char *)"MARIOKART64"))
    settings.hacks |= hack_MK64;
  else if (strstr(name, (const char *)"NITRO64"))
    settings.hacks |= hack_WCWnitro;
  else if (strstr(name, (const char *)"CHOPPER_ATTACK") || strstr(name, (const char *)"WILD CHOPPERS"))
    settings.hacks |= hack_Chopper;
  else if (strstr(name, (const char *)"Resident Evil II") || strstr(name, (const char *)"BioHazard II"))
    settings.hacks |= hack_RE2;
  else if (strstr(name, (const char *)"YOSHI STORY"))
    settings.hacks |= hack_Yoshi;
  else if (strstr(name, (const char *)"F-Zero X") || strstr(name, (const char *)"F-ZERO X"))
    settings.hacks |= hack_Fzero;
  else if (strstr(name, (const char *)"PAPER MARIO") || strstr(name, (const char *)"MARIO STORY"))
    settings.hacks |= hack_PMario;
  else if (strstr(name, (const char *)"TOP GEAR RALLY 2"))
    settings.hacks |= hack_TGR2;
  else if (strstr(name, (const char *)"TOP GEAR RALLY"))
    settings.hacks |= hack_TGR;
  else if (strstr(name, (const char *)"Top Gear Hyper Bike"))
    settings.hacks |= hack_Hyperbike;
  else if (strstr(name, (const char *)"Killer Instinct Gold") || strstr(name, (const char *)"KILLER INSTINCT GOLD"))
    settings.hacks |= hack_KI;
  else if (strstr(name, (const char *)"Knockout Kings 2000"))
    settings.hacks |= hack_Knockout;
  else if (strstr(name, (const char *)"LEGORacers"))
    settings.hacks |= hack_Lego;
  else if (strstr(name, (const char *)"OgreBattle64"))
    settings.hacks |= hack_Ogre64;
  else if (strstr(name, (const char *)"Pilot Wings64"))
    settings.hacks |= hack_Pilotwings;
  else if (strstr(name, (const char *)"Supercross"))
    settings.hacks |= hack_Supercross;
  else if (strstr(name, (const char *)"STARCRAFT 64"))
    settings.hacks |= hack_Starcraft;
  else if (strstr(name, (const char *)"BANJO KAZOOIE 2") || strstr(name, (const char *)"BANJO TOOIE"))
    settings.hacks |= hack_Banjo2;
  else if (strstr(name, (const char *)"FIFA: RTWC 98") || strstr(name, (const char *)"RoadToWorldCup98"))
    settings.hacks |= hack_Fifa98;
  else if (strstr(name, (const char *)"Mega Man 64") || strstr(name, (const char *)"RockMan Dash"))
    settings.hacks |= hack_Megaman;
  else if (strstr(name, (const char *)"MISCHIEF MAKERS") || strstr(name, (const char *)"TROUBLE MAKERS"))
    settings.hacks |= hack_Makers;
  else if (strstr(name, (const char *)"GOLDENEYE"))
    settings.hacks |= hack_GoldenEye;
  else if (strstr(name, (const char *)"PUZZLE LEAGUE"))
    settings.hacks |= hack_PPL;

  Ini * ini = Ini::OpenIni();
  if (!ini)
    return;
  ini->SetPath(name);

  ini->Read(_T("alt_tex_size"), &(settings.alt_tex_size));
  if (settings.special_alt_tex_size >= 0)
    settings.alt_tex_size = settings.special_alt_tex_size;

  ini->Read(_T("use_sts1_only"), &(settings.use_sts1_only));
  if (settings.special_use_sts1_only >= 0)
    settings.use_sts1_only = settings.special_use_sts1_only;

  ini->Read(_T("force_calc_sphere"), &(settings.force_calc_sphere));
  if (settings.special_force_calc_sphere >= 0)
    settings.force_calc_sphere = settings.special_force_calc_sphere;

  ini->Read(_T("correct_viewport"), &(settings.correct_viewport));
  if (settings.special_correct_viewport >= 0)
    settings.correct_viewport = settings.special_correct_viewport;

  ini->Read(_T("increase_texrect_edge"), &(settings.increase_texrect_edge));
  if (settings.special_increase_texrect_edge >= 0)
    settings.increase_texrect_edge = settings.special_increase_texrect_edge;

  ini->Read(_T("decrease_fillrect_edge"), &(settings.decrease_fillrect_edge));
  if (settings.special_decrease_fillrect_edge >= 0)
    settings.decrease_fillrect_edge = settings.special_decrease_fillrect_edge;

  if (ini->Read(_T("texture_correction"), -1) == 0) settings.texture_correction = 0;
  else settings.texture_correction = 1;
  if (settings.special_texture_correction >= 0)
    settings.texture_correction = settings.special_texture_correction;

  if (ini->Read(_T("pal230"), -1) == 1) settings.pal230 = 1;
  else settings.pal230 = 0;
  if (settings.special_pal230 >= 0)
    settings.pal230 = settings.special_pal230;

  ini->Read(_T("stipple_mode"), &(settings.stipple_mode));
  if (settings.special_stipple_mode >= 0)
    settings.stipple_mode = settings.special_stipple_mode;

  int stipple_pattern = ini->Read(_T("stipple_pattern"), -1);
  if (stipple_pattern > 0) settings.stipple_pattern = (uint32_t)stipple_pattern;
  if (settings.special_stipple_pattern >= 0)
    stipple_pattern = settings.special_stipple_pattern;

  ini->Read(_T("force_microcheck"), &(settings.force_microcheck));
  if (settings.special_force_microcheck >= 0)
    settings.force_microcheck = settings.special_force_microcheck;

  ini->Read(_T("force_quad3d"), &(settings.force_quad3d));
  if (settings.special_force_quad3d >= 0)
    settings.force_quad3d = settings.special_force_quad3d;

  ini->Read(_T("clip_zmin"), &(settings.clip_zmin));
  if (settings.special_clip_zmin >= 0)
    settings.clip_zmin = settings.special_clip_zmin;

  ini->Read(_T("clip_zmax"), &(settings.clip_zmax));
  if (settings.special_clip_zmax >= 0)
    settings.clip_zmax = settings.special_clip_zmax;

  ini->Read(_T("fast_crc"), &(settings.fast_crc));
  if (settings.special_fast_crc >= 0)
    settings.fast_crc = settings.special_fast_crc;

  ini->Read(_T("adjust_aspect"), &(settings.adjust_aspect), 1);
  if (settings.special_adjust_aspect >= 0)
    settings.adjust_aspect = settings.special_adjust_aspect;

  ini->Read(_T("zmode_compare_less"), &(settings.zmode_compare_less));
  if (settings.special_zmode_compare_less >= 0)
    settings.zmode_compare_less = settings.special_zmode_compare_less;

  ini->Read(_T("old_style_adither"), &(settings.old_style_adither));
  if (settings.special_old_style_adither >= 0)
    settings.old_style_adither = settings.special_old_style_adither;

  ini->Read(_T("n64_z_scale"), &(settings.n64_z_scale));
  if (settings.special_n64_z_scale >= 0)
    settings.n64_z_scale = settings.special_n64_z_scale;

  if (settings.n64_z_scale)
    ZLUT_init();

  //frame buffer
  int optimize_texrect = ini->Read(_T("optimize_texrect"), -1);
  if (settings.special_optimize_texrect >= 0)
    optimize_texrect = settings.special_optimize_texrect;

  int ignore_aux_copy = ini->Read(_T("ignore_aux_copy"), -1);
  if (settings.special_ignore_aux_copy >= 0)
    ignore_aux_copy = settings.special_ignore_aux_copy;

  int hires_buf_clear = ini->Read(_T("hires_buf_clear"), -1);
  if (settings.special_hires_buf_clear >= 0)
    hires_buf_clear = settings.special_hires_buf_clear;

  int read_alpha = ini->Read(_T("fb_read_alpha"), -1);
  if (settings.special_fb_read_alpha >= 0)
    read_alpha = settings.special_fb_read_alpha;

  int useless_is_useless = ini->Read(_T("useless_is_useless"), -1);
  if (settings.special_useless_is_useless >= 0)
    useless_is_useless = settings.special_useless_is_useless;

  int fb_crc_mode = ini->Read(_T("fb_crc_mode"), -1);
  if (settings.special_fb_crc_mode >= 0)
    fb_crc_mode = settings.special_fb_crc_mode;


  if (optimize_texrect > 0) settings.frame_buffer |= fb_optimize_texrect;
  else if (optimize_texrect == 0) settings.frame_buffer &= ~fb_optimize_texrect;
  if (ignore_aux_copy > 0) settings.frame_buffer |= fb_ignore_aux_copy;
  else if (ignore_aux_copy == 0) settings.frame_buffer &= ~fb_ignore_aux_copy;
  if (hires_buf_clear > 0) settings.frame_buffer |= fb_hwfbe_buf_clear;
  else if (hires_buf_clear == 0) settings.frame_buffer &= ~fb_hwfbe_buf_clear;
  if (read_alpha > 0) settings.frame_buffer |= fb_read_alpha;
  else if (read_alpha == 0) settings.frame_buffer &= ~fb_read_alpha;
  if (useless_is_useless > 0) settings.frame_buffer |= fb_useless_is_useless;
  else settings.frame_buffer &= ~fb_useless_is_useless;
  if (fb_crc_mode >= 0) settings.fb_crc_mode = (SETTINGS::FBCRCMODE)fb_crc_mode;

  //  if (settings.custom_ini)
  {
    ini->Read(_T("filtering"), &(settings.filtering));
    if (settings.special_filtering >= 0)
      settings.filtering = settings.special_filtering;

    ini->Read(_T("fog"), &(settings.fog));
    if (settings.special_fog >= 0)
      settings.fog = settings.special_fog;

    ini->Read(_T("buff_clear"), &(settings.buff_clear));
    if (settings.special_buff_clear >= 0)
      settings.buff_clear = settings.special_buff_clear;

    ini->Read(_T("swapmode"), &(settings.swapmode));
    if (settings.special_swapmode >= 0)
      settings.swapmode = settings.special_swapmode;

    ini->Read(_T("aspect"), &(settings.aspectmode));
    if (settings.special_aspect >= 0)
      settings.aspectmode = settings.special_aspect;

    ini->Read(_T("lodmode"), &(settings.lodmode));
    if (settings.special_lodmode >= 0)
      settings.lodmode = settings.special_lodmode;

    /*
    TODO-port: fix resolutions
    int resolution;
    if (ini->Read(_T("resolution"), &resolution))
    {
      settings.res_data = (uint32_t)resolution;
      if (settings.res_data >= 0x18) settings.res_data = 12;
      settings.scr_res_x = settings.res_x = resolutions[settings.res_data][0];
      settings.scr_res_y = settings.res_y = resolutions[settings.res_data][1];
    }
    */
	
	PackedScreenResolution tmpRes = Config_ReadScreenSettings();
	settings.res_data = tmpRes.resolution;
	settings.scr_res_x = settings.res_x = tmpRes.width;
	settings.scr_res_y = settings.res_y = tmpRes.height;

    //frame buffer
    int smart_read = ini->Read(_T("fb_smart"), -1);
    if (settings.special_fb_smart >= 0)
      smart_read = settings.special_fb_smart;

    int hires = ini->Read(_T("fb_hires"), -1);
    if (settings.special_fb_hires >= 0)
      hires = settings.special_fb_hires;

    int read_always = ini->Read(_T("fb_read_always"), -1);
    if (settings.special_fb_read_always >= 0)
      read_always = settings.special_fb_read_always;

    int read_back_to_screen = ini->Read(_T("read_back_to_screen"), -1);
    if (settings.special_read_back_to_screen >= 0)
      read_back_to_screen = settings.special_read_back_to_screen;

    int cpu_write_hack = ini->Read(_T("detect_cpu_write"), -1);
    if (settings.special_detect_cpu_write >= 0)
      cpu_write_hack = settings.special_detect_cpu_write;

    int get_fbinfo = ini->Read(_T("fb_get_info"), -1);
    if (settings.special_fb_get_info >= 0)
      get_fbinfo = settings.special_fb_get_info;

    int depth_render = ini->Read(_T("fb_render"), -1);
    if (settings.special_fb_render >= 0)
      depth_render = settings.special_fb_render;

    if (smart_read > 0) settings.frame_buffer |= fb_emulation;
    else if (smart_read == 0) settings.frame_buffer &= ~fb_emulation;
    if (hires > 0) settings.frame_buffer |= fb_hwfbe;
    else if (hires == 0) settings.frame_buffer &= ~fb_hwfbe;
    if (read_always > 0) settings.frame_buffer |= fb_ref;
    else if (read_always == 0) settings.frame_buffer &= ~fb_ref;
    if (read_back_to_screen == 1) settings.frame_buffer |= fb_read_back_to_screen;
    else if (read_back_to_screen == 2) settings.frame_buffer |= fb_read_back_to_screen2;
    else if (read_back_to_screen == 0) settings.frame_buffer &= ~(fb_read_back_to_screen|fb_read_back_to_screen2);
    if (cpu_write_hack > 0) settings.frame_buffer |= fb_cpu_write_hack;
    else if (cpu_write_hack == 0) settings.frame_buffer &= ~fb_cpu_write_hack;
    if (get_fbinfo > 0) settings.frame_buffer |= fb_get_info;
    else if (get_fbinfo == 0) settings.frame_buffer &= ~fb_get_info;
    if (depth_render > 0) settings.frame_buffer |= fb_depth_render;
    else if (depth_render == 0) settings.frame_buffer &= ~fb_depth_render;
    settings.frame_buffer |= fb_motionblur;
  }
  settings.flame_corona = (settings.hacks & hack_Zelda) && !fb_depth_render_enabled;
}
