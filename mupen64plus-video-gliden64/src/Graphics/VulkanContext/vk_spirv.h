#ifndef VK_SPIRV_H
#define VK_SPIRV_H

/* A SPIR-V module written word by word: no GLSL, no compiler. C89.
 *
 * Values carry their type, so a shader writer reads like the expressions
 * it writes. A condition may be known while writing: then only the arm
 * that runs is written, which is how one writer gives both the exact
 * generic shader and each combiner's own.
 *
 * The module holds fixed buffers, allocated once. One that does not fit
 * is refused at spv_finish, never grown. */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum spv_tag
{
   T_VOID = 0, T_BOOL, T_F1, T_F2, T_F3, T_F4, T_I1, T_I2, T_I3, T_I4, T_U1,
   T_B2, T_B3, T_B4, T_U4, T_COUNT
};

typedef struct spv_v   { uint32_t id; int t; } spv_v;
typedef struct spv_var { uint32_t id; int t; } spv_var;

/* known: -1 only the GPU can tell, 0 false, 1 true */
typedef struct spv_c   { int known; uint32_t id; } spv_c;

typedef struct spv_if_t
{
   spv_c c;
   uint32_t else_id;
   uint32_t merge_id;
   int in_else;
} spv_if_t;

typedef struct spv_section
{
   uint32_t *w;
   unsigned  n;
   unsigned  cap;
} spv_section;

#define SPV_MAX_CONSTS 256
#define SPV_MAX_PTRS   64
#define SPV_MAX_IFACE  32

enum
{
   SPV_STAGE_VERTEX = 0, SPV_STAGE_FRAGMENT = 4,
   SPV_UNIFORM_CONSTANT = 0, SPV_INPUT = 1, SPV_UNIFORM = 2, SPV_OUTPUT = 3,
   SPV_FUNCTION = 7, SPV_PUSH_CONSTANT = 9,
   SPV_DEC_BLOCK = 2, SPV_DEC_ARRAY_STRIDE = 6, SPV_DEC_BUILTIN = 11,
   SPV_DEC_FLAT = 14, SPV_DEC_LOCATION = 30, SPV_DEC_BINDING = 33,
   SPV_DEC_SET = 34, SPV_DEC_OFFSET = 35,
   SPV_BUILTIN_POSITION = 0, SPV_BUILTIN_FRAGCOORD = 15, SPV_BUILTIN_FRAGDEPTH = 22,
   SPV_MODE_DEPTH_REPLACING = 12, SPV_MODE_PIXEL_INTERLOCK = 5366,
   SPV_CAP_INTERLOCK = 5378
};

typedef struct spv_module
{
   spv_section caps, exts, modes, annotations, globals, vars, code, out;
   int      stage;
   int      overflow;
   uint32_t bound;
   uint32_t glsl;
   uint32_t main_id;
   uint32_t void_fn;
   uint32_t types[T_COUNT];
   uint32_t sampled[3];
   uint32_t image[3];
   uint32_t storage_image;
   uint32_t iface[SPV_MAX_IFACE];
   unsigned n_iface;
   struct { int t; uint32_t bits; uint32_t id; } consts[SPV_MAX_CONSTS];
   unsigned n_consts;
   struct { uint32_t storage; uint32_t type; uint32_t id; } ptrs[SPV_MAX_PTRS];
   unsigned n_ptrs;
} spv_module;

/* Once per module object; 0 when out of memory. */
int  spv_init(spv_module *m);
void spv_free(spv_module *m);

void spv_begin(spv_module *m, int stage);
/* The words, or NULL if it did not fit. Valid until the next spv_begin. */
const uint32_t *spv_finish(spv_module *m, unsigned *count);

uint32_t spv_id(spv_module *m);
void spv_capability(spv_module *m, uint32_t cap);
void spv_extension(spv_module *m, const char *name);
void spv_mode(spv_module *m, uint32_t mode);

uint32_t spv_type(spv_module *m, int t);
uint32_t spv_ptr(spv_module *m, uint32_t storage, uint32_t type);
uint32_t spv_array_type(spv_module *m, uint32_t element, uint32_t length, uint32_t stride);
uint32_t spv_struct(spv_module *m, const uint32_t *members, unsigned count);
uint32_t spv_sampled_image(spv_module *m, int is_uint, int ms);
uint32_t spv_storage_image(spv_module *m);

uint32_t spv_global(spv_module *m, uint32_t type, uint32_t storage);
void spv_decorate(spv_module *m, uint32_t id, uint32_t dec);
void spv_decorate1(spv_module *m, uint32_t id, uint32_t dec, uint32_t value);
void spv_member_decorate(spv_module *m, uint32_t id, uint32_t member, uint32_t dec, uint32_t value);
spv_v   spv_input(spv_module *m, int t, uint32_t location);
spv_var spv_output(spv_module *m, int t, uint32_t location);
spv_v   spv_builtin_in(spv_module *m, int t, uint32_t builtin);
spv_var spv_builtin_out(spv_module *m, int t, uint32_t builtin);
uint32_t spv_clip_distance(spv_module *m);

spv_v spv_f(spv_module *m, float v);
spv_v spv_i(spv_module *m, int32_t v);
spv_v spv_vec(spv_module *m, int t, float a, float b, float c, float d);
spv_v spv_ivec(spv_module *m, int t, int32_t a, int32_t b, int32_t c, int32_t d);
spv_v spv_zero(spv_module *m, int t);

void spv_begin_main(spv_module *m);
void spv_end_main(spv_module *m);

spv_var spv_local(spv_module *m, int t);
spv_var spv_local_array(spv_module *m, int t, uint32_t length);
spv_v spv_load(spv_module *m, spv_var v);
void  spv_store(spv_module *m, spv_var v, spv_v value);
spv_v spv_load_at(spv_module *m, spv_var a, spv_v index);
void  spv_store_at(spv_module *m, spv_var a, uint32_t index, spv_v value);
void  spv_store_clip(spv_module *m, uint32_t clip, spv_v value);
spv_v spv_load_member(spv_module *m, uint32_t block, uint32_t storage, uint32_t member, int t);
spv_v spv_load_member_at(spv_module *m, uint32_t block, uint32_t storage, uint32_t member, spv_v index, int t);

/* a scalar beside a vector is spread over it */
spv_v spv_add(spv_module *m, spv_v a, spv_v b);
spv_v spv_sub(spv_module *m, spv_v a, spv_v b);
spv_v spv_mul(spv_module *m, spv_v a, spv_v b);
spv_v spv_div(spv_module *m, spv_v a, spv_v b);
spv_v spv_rem(spv_module *m, spv_v a, spv_v b);
spv_v spv_neg(spv_module *m, spv_v a);
spv_v spv_band(spv_module *m, spv_v a, spv_v b);
spv_v spv_bor(spv_module *m, spv_v a, spv_v b);
spv_v spv_shr(spv_module *m, spv_v a, spv_v b);
spv_v spv_dot(spv_module *m, spv_v a, spv_v b);
spv_v spv_dfdx(spv_module *m, spv_v a);
spv_v spv_fwidth(spv_module *m, spv_v a);
spv_v spv_clamp(spv_module *m, spv_v a, float lo, float hi);
spv_v spv_mix(spv_module *m, spv_v a, spv_v b, spv_v t);
spv_v spv_step(spv_module *m, spv_v edge, spv_v x);
spv_v spv_fract(spv_module *m, spv_v a);
spv_v spv_floor(spv_module *m, spv_v a);
spv_v spv_abs(spv_module *m, spv_v a);
spv_v spv_sign(spv_module *m, spv_v a);
spv_v spv_min(spv_module *m, spv_v a, spv_v b);
spv_v spv_max(spv_module *m, spv_v a, spv_v b);
spv_v spv_pow(spv_module *m, spv_v a, spv_v b);
spv_v spv_log2(spv_module *m, spv_v a);

spv_v spv_spread(spv_module *m, spv_v a, int n);
spv_v spv_x(spv_module *m, spv_v a, uint32_t i);
spv_v spv_xy(spv_module *m, spv_v a);
spv_v spv_xyz(spv_module *m, spv_v a);
spv_v spv_vec2(spv_module *m, spv_v a, spv_v b);
spv_v spv_vec3(spv_module *m, spv_v a, spv_v b, spv_v c);
spv_v spv_vec4(spv_module *m, spv_v a, spv_v b, spv_v c, spv_v d);
spv_v spv_vec3_1(spv_module *m, spv_v xyz, spv_v w);
spv_v spv_to_float(spv_module *m, spv_v a);
spv_v spv_to_int(spv_module *m, spv_v a);

spv_v spv_lt(spv_module *m, spv_v a, spv_v b);
spv_v spv_le(spv_module *m, spv_v a, spv_v b);
spv_v spv_gt(spv_module *m, spv_v a, spv_v b);
spv_v spv_ge(spv_module *m, spv_v a, spv_v b);
spv_v spv_eq(spv_module *m, spv_v a, spv_v b);
spv_v spv_ne(spv_module *m, spv_v a, spv_v b);
spv_v spv_all(spv_module *m, spv_v a);
spv_v spv_bool_to_float(spv_module *m, spv_v a);
spv_v spv_select(spv_module *m, spv_v c, spv_v a, spv_v b);

spv_c spv_cond(spv_v a);
spv_c spv_known(int v);
spv_c spv_and(spv_module *m, spv_c a, spv_c b);
spv_c spv_or(spv_module *m, spv_c a, spv_c b);
spv_c spv_not(spv_module *m, spv_c a);

uint32_t spv_load_sampler(spv_module *m, uint32_t variable, uint32_t type);
spv_v spv_sample(spv_module *m, uint32_t s, spv_v coord);
spv_v spv_sample_lod(spv_module *m, uint32_t s, spv_v coord, spv_v lod);
spv_v spv_fetch(spv_module *m, uint32_t s, spv_v coord);
spv_v spv_fetch_ms(spv_module *m, uint32_t s, spv_v coord, int32_t sample);
spv_v spv_fetch_uint(spv_module *m, uint32_t s, spv_v coord);
spv_v spv_texture_size(spv_module *m, uint32_t s, spv_v lod);
spv_v spv_texture_size_ms(spv_module *m, uint32_t s);
spv_v spv_image_load(spv_module *m, uint32_t image, spv_v coord);
void  spv_image_store(spv_module *m, uint32_t image, spv_v coord, spv_v value);

void spv_kill(spv_module *m);
void spv_begin_interlock(spv_module *m);
void spv_end_interlock(spv_module *m);

/* if (spv_if(m, &b, c)) { then-arm }
 * if (spv_else(m, &b)) { else-arm }   (may be left out)
 * spv_endif(m, &b);
 * An arm known not to run is not written. */
int  spv_if(spv_module *m, spv_if_t *b, spv_c c);
int  spv_else(spv_module *m, spv_if_t *b);
void spv_endif(spv_module *m, spv_if_t *b);

#ifdef __cplusplus
}
#endif

#endif
