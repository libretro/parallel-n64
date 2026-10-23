/* test_mouse_accum -- the mouse-to-stick accumulator.
 *
 *  - motion over the poll periods between two controller reads reaches
 *    the stick once, and further reads in a period see the same latch;
 *  - the accumulator is restored by a savestate, so replaying from one
 *    (netplay rollback, run-ahead) gives the stick it gave the first time;
 *  - a game that stops reading cannot overflow the sum;
 *  - with no boost the scale is the plain truncating delta * sens / 50.
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
bool mouse_mode = true;
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
static int s_dx, s_dy;

#define CHECK(cond, what) \
    do { if (!(cond)) { printf("FAIL: %s\n", what); s_failures++; } } while (0)

static int16_t test_input(unsigned port, unsigned device, unsigned index,
      unsigned id)
{
   (void)port; (void)index;
   if (device == RETRO_DEVICE_MOUSE && id == RETRO_DEVICE_ID_MOUSE_X)
      return (int16_t)s_dx;
   if (device == RETRO_DEVICE_MOUSE && id == RETRO_DEVICE_ID_MOUSE_Y)
      return (int16_t)s_dy;
   return 0;
}

/* One VI: the frontend delta for the period, then the core's poll. */
static void vi(int dx, int dy)
{
   s_dx = dx;
   s_dy = dy;
   inputPollMouse();
}

static BUTTONS read_pad(void)
{
   BUTTONS k;
   memset(&k, 0, sizeof(k));
   inputGetKeys(0, &k);
   return k;
}

static void reset_state(void)
{
   inputMouseStateLoad(NULL);
   s_dx = s_dy = 0;
}

static void test_accumulates(void)
{
   BUTTONS k;
   reset_state();
   /* a game reading every third VI: two periods go unread */
   vi(10, 4);
   vi(10, 4);
   s_dx = 10;
   s_dy = 4;
   k = read_pad();
   CHECK(k.X_AXIS == 30 * 100 / 50, "three periods of X motion reach the stick");
   CHECK(k.Y_AXIS == -(12 * 100 / 50), "three periods of Y motion reach the stick");
   k = read_pad();
   CHECK(k.X_AXIS == 30 * 100 / 50, "a second read in the period sees the latch");
   vi(0, 0);
   k = read_pad();
   CHECK(k.X_AXIS == 0 && k.Y_AXIS == 0, "motion is consumed by the read");
}

static void test_rollback(void)
{
   int32_t saved[INPUT_MOUSE_STATE_WORDS];
   BUTTONS first, replay;
   reset_state();
   vi(7, 0);
   vi(9, 0);
   inputMouseStateSave(saved);
   /* first run of the next frame */
   vi(5, 0);
   s_dx = 3;
   first = read_pad();
   /* rollback: restore and replay the same frame */
   inputMouseStateLoad(saved);
   vi(5, 0);
   s_dx = 3;
   replay = read_pad();
   CHECK(first.X_AXIS == replay.X_AXIS, "replay from a state reads the same stick");
   CHECK(first.X_AXIS == (7 + 9 + 5 + 3) * 100 / 50,
         "the replayed frame counts each period once");
}

static void test_saturates(void)
{
   int i;
   BUTTONS k;
   int32_t s[INPUT_MOUSE_STATE_WORDS];
   reset_state();
   /* a game not reading the pad while the mouse keeps moving */
   for (i = 0; i < 200000; i++)
      vi(32767, -32768);
   inputMouseStateSave(s);
   CHECK(s[0] == 32767 && s[1] == -32767, "the sum saturates");
   k = read_pad();
   CHECK(k.X_AXIS == 80 && k.Y_AXIS == 80, "a saturated sum is full deflection");
}

static void test_plain_scale(void)
{
   int d, sens;
   mouse_boost = 0;
   for (sens = -500; sens <= 500; sens += 37)
      for (d = -40000; d <= 40000; d += 13)
      {
         int want = d * sens / 50;
         want = (want > 80) ? 80 : (want < -80) ? -80 : want;
         if (mouse_axis(d, sens) != want)
         {
            printf("FAIL: mouse_axis(%d, %d) = %d, want %d\n",
                  d, sens, mouse_axis(d, sens), want);
            s_failures++;
            return;
         }
      }
}

static void test_boost_monotonic(void)
{
   int b, d;
   for (b = 1; b <= 10; b++)
   {
      int prev = 0;
      mouse_boost = b;
      for (d = 0; d <= 4000; d++)
      {
         int v = mouse_axis(d, 100);
         if (v < prev || v > 80 || mouse_axis(-d, 100) != -v)
         {
            printf("FAIL: boost %d not monotonic/symmetric at %d\n", b, d);
            s_failures++;
            break;
         }
         prev = v;
      }
   }
   mouse_boost = 0;
}

/* A VR laser aim drives the stick ahead of the mouse, which still
 * consumes its motion so none of it is replayed later. */
static void test_vr_aim_precedence(void)
{
   BUTTONS k;
   reset_state();
   s_aim_on = true;
   s_aim_x  = 0.5f;
   s_aim_y  = -0.25f;
   vi(20, 0);
   s_dx = 20;
   k = read_pad();
   CHECK(k.X_AXIS == 40 && k.Y_AXIS == -20, "the VR aim drives the stick");
   s_aim_on = false;
   vi(0, 0);
   k = read_pad();
   CHECK(k.X_AXIS == 0, "mouse motion made under the VR aim is not replayed");
}

int main(void)
{
   static CONTROL ctl;
   ctl.Present = 1;
   controller[0].control = &ctl;
   input_cb = test_input;

   test_accumulates();
   test_rollback();
   test_saturates();
   test_plain_scale();
   test_boost_monotonic();
   test_vr_aim_precedence();

   if (s_failures)
   {
      printf("%d failure(s)\n", s_failures);
      return 1;
   }
   printf("test_mouse_accum: all checks passed\n");
   return 0;
}
