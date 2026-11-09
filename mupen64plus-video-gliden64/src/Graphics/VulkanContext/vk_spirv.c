#include <stdlib.h>
#include <string.h>

#include "vk_spirv.h"

enum
{
   OP_EXTENSION = 10, OP_EXT_IMPORT = 11, OP_EXT_INST = 12, OP_MEMORY_MODEL = 14, OP_ENTRY_POINT = 15,
   OP_EXECUTION_MODE = 16, OP_CAPABILITY = 17, OP_TYPE_VOID = 19, OP_TYPE_BOOL = 20, OP_TYPE_INT = 21,
   OP_TYPE_FLOAT = 22, OP_TYPE_VECTOR = 23, OP_TYPE_IMAGE = 25, OP_TYPE_SAMPLED_IMAGE = 27, OP_TYPE_ARRAY = 28,
   OP_TYPE_STRUCT = 30, OP_TYPE_POINTER = 32, OP_TYPE_FUNCTION = 33,
   OP_CONSTANT = 43, OP_CONSTANT_COMPOSITE = 44, OP_FUNCTION = 54, OP_FUNCTION_END = 56, OP_VARIABLE = 59,
   OP_LOAD = 61, OP_STORE = 62, OP_ACCESS_CHAIN = 65, OP_DECORATE = 71, OP_MEMBER_DECORATE = 72,
   OP_VECTOR_SHUFFLE = 79, OP_COMPOSITE_CONSTRUCT = 80, OP_COMPOSITE_EXTRACT = 81,
   OP_SAMPLE_IMPLICIT = 87, OP_SAMPLE_EXPLICIT = 88, OP_IMAGE_FETCH = 95, OP_IMAGE_READ = 98,
   OP_IMAGE_WRITE = 99, OP_IMAGE = 100, OP_QUERY_SIZE_LOD = 103, OP_QUERY_SIZE = 104,
   OP_F_TO_S = 110, OP_S_TO_F = 111, OP_BITCAST = 124, OP_SNEG = 126, OP_FNEG = 127, OP_IADD = 128, OP_FADD = 129,
   OP_ISUB = 130, OP_FSUB = 131, OP_IMUL = 132, OP_FMUL = 133, OP_SDIV = 135, OP_FDIV = 136, OP_SREM = 138,
   OP_DOT = 148, OP_ALL = 155, OP_LOGICAL_OR = 166, OP_LOGICAL_AND = 167, OP_LOGICAL_NOT = 168, OP_SELECT = 169,
   OP_IEQ = 170, OP_INE = 171, OP_SGT = 173, OP_SGE = 175, OP_SLT = 177, OP_SLE = 179,
   OP_FEQ = 180, OP_FNE = 183, OP_FLT = 184, OP_FGT = 186, OP_FLE = 188, OP_FGE = 190,
   OP_SHR = 195, OP_BOR = 197, OP_BAND = 199, OP_DPDX = 207, OP_FWIDTH = 209,
   OP_SELECTION_MERGE = 247, OP_LABEL = 248, OP_BRANCH = 249, OP_BRANCH_CONDITIONAL = 250, OP_KILL = 252,
   OP_RETURN = 253, OP_BEGIN_INTERLOCK = 5364, OP_END_INTERLOCK = 5365
};

enum
{
   GL_FABS = 4, GL_FSIGN = 6, GL_FLOOR = 8, GL_FRACT = 10, GL_POW = 26, GL_LOG2 = 30,
   GL_FMIN = 37, GL_FMAX = 40, GL_FCLAMP = 43, GL_FMIX = 46, GL_STEP = 48
};

enum { CAP_SHADER = 1, CAP_CLIP_DISTANCE = 32, CAP_IMAGE_QUERY = 50 };

static spv_v mk(uint32_t id, int t)
{
   spv_v v;
   v.id = id;
   v.t  = t;
   return v;
}

static int tag_size(int t)
{
   switch (t)
   {
      case T_F2: case T_I2: case T_B2:
         return 2;
      case T_F3: case T_I3: case T_B3:
         return 3;
      case T_F4: case T_I4: case T_B4: case T_U4:
         return 4;
      default:
         break;
   }
   return 1;
}

static int is_float(int t) { return t >= T_F1 && t <= T_F4; }
static int float_tag(int n) { return T_F1 + n - 1; }
static int int_tag(int n)   { return T_I1 + n - 1; }
static int bool_tag(int n)  { return n == 1 ? T_BOOL : T_B2 + n - 2; }

static void push(spv_module *m, spv_section *s, uint32_t w)
{
   if (s->n >= s->cap)
   {
      m->overflow = 1;
      return;
   }
   s->w[s->n++] = w;
}

static void op0(spv_module *m, spv_section *s, uint32_t op)
{
   push(m, s, 1u << 16 | op);
}

static void op1(spv_module *m, spv_section *s, uint32_t op, uint32_t a)
{
   push(m, s, 2u << 16 | op);
   push(m, s, a);
}

static void op2(spv_module *m, spv_section *s, uint32_t op, uint32_t a, uint32_t b)
{
   push(m, s, 3u << 16 | op);
   push(m, s, a);
   push(m, s, b);
}

static void op3(spv_module *m, spv_section *s, uint32_t op, uint32_t a, uint32_t b, uint32_t c)
{
   push(m, s, 4u << 16 | op);
   push(m, s, a);
   push(m, s, b);
   push(m, s, c);
}

static void op4(spv_module *m, spv_section *s, uint32_t op, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
   push(m, s, 5u << 16 | op);
   push(m, s, a);
   push(m, s, b);
   push(m, s, c);
   push(m, s, d);
}

static void opn(spv_module *m, spv_section *s, uint32_t op, const uint32_t *w, unsigned n)
{
   unsigned i;
   push(m, s, (uint32_t)(n + 1) << 16 | op);
   for (i = 0; i < n; i++)
      push(m, s, w[i]);
}

static unsigned string_words(spv_module *m, spv_section *s, const char *str)
{
   size_t   len   = strlen(str) + 1;
   unsigned words = (unsigned)((len + 3) / 4);
   unsigned i;
   if (s->n + words > s->cap)
   {
      m->overflow = 1;
      return 0;
   }
   for (i = 0; i < words; i++)
      s->w[s->n + i] = 0;
   memcpy(&s->w[s->n], str, len);
   s->n += words;
   return words;
}

static int section_init(spv_section *s, unsigned cap)
{
   s->w   = (uint32_t*)malloc(cap * sizeof(uint32_t));
   s->n   = 0;
   s->cap = cap;
   return s->w != NULL;
}

int spv_init(spv_module *m)
{
   memset(m, 0, sizeof(*m));
   if (   !section_init(&m->caps, 32)
       || !section_init(&m->exts, 32)
       || !section_init(&m->modes, 32)
       || !section_init(&m->annotations, 2048)
       || !section_init(&m->globals, 8192)
       || !section_init(&m->vars, 1024)
       || !section_init(&m->code, 65536)
       || !section_init(&m->out, 80000))
   {
      spv_free(m);
      return 0;
   }
   return 1;
}

void spv_free(spv_module *m)
{
   free(m->caps.w);
   free(m->exts.w);
   free(m->modes.w);
   free(m->annotations.w);
   free(m->globals.w);
   free(m->vars.w);
   free(m->code.w);
   free(m->out.w);
   memset(m, 0, sizeof(*m));
}

uint32_t spv_id(spv_module *m) { return m->bound++; }

void spv_begin(spv_module *m, int stage)
{
   m->stage         = stage;
   m->overflow      = 0;
   m->bound         = 1;
   m->caps.n        = 0;
   m->exts.n        = 0;
   m->modes.n       = 0;
   m->annotations.n = 0;
   m->globals.n     = 0;
   m->vars.n        = 0;
   m->code.n        = 0;
   m->out.n         = 0;
   m->n_iface       = 0;
   m->n_consts      = 0;
   m->n_ptrs        = 0;
   m->void_fn       = 0;
   m->storage_image = 0;
   memset(m->types, 0, sizeof(m->types));
   memset(m->sampled, 0, sizeof(m->sampled));
   memset(m->image, 0, sizeof(m->image));
   m->glsl    = spv_id(m);
   m->main_id = spv_id(m);
   spv_capability(m, CAP_SHADER);
   spv_capability(m, CAP_IMAGE_QUERY);
}

static void append(spv_module *m, const spv_section *s)
{
   unsigned i;
   for (i = 0; i < s->n; i++)
      push(m, &m->out, s->w[i]);
}

const uint32_t *spv_finish(spv_module *m, unsigned *count)
{
   unsigned at, words;
   unsigned i;

   push(m, &m->out, 0x07230203);
   push(m, &m->out, 0x00010000);
   push(m, &m->out, 0);
   push(m, &m->out, m->bound);
   push(m, &m->out, 0);
   append(m, &m->caps);
   append(m, &m->exts);

   at = m->out.n;
   push(m, &m->out, 0);
   push(m, &m->out, m->glsl);
   words = string_words(m, &m->out, "GLSL.std.450");
   if (at < m->out.cap)
      m->out.w[at] = (words + 2) << 16 | OP_EXT_IMPORT;

   op2(m, &m->out, OP_MEMORY_MODEL, 0, 1);

   at = m->out.n;
   push(m, &m->out, 0);
   push(m, &m->out, (uint32_t)m->stage);
   push(m, &m->out, m->main_id);
   words = string_words(m, &m->out, "main");
   for (i = 0; i < m->n_iface; i++)
      push(m, &m->out, m->iface[i]);
   if (at < m->out.cap)
      m->out.w[at] = (uint32_t)(3 + words + m->n_iface) << 16 | OP_ENTRY_POINT;

   append(m, &m->modes);
   if (m->stage == SPV_STAGE_FRAGMENT)
      op2(m, &m->out, OP_EXECUTION_MODE, m->main_id, 7);
   append(m, &m->annotations);
   append(m, &m->globals);

   /* the function's variables belong at the top of its first block,
    * behind the OpFunction (5 words) and the OpLabel (2) */
   for (i = 0; i < 7 && i < m->code.n; i++)
      push(m, &m->out, m->code.w[i]);
   append(m, &m->vars);
   for (i = 7; i < m->code.n; i++)
      push(m, &m->out, m->code.w[i]);

   if (m->overflow)
      return NULL;
   *count = m->out.n;
   return m->out.w;
}

void spv_capability(spv_module *m, uint32_t cap)
{
   unsigned i;
   for (i = 1; i < m->caps.n; i += 2)
      if (m->caps.w[i] == cap)
         return;
   op1(m, &m->caps, OP_CAPABILITY, cap);
}

void spv_extension(spv_module *m, const char *name)
{
   unsigned at = m->exts.n;
   unsigned words;
   push(m, &m->exts, 0);
   words = string_words(m, &m->exts, name);
   if (at < m->exts.cap)
      m->exts.w[at] = (words + 1) << 16 | OP_EXTENSION;
}

void spv_mode(spv_module *m, uint32_t mode)
{
   op2(m, &m->modes, OP_EXECUTION_MODE, m->main_id, mode);
}

uint32_t spv_type(spv_module *m, int t)
{
   uint32_t id, e;

   if (m->types[t])
      return m->types[t];
   switch (t)
   {
      case T_VOID:
         id = spv_id(m);
         op1(m, &m->globals, OP_TYPE_VOID, id);
         break;
      case T_BOOL:
         id = spv_id(m);
         op1(m, &m->globals, OP_TYPE_BOOL, id);
         break;
      case T_F1:
         id = spv_id(m);
         op2(m, &m->globals, OP_TYPE_FLOAT, id, 32);
         break;
      case T_I1:
         id = spv_id(m);
         op3(m, &m->globals, OP_TYPE_INT, id, 32, 1);
         break;
      case T_U1:
         id = spv_id(m);
         op3(m, &m->globals, OP_TYPE_INT, id, 32, 0);
         break;
      default:
         e  = spv_type(m, is_float(t) ? T_F1 : (t == T_U4 ? T_U1 : (t >= T_B2 ? T_BOOL : T_I1)));
         id = spv_id(m);
         op3(m, &m->globals, OP_TYPE_VECTOR, id, e, (uint32_t)tag_size(t));
         break;
   }
   m->types[t] = id;
   return id;
}

uint32_t spv_ptr(spv_module *m, uint32_t storage, uint32_t type)
{
   unsigned i;
   uint32_t id;

   for (i = 0; i < m->n_ptrs; i++)
      if (m->ptrs[i].storage == storage && m->ptrs[i].type == type)
         return m->ptrs[i].id;
   id = spv_id(m);
   op3(m, &m->globals, OP_TYPE_POINTER, id, storage, type);
   if (m->n_ptrs >= SPV_MAX_PTRS)
   {
      m->overflow = 1;
      return id;
   }
   m->ptrs[m->n_ptrs].storage = storage;
   m->ptrs[m->n_ptrs].type    = type;
   m->ptrs[m->n_ptrs].id      = id;
   m->n_ptrs++;
   return id;
}

static uint32_t scalar_const(spv_module *m, int t, uint32_t bits)
{
   unsigned i;
   uint32_t id, ty;

   for (i = 0; i < m->n_consts; i++)
      if (m->consts[i].t == t && m->consts[i].bits == bits)
         return m->consts[i].id;
   ty = spv_type(m, t);
   id = spv_id(m);
   op3(m, &m->globals, OP_CONSTANT, ty, id, bits);
   if (m->n_consts >= SPV_MAX_CONSTS)
   {
      m->overflow = 1;
      return id;
   }
   m->consts[m->n_consts].t    = t;
   m->consts[m->n_consts].bits = bits;
   m->consts[m->n_consts].id   = id;
   m->n_consts++;
   return id;
}

uint32_t spv_array_type(spv_module *m, uint32_t element, uint32_t length, uint32_t stride)
{
   uint32_t len = scalar_const(m, T_U1, length);
   uint32_t id  = spv_id(m);
   op3(m, &m->globals, OP_TYPE_ARRAY, id, element, len);
   if (stride)
      spv_decorate1(m, id, SPV_DEC_ARRAY_STRIDE, stride);
   return id;
}

uint32_t spv_struct(spv_module *m, const uint32_t *members, unsigned count)
{
   uint32_t id = spv_id(m);
   unsigned i;
   push(m, &m->globals, (count + 2) << 16 | OP_TYPE_STRUCT);
   push(m, &m->globals, id);
   for (i = 0; i < count; i++)
      push(m, &m->globals, members[i]);
   return id;
}

uint32_t spv_sampled_image(spv_module *m, int is_uint, int ms)
{
   int      i = is_uint ? 1 : (ms ? 2 : 0);
   uint32_t element, img, id;
   uint32_t w[8];

   if (m->sampled[i])
      return m->sampled[i];
   element = spv_type(m, is_uint ? T_U1 : T_F1);
   img     = spv_id(m);
   w[0] = img; w[1] = element; w[2] = 1; w[3] = 0; w[4] = 0;
   w[5] = ms ? 1 : 0; w[6] = 1; w[7] = 0;
   opn(m, &m->globals, OP_TYPE_IMAGE, w, 8);
   m->image[i] = img;
   id = spv_id(m);
   op2(m, &m->globals, OP_TYPE_SAMPLED_IMAGE, id, img);
   m->sampled[i] = id;
   return id;
}

uint32_t spv_storage_image(spv_module *m)
{
   uint32_t element;
   uint32_t w[8];

   if (m->storage_image)
      return m->storage_image;
   element = spv_type(m, T_F1);
   m->storage_image = spv_id(m);
   w[0] = m->storage_image; w[1] = element; w[2] = 1; w[3] = 0; w[4] = 0;
   w[5] = 0; w[6] = 2; w[7] = 3; /* R32f */
   opn(m, &m->globals, OP_TYPE_IMAGE, w, 8);
   return m->storage_image;
}

uint32_t spv_global(spv_module *m, uint32_t type, uint32_t storage)
{
   uint32_t ptr = spv_ptr(m, storage, type);
   uint32_t id  = spv_id(m);
   op3(m, &m->globals, OP_VARIABLE, ptr, id, storage);
   return id;
}

void spv_decorate(spv_module *m, uint32_t id, uint32_t dec)
{
   op2(m, &m->annotations, OP_DECORATE, id, dec);
}

void spv_decorate1(spv_module *m, uint32_t id, uint32_t dec, uint32_t value)
{
   op3(m, &m->annotations, OP_DECORATE, id, dec, value);
}

void spv_member_decorate(spv_module *m, uint32_t id, uint32_t member, uint32_t dec, uint32_t value)
{
   op4(m, &m->annotations, OP_MEMBER_DECORATE, id, member, dec, value);
}

static void add_iface(spv_module *m, uint32_t id)
{
   if (m->n_iface >= SPV_MAX_IFACE)
   {
      m->overflow = 1;
      return;
   }
   m->iface[m->n_iface++] = id;
}

spv_v spv_input(spv_module *m, int t, uint32_t location)
{
   uint32_t id = spv_global(m, spv_type(m, t), SPV_INPUT);
   spv_decorate1(m, id, SPV_DEC_LOCATION, location);
   add_iface(m, id);
   return mk(id, t);
}

spv_var spv_output(spv_module *m, int t, uint32_t location)
{
   spv_var v;
   v.id = spv_global(m, spv_type(m, t), SPV_OUTPUT);
   v.t  = t;
   spv_decorate1(m, v.id, SPV_DEC_LOCATION, location);
   add_iface(m, v.id);
   return v;
}

spv_v spv_builtin_in(spv_module *m, int t, uint32_t builtin)
{
   uint32_t id = spv_global(m, spv_type(m, t), SPV_INPUT);
   spv_decorate1(m, id, SPV_DEC_BUILTIN, builtin);
   add_iface(m, id);
   return mk(id, t);
}

spv_var spv_builtin_out(spv_module *m, int t, uint32_t builtin)
{
   spv_var v;
   v.id = spv_global(m, spv_type(m, t), SPV_OUTPUT);
   v.t  = t;
   spv_decorate1(m, v.id, SPV_DEC_BUILTIN, builtin);
   add_iface(m, v.id);
   return v;
}

uint32_t spv_clip_distance(spv_module *m)
{
   uint32_t arr, id;
   spv_capability(m, CAP_CLIP_DISTANCE);
   arr = spv_array_type(m, spv_type(m, T_F1), 1, 0);
   id  = spv_global(m, arr, SPV_OUTPUT);
   spv_decorate1(m, id, SPV_DEC_BUILTIN, 3);
   add_iface(m, id);
   return id;
}

spv_v spv_f(spv_module *m, float v)
{
   uint32_t bits;
   memcpy(&bits, &v, sizeof(bits));
   return mk(scalar_const(m, T_F1, bits), T_F1);
}

spv_v spv_i(spv_module *m, int32_t v)
{
   return mk(scalar_const(m, T_I1, (uint32_t)v), T_I1);
}

static spv_v composite_const(spv_module *m, int t, const uint32_t *comps)
{
   int      n = tag_size(t);
   uint32_t w[6];
   int      k;
   w[0] = spv_type(m, t);
   w[1] = spv_id(m);
   for (k = 0; k < n; k++)
      w[2 + k] = comps[k];
   opn(m, &m->globals, OP_CONSTANT_COMPOSITE, w, (unsigned)(n + 2));
   return mk(w[1], t);
}

spv_v spv_vec(spv_module *m, int t, float a, float b, float c, float d)
{
   uint32_t comps[4];
   comps[0] = spv_f(m, a).id;
   if (tag_size(t) == 1)
      return mk(comps[0], T_F1);
   comps[1] = spv_f(m, b).id;
   comps[2] = spv_f(m, c).id;
   comps[3] = spv_f(m, d).id;
   return composite_const(m, t, comps);
}

spv_v spv_ivec(spv_module *m, int t, int32_t a, int32_t b, int32_t c, int32_t d)
{
   uint32_t comps[4];
   comps[0] = spv_i(m, a).id;
   if (tag_size(t) == 1)
      return mk(comps[0], T_I1);
   comps[1] = spv_i(m, b).id;
   comps[2] = spv_i(m, c).id;
   comps[3] = spv_i(m, d).id;
   return composite_const(m, t, comps);
}

spv_v spv_zero(spv_module *m, int t)
{
   return is_float(t) ? spv_vec(m, t, 0.f, 0.f, 0.f, 0.f) : spv_ivec(m, t, 0, 0, 0, 0);
}

void spv_begin_main(spv_module *m)
{
   uint32_t v = spv_type(m, T_VOID);
   uint32_t w[4];
   if (!m->void_fn)
   {
      m->void_fn = spv_id(m);
      op2(m, &m->globals, OP_TYPE_FUNCTION, m->void_fn, v);
   }
   w[0] = v; w[1] = m->main_id; w[2] = 0; w[3] = m->void_fn;
   opn(m, &m->code, OP_FUNCTION, w, 4);
   op1(m, &m->code, OP_LABEL, spv_id(m));
}

void spv_end_main(spv_module *m)
{
   op0(m, &m->code, OP_RETURN);
   op0(m, &m->code, OP_FUNCTION_END);
}

spv_var spv_local(spv_module *m, int t)
{
   spv_var v;
   uint32_t ptr = spv_ptr(m, SPV_FUNCTION, spv_type(m, t));
   v.id = spv_id(m);
   v.t  = t;
   op3(m, &m->vars, OP_VARIABLE, ptr, v.id, SPV_FUNCTION);
   return v;
}

spv_var spv_local_array(spv_module *m, int t, uint32_t length)
{
   spv_var v;
   uint32_t arr = spv_array_type(m, spv_type(m, t), length, 0);
   uint32_t ptr = spv_ptr(m, SPV_FUNCTION, arr);
   v.id = spv_id(m);
   v.t  = t;
   op3(m, &m->vars, OP_VARIABLE, ptr, v.id, SPV_FUNCTION);
   return v;
}

spv_v spv_load(spv_module *m, spv_var v)
{
   uint32_t id = spv_id(m);
   op3(m, &m->code, OP_LOAD, spv_type(m, v.t), id, v.id);
   return mk(id, v.t);
}

void spv_store(spv_module *m, spv_var v, spv_v value)
{
   op2(m, &m->code, OP_STORE, v.id, value.id);
}

static uint32_t access1(spv_module *m, uint32_t storage, int t, uint32_t base, uint32_t index)
{
   uint32_t ptr   = spv_ptr(m, storage, spv_type(m, t));
   uint32_t chain = spv_id(m);
   op4(m, &m->code, OP_ACCESS_CHAIN, ptr, chain, base, index);
   return chain;
}

static spv_v load_ptr(spv_module *m, uint32_t ptr, int t)
{
   uint32_t id = spv_id(m);
   op3(m, &m->code, OP_LOAD, spv_type(m, t), id, ptr);
   return mk(id, t);
}

spv_v spv_load_at(spv_module *m, spv_var a, spv_v index)
{
   return load_ptr(m, access1(m, SPV_FUNCTION, a.t, a.id, index.id), a.t);
}

void spv_store_at(spv_module *m, spv_var a, uint32_t index, spv_v value)
{
   uint32_t chain = access1(m, SPV_FUNCTION, a.t, a.id, spv_i(m, (int32_t)index).id);
   op2(m, &m->code, OP_STORE, chain, value.id);
}

void spv_store_clip(spv_module *m, uint32_t clip, spv_v value)
{
   uint32_t chain = access1(m, SPV_OUTPUT, T_F1, clip, spv_i(m, 0).id);
   op2(m, &m->code, OP_STORE, chain, value.id);
}

spv_v spv_load_member(spv_module *m, uint32_t block, uint32_t storage, uint32_t member, int t)
{
   return load_ptr(m, access1(m, storage, t, block, spv_i(m, (int32_t)member).id), t);
}

spv_v spv_load_member_at(spv_module *m, uint32_t block, uint32_t storage, uint32_t member, spv_v index, int t)
{
   uint32_t ptr   = spv_ptr(m, storage, spv_type(m, t));
   uint32_t chain = spv_id(m);
   uint32_t w[5];
   w[0] = ptr; w[1] = chain; w[2] = block; w[3] = spv_i(m, (int32_t)member).id; w[4] = index.id;
   opn(m, &m->code, OP_ACCESS_CHAIN, w, 5);
   return load_ptr(m, chain, t);
}

static uint32_t unary(spv_module *m, uint32_t op, uint32_t type, uint32_t a)
{
   uint32_t id = spv_id(m);
   op3(m, &m->code, op, type, id, a);
   return id;
}

static uint32_t binary(spv_module *m, uint32_t op, uint32_t type, uint32_t a, uint32_t b)
{
   uint32_t id = spv_id(m);
   op4(m, &m->code, op, type, id, a, b);
   return id;
}

static void spread_both(spv_module *m, spv_v *a, spv_v *b)
{
   int na = tag_size(a->t);
   int nb = tag_size(b->t);
   if (na > nb)
      *b = spv_spread(m, *b, na);
   else if (nb > na)
      *a = spv_spread(m, *a, nb);
}

static spv_v arith(spv_module *m, uint32_t fop, uint32_t iop, spv_v a, spv_v b)
{
   spread_both(m, &a, &b);
   return mk(binary(m, is_float(a.t) ? fop : iop, spv_type(m, a.t), a.id, b.id), a.t);
}

spv_v spv_add(spv_module *m, spv_v a, spv_v b) { return arith(m, OP_FADD, OP_IADD, a, b); }
spv_v spv_sub(spv_module *m, spv_v a, spv_v b) { return arith(m, OP_FSUB, OP_ISUB, a, b); }
spv_v spv_mul(spv_module *m, spv_v a, spv_v b) { return arith(m, OP_FMUL, OP_IMUL, a, b); }
spv_v spv_div(spv_module *m, spv_v a, spv_v b) { return arith(m, OP_FDIV, OP_SDIV, a, b); }
spv_v spv_rem(spv_module *m, spv_v a, spv_v b) { return arith(m, OP_SREM, OP_SREM, a, b); }
spv_v spv_band(spv_module *m, spv_v a, spv_v b) { return arith(m, OP_BAND, OP_BAND, a, b); }
spv_v spv_bor(spv_module *m, spv_v a, spv_v b) { return arith(m, OP_BOR, OP_BOR, a, b); }
spv_v spv_shr(spv_module *m, spv_v a, spv_v b) { return arith(m, OP_SHR, OP_SHR, a, b); }

spv_v spv_neg(spv_module *m, spv_v a)
{
   return mk(unary(m, is_float(a.t) ? OP_FNEG : OP_SNEG, spv_type(m, a.t), a.id), a.t);
}

spv_v spv_dot(spv_module *m, spv_v a, spv_v b)
{
   return mk(binary(m, OP_DOT, spv_type(m, T_F1), a.id, b.id), T_F1);
}

static spv_v ext1(spv_module *m, uint32_t inst, spv_v a)
{
   uint32_t id = spv_id(m);
   uint32_t w[5];
   w[0] = spv_type(m, a.t); w[1] = id; w[2] = m->glsl; w[3] = inst; w[4] = a.id;
   opn(m, &m->code, OP_EXT_INST, w, 5);
   return mk(id, a.t);
}

static spv_v ext2(spv_module *m, uint32_t inst, spv_v a, spv_v b)
{
   uint32_t id;
   uint32_t w[6];
   spread_both(m, &a, &b);
   id = spv_id(m);
   w[0] = spv_type(m, a.t); w[1] = id; w[2] = m->glsl; w[3] = inst; w[4] = a.id; w[5] = b.id;
   opn(m, &m->code, OP_EXT_INST, w, 6);
   return mk(id, a.t);
}

static spv_v ext3(spv_module *m, uint32_t inst, spv_v a, spv_v b, spv_v c)
{
   uint32_t id;
   uint32_t w[7];
   spread_both(m, &a, &b);
   if (tag_size(c.t) != tag_size(a.t))
      c = spv_spread(m, c, tag_size(a.t));
   id = spv_id(m);
   w[0] = spv_type(m, a.t); w[1] = id; w[2] = m->glsl; w[3] = inst; w[4] = a.id; w[5] = b.id; w[6] = c.id;
   opn(m, &m->code, OP_EXT_INST, w, 7);
   return mk(id, a.t);
}

spv_v spv_dfdx(spv_module *m, spv_v a) { return mk(unary(m, OP_DPDX, spv_type(m, a.t), a.id), a.t); }
spv_v spv_fwidth(spv_module *m, spv_v a) { return mk(unary(m, OP_FWIDTH, spv_type(m, a.t), a.id), a.t); }
spv_v spv_clamp(spv_module *m, spv_v a, float lo, float hi) { return ext3(m, GL_FCLAMP, a, spv_f(m, lo), spv_f(m, hi)); }
spv_v spv_mix(spv_module *m, spv_v a, spv_v b, spv_v t) { return ext3(m, GL_FMIX, a, b, t); }
spv_v spv_step(spv_module *m, spv_v edge, spv_v x) { return ext2(m, GL_STEP, edge, x); }
spv_v spv_fract(spv_module *m, spv_v a) { return ext1(m, GL_FRACT, a); }
spv_v spv_floor(spv_module *m, spv_v a) { return ext1(m, GL_FLOOR, a); }
spv_v spv_abs(spv_module *m, spv_v a) { return ext1(m, GL_FABS, a); }
spv_v spv_sign(spv_module *m, spv_v a) { return ext1(m, GL_FSIGN, a); }
spv_v spv_min(spv_module *m, spv_v a, spv_v b) { return ext2(m, GL_FMIN, a, b); }
spv_v spv_max(spv_module *m, spv_v a, spv_v b) { return ext2(m, GL_FMAX, a, b); }
spv_v spv_pow(spv_module *m, spv_v a, spv_v b) { return ext2(m, GL_POW, a, b); }
spv_v spv_log2(spv_module *m, spv_v a) { return ext1(m, GL_LOG2, a); }

spv_v spv_spread(spv_module *m, spv_v a, int n)
{
   int      t;
   uint32_t id;
   int      k;
   if (n == 1 || tag_size(a.t) == n)
      return a;
   t  = is_float(a.t) ? float_tag(n) : (a.t == T_BOOL ? bool_tag(n) : int_tag(n));
   id = spv_id(m);
   push(m, &m->code, (uint32_t)(n + 3) << 16 | OP_COMPOSITE_CONSTRUCT);
   push(m, &m->code, spv_type(m, t));
   push(m, &m->code, id);
   for (k = 0; k < n; k++)
      push(m, &m->code, a.id);
   return mk(id, t);
}

spv_v spv_x(spv_module *m, spv_v a, uint32_t i)
{
   int      t;
   uint32_t id;
   if (tag_size(a.t) == 1)
      return a;
   t  = is_float(a.t) ? T_F1 : (a.t >= T_B2 ? T_BOOL : T_I1);
   id = spv_id(m);
   op4(m, &m->code, OP_COMPOSITE_EXTRACT, spv_type(m, t), id, a.id, i);
   return mk(id, t);
}

spv_v spv_xy(spv_module *m, spv_v a)
{
   int      t  = is_float(a.t) ? T_F2 : T_I2;
   uint32_t id = spv_id(m);
   uint32_t w[6];
   w[0] = spv_type(m, t); w[1] = id; w[2] = a.id; w[3] = a.id; w[4] = 0; w[5] = 1;
   opn(m, &m->code, OP_VECTOR_SHUFFLE, w, 6);
   return mk(id, t);
}

spv_v spv_xyz(spv_module *m, spv_v a)
{
   int      t  = is_float(a.t) ? T_F3 : T_I3;
   uint32_t id = spv_id(m);
   uint32_t w[7];
   w[0] = spv_type(m, t); w[1] = id; w[2] = a.id; w[3] = a.id; w[4] = 0; w[5] = 1; w[6] = 2;
   opn(m, &m->code, OP_VECTOR_SHUFFLE, w, 7);
   return mk(id, t);
}

spv_v spv_vec2(spv_module *m, spv_v a, spv_v b)
{
   int      t  = is_float(a.t) ? T_F2 : T_I2;
   uint32_t id = spv_id(m);
   op4(m, &m->code, OP_COMPOSITE_CONSTRUCT, spv_type(m, t), id, a.id, b.id);
   return mk(id, t);
}

spv_v spv_vec3(spv_module *m, spv_v a, spv_v b, spv_v c)
{
   int      t  = is_float(a.t) ? T_F3 : T_I3;
   uint32_t id = spv_id(m);
   uint32_t w[5];
   w[0] = spv_type(m, t); w[1] = id; w[2] = a.id; w[3] = b.id; w[4] = c.id;
   opn(m, &m->code, OP_COMPOSITE_CONSTRUCT, w, 5);
   return mk(id, t);
}

spv_v spv_vec4(spv_module *m, spv_v a, spv_v b, spv_v c, spv_v d)
{
   int      t  = is_float(a.t) ? T_F4 : T_I4;
   uint32_t id = spv_id(m);
   uint32_t w[6];
   w[0] = spv_type(m, t); w[1] = id; w[2] = a.id; w[3] = b.id; w[4] = c.id; w[5] = d.id;
   opn(m, &m->code, OP_COMPOSITE_CONSTRUCT, w, 6);
   return mk(id, t);
}

spv_v spv_vec3_1(spv_module *m, spv_v xyz, spv_v w_)
{
   uint32_t id = spv_id(m);
   op4(m, &m->code, OP_COMPOSITE_CONSTRUCT, spv_type(m, T_F4), id, xyz.id, w_.id);
   return mk(id, T_F4);
}

spv_v spv_to_float(spv_module *m, spv_v a)
{
   int t;
   if (is_float(a.t))
      return a;
   t = float_tag(tag_size(a.t));
   return mk(unary(m, OP_S_TO_F, spv_type(m, t), a.id), t);
}

spv_v spv_to_int(spv_module *m, spv_v a)
{
   int t;
   if (!is_float(a.t))
      return a;
   t = int_tag(tag_size(a.t));
   return mk(unary(m, OP_F_TO_S, spv_type(m, t), a.id), t);
}

static spv_v compare(spv_module *m, uint32_t fop, uint32_t iop, spv_v a, spv_v b)
{
   int t;
   spread_both(m, &a, &b);
   t = bool_tag(tag_size(a.t));
   return mk(binary(m, is_float(a.t) ? fop : iop, spv_type(m, t), a.id, b.id), t);
}

spv_v spv_lt(spv_module *m, spv_v a, spv_v b) { return compare(m, OP_FLT, OP_SLT, a, b); }
spv_v spv_le(spv_module *m, spv_v a, spv_v b) { return compare(m, OP_FLE, OP_SLE, a, b); }
spv_v spv_gt(spv_module *m, spv_v a, spv_v b) { return compare(m, OP_FGT, OP_SGT, a, b); }
spv_v spv_ge(spv_module *m, spv_v a, spv_v b) { return compare(m, OP_FGE, OP_SGE, a, b); }
spv_v spv_eq(spv_module *m, spv_v a, spv_v b) { return compare(m, OP_FEQ, OP_IEQ, a, b); }
spv_v spv_ne(spv_module *m, spv_v a, spv_v b) { return compare(m, OP_FNE, OP_INE, a, b); }

spv_v spv_all(spv_module *m, spv_v a)
{
   if (a.t == T_BOOL)
      return a;
   return mk(unary(m, OP_ALL, spv_type(m, T_BOOL), a.id), T_BOOL);
}

spv_v spv_select(spv_module *m, spv_v c, spv_v a, spv_v b)
{
   uint32_t id = spv_id(m);
   uint32_t w[5];
   w[0] = spv_type(m, a.t); w[1] = id; w[2] = c.id; w[3] = a.id; w[4] = b.id;
   opn(m, &m->code, OP_SELECT, w, 5);
   return mk(id, a.t);
}

spv_v spv_bool_to_float(spv_module *m, spv_v a)
{
   int t = float_tag(tag_size(a.t));
   return spv_select(m, a, spv_vec(m, t, 1.f, 1.f, 1.f, 1.f), spv_vec(m, t, 0.f, 0.f, 0.f, 0.f));
}

spv_c spv_cond(spv_v a)
{
   spv_c c;
   c.known = -1;
   c.id    = a.id;
   return c;
}

spv_c spv_known(int v)
{
   spv_c c;
   c.known = v ? 1 : 0;
   c.id    = 0;
   return c;
}

spv_c spv_and(spv_module *m, spv_c a, spv_c b)
{
   spv_c r;
   if (a.known == 0 || b.known == 0)
      return spv_known(0);
   if (a.known == 1)
      return b;
   if (b.known == 1)
      return a;
   r.known = -1;
   r.id    = binary(m, OP_LOGICAL_AND, spv_type(m, T_BOOL), a.id, b.id);
   return r;
}

spv_c spv_or(spv_module *m, spv_c a, spv_c b)
{
   spv_c r;
   if (a.known == 1 || b.known == 1)
      return spv_known(1);
   if (a.known == 0)
      return b;
   if (b.known == 0)
      return a;
   r.known = -1;
   r.id    = binary(m, OP_LOGICAL_OR, spv_type(m, T_BOOL), a.id, b.id);
   return r;
}

spv_c spv_not(spv_module *m, spv_c a)
{
   spv_c r;
   if (a.known >= 0)
      return spv_known(a.known == 0);
   r.known = -1;
   r.id    = unary(m, OP_LOGICAL_NOT, spv_type(m, T_BOOL), a.id);
   return r;
}

uint32_t spv_load_sampler(spv_module *m, uint32_t variable, uint32_t type)
{
   uint32_t id = spv_id(m);
   op3(m, &m->code, OP_LOAD, type, id, variable);
   return id;
}

spv_v spv_sample(spv_module *m, uint32_t s, spv_v coord)
{
   uint32_t id = spv_id(m);
   op4(m, &m->code, OP_SAMPLE_IMPLICIT, spv_type(m, T_F4), id, s, coord.id);
   return mk(id, T_F4);
}

spv_v spv_sample_lod(spv_module *m, uint32_t s, spv_v coord, spv_v lod)
{
   uint32_t id = spv_id(m);
   uint32_t w[6];
   w[0] = spv_type(m, T_F4); w[1] = id; w[2] = s; w[3] = coord.id; w[4] = 2; w[5] = lod.id;
   opn(m, &m->code, OP_SAMPLE_EXPLICIT, w, 6);
   return mk(id, T_F4);
}

static spv_v fetch_op(spv_module *m, uint32_t image_type, int result_t, uint32_t s, spv_v coord, uint32_t operand, int32_t arg)
{
   uint32_t img = unary(m, OP_IMAGE, image_type, s);
   uint32_t id  = spv_id(m);
   uint32_t w[6];
   w[0] = spv_type(m, result_t); w[1] = id; w[2] = img; w[3] = coord.id; w[4] = operand; w[5] = spv_i(m, arg).id;
   opn(m, &m->code, OP_IMAGE_FETCH, w, 6);
   return mk(id, result_t);
}

spv_v spv_fetch(spv_module *m, uint32_t s, spv_v coord)
{
   return fetch_op(m, m->image[0], T_F4, s, coord, 2, 0);
}

spv_v spv_fetch_ms(spv_module *m, uint32_t s, spv_v coord, int32_t sample)
{
   return fetch_op(m, m->image[2], T_F4, s, coord, 64, sample);
}

spv_v spv_fetch_uint(spv_module *m, uint32_t s, spv_v coord)
{
   spv_v    rgba = fetch_op(m, m->image[1], T_U4, s, coord, 2, 0);
   uint32_t r    = spv_id(m);
   uint32_t id;
   op4(m, &m->code, OP_COMPOSITE_EXTRACT, spv_type(m, T_U1), r, rgba.id, 0);
   /* every value here fits an int */
   id = unary(m, OP_BITCAST, spv_type(m, T_I1), r);
   return mk(id, T_I1);
}

spv_v spv_texture_size(spv_module *m, uint32_t s, spv_v lod)
{
   uint32_t img = unary(m, OP_IMAGE, m->image[0], s);
   return mk(binary(m, OP_QUERY_SIZE_LOD, spv_type(m, T_I2), img, lod.id), T_I2);
}

spv_v spv_texture_size_ms(spv_module *m, uint32_t s)
{
   uint32_t img = unary(m, OP_IMAGE, m->image[2], s);
   return mk(unary(m, OP_QUERY_SIZE, spv_type(m, T_I2), img), T_I2);
}

spv_v spv_image_load(spv_module *m, uint32_t image, spv_v coord)
{
   uint32_t id = spv_id(m);
   op4(m, &m->code, OP_IMAGE_READ, spv_type(m, T_F4), id, image, coord.id);
   return mk(id, T_F4);
}

void spv_image_store(spv_module *m, uint32_t image, spv_v coord, spv_v value)
{
   op3(m, &m->code, OP_IMAGE_WRITE, image, coord.id, value.id);
}

void spv_kill(spv_module *m)
{
   op0(m, &m->code, OP_KILL);
   /* what follows is written into a block nothing reaches */
   op1(m, &m->code, OP_LABEL, spv_id(m));
}

void spv_begin_interlock(spv_module *m) { op0(m, &m->code, OP_BEGIN_INTERLOCK); }
void spv_end_interlock(spv_module *m)   { op0(m, &m->code, OP_END_INTERLOCK); }

int spv_if(spv_module *m, spv_if_t *b, spv_c c)
{
   uint32_t then_id;
   b->c       = c;
   b->in_else = 0;
   if (c.known >= 0)
      return c.known;
   then_id     = spv_id(m);
   b->else_id  = spv_id(m);
   b->merge_id = spv_id(m);
   op2(m, &m->code, OP_SELECTION_MERGE, b->merge_id, 0);
   op3(m, &m->code, OP_BRANCH_CONDITIONAL, c.id, then_id, b->else_id);
   op1(m, &m->code, OP_LABEL, then_id);
   return 1;
}

int spv_else(spv_module *m, spv_if_t *b)
{
   if (b->c.known >= 0)
      return b->c.known == 0;
   op1(m, &m->code, OP_BRANCH, b->merge_id);
   op1(m, &m->code, OP_LABEL, b->else_id);
   b->in_else = 1;
   return 1;
}

void spv_endif(spv_module *m, spv_if_t *b)
{
   if (b->c.known >= 0)
      return;
   if (!b->in_else)
   {
      op1(m, &m->code, OP_BRANCH, b->merge_id);
      op1(m, &m->code, OP_LABEL, b->else_id);
   }
   op1(m, &m->code, OP_BRANCH, b->merge_id);
   op1(m, &m->code, OP_LABEL, b->merge_id);
}
