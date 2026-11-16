/* test_input_bitmasks -- the pad read in one call where the frontend can.
 *
 * With RETRO_ENVIRONMENT_GET_INPUT_BITMASKS answered, the controllers set
 * up at a ROM's opening read each pad with one RETRO_DEVICE_ID_JOYPAD_MASK
 * call a poll, and nothing more of the joypad; without it, each button
 * once. The core used to ask only in inputPluginStartup, which nothing
 * called, and read the d-pad, Start and Select again one call each
 * after the mask (the control profiles every button), so a poll was 7
 * to 16 calls whatever the frontend had. The buttons read are the same
 * both ways.
 *
 * Built by `make check-core` (top-level Makefile). Exit status 0 on pass.
 */
#include "../mupen64plus-core/src/plugin/emulate_game_controller_via_libretro.c"

#include <stdio.h>

retro_environment_t environ_cb;
retro_input_state_t input_cb;
struct retro_rumble_interface rumble;
int pad_pak_types[4];
int pad_present[4];
int astick_deadzone;
int astick_sensitivity = 100;
int astick_snap_active;
int astick_snap_max_angle;
int astick_snap_min_displacement_percent;
int r_cbutton, l_cbutton, d_cbutton, u_cbutton;
bool alternate_mapping;
bool mouse_mode;
int mouse_sensitivity_x = 100;
int mouse_sensitivity_y = -100;
int mouse_boost;
int mouse_left_btn, mouse_right_btn, mouse_middle_btn;
int mouse_wheel_up_btn, mouse_wheel_down_btn;
m64p_rom_header ROM_HEADER;
int g_aleck64_dpad_disabled;
int vr_aim_btn_up, vr_aim_btn_down, vr_aim_btn_left, vr_aim_btn_right;

/* VR laser aim hooks (libretro.c): off unless a test sets an aim */
static bool  s_aim_on;
static float s_aim_x, s_aim_y;
bool libretro_vr_aim_stick(int port, float *x, float *y)
{
   if (!s_aim_on || port != 0)
      return false;
   *x = s_aim_x;
   *y = s_aim_y;
   return true;
}
unsigned libretro_vr_aim_dirs(int port) { (void)port; return 0; }

static int s_failures;
static bool s_frontend_bitmasks;
static unsigned s_reads, s_mask_reads;
/* RetroPad B is the N64's A */
static unsigned s_held = (1u << RETRO_DEVICE_ID_JOYPAD_B) | (1u << RETRO_DEVICE_ID_JOYPAD_START)
      | (1u << RETRO_DEVICE_ID_JOYPAD_UP);

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

static bool test_environ(unsigned cmd, void *data)
{
   (void)data;
   if (cmd == RETRO_ENVIRONMENT_GET_INPUT_BITMASKS)
      return s_frontend_bitmasks;
   return false;
}

static int16_t test_input(unsigned port, unsigned device, unsigned index, unsigned id)
{
   (void)index;
   if (port != 0 || device != RETRO_DEVICE_JOYPAD)
      return 0;
   s_reads++;
   if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
   {
      s_mask_reads++;
      return (int16_t)s_held;
   }
   return id < 16 && (s_held & (1u << id)) ? 1 : 0;
}

static BUTTONS opened_and_read(bool bitmasks, const char *profile)
{
   static CONTROL controls[4];
   CONTROL_INFO info;
   BUTTONS k;
   memset(&info, 0, sizeof(info));
   info.Controls = controls;
   pad_present[0] = 1;
   s_frontend_bitmasks = bitmasks;
   inputInitiateControllers(info);
   if (profile)
      inputInitiateCallback(profile);
   s_reads = s_mask_reads = 0;
   memset(&k, 0, sizeof(k));
   inputGetKeys(0, &k);
   return k;
}

int main(void)
{
   BUTTONS with, without;
   unsigned reads_without;
   environ_cb = test_environ;
   input_cb = test_input;

   with = opened_and_read(true, NULL);
   CHECK(s_mask_reads == 1 && s_reads == 1, "with bitmasks, the pad is read in one mask call");
   without = opened_and_read(false, NULL);
   reads_without = s_reads;
   CHECK(s_mask_reads == 0 && reads_without == 16, "without, a button a call, each once");
   CHECK(with.Value == without.Value, "the same buttons either way");
   CHECK(with.A_BUTTON && with.START_BUTTON && with.U_DPAD && !with.B_BUTTON, "the buttons held");

   /* a game's own control profile reads from the same bits */
   with = opened_and_read(true, "KILLER INSTINCT GOLD");
   CHECK(s_mask_reads == 1 && s_reads == 1, "a game's profile: one mask call");
   without = opened_and_read(false, "KILLER INSTINCT GOLD");
   CHECK(s_mask_reads == 0 && s_reads == 16, "a game's profile without bitmasks: each button once");
   CHECK(with.Value == without.Value && with.START_BUTTON && with.U_DPAD, "a game's profile: the same buttons");

   if (s_failures)
   {
      printf("%d failure(s)\n", s_failures);
      return 1;
   }
   printf("test_input_bitmasks: one read with bitmasks, %u without: ok\n", reads_without);
   return 0;
}
