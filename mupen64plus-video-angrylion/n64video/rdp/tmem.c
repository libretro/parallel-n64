#define tmem16 ((uint16_t*)state[wid].tmem)
#include "../simd.h"

#define tc16   ((uint16_t*)state[wid].tmem)
#define tlut   ((uint16_t*)(&state[wid].tmem[0x800]))

static uint8_t replicated_rgba[32];

#define GET_LOW_RGBA16_TMEM(x)  (replicated_rgba[((x) >> 1) & 0x1f])
#define GET_MED_RGBA16_TMEM(x)  (replicated_rgba[((x) >> 6) & 0x1f])
#define GET_HI_RGBA16_TMEM(x)   (replicated_rgba[(x) >> 11])

static void sort_tmem_idx(uint32_t *idx, uint32_t idxa, uint32_t idxb, uint32_t idxc, uint32_t idxd, uint32_t bankno)
{
    if ((idxa & 3) == bankno)
        *idx = idxa & 0x3ff;
    else if ((idxb & 3) == bankno)
        *idx = idxb & 0x3ff;
    else if ((idxc & 3) == bankno)
        *idx = idxc & 0x3ff;
    else if ((idxd & 3) == bankno)
        *idx = idxd & 0x3ff;
    else
        *idx = 0;
}

static void sort_tmem_shorts_lowhalf(uint32_t* bindshort, uint32_t short0, uint32_t short1, uint32_t short2, uint32_t short3, uint32_t bankno)
{
    switch(bankno)
    {
    case 0:
        *bindshort = short0;
        break;
    case 1:
        *bindshort = short1;
        break;
    case 2:
        *bindshort = short2;
        break;
    case 3:
        *bindshort = short3;
        break;
    }
}

static void compute_color_index(uint32_t wid, uint32_t* cidx, uint32_t readshort, uint32_t nybbleoffset, uint32_t tilenum)
{
    uint32_t lownib, hinib;
    if (state[wid].tile[tilenum].size == PIXEL_SIZE_4BIT)
    {
        lownib = (nybbleoffset ^ 3) << 2;
        hinib = state[wid].tile[tilenum].palette;
    }
    else
    {
        lownib = ((nybbleoffset & 2) ^ 2) << 2;
        hinib = lownib ? ((readshort >> 12) & 0xf) : ((readshort >> 4) & 0xf);
    }
    lownib = (readshort >> lownib) & 0xf;
    *cidx = (hinib << 4) | lownib;
}

static INLINE void fetch_texel(uint32_t wid, struct color *color, int s, int t, uint32_t tilenum)
{
    uint32_t tbase = state[wid].tile[tilenum].line * (t & 0xff) + state[wid].tile[tilenum].tmem;



    uint32_t tpal   = state[wid].tile[tilenum].palette;








    uint32_t taddr = 0;





    switch (state[wid].tile[tilenum].f.notlutswitch)
    {
    case TEXEL_RGBA4:
        {
            taddr = ((tbase << 4) + s) >> 1;
            taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
            uint8_t byteval, c;

            byteval = state[wid].tmem[taddr & 0xfff];
            c = ((s & 1)) ? (byteval & 0xf) : (byteval >> 4);
            c |= (c << 4);
            color->r = c;
            color->g = c;
            color->b = c;
            color->a = c;
        }
        break;
    case TEXEL_RGBA8:
        {
            taddr = (tbase << 3) + s;
            taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);

            uint8_t p;

            p = state[wid].tmem[taddr & 0xfff];
            color->r = p;
            color->g = p;
            color->b = p;
            color->a = p;
        }
        break;
    case TEXEL_RGBA16:
        {
            taddr = (tbase << 2) + s;
            taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);


            uint16_t c;

            c = tc16[taddr & 0x7ff];
            color->r = GET_HI_RGBA16_TMEM(c);
            color->g = GET_MED_RGBA16_TMEM(c);
            color->b = GET_LOW_RGBA16_TMEM(c);
            color->a = (c & 1) ? 0xff : 0;
        }
        break;
    case TEXEL_RGBA32:
        {



            taddr = (tbase << 2) + s;
            taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);

            uint16_t c;


            taddr &= 0x3ff;
            c = tc16[taddr];
            color->r = c >> 8;
            color->g = c & 0xff;
            c = tc16[taddr | 0x400];
            color->b = c >> 8;
            color->a = c & 0xff;
        }
        break;
    case TEXEL_YUV4:
        {
            taddr = (tbase << 3) + s;

            taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);

            int32_t u, save;

            save = state[wid].tmem[taddr & 0x7ff];

            save &= 0xf0;
            save |= (save >> 4);

            u = save - 0x80;

            color->r = u;
            color->g = u;
            color->b = save;
            color->a = save;
        }
        break;
    case TEXEL_YUV8:
        {
            taddr = (tbase << 3) + s;

            taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);

            int32_t u, save;

            save = u = state[wid].tmem[taddr & 0x7ff];

            u = u - 0x80;

            color->r = u;
            color->g = u;
            color->b = save;
            color->a = save;
        }
        break;
    case TEXEL_YUV16:
        {
            taddr = (tbase << 3) + s;
            int taddrlow = taddr >> 1;

            taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
            taddrlow ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);

            taddr &= 0x7ff;
            taddrlow &= 0x3ff;

            uint16_t c = tc16[taddrlow];

            int32_t y, u, v;
            y = state[wid].tmem[taddr | 0x800];
            u = c >> 8;
            v = c & 0xff;

            u = u - 0x80;
            v = v - 0x80;



            color->r = u;
            color->g = v;
            color->b = y;
            color->a = y;
        }
        break;
    case TEXEL_YUV32:
        {
            int taddrlow;
            uint16_t c;
            int32_t y, u, v;

            taddr = (tbase << 3) + s;
            taddrlow = taddr >> 1;

            taddrlow ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);

            taddrlow &= 0x3ff;

            c = tc16[taddrlow];

            u = c >> 8;
            v = c & 0xff;

            u = u - 0x80;
            v = v - 0x80;

            color->r = u;
            color->g = v;

            if (s & 1)
            {
                taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
                taddr &= 0x7ff;
                y = state[wid].tmem[taddr | 0x800];

                color->b = y;
                color->a = y;
            }
            else
            {
                y = tc16[taddrlow | 0x400];

                color->b = y >> 8;
                color->a = ((y >> 8) & 0xf) | (y & 0xf0);
            }
        }
        break;
    case TEXEL_CI4:
        {
            taddr = ((tbase << 4) + s) >> 1;
            taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);

            uint8_t p;



            p = state[wid].tmem[taddr & 0xfff];
            p = (s & 1) ? (p & 0xf) : (p >> 4);
            p = (tpal << 4) | p;
            color->r = color->g = color->b = color->a = p;
        }
        break;
    case TEXEL_CI8:
        {
            taddr = (tbase << 3) + s;
            taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);

            uint8_t p;


            p = state[wid].tmem[taddr & 0xfff];
            color->r = p;
            color->g = p;
            color->b = p;
            color->a = p;
        }
        break;
    case TEXEL_CI16:
    case TEXEL_CI32:
        {
            taddr = (tbase << 2) + s;
            taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);

            uint16_t c;

            c = tc16[taddr & 0x7ff];
            color->r = c >> 8;
            color->g = c & 0xff;
            color->b = color->r;
            color->a = color->g;
        }
        break;
    case TEXEL_IA4:
        {
            taddr = ((tbase << 4) + s) >> 1;
            taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);

            uint8_t p, i;


            p = state[wid].tmem[taddr & 0xfff];
            p = (s & 1) ? (p & 0xf) : (p >> 4);
            i = p & 0xe;
            i = (i << 4) | (i << 1) | (i >> 2);
            color->r = i;
            color->g = i;
            color->b = i;
            color->a = (p & 0x1) ? 0xff : 0;
        }
        break;
    case TEXEL_IA8:
        {
            taddr = (tbase << 3) + s;
            taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);

            uint8_t p, i;


            p = state[wid].tmem[taddr & 0xfff];
            i = p & 0xf0;
            i |= (i >> 4);
            color->r = i;
            color->g = i;
            color->b = i;
            color->a = ((p & 0xf) << 4) | (p & 0xf);
        }
        break;
    case TEXEL_IA16:
        {


            taddr = (tbase << 2) + s;
            taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);

            uint16_t c;

            c = tc16[taddr & 0x7ff];
            color->r = color->g = color->b = (c >> 8);
            color->a = c & 0xff;
        }
        break;
    case TEXEL_IA32:
        {
            taddr = (tbase << 2) + s;
            taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);

            uint16_t c;

            c = tc16[taddr & 0x7ff];
            color->r = c >> 8;
            color->g = c & 0xff;
            color->b = color->r;
            color->a = color->g;
        }
        break;
    case TEXEL_I4:
        {
            taddr = ((tbase << 4) + s) >> 1;
            taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);

            uint8_t byteval, c;

            byteval = state[wid].tmem[taddr & 0xfff];
            c = (s & 1) ? (byteval & 0xf) : (byteval >> 4);
            c |= (c << 4);
            color->r = c;
            color->g = c;
            color->b = c;
            color->a = c;
        }
        break;
    case TEXEL_I8:
        {
            taddr = (tbase << 3) + s;
            taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);

            uint8_t c;

            c = state[wid].tmem[taddr & 0xfff];
            color->r = c;
            color->g = c;
            color->b = c;
            color->a = c;
        }
        break;
    case TEXEL_I16:
    case TEXEL_I32:
    default:
        {
            taddr = (tbase << 2) + s;
            taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);

            uint16_t c;

            c = tc16[taddr & 0x7ff];
            color->r = c >> 8;
            color->g = c & 0xff;
            color->b = color->r;
            color->a = color->g;
        }
        break;
    }
}

static INLINE void fetch_texel_quadro(uint32_t wid, struct color *color0, struct color *color1, struct color *color2, struct color *color3, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int unequaluppers)
{

    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;

    int t1 = (t0 & 0xff) + tdiff;



    int s1 = s0 + sdiff;

    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t tpal = state[wid].tile[tilenum].palette;
    uint32_t xort, ands;




    uint32_t taddr0, taddr1, taddr2, taddr3;
    uint32_t taddrlow0, taddrlow1, taddrlow2, taddrlow3;

    switch (state[wid].tile[tilenum].f.notlutswitch)
    {
    case TEXEL_RGBA4:
        {
            taddr0 = ((tbase0 << 4) + s0) >> 1;
            taddr1 = ((tbase0 << 4) + s1) >> 1;
            taddr2 = ((tbase2 << 4) + s0) >> 1;
            taddr3 = ((tbase2 << 4) + s1) >> 1;
            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            uint32_t byteval, c;

            taddr0 &= 0xfff;
            taddr1 &= 0xfff;
            taddr2 &= 0xfff;
            taddr3 &= 0xfff;
            ands = s0 & 1;
            byteval = state[wid].tmem[taddr0];
            c = (ands) ? (byteval & 0xf) : (byteval >> 4);
            c |= (c << 4);
            color0->r = c;
            color0->g = c;
            color0->b = c;
            color0->a = c;
            byteval = state[wid].tmem[taddr2];
            c = (ands) ? (byteval & 0xf) : (byteval >> 4);
            c |= (c << 4);
            color2->r = c;
            color2->g = c;
            color2->b = c;
            color2->a = c;

            ands = s1 & 1;
            byteval = state[wid].tmem[taddr1];
            c = (ands) ? (byteval & 0xf) : (byteval >> 4);
            c |= (c << 4);
            color1->r = c;
            color1->g = c;
            color1->b = c;
            color1->a = c;
            byteval = state[wid].tmem[taddr3];
            c = (ands) ? (byteval & 0xf) : (byteval >> 4);
            c |= (c << 4);
            color3->r = c;
            color3->g = c;
            color3->b = c;
            color3->a = c;
        }
        break;
    case TEXEL_RGBA8:
        {
            taddr0 = (tbase0 << 3) + s0;
            taddr1 = (tbase0 << 3) + s1;
            taddr2 = (tbase2 << 3) + s0;
            taddr3 = (tbase2 << 3) + s1;
            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            uint32_t p;

            taddr0 &= 0xfff;
            taddr1 &= 0xfff;
            taddr2 &= 0xfff;
            taddr3 &= 0xfff;
            p = state[wid].tmem[taddr0];
            color0->r = p;
            color0->g = p;
            color0->b = p;
            color0->a = p;
            p = state[wid].tmem[taddr2];
            color2->r = p;
            color2->g = p;
            color2->b = p;
            color2->a = p;
            p = state[wid].tmem[taddr1];
            color1->r = p;
            color1->g = p;
            color1->b = p;
            color1->a = p;
            p = state[wid].tmem[taddr3];
            color3->r = p;
            color3->g = p;
            color3->b = p;
            color3->a = p;
        }
        break;
    case TEXEL_RGBA16:
        {
            taddr0 = (tbase0 << 2) + s0;
            taddr1 = (tbase0 << 2) + s1;
            taddr2 = (tbase2 << 2) + s0;
            taddr3 = (tbase2 << 2) + s1;
            xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            uint32_t c0, c1, c2, c3;

            taddr0 &= 0x7ff;
            taddr1 &= 0x7ff;
            taddr2 &= 0x7ff;
            taddr3 &= 0x7ff;
            c0 = tc16[taddr0];
            c1 = tc16[taddr1];
            c2 = tc16[taddr2];
            c3 = tc16[taddr3];
            color0->r = GET_HI_RGBA16_TMEM(c0);
            color0->g = GET_MED_RGBA16_TMEM(c0);
            color0->b = GET_LOW_RGBA16_TMEM(c0);
            color0->a = (c0 & 1) ? 0xff : 0;
            color1->r = GET_HI_RGBA16_TMEM(c1);
            color1->g = GET_MED_RGBA16_TMEM(c1);
            color1->b = GET_LOW_RGBA16_TMEM(c1);
            color1->a = (c1 & 1) ? 0xff : 0;
            color2->r = GET_HI_RGBA16_TMEM(c2);
            color2->g = GET_MED_RGBA16_TMEM(c2);
            color2->b = GET_LOW_RGBA16_TMEM(c2);
            color2->a = (c2 & 1) ? 0xff : 0;
            color3->r = GET_HI_RGBA16_TMEM(c3);
            color3->g = GET_MED_RGBA16_TMEM(c3);
            color3->b = GET_LOW_RGBA16_TMEM(c3);
            color3->a = (c3 & 1) ? 0xff : 0;
        }
        break;
    case TEXEL_RGBA32:
        {
            taddr0 = (tbase0 << 2) + s0;
            taddr1 = (tbase0 << 2) + s1;
            taddr2 = (tbase2 << 2) + s0;
            taddr3 = (tbase2 << 2) + s1;
            xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            uint16_t c0, c1, c2, c3;

            taddr0 &= 0x3ff;
            taddr1 &= 0x3ff;
            taddr2 &= 0x3ff;
            taddr3 &= 0x3ff;
            c0 = tc16[taddr0];
            color0->r = c0 >> 8;
            color0->g = c0 & 0xff;
            c0 = tc16[taddr0 | 0x400];
            color0->b = c0 >>  8;
            color0->a = c0 & 0xff;
            c1 = tc16[taddr1];
            color1->r = c1 >> 8;
            color1->g = c1 & 0xff;
            c1 = tc16[taddr1 | 0x400];
            color1->b = c1 >>  8;
            color1->a = c1 & 0xff;
            c2 = tc16[taddr2];
            color2->r = c2 >> 8;
            color2->g = c2 & 0xff;
            c2 = tc16[taddr2 | 0x400];
            color2->b = c2 >>  8;
            color2->a = c2 & 0xff;
            c3 = tc16[taddr3];
            color3->r = c3 >> 8;
            color3->g = c3 & 0xff;
            c3 = tc16[taddr3 | 0x400];
            color3->b = c3 >>  8;
            color3->a = c3 & 0xff;
        }
        break;
    case TEXEL_YUV4:
        {
            taddr0 = (tbase0 << 3) + s0;
            taddr1 = (tbase0 << 3) + s1 + sdiff;
            taddr2 = (tbase2 << 3) + s0;
            taddr3 = (tbase2 << 3) + s1 + sdiff;

            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;

            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            int32_t u0, u1, u2, u3, save0, save1, save2, save3;

            save0 = state[wid].tmem[taddr0 & 0x7ff];
            save0 &= 0xf0;
            save0 |= (save0 >> 4);
            u0 = save0 - 0x80;

            save1 = state[wid].tmem[taddr1 & 0x7ff];
            save1 &= 0xf0;
            save1 |= (save1 >> 4);
            u1 = save1 - 0x80;

            save2 = state[wid].tmem[taddr2 & 0x7ff];
            save2 &= 0xf0;
            save2 |= (save2 >> 4);
            u2 = save2 - 0x80;

            save3 = state[wid].tmem[taddr3 & 0x7ff];
            save3 &= 0xf0;
            save3 |= (save3 >> 4);
            u3 = save3 - 0x80;

            color0->r = u0;
            color0->g = u0;
            color1->r = u1;
            color1->g = u1;
            color2->r = u2;
            color2->g = u2;
            color3->r = u3;
            color3->g = u3;

            if (unequaluppers)
            {
                color0->b = color0->a = save3;
                color1->b = color1->a = save2;
                color2->b = color2->a = save1;
                color3->b = color3->a = save0;
            }
            else
            {
                color0->b = color0->a = save0;
                color1->b = color1->a = save1;
                color2->b = color2->a = save2;
                color3->b = color3->a = save3;
            }
        }
        break;
    case TEXEL_YUV8:
        {
            taddr0 = (tbase0 << 3) + s0;
            taddr1 = (tbase0 << 3) + s1 + sdiff;
            taddr2 = (tbase2 << 3) + s0;
            taddr3 = (tbase2 << 3) + s1 + sdiff;

            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            int32_t u0, u1, u2, u3, save0, save1, save2, save3;

            save0 = u0 = state[wid].tmem[taddr0 & 0x7ff];
            u0 = u0 - 0x80;
            save1 = u1 = state[wid].tmem[taddr1 & 0x7ff];
            u1 = u1 - 0x80;
            save2 = u2 = state[wid].tmem[taddr2 & 0x7ff];
            u2 = u2 - 0x80;
            save3 = u3 = state[wid].tmem[taddr3 & 0x7ff];
            u3 = u3 - 0x80;

            color0->r = u0;
            color0->g = u0;
            color1->r = u1;
            color1->g = u1;
            color2->r = u2;
            color2->g = u2;
            color3->r = u3;
            color3->g = u3;

            if (unequaluppers)
            {
                color0->b = color0->a = save3;
                color1->b = color1->a = save2;
                color2->b = color2->a = save1;
                color3->b = color3->a = save0;
            }
            else
            {
                color0->b = color0->a = save0;
                color1->b = color1->a = save1;
                color2->b = color2->a = save2;
                color3->b = color3->a = save3;
            }
        }
        break;
    case TEXEL_YUV16:
        {
            taddr0 = (tbase0 << 3) + s0;
            taddr1 = (tbase0 << 3) + s1;
            taddr2 = (tbase2 << 3) + s0;
            taddr3 = (tbase2 << 3) + s1;


            taddrlow0 = (taddr0) >> 1;
            taddrlow1 = (taddr1 + sdiff) >> 1;
            taddrlow2 = (taddr2) >> 1;
            taddrlow3 = (taddr3 + sdiff) >> 1;

            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;
            xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddrlow0 ^= xort;
            taddrlow1 ^= xort;
            xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddrlow2 ^= xort;
            taddrlow3 ^= xort;

            taddr0 &= 0x7ff;
            taddr1 &= 0x7ff;
            taddr2 &= 0x7ff;
            taddr3 &= 0x7ff;
            taddrlow0 &= 0x3ff;
            taddrlow1 &= 0x3ff;
            taddrlow2 &= 0x3ff;
            taddrlow3 &= 0x3ff;

            uint16_t c0, c1, c2, c3;
            int32_t y0, y1, y2, y3, u0, u1, u2, u3, v0, v1, v2, v3;

            c0 = tc16[taddrlow0];
            c1 = tc16[taddrlow1];
            c2 = tc16[taddrlow2];
            c3 = tc16[taddrlow3];

            y0 = state[wid].tmem[taddr0 | 0x800];
            u0 = c0 >> 8;
            v0 = c0 & 0xff;
            y1 = state[wid].tmem[taddr1 | 0x800];
            u1 = c1 >> 8;
            v1 = c1 & 0xff;
            y2 = state[wid].tmem[taddr2 | 0x800];
            u2 = c2 >> 8;
            v2 = c2 & 0xff;
            y3 = state[wid].tmem[taddr3 | 0x800];
            u3 = c3 >> 8;
            v3 = c3 & 0xff;

            u0 = u0 - 0x80;
            v0 = v0 - 0x80;
            u1 = u1 - 0x80;
            v1 = v1 - 0x80;
            u2 = u2 - 0x80;
            v2 = v2 - 0x80;
            u3 = u3 - 0x80;
            v3 = v3 - 0x80;

            color0->r = u0;
            color0->g = v0;
            color1->r = u1;
            color1->g = v1;
            color2->r = u2;
            color2->g = v2;
            color3->r = u3;
            color3->g = v3;

            color0->b = color0->a = y0;
            color1->b = color1->a = y1;
            color2->b = color2->a = y2;
            color3->b = color3->a = y3;
        }
        break;
    case TEXEL_YUV32:
        {
            uint16_t c0, c1, c2, c3;
            int32_t y0, y1, y2, y3, u0, u1, u2, u3, v0, v1, v2, v3;
            uint32_t xort0, xort1;

            taddr0 = (tbase0 << 3) + s0;
            taddr1 = (tbase0 << 3) + s1;
            taddr2 = (tbase2 << 3) + s0;
            taddr3 = (tbase2 << 3) + s1;

            taddrlow0 = (taddr0) >> 1;
            taddrlow1 = (taddr1 + sdiff) >> 1;
            taddrlow2 = (taddr2) >> 1;
            taddrlow3 = (taddr3 + sdiff) >> 1;

            xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddrlow0 ^= xort;
            taddrlow1 ^= xort;
            xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddrlow2 ^= xort;
            taddrlow3 ^= xort;

            taddrlow0 &= 0x3ff;
            taddrlow1 &= 0x3ff;
            taddrlow2 &= 0x3ff;
            taddrlow3 &= 0x3ff;

            c0 = tc16[taddrlow0];
            c1 = tc16[taddrlow1];
            c2 = tc16[taddrlow2];
            c3 = tc16[taddrlow3];

            u0 = c0 >> 8;
            v0 = c0 & 0xff;
            u1 = c1 >> 8;
            v1 = c1 & 0xff;
            u2 = c2 >> 8;
            v2 = c2 & 0xff;
            u3 = c3 >> 8;
            v3 = c3 & 0xff;

            u0 = u0 - 0x80;
            v0 = v0 - 0x80;
            u1 = u1 - 0x80;
            v1 = v1 - 0x80;
            u2 = u2 - 0x80;
            v2 = v2 - 0x80;
            u3 = u3 - 0x80;
            v3 = v3 - 0x80;

            color0->r = u0;
            color0->g = v0;
            color1->r = u1;
            color1->g = v1;
            color2->r = u2;
            color2->g = v2;
            color3->r = u3;
            color3->g = v3;

            xort0 = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            xort1 = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;

            if (s0 & 1)
            {
                taddr0 ^= xort0;
                taddr2 ^= xort1;

                taddr0 &= 0x7ff;
                taddr2 &= 0x7ff;

                y0 = state[wid].tmem[taddr0 | 0x800];
                y2 = state[wid].tmem[taddr2 | 0x800];

                color0->b = color0->a = y0;
                color2->b = color2->a = y2;
            }
            else
            {
                y0 = tc16[taddrlow0 | 0x400];
                y2 = tc16[taddrlow2 | 0x400];

                color0->b = y0 >> 8;
                color0->a = ((y0 >> 8) & 0xf) | (y0 & 0xf0);
                color2->b = y2 >> 8;
                color2->a = ((y2 >> 8) & 0xf) | (y2 & 0xf0);
            }

            if (s1 & 1)
            {
                taddr1 ^= xort0;
                taddr3 ^= xort1;

                taddr1 &= 0x7ff;
                taddr3 &= 0x7ff;

                y1 = state[wid].tmem[taddr1 | 0x800];
                y3 = state[wid].tmem[taddr3 | 0x800];

                color1->b = color1->a = y1;
                color3->b = color3->a = y3;
            }
            else
            {
                taddr1 ^= xort0;
                taddr3 ^= xort1;

                taddr1 = (taddr1 >> 1) & 0x3ff;
                taddr3 = (taddr3 >> 1) & 0x3ff;

                y1 = tc16[taddr1 | 0x400];
                y3 = tc16[taddr3 | 0x400];

                color1->b = y1 >> 8;
                color1->a = ((y1 >> 8) & 0xf) | (y1 & 0xf0);
                color3->b = y3 >> 8;
                color3->a = ((y3 >> 8) & 0xf) | (y3 & 0xf0);
            }
        }
        break;
    case TEXEL_CI4:
        {
            taddr0 = ((tbase0 << 4) + s0) >> 1;
            taddr1 = ((tbase0 << 4) + s1) >> 1;
            taddr2 = ((tbase2 << 4) + s0) >> 1;
            taddr3 = ((tbase2 << 4) + s1) >> 1;
            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            uint32_t p;

            taddr0 &= 0xfff;
            taddr1 &= 0xfff;
            taddr2 &= 0xfff;
            taddr3 &= 0xfff;
            ands = s0 & 1;
            p = state[wid].tmem[taddr0];
            p = (ands) ? (p & 0xf) : (p >> 4);
            p = (tpal << 4) | p;
            color0->r = color0->g = color0->b = color0->a = p;
            p = state[wid].tmem[taddr2];
            p = (ands) ? (p & 0xf) : (p >> 4);
            p = (tpal << 4) | p;
            color2->r = color2->g = color2->b = color2->a = p;

            ands = s1 & 1;
            p = state[wid].tmem[taddr1];
            p = (ands) ? (p & 0xf) : (p >> 4);
            p = (tpal << 4) | p;
            color1->r = color1->g = color1->b = color1->a = p;
            p = state[wid].tmem[taddr3];
            p = (ands) ? (p & 0xf) : (p >> 4);
            p = (tpal << 4) | p;
            color3->r = color3->g = color3->b = color3->a = p;
        }
        break;
    case TEXEL_CI8:
        {
            taddr0 = (tbase0 << 3) + s0;
            taddr1 = (tbase0 << 3) + s1;
            taddr2 = (tbase2 << 3) + s0;
            taddr3 = (tbase2 << 3) + s1;
            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            uint32_t p;

            taddr0 &= 0xfff;
            taddr1 &= 0xfff;
            taddr2 &= 0xfff;
            taddr3 &= 0xfff;
            p = state[wid].tmem[taddr0];
            color0->r = p;
            color0->g = p;
            color0->b = p;
            color0->a = p;
            p = state[wid].tmem[taddr2];
            color2->r = p;
            color2->g = p;
            color2->b = p;
            color2->a = p;
            p = state[wid].tmem[taddr1];
            color1->r = p;
            color1->g = p;
            color1->b = p;
            color1->a = p;
            p = state[wid].tmem[taddr3];
            color3->r = p;
            color3->g = p;
            color3->b = p;
            color3->a = p;
        }
        break;
    case TEXEL_CI16:
    case TEXEL_CI32:
        {
            taddr0 = (tbase0 << 2) + s0;
            taddr1 = (tbase0 << 2) + s1;
            taddr2 = (tbase2 << 2) + s0;
            taddr3 = (tbase2 << 2) + s1;
            xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            uint16_t c0, c1, c2, c3;

            taddr0 &= 0x7ff;
            taddr1 &= 0x7ff;
            taddr2 &= 0x7ff;
            taddr3 &= 0x7ff;
            c0 = tc16[taddr0];
            color0->r = c0 >> 8;
            color0->g = c0 & 0xff;
            color0->b = c0 >> 8;
            color0->a = c0 & 0xff;
            c1 = tc16[taddr1];
            color1->r = c1 >> 8;
            color1->g = c1 & 0xff;
            color1->b = c1 >> 8;
            color1->a = c1 & 0xff;
            c2 = tc16[taddr2];
            color2->r = c2 >> 8;
            color2->g = c2 & 0xff;
            color2->b = c2 >> 8;
            color2->a = c2 & 0xff;
            c3 = tc16[taddr3];
            color3->r = c3 >> 8;
            color3->g = c3 & 0xff;
            color3->b = c3 >> 8;
            color3->a = c3 & 0xff;
        }
        break;
    case TEXEL_IA4:
        {
            taddr0 = ((tbase0 << 4) + s0) >> 1;
            taddr1 = ((tbase0 << 4) + s1) >> 1;
            taddr2 = ((tbase2 << 4) + s0) >> 1;
            taddr3 = ((tbase2 << 4) + s1) >> 1;
            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            uint32_t p, i;

            taddr0 &= 0xfff;
            taddr1 &= 0xfff;
            taddr2 &= 0xfff;
            taddr3 &= 0xfff;
            ands = s0 & 1;
            p = state[wid].tmem[taddr0];
            p = ands ? (p & 0xf) : (p >> 4);
            i = p & 0xe;
            i = (i << 4) | (i << 1) | (i >> 2);
            color0->r = i;
            color0->g = i;
            color0->b = i;
            color0->a = (p & 0x1) ? 0xff : 0;
            p = state[wid].tmem[taddr2];
            p = ands ? (p & 0xf) : (p >> 4);
            i = p & 0xe;
            i = (i << 4) | (i << 1) | (i >> 2);
            color2->r = i;
            color2->g = i;
            color2->b = i;
            color2->a = (p & 0x1) ? 0xff : 0;

            ands = s1 & 1;
            p = state[wid].tmem[taddr1];
            p = ands ? (p & 0xf) : (p >> 4);
            i = p & 0xe;
            i = (i << 4) | (i << 1) | (i >> 2);
            color1->r = i;
            color1->g = i;
            color1->b = i;
            color1->a = (p & 0x1) ? 0xff : 0;
            p = state[wid].tmem[taddr3];
            p = ands ? (p & 0xf) : (p >> 4);
            i = p & 0xe;
            i = (i << 4) | (i << 1) | (i >> 2);
            color3->r = i;
            color3->g = i;
            color3->b = i;
            color3->a = (p & 0x1) ? 0xff : 0;
        }
        break;
    case TEXEL_IA8:
        {
            taddr0 = (tbase0 << 3) + s0;
            taddr1 = (tbase0 << 3) + s1;
            taddr2 = (tbase2 << 3) + s0;
            taddr3 = (tbase2 << 3) + s1;
            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            uint32_t p, i;

            taddr0 &= 0xfff;
            taddr1 &= 0xfff;
            taddr2 &= 0xfff;
            taddr3 &= 0xfff;
            p = state[wid].tmem[taddr0];
            i = p & 0xf0;
            i |= (i >> 4);
            color0->r = i;
            color0->g = i;
            color0->b = i;
            color0->a = ((p & 0xf) << 4) | (p & 0xf);
            p = state[wid].tmem[taddr1];
            i = p & 0xf0;
            i |= (i >> 4);
            color1->r = i;
            color1->g = i;
            color1->b = i;
            color1->a = ((p & 0xf) << 4) | (p & 0xf);
            p = state[wid].tmem[taddr2];
            i = p & 0xf0;
            i |= (i >> 4);
            color2->r = i;
            color2->g = i;
            color2->b = i;
            color2->a = ((p & 0xf) << 4) | (p & 0xf);
            p = state[wid].tmem[taddr3];
            i = p & 0xf0;
            i |= (i >> 4);
            color3->r = i;
            color3->g = i;
            color3->b = i;
            color3->a = ((p & 0xf) << 4) | (p & 0xf);
        }
        break;
    case TEXEL_IA16:
        {
            taddr0 = (tbase0 << 2) + s0;
            taddr1 = (tbase0 << 2) + s1;
            taddr2 = (tbase2 << 2) + s0;
            taddr3 = (tbase2 << 2) + s1;
            xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            uint16_t c0, c1, c2, c3;

            taddr0 &= 0x7ff;
            taddr1 &= 0x7ff;
            taddr2 &= 0x7ff;
            taddr3 &= 0x7ff;
            c0 = tc16[taddr0];
            color0->r = color0->g = color0->b = c0 >> 8;
            color0->a = c0 & 0xff;
            c1 = tc16[taddr1];
            color1->r = color1->g = color1->b = c1 >> 8;
            color1->a = c1 & 0xff;
            c2 = tc16[taddr2];
            color2->r = color2->g = color2->b = c2 >> 8;
            color2->a = c2 & 0xff;
            c3 = tc16[taddr3];
            color3->r = color3->g = color3->b = c3 >> 8;
            color3->a = c3 & 0xff;

        }
        break;
    case TEXEL_IA32:
        {
            taddr0 = (tbase0 << 2) + s0;
            taddr1 = (tbase0 << 2) + s1;
            taddr2 = (tbase2 << 2) + s0;
            taddr3 = (tbase2 << 2) + s1;
            xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            uint16_t c0, c1, c2, c3;

            taddr0 &= 0x7ff;
            taddr1 &= 0x7ff;
            taddr2 &= 0x7ff;
            taddr3 &= 0x7ff;
            c0 = tc16[taddr0];
            color0->r = c0 >> 8;
            color0->g = c0 & 0xff;
            color0->b = c0 >> 8;
            color0->a = c0 & 0xff;
            c1 = tc16[taddr1];
            color1->r = c1 >> 8;
            color1->g = c1 & 0xff;
            color1->b = c1 >> 8;
            color1->a = c1 & 0xff;
            c2 = tc16[taddr2];
            color2->r = c2 >> 8;
            color2->g = c2 & 0xff;
            color2->b = c2 >> 8;
            color2->a = c2 & 0xff;
            c3 = tc16[taddr3];
            color3->r = c3 >> 8;
            color3->g = c3 & 0xff;
            color3->b = c3 >> 8;
            color3->a = c3 & 0xff;

        }
        break;
    case TEXEL_I4:
        {
            taddr0 = ((tbase0 << 4) + s0) >> 1;
            taddr1 = ((tbase0 << 4) + s1) >> 1;
            taddr2 = ((tbase2 << 4) + s0) >> 1;
            taddr3 = ((tbase2 << 4) + s1) >> 1;
            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            uint32_t p, c0, c1, c2, c3;

            taddr0 &= 0xfff;
            taddr1 &= 0xfff;
            taddr2 &= 0xfff;
            taddr3 &= 0xfff;
            ands = s0 & 1;
            p = state[wid].tmem[taddr0];
            c0 = ands ? (p & 0xf) : (p >> 4);
            c0 |= (c0 << 4);
            color0->r = color0->g = color0->b = color0->a = c0;
            p = state[wid].tmem[taddr2];
            c2 = ands ? (p & 0xf) : (p >> 4);
            c2 |= (c2 << 4);
            color2->r = color2->g = color2->b = color2->a = c2;

            ands = s1 & 1;
            p = state[wid].tmem[taddr1];
            c1 = ands ? (p & 0xf) : (p >> 4);
            c1 |= (c1 << 4);
            color1->r = color1->g = color1->b = color1->a = c1;
            p = state[wid].tmem[taddr3];
            c3 = ands ? (p & 0xf) : (p >> 4);
            c3 |= (c3 << 4);
            color3->r = color3->g = color3->b = color3->a = c3;

        }
        break;
    case TEXEL_I8:
        {
            taddr0 = (tbase0 << 3) + s0;
            taddr1 = (tbase0 << 3) + s1;
            taddr2 = (tbase2 << 3) + s0;
            taddr3 = (tbase2 << 3) + s1;
            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            uint32_t p;

            taddr0 &= 0xfff;
            taddr1 &= 0xfff;
            taddr2 &= 0xfff;
            taddr3 &= 0xfff;

            p = state[wid].tmem[taddr0];
            color0->r = p;
            color0->g = p;
            color0->b = p;
            color0->a = p;
            p = state[wid].tmem[taddr1];
            color1->r = p;
            color1->g = p;
            color1->b = p;
            color1->a = p;
            p = state[wid].tmem[taddr2];
            color2->r = p;
            color2->g = p;
            color2->b = p;
            color2->a = p;
            p = state[wid].tmem[taddr3];
            color3->r = p;
            color3->g = p;
            color3->b = p;
            color3->a = p;
        }
        break;
    case TEXEL_I16:
    case TEXEL_I32:
    default:
        {
            taddr0 = (tbase0 << 2) + s0;
            taddr1 = (tbase0 << 2) + s1;
            taddr2 = (tbase2 << 2) + s0;
            taddr3 = (tbase2 << 2) + s1;
            xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            uint16_t c0, c1, c2, c3;

            taddr0 &= 0x7ff;
            taddr1 &= 0x7ff;
            taddr2 &= 0x7ff;
            taddr3 &= 0x7ff;
            c0 = tc16[taddr0];
            color0->r = c0 >> 8;
            color0->g = c0 & 0xff;
            color0->b = c0 >> 8;
            color0->a = c0 & 0xff;
            c1 = tc16[taddr1];
            color1->r = c1 >> 8;
            color1->g = c1 & 0xff;
            color1->b = c1 >> 8;
            color1->a = c1 & 0xff;
            c2 = tc16[taddr2];
            color2->r = c2 >> 8;
            color2->g = c2 & 0xff;
            color2->b = c2 >> 8;
            color2->a = c2 & 0xff;
            c3 = tc16[taddr3];
            color3->r = c3 >> 8;
            color3->g = c3 & 0xff;
            color3->b = c3 >> 8;
            color3->a = c3 & 0xff;
        }
        break;
    }
}


/* Fused fetch + bilinear lerp for the dominant texture path: RGBA16,
 * point-or-bilinear sampled without a TLUT, mid_texel off, convert
 * off (so center is never taken, rg and ba share the fractions and
 * the upper flag, and the two channel-pair lerps merge into one).
 * The TMEM address computation is equivalent to the
 * TEXEL_RGBA16 case of fetch_texel_quadro above; the four texel reads
 * stay scalar (TMEM is bank-swizzled), but the 5-to-8 replication -
 * replicated_rgba[i] is (i << 3) | (i >> 2), a memoized expansion -
 * and the triangular lerp run in lanes. The upper flag is uniform
 * across the four channels of one pixel, so operand selection is an
 * ordinary branch, not a mask. All intermediate products fit signed
 * 16-bit lanes: channels are at most 255, fractions at most 0x20, so
 * |f1*d1 + f2*d2 + 0x10| <= 16336. */
#if defined(AL_SIMD_SSE2)
/* Shared tail of the fused texture kernels: transpose texel-lane
 * channel vectors into per-texel RGBA vectors and run the triangular
 * lerp vertically. See the RGBA16 kernel below for the range
 * argument; all intermediates fit signed 16-bit lanes. */
static STRICTINLINE void texel_quad_transpose_lerp_simd(struct color* TEX, __m128i r8, __m128i g8, __m128i b8, __m128i a8, int sfrac, int tfrac, int upper)
{
    __m128i rg_lo = _mm_unpacklo_epi32(r8, g8);
    __m128i rg_hi = _mm_unpackhi_epi32(r8, g8);
    __m128i ba_lo = _mm_unpacklo_epi32(b8, a8);
    __m128i ba_hi = _mm_unpackhi_epi32(b8, a8);
    __m128i t0v = _mm_unpacklo_epi64(rg_lo, ba_lo);
    __m128i t1v = _mm_unpackhi_epi64(rg_lo, ba_lo);
    __m128i t2v = _mm_unpacklo_epi64(rg_hi, ba_hi);
    __m128i t3v = _mm_unpackhi_epi64(rg_hi, ba_hi);

    __m128i base, ta, tb, acc, res;
    int f1, f2;
    if (upper)
    {
        base = t3v;
        ta = t2v;
        tb = t1v;
        f1 = 0x20 - sfrac;
        f2 = 0x20 - tfrac;
    }
    else
    {
        base = t0v;
        ta = t1v;
        tb = t2v;
        f1 = sfrac;
        f2 = tfrac;
    }

    base = _mm_packs_epi32(base, base);
    ta = _mm_packs_epi32(ta, ta);
    tb = _mm_packs_epi32(tb, tb);

    acc = _mm_add_epi16(
        _mm_mullo_epi16(_mm_sub_epi16(ta, base), _mm_set1_epi16((short)f1)),
        _mm_mullo_epi16(_mm_sub_epi16(tb, base), _mm_set1_epi16((short)f2)));
    acc = _mm_add_epi16(acc, _mm_set1_epi16(0x10));
    acc = _mm_srai_epi16(acc, 5);
    acc = _mm_add_epi16(acc, base);

    res = _mm_srai_epi32(_mm_unpacklo_epi16(acc, acc), 16);
    _mm_storeu_si128((__m128i*)&TEX->r, res);
}

/* Center (mid_texel) tail: the scalar center formula
 *   t3 + ((((t1+t2)<<6) - (t3<<7) + ((~t3+t0)<<6) + 0xc0) >> 8)
 * folds, via ~t3 + t0 == t0 - t3 - 1, to
 *   t3 + ((t0 + t1 + t2 - 3*t3 + 2) >> 2)
 * which stays inside signed 16-bit lanes (|S|+2 <= 767) and lands in
 * [0,255], so the same pack/widen framing as the lerp tail applies
 * and the trailing &0x1ff remains a no-op. */
static STRICTINLINE void texel_quad_transpose_avg_simd(struct color* TEX, __m128i r8, __m128i g8, __m128i b8, __m128i a8)
{
    __m128i rg_lo = _mm_unpacklo_epi32(r8, g8);
    __m128i rg_hi = _mm_unpackhi_epi32(r8, g8);
    __m128i ba_lo = _mm_unpacklo_epi32(b8, a8);
    __m128i ba_hi = _mm_unpackhi_epi32(b8, a8);
    __m128i t0v = _mm_unpacklo_epi64(rg_lo, ba_lo);
    __m128i t1v = _mm_unpackhi_epi64(rg_lo, ba_lo);
    __m128i t2v = _mm_unpacklo_epi64(rg_hi, ba_hi);
    __m128i t3v = _mm_unpackhi_epi64(rg_hi, ba_hi);

    __m128i t0 = _mm_packs_epi32(t0v, t0v);
    __m128i t1 = _mm_packs_epi32(t1v, t1v);
    __m128i t2 = _mm_packs_epi32(t2v, t2v);
    __m128i t3 = _mm_packs_epi32(t3v, t3v);

    __m128i s3 = _mm_add_epi16(t3, _mm_add_epi16(t3, t3));
    __m128i sum = _mm_sub_epi16(_mm_add_epi16(_mm_add_epi16(t0, t1), t2), s3);
    __m128i avg = _mm_add_epi16(_mm_srai_epi16(_mm_add_epi16(sum, _mm_set1_epi16(2)), 2), t3);
    __m128i res = _mm_srai_epi32(_mm_unpacklo_epi16(avg, avg), 16);
    _mm_storeu_si128((__m128i*)&TEX->r, res);
}

static STRICTINLINE void texel_quad_transpose_finish_simd(struct color* TEX, __m128i r8, __m128i g8, __m128i b8, __m128i a8, int sfrac, int tfrac, int upper, int center)
{
    if (center)
        texel_quad_transpose_avg_simd(TEX, r8, g8, b8, a8);
    else
        texel_quad_transpose_lerp_simd(TEX, r8, g8, b8, a8, sfrac, tfrac, upper);
}

static STRICTINLINE void texture_quadro_lerp_rgba16_simd(uint32_t wid, struct color* TEX, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int sfrac, int tfrac, int upper, int center)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;
    int t1 = (t0 & 0xff) + tdiff;
    int s1 = s0 + sdiff;
    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t xort0 = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
    uint32_t xort2 = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
    uint32_t taddr0 = (((tbase0 << 2) + s0) ^ xort0) & 0x7ff;
    uint32_t taddr1 = (((tbase0 << 2) + s1) ^ xort0) & 0x7ff;
    uint32_t taddr2 = (((tbase2 << 2) + s0) ^ xort2) & 0x7ff;
    uint32_t taddr3 = (((tbase2 << 2) + s1) ^ xort2) & 0x7ff;

    __m128i v = _mm_set_epi32(tc16[taddr3], tc16[taddr2], tc16[taddr1], tc16[taddr0]);
    __m128i m5 = _mm_set1_epi32(0x1f);

    __m128i r5 = _mm_and_si128(_mm_srli_epi32(v, 11), m5);
    __m128i g5 = _mm_and_si128(_mm_srli_epi32(v, 6), m5);
    __m128i b5 = _mm_and_si128(_mm_srli_epi32(v, 1), m5);

    __m128i r8 = _mm_or_si128(_mm_slli_epi32(r5, 3), _mm_srli_epi32(r5, 2));
    __m128i g8 = _mm_or_si128(_mm_slli_epi32(g5, 3), _mm_srli_epi32(g5, 2));
    __m128i b8 = _mm_or_si128(_mm_slli_epi32(b5, 3), _mm_srli_epi32(b5, 2));
    __m128i one32 = _mm_set1_epi32(1);
    __m128i a8 = _mm_and_si128(_mm_cmpeq_epi32(_mm_and_si128(v, one32), one32), _mm_set1_epi32(0xff));

    texel_quad_transpose_finish_simd(TEX, r8, g8, b8, a8, sfrac, tfrac, upper, center);
}

/* Fused fetch + lerp for the byte texel formats: IA8 (high nibble is
 * intensity, low nibble alpha, both replicated to 8 bits) and I8
 * (the byte replicated across all four channels). The byte TMEM
 * addressing is equivalent to the TEXEL_IA8/TEXEL_I8 quadro
 * cases above; aliasing the channel vectors reuses the shared
 * transpose and lerp unchanged. */
static STRICTINLINE void texture_quadro_lerp_bytefmt_simd(uint32_t wid, struct color* TEX, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int sfrac, int tfrac, int upper, int is_ia8, int center)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;
    int t1 = (t0 & 0xff) + tdiff;
    int s1 = s0 + sdiff;
    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t xort0 = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t xort2 = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t taddr0 = (((tbase0 << 3) + s0) ^ xort0) & 0xfff;
    uint32_t taddr1 = (((tbase0 << 3) + s1) ^ xort0) & 0xfff;
    uint32_t taddr2 = (((tbase2 << 3) + s0) ^ xort2) & 0xfff;
    uint32_t taddr3 = (((tbase2 << 3) + s1) ^ xort2) & 0xfff;

    __m128i v = _mm_set_epi32(state[wid].tmem[taddr3], state[wid].tmem[taddr2],
                              state[wid].tmem[taddr1], state[wid].tmem[taddr0]);

    if (is_ia8)
    {
        __m128i ihi = _mm_and_si128(v, _mm_set1_epi32(0xf0));
        __m128i iv = _mm_or_si128(ihi, _mm_srli_epi32(ihi, 4));
        __m128i alo = _mm_and_si128(v, _mm_set1_epi32(0x0f));
        __m128i av = _mm_or_si128(_mm_slli_epi32(alo, 4), alo);
        texel_quad_transpose_finish_simd(TEX, iv, iv, iv, av, sfrac, tfrac, upper, center);
    }
    else
    {
        texel_quad_transpose_finish_simd(TEX, v, v, v, v, sfrac, tfrac, upper, center);
    }
}

/* Fused fetch + lerp for I4 and IA4: two texels per TMEM byte, the nibble
 * chosen by the texel's s parity. Texels 0 and 2 share s0's parity
 * and texels 1 and 3 share s1's, so the per-lane nibble selection is
 * a blend between the high- and low-nibble vectors under a mask
 * built from two scalars. The replicated intensity feeds all four
 * channels, so the shared tail is called with the same vector in
 * every channel slot, exactly as for I8. The TMEM addressing is
 * equivalent to the TEXEL_I4 quadro case above. */
static STRICTINLINE void texture_quadro_lerp_i4_simd(uint32_t wid, struct color* TEX, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int sfrac, int tfrac, int upper, int is_ia4, int center)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;
    int t1 = (t0 & 0xff) + tdiff;
    int s1 = s0 + sdiff;
    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t xort0 = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t xort2 = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t taddr0 = ((((tbase0 << 4) + s0) >> 1) ^ xort0) & 0xfff;
    uint32_t taddr1 = ((((tbase0 << 4) + s1) >> 1) ^ xort0) & 0xfff;
    uint32_t taddr2 = ((((tbase2 << 4) + s0) >> 1) ^ xort2) & 0xfff;
    uint32_t taddr3 = ((((tbase2 << 4) + s1) >> 1) ^ xort2) & 0xfff;

    __m128i v = _mm_set_epi32(state[wid].tmem[taddr3], state[wid].tmem[taddr2],
                              state[wid].tmem[taddr1], state[wid].tmem[taddr0]);

    /* lane mask is all-ones where the low nibble is selected */
    __m128i msk = _mm_set_epi32(-(int)(s1 & 1), -(int)(s0 & 1), -(int)(s1 & 1), -(int)(s0 & 1));
    __m128i lo = _mm_and_si128(v, _mm_set1_epi32(0x0f));
    __m128i hi = _mm_srli_epi32(v, 4);
    __m128i c = _mm_or_si128(_mm_and_si128(lo, msk), _mm_andnot_si128(msk, hi));

    if (is_ia4)
    {
        __m128i i3 = _mm_and_si128(c, _mm_set1_epi32(0xe));
        __m128i iv = _mm_or_si128(_mm_or_si128(_mm_slli_epi32(i3, 4), _mm_slli_epi32(i3, 1)), _mm_srli_epi32(i3, 2));
        __m128i one32 = _mm_set1_epi32(1);
        __m128i av = _mm_and_si128(_mm_cmpeq_epi32(_mm_and_si128(c, one32), one32), _mm_set1_epi32(0xff));
        texel_quad_transpose_finish_simd(TEX, iv, iv, iv, av, sfrac, tfrac, upper, center);
    }
    else
    {
        __m128i iv = _mm_or_si128(c, _mm_slli_epi32(c, 4));
        texel_quad_transpose_finish_simd(TEX, iv, iv, iv, iv, sfrac, tfrac, upper, center);
    }
}

/* Fused fetch + lerp for IA16: 16-bit texels addressed exactly like
 * RGBA16 (the taddr math is equivalent to the TEXEL_IA16
 * quadro case above); the high byte is intensity across r/g/b and
 * the low byte is alpha, so the decode is two ops and the shared
 * transpose+lerp tail runs unchanged. */
static STRICTINLINE void texture_quadro_lerp_ia16_simd(uint32_t wid, struct color* TEX, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int sfrac, int tfrac, int upper, int center)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;
    int t1 = (t0 & 0xff) + tdiff;
    int s1 = s0 + sdiff;
    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t xort0 = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
    uint32_t xort2 = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
    uint32_t taddr0 = (((tbase0 << 2) + s0) ^ xort0) & 0x7ff;
    uint32_t taddr1 = (((tbase0 << 2) + s1) ^ xort0) & 0x7ff;
    uint32_t taddr2 = (((tbase2 << 2) + s0) ^ xort2) & 0x7ff;
    uint32_t taddr3 = (((tbase2 << 2) + s1) ^ xort2) & 0x7ff;

    __m128i v = _mm_set_epi32(tc16[taddr3], tc16[taddr2], tc16[taddr1], tc16[taddr0]);
    __m128i iv = _mm_srli_epi32(v, 8);
    __m128i av = _mm_and_si128(v, _mm_set1_epi32(0xff));

    texel_quad_transpose_finish_simd(TEX, iv, iv, iv, av, sfrac, tfrac, upper, center);
}

/* Fused fetch + lerp for RGBA32: split-bank 16-bit fetches (the
 * taddr math is equivalent to the TEXEL_RGBA32 quadro case
 * above, with the 0x3ff half-space mask); the low bank word carries
 * r/g and the high bank word (|0x400) carries b/a, giving four
 * independent channel vectors for the shared transpose+lerp tail. */
static STRICTINLINE void texture_quadro_lerp_rgba32_simd(uint32_t wid, struct color* TEX, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int sfrac, int tfrac, int upper, int center)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;
    int t1 = (t0 & 0xff) + tdiff;
    int s1 = s0 + sdiff;
    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t xort0 = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
    uint32_t xort2 = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
    uint32_t taddr0 = (((tbase0 << 2) + s0) ^ xort0) & 0x3ff;
    uint32_t taddr1 = (((tbase0 << 2) + s1) ^ xort0) & 0x3ff;
    uint32_t taddr2 = (((tbase2 << 2) + s0) ^ xort2) & 0x3ff;
    uint32_t taddr3 = (((tbase2 << 2) + s1) ^ xort2) & 0x3ff;

    __m128i vlo = _mm_set_epi32(tc16[taddr3], tc16[taddr2], tc16[taddr1], tc16[taddr0]);
    __m128i vhi = _mm_set_epi32(tc16[taddr3 | 0x400], tc16[taddr2 | 0x400],
                                tc16[taddr1 | 0x400], tc16[taddr0 | 0x400]);
    __m128i m8 = _mm_set1_epi32(0xff);

    __m128i r8 = _mm_srli_epi32(vlo, 8);
    __m128i g8 = _mm_and_si128(vlo, m8);
    __m128i b8 = _mm_srli_epi32(vhi, 8);
    __m128i a8 = _mm_and_si128(vhi, m8);

    texel_quad_transpose_finish_simd(TEX, r8, g8, b8, a8, sfrac, tfrac, upper, center);
}

/* Shared TLUT finish for the CI kernels: gather one palette entry per
 * replicated TLUT bank (texel n reads copy n, bank parity rotated by
 * isupperrg via the precomputed xor) and decode per tlut_type --
 * RGBA16 exactly as in texture_quadro_lerp_rgba16_simd (the
 * replicated_rgba table is (i << 3) | (i >> 2), so the shifted
 * replicate is bit-identical), or IA16 as in the IA16 kernel. Only
 * for non-YUV tiles (isupper == isupperrg), which the dispatch gate
 * guarantees; the crossed bank assignment stays scalar. */
static STRICTINLINE void texel_quad_tlut_finish_simd(uint32_t wid, struct color* TEX, uint32_t ta0, uint32_t ta1, uint32_t ta2, uint32_t ta3, uint32_t xorrg, int sfrac, int tfrac, int upper, int center)
{
    __m128i v = _mm_set_epi32(tlut[ta3 ^ xorrg], tlut[ta2 ^ xorrg],
                              tlut[ta1 ^ xorrg], tlut[ta0 ^ xorrg]);

    if (!state[wid].other_modes.tlut_type)
    {
        __m128i m5 = _mm_set1_epi32(0x1f);
        __m128i r5 = _mm_and_si128(_mm_srli_epi32(v, 11), m5);
        __m128i g5 = _mm_and_si128(_mm_srli_epi32(v, 6), m5);
        __m128i b5 = _mm_and_si128(_mm_srli_epi32(v, 1), m5);
        __m128i r8 = _mm_or_si128(_mm_slli_epi32(r5, 3), _mm_srli_epi32(r5, 2));
        __m128i g8 = _mm_or_si128(_mm_slli_epi32(g5, 3), _mm_srli_epi32(g5, 2));
        __m128i b8 = _mm_or_si128(_mm_slli_epi32(b5, 3), _mm_srli_epi32(b5, 2));
        __m128i one32 = _mm_set1_epi32(1);
        __m128i a8 = _mm_and_si128(_mm_cmpeq_epi32(_mm_and_si128(v, one32), one32), _mm_set1_epi32(0xff));
        texel_quad_transpose_finish_simd(TEX, r8, g8, b8, a8, sfrac, tfrac, upper, center);
    }
    else
    {
        __m128i iv = _mm_srli_epi32(v, 8);
        __m128i av = _mm_and_si128(v, _mm_set1_epi32(0xff));
        texel_quad_transpose_finish_simd(TEX, iv, iv, iv, av, sfrac, tfrac, upper, center);
    }
}

/* Fused fetch + lerp for CI8 under a TLUT (tlutswitch 4..6): the
 * byte index fetch is equivalent to the corresponding
 * fetch_texel_entlut_quadro case (including the 0x7ff low-half
 * fetch mask and per-texel TLUT lane offsets); the palette decode
 * and lerp run through the shared TLUT finish. */
static STRICTINLINE void texture_quadro_lerp_ci8_simd(uint32_t wid, struct color* TEX, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int sfrac, int tfrac, int upper, int center, int isupperrg)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;
    int t1 = (t0 & 0xff) + tdiff;
    int s1 = s0 + sdiff;
    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t xort0 = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t xort2 = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t taddr0 = (((tbase0 << 3) + s0) ^ xort0) & 0x7ff;
    uint32_t taddr1 = (((tbase0 << 3) + s1) ^ xort0) & 0x7ff;
    uint32_t taddr2 = (((tbase2 << 3) + s0) ^ xort2) & 0x7ff;
    uint32_t taddr3 = (((tbase2 << 3) + s1) ^ xort2) & 0x7ff;
    uint32_t ta0 = (uint32_t)state[wid].tmem[taddr0] << 2;
    uint32_t ta1 = ((uint32_t)state[wid].tmem[taddr1] << 2) + 1;
    uint32_t ta2 = ((uint32_t)state[wid].tmem[taddr2] << 2) + 2;
    uint32_t ta3 = ((uint32_t)state[wid].tmem[taddr3] << 2) + 3;
    uint32_t xorrg = isupperrg ? (WORD_ADDR_XOR ^ 3) : WORD_ADDR_XOR;

    texel_quad_tlut_finish_simd(wid, TEX, ta0, ta1, ta2, ta3, xorrg, sfrac, tfrac, upper, center);
}

/* Fused fetch + lerp for CI4 under a TLUT (tlutswitch 0..2): two
 * indices per TMEM byte, the nibble chosen by the texel's s parity
 * and merged with the tile palette; equivalent to the corresponding
 * fetch_texel_entlut_quadro case (including the 0x7ff low-half
 * fetch mask and per-texel TLUT lane offsets). The index fetch is
 * scalar; the palette decode and lerp run through the shared TLUT
 * finish, so this body is ISA-independent. */
static STRICTINLINE void texture_quadro_lerp_ci4_simd(uint32_t wid, struct color* TEX, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int sfrac, int tfrac, int upper, int center, int isupperrg)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;
    int t1 = (t0 & 0xff) + tdiff;
    int s1 = s0 + sdiff;
    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t tpal = state[wid].tile[tilenum].palette << 4;
    uint32_t xort0 = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t xort2 = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t taddr0 = ((((tbase0 << 4) + s0) >> 1) ^ xort0) & 0x7ff;
    uint32_t taddr1 = ((((tbase0 << 4) + s1) >> 1) ^ xort0) & 0x7ff;
    uint32_t taddr2 = ((((tbase2 << 4) + s0) >> 1) ^ xort2) & 0x7ff;
    uint32_t taddr3 = ((((tbase2 << 4) + s1) >> 1) ^ xort2) & 0x7ff;
    uint32_t p0 = state[wid].tmem[taddr0];
    uint32_t p1 = state[wid].tmem[taddr1];
    uint32_t p2 = state[wid].tmem[taddr2];
    uint32_t p3 = state[wid].tmem[taddr3];
    uint32_t c0 = (s0 & 1) ? (p0 & 0xf) : (p0 >> 4);
    uint32_t c1 = (s1 & 1) ? (p1 & 0xf) : (p1 >> 4);
    uint32_t c2 = (s0 & 1) ? (p2 & 0xf) : (p2 >> 4);
    uint32_t c3 = (s1 & 1) ? (p3 & 0xf) : (p3 >> 4);
    uint32_t ta0 = (tpal | c0) << 2;
    uint32_t ta1 = ((tpal | c1) << 2) + 1;
    uint32_t ta2 = ((tpal | c2) << 2) + 2;
    uint32_t ta3 = ((tpal | c3) << 2) + 3;
    uint32_t xorrg = isupperrg ? (WORD_ADDR_XOR ^ 3) : WORD_ADDR_XOR;

    texel_quad_tlut_finish_simd(wid, TEX, ta0, ta1, ta2, ta3, xorrg, sfrac, tfrac, upper, center);
}

/* Fused fetch + lerp for nearest-sampled TLUT CI textures: a single
 * index per pixel (CI4 nibble + palette merge, or CI8 byte), but the
 * hardware still reads one entry from each of the four replicated
 * TLUT banks at that index -- the bank copies are not guaranteed
 * coherent, so the quad is fetched for real and runs the real lerp
 * through the shared TLUT finish rather than collapsing to the
 * texel. Equivalent to fetch_texel_entlut_quadro_nearest for
 * tlutswitch 0..2 / 4..6 (note the unmasked t coordinate). The body
 * is ISA-independent. */
static STRICTINLINE void texture_quadro_lerp_cinear_simd(uint32_t wid, struct color* TEX, int s0, int t0, uint32_t tilenum, int sfrac, int tfrac, int upper, int center, int isupperrg, int is_ci4)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * t0 + state[wid].tile[tilenum].tmem;
    uint32_t xort0 = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t xorrg = isupperrg ? (WORD_ADDR_XOR ^ 3) : WORD_ADDR_XOR;
    uint32_t ta0;

    if (is_ci4)
    {
        uint32_t tpal = state[wid].tile[tilenum].palette << 4;
        uint32_t taddr0 = ((((tbase0 << 4) + s0) >> 1) ^ xort0) & 0x7ff;
        uint32_t p0 = state[wid].tmem[taddr0];
        uint32_t c0 = (s0 & 1) ? (p0 & 0xf) : (p0 >> 4);
        ta0 = (tpal | c0) << 2;
    }
    else
    {
        uint32_t taddr0 = (((tbase0 << 3) + s0) ^ xort0) & 0x7ff;
        ta0 = (uint32_t)state[wid].tmem[taddr0] << 2;
    }

    texel_quad_tlut_finish_simd(wid, TEX, ta0, ta0 + 1, ta0 + 2, ta0 + 3, xorrg, sfrac, tfrac, upper, center);
}

#elif defined(AL_SIMD_NEON)
/* Shared tail of the fused texture kernels: transpose texel-lane
 * channel vectors into per-texel RGBA vectors and run the triangular
 * lerp vertically in signed 16-bit lanes; see the SSE2 variant above
 * for the range argument. vtrnq pairs the even/odd lanes so that
 * combining low/high halves yields {r,g,b,a} per texel. */
static STRICTINLINE void texel_quad_transpose_lerp_simd(struct color* TEX, uint32x4_t r8, uint32x4_t g8, uint32x4_t b8, uint32x4_t a8, int sfrac, int tfrac, int upper)
{
    uint32x4x2_t rg = vtrnq_u32(r8, g8);
    uint32x4x2_t ba = vtrnq_u32(b8, a8);
    int32x4_t t0v = vreinterpretq_s32_u32(vcombine_u32(vget_low_u32(rg.val[0]), vget_low_u32(ba.val[0])));
    int32x4_t t1v = vreinterpretq_s32_u32(vcombine_u32(vget_low_u32(rg.val[1]), vget_low_u32(ba.val[1])));
    int32x4_t t2v = vreinterpretq_s32_u32(vcombine_u32(vget_high_u32(rg.val[0]), vget_high_u32(ba.val[0])));
    int32x4_t t3v = vreinterpretq_s32_u32(vcombine_u32(vget_high_u32(rg.val[1]), vget_high_u32(ba.val[1])));

    int32x4_t base, ta, tb;
    int16x4_t b16, a16, c16, acc;
    int f1, f2;
    if (upper)
    {
        base = t3v;
        ta = t2v;
        tb = t1v;
        f1 = 0x20 - sfrac;
        f2 = 0x20 - tfrac;
    }
    else
    {
        base = t0v;
        ta = t1v;
        tb = t2v;
        f1 = sfrac;
        f2 = tfrac;
    }

    b16 = vmovn_s32(base);
    a16 = vmovn_s32(ta);
    c16 = vmovn_s32(tb);

    acc = vadd_s16(
        vmul_s16(vsub_s16(a16, b16), vdup_n_s16((short)f1)),
        vmul_s16(vsub_s16(c16, b16), vdup_n_s16((short)f2)));
    acc = vadd_s16(acc, vdup_n_s16(0x10));
    acc = vshr_n_s16(acc, 5);
    acc = vadd_s16(acc, b16);

    vst1q_s32(&TEX->r, vmovl_s16(acc));
}

/* Center (mid_texel) tail: the scalar center formula
 *   t3 + ((((t1+t2)<<6) - (t3<<7) + ((~t3+t0)<<6) + 0xc0) >> 8)
 * folds, via ~t3 + t0 == t0 - t3 - 1, to
 *   t3 + ((t0 + t1 + t2 - 3*t3 + 2) >> 2)
 * which stays inside signed 16-bit lanes (|S|+2 <= 767) and lands in
 * [0,255], so the same pack/widen framing as the lerp tail applies
 * and the trailing &0x1ff remains a no-op. */
static STRICTINLINE void texel_quad_transpose_avg_simd(struct color* TEX, uint32x4_t r8, uint32x4_t g8, uint32x4_t b8, uint32x4_t a8)
{
    uint32x4x2_t rg = vtrnq_u32(r8, g8);
    uint32x4x2_t ba = vtrnq_u32(b8, a8);
    int32x4_t t0v = vreinterpretq_s32_u32(vcombine_u32(vget_low_u32(rg.val[0]), vget_low_u32(ba.val[0])));
    int32x4_t t1v = vreinterpretq_s32_u32(vcombine_u32(vget_low_u32(rg.val[1]), vget_low_u32(ba.val[1])));
    int32x4_t t2v = vreinterpretq_s32_u32(vcombine_u32(vget_high_u32(rg.val[0]), vget_high_u32(ba.val[0])));
    int32x4_t t3v = vreinterpretq_s32_u32(vcombine_u32(vget_high_u32(rg.val[1]), vget_high_u32(ba.val[1])));

    int16x4_t t0 = vmovn_s32(t0v);
    int16x4_t t1 = vmovn_s32(t1v);
    int16x4_t t2 = vmovn_s32(t2v);
    int16x4_t t3 = vmovn_s32(t3v);

    int16x4_t acc = vadd_s16(vadd_s16(t0, t1), t2);
    acc = vsub_s16(acc, vadd_s16(t3, vadd_s16(t3, t3)));
    acc = vadd_s16(acc, vdup_n_s16(2));
    acc = vshr_n_s16(acc, 2);
    acc = vadd_s16(acc, t3);

    vst1q_s32(&TEX->r, vmovl_s16(acc));
}

static STRICTINLINE void texel_quad_transpose_finish_simd(struct color* TEX, uint32x4_t r8, uint32x4_t g8, uint32x4_t b8, uint32x4_t a8, int sfrac, int tfrac, int upper, int center)
{
    if (center)
        texel_quad_transpose_avg_simd(TEX, r8, g8, b8, a8);
    else
        texel_quad_transpose_lerp_simd(TEX, r8, g8, b8, a8, sfrac, tfrac, upper);
}

static STRICTINLINE void texture_quadro_lerp_rgba16_simd(uint32_t wid, struct color* TEX, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int sfrac, int tfrac, int upper, int center)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;
    int t1 = (t0 & 0xff) + tdiff;
    int s1 = s0 + sdiff;
    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t xort0 = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
    uint32_t xort2 = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
    uint32_t taddr0 = (((tbase0 << 2) + s0) ^ xort0) & 0x7ff;
    uint32_t taddr1 = (((tbase0 << 2) + s1) ^ xort0) & 0x7ff;
    uint32_t taddr2 = (((tbase2 << 2) + s0) ^ xort2) & 0x7ff;
    uint32_t taddr3 = (((tbase2 << 2) + s1) ^ xort2) & 0x7ff;

    uint32x4_t v = vsetq_lane_u32(tc16[taddr3],
        vsetq_lane_u32(tc16[taddr2],
        vsetq_lane_u32(tc16[taddr1],
        vdupq_n_u32(tc16[taddr0]), 1), 2), 3);
    uint32x4_t m5 = vdupq_n_u32(0x1f);

    uint32x4_t r5 = vandq_u32(vshrq_n_u32(v, 11), m5);
    uint32x4_t g5 = vandq_u32(vshrq_n_u32(v, 6), m5);
    uint32x4_t b5 = vandq_u32(vshrq_n_u32(v, 1), m5);

    uint32x4_t r8 = vorrq_u32(vshlq_n_u32(r5, 3), vshrq_n_u32(r5, 2));
    uint32x4_t g8 = vorrq_u32(vshlq_n_u32(g5, 3), vshrq_n_u32(g5, 2));
    uint32x4_t b8 = vorrq_u32(vshlq_n_u32(b5, 3), vshrq_n_u32(b5, 2));
    uint32x4_t a8 = vandq_u32(vceqq_u32(vandq_u32(v, vdupq_n_u32(1)), vdupq_n_u32(1)), vdupq_n_u32(0xff));

    texel_quad_transpose_finish_simd(TEX, r8, g8, b8, a8, sfrac, tfrac, upper, center);
}

static STRICTINLINE void texture_quadro_lerp_bytefmt_simd(uint32_t wid, struct color* TEX, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int sfrac, int tfrac, int upper, int is_ia8, int center)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;
    int t1 = (t0 & 0xff) + tdiff;
    int s1 = s0 + sdiff;
    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t xort0 = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t xort2 = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t taddr0 = (((tbase0 << 3) + s0) ^ xort0) & 0xfff;
    uint32_t taddr1 = (((tbase0 << 3) + s1) ^ xort0) & 0xfff;
    uint32_t taddr2 = (((tbase2 << 3) + s0) ^ xort2) & 0xfff;
    uint32_t taddr3 = (((tbase2 << 3) + s1) ^ xort2) & 0xfff;

    uint32x4_t v = vsetq_lane_u32(state[wid].tmem[taddr3],
        vsetq_lane_u32(state[wid].tmem[taddr2],
        vsetq_lane_u32(state[wid].tmem[taddr1],
        vdupq_n_u32(state[wid].tmem[taddr0]), 1), 2), 3);

    if (is_ia8)
    {
        uint32x4_t ihi = vandq_u32(v, vdupq_n_u32(0xf0));
        uint32x4_t iv = vorrq_u32(ihi, vshrq_n_u32(ihi, 4));
        uint32x4_t alo = vandq_u32(v, vdupq_n_u32(0x0f));
        uint32x4_t av = vorrq_u32(vshlq_n_u32(alo, 4), alo);
        texel_quad_transpose_finish_simd(TEX, iv, iv, iv, av, sfrac, tfrac, upper, center);
    }
    else
    {
        texel_quad_transpose_finish_simd(TEX, v, v, v, v, sfrac, tfrac, upper, center);
    }
}


static STRICTINLINE void texture_quadro_lerp_i4_simd(uint32_t wid, struct color* TEX, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int sfrac, int tfrac, int upper, int is_ia4, int center)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;
    int t1 = (t0 & 0xff) + tdiff;
    int s1 = s0 + sdiff;
    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t xort0 = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t xort2 = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t taddr0 = ((((tbase0 << 4) + s0) >> 1) ^ xort0) & 0xfff;
    uint32_t taddr1 = ((((tbase0 << 4) + s1) >> 1) ^ xort0) & 0xfff;
    uint32_t taddr2 = ((((tbase2 << 4) + s0) >> 1) ^ xort2) & 0xfff;
    uint32_t taddr3 = ((((tbase2 << 4) + s1) >> 1) ^ xort2) & 0xfff;

    uint32x4_t v = vsetq_lane_u32(state[wid].tmem[taddr3],
        vsetq_lane_u32(state[wid].tmem[taddr2],
        vsetq_lane_u32(state[wid].tmem[taddr1],
        vdupq_n_u32(state[wid].tmem[taddr0]), 1), 2), 3);
    uint32x4_t m0 = vdupq_n_u32((s0 & 1) ? 0xffffffffu : 0);
    uint32x4_t m1 = vdupq_n_u32((s1 & 1) ? 0xffffffffu : 0);
    /* lanes 0/2 take s0's parity, lanes 1/3 take s1's */
    uint32x4_t msk = vtrnq_u32(m0, m1).val[0];

    uint32x4_t lo = vandq_u32(v, vdupq_n_u32(0x0f));
    uint32x4_t hi = vshrq_n_u32(v, 4);
    uint32x4_t c = vbslq_u32(msk, lo, hi);

    if (is_ia4)
    {
        uint32x4_t i3 = vandq_u32(c, vdupq_n_u32(0xe));
        uint32x4_t iv = vorrq_u32(vorrq_u32(vshlq_n_u32(i3, 4), vshlq_n_u32(i3, 1)), vshrq_n_u32(i3, 2));
        uint32x4_t av = vandq_u32(vceqq_u32(vandq_u32(c, vdupq_n_u32(1)), vdupq_n_u32(1)), vdupq_n_u32(0xff));
        texel_quad_transpose_finish_simd(TEX, iv, iv, iv, av, sfrac, tfrac, upper, center);
    }
    else
    {
        uint32x4_t iv = vorrq_u32(c, vshlq_n_u32(c, 4));
        texel_quad_transpose_finish_simd(TEX, iv, iv, iv, iv, sfrac, tfrac, upper, center);
    }
}

/* Fused fetch + lerp for IA16: 16-bit texels addressed exactly like
 * RGBA16 (the taddr math is equivalent to the TEXEL_IA16
 * quadro case above); the high byte is intensity across r/g/b and
 * the low byte is alpha, so the decode is two ops and the shared
 * transpose+lerp tail runs unchanged. */
static STRICTINLINE void texture_quadro_lerp_ia16_simd(uint32_t wid, struct color* TEX, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int sfrac, int tfrac, int upper, int center)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;
    int t1 = (t0 & 0xff) + tdiff;
    int s1 = s0 + sdiff;
    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t xort0 = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
    uint32_t xort2 = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
    uint32_t taddr0 = (((tbase0 << 2) + s0) ^ xort0) & 0x7ff;
    uint32_t taddr1 = (((tbase0 << 2) + s1) ^ xort0) & 0x7ff;
    uint32_t taddr2 = (((tbase2 << 2) + s0) ^ xort2) & 0x7ff;
    uint32_t taddr3 = (((tbase2 << 2) + s1) ^ xort2) & 0x7ff;

    uint32x4_t v = vsetq_lane_u32(tc16[taddr3],
        vsetq_lane_u32(tc16[taddr2],
        vsetq_lane_u32(tc16[taddr1],
        vdupq_n_u32(tc16[taddr0]), 1), 2), 3);
    uint32x4_t iv = vshrq_n_u32(v, 8);
    uint32x4_t av = vandq_u32(v, vdupq_n_u32(0xff));

    texel_quad_transpose_finish_simd(TEX, iv, iv, iv, av, sfrac, tfrac, upper, center);
}

/* Fused fetch + lerp for RGBA32: split-bank 16-bit fetches (the
 * taddr math is equivalent to the TEXEL_RGBA32 quadro case
 * above, with the 0x3ff half-space mask); the low bank word carries
 * r/g and the high bank word (|0x400) carries b/a, giving four
 * independent channel vectors for the shared transpose+lerp tail. */
static STRICTINLINE void texture_quadro_lerp_rgba32_simd(uint32_t wid, struct color* TEX, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int sfrac, int tfrac, int upper, int center)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;
    int t1 = (t0 & 0xff) + tdiff;
    int s1 = s0 + sdiff;
    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t xort0 = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
    uint32_t xort2 = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
    uint32_t taddr0 = (((tbase0 << 2) + s0) ^ xort0) & 0x3ff;
    uint32_t taddr1 = (((tbase0 << 2) + s1) ^ xort0) & 0x3ff;
    uint32_t taddr2 = (((tbase2 << 2) + s0) ^ xort2) & 0x3ff;
    uint32_t taddr3 = (((tbase2 << 2) + s1) ^ xort2) & 0x3ff;

    uint32x4_t vlo = vsetq_lane_u32(tc16[taddr3],
        vsetq_lane_u32(tc16[taddr2],
        vsetq_lane_u32(tc16[taddr1],
        vdupq_n_u32(tc16[taddr0]), 1), 2), 3);
    uint32x4_t vhi = vsetq_lane_u32(tc16[taddr3 | 0x400],
        vsetq_lane_u32(tc16[taddr2 | 0x400],
        vsetq_lane_u32(tc16[taddr1 | 0x400],
        vdupq_n_u32(tc16[taddr0 | 0x400]), 1), 2), 3);
    uint32x4_t m8 = vdupq_n_u32(0xff);

    uint32x4_t r8 = vshrq_n_u32(vlo, 8);
    uint32x4_t g8 = vandq_u32(vlo, m8);
    uint32x4_t b8 = vshrq_n_u32(vhi, 8);
    uint32x4_t a8 = vandq_u32(vhi, m8);

    texel_quad_transpose_finish_simd(TEX, r8, g8, b8, a8, sfrac, tfrac, upper, center);
}

/* Shared TLUT finish for the CI kernels: gather one palette entry per
 * replicated TLUT bank (texel n reads copy n, bank parity rotated by
 * isupperrg via the precomputed xor) and decode per tlut_type --
 * RGBA16 exactly as in texture_quadro_lerp_rgba16_simd (the
 * replicated_rgba table is (i << 3) | (i >> 2), so the shifted
 * replicate is bit-identical), or IA16 as in the IA16 kernel. Only
 * for non-YUV tiles (isupper == isupperrg), which the dispatch gate
 * guarantees; the crossed bank assignment stays scalar. */
static STRICTINLINE void texel_quad_tlut_finish_simd(uint32_t wid, struct color* TEX, uint32_t ta0, uint32_t ta1, uint32_t ta2, uint32_t ta3, uint32_t xorrg, int sfrac, int tfrac, int upper, int center)
{
    uint32x4_t v = vsetq_lane_u32(tlut[ta3 ^ xorrg],
        vsetq_lane_u32(tlut[ta2 ^ xorrg],
        vsetq_lane_u32(tlut[ta1 ^ xorrg],
        vdupq_n_u32(tlut[ta0 ^ xorrg]), 1), 2), 3);

    if (!state[wid].other_modes.tlut_type)
    {
        uint32x4_t m5 = vdupq_n_u32(0x1f);
        uint32x4_t r5 = vandq_u32(vshrq_n_u32(v, 11), m5);
        uint32x4_t g5 = vandq_u32(vshrq_n_u32(v, 6), m5);
        uint32x4_t b5 = vandq_u32(vshrq_n_u32(v, 1), m5);
        uint32x4_t r8 = vorrq_u32(vshlq_n_u32(r5, 3), vshrq_n_u32(r5, 2));
        uint32x4_t g8 = vorrq_u32(vshlq_n_u32(g5, 3), vshrq_n_u32(g5, 2));
        uint32x4_t b8 = vorrq_u32(vshlq_n_u32(b5, 3), vshrq_n_u32(b5, 2));
        uint32x4_t a8 = vandq_u32(vceqq_u32(vandq_u32(v, vdupq_n_u32(1)), vdupq_n_u32(1)), vdupq_n_u32(0xff));
        texel_quad_transpose_finish_simd(TEX, r8, g8, b8, a8, sfrac, tfrac, upper, center);
    }
    else
    {
        uint32x4_t iv = vshrq_n_u32(v, 8);
        uint32x4_t av = vandq_u32(v, vdupq_n_u32(0xff));
        texel_quad_transpose_finish_simd(TEX, iv, iv, iv, av, sfrac, tfrac, upper, center);
    }
}

/* Fused fetch + lerp for CI8 under a TLUT (tlutswitch 4..6): the
 * byte index fetch is equivalent to the corresponding
 * fetch_texel_entlut_quadro case (including the 0x7ff low-half
 * fetch mask and per-texel TLUT lane offsets); the palette decode
 * and lerp run through the shared TLUT finish. */
static STRICTINLINE void texture_quadro_lerp_ci8_simd(uint32_t wid, struct color* TEX, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int sfrac, int tfrac, int upper, int center, int isupperrg)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;
    int t1 = (t0 & 0xff) + tdiff;
    int s1 = s0 + sdiff;
    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t xort0 = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t xort2 = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t taddr0 = (((tbase0 << 3) + s0) ^ xort0) & 0x7ff;
    uint32_t taddr1 = (((tbase0 << 3) + s1) ^ xort0) & 0x7ff;
    uint32_t taddr2 = (((tbase2 << 3) + s0) ^ xort2) & 0x7ff;
    uint32_t taddr3 = (((tbase2 << 3) + s1) ^ xort2) & 0x7ff;
    uint32_t ta0 = (uint32_t)state[wid].tmem[taddr0] << 2;
    uint32_t ta1 = ((uint32_t)state[wid].tmem[taddr1] << 2) + 1;
    uint32_t ta2 = ((uint32_t)state[wid].tmem[taddr2] << 2) + 2;
    uint32_t ta3 = ((uint32_t)state[wid].tmem[taddr3] << 2) + 3;
    uint32_t xorrg = isupperrg ? (WORD_ADDR_XOR ^ 3) : WORD_ADDR_XOR;

    texel_quad_tlut_finish_simd(wid, TEX, ta0, ta1, ta2, ta3, xorrg, sfrac, tfrac, upper, center);
}

/* Fused fetch + lerp for CI4 under a TLUT (tlutswitch 0..2): two
 * indices per TMEM byte, the nibble chosen by the texel's s parity
 * and merged with the tile palette; equivalent to the corresponding
 * fetch_texel_entlut_quadro case (including the 0x7ff low-half
 * fetch mask and per-texel TLUT lane offsets). The index fetch is
 * scalar; the palette decode and lerp run through the shared TLUT
 * finish, so this body is ISA-independent. */
static STRICTINLINE void texture_quadro_lerp_ci4_simd(uint32_t wid, struct color* TEX, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int sfrac, int tfrac, int upper, int center, int isupperrg)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;
    int t1 = (t0 & 0xff) + tdiff;
    int s1 = s0 + sdiff;
    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t tpal = state[wid].tile[tilenum].palette << 4;
    uint32_t xort0 = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t xort2 = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t taddr0 = ((((tbase0 << 4) + s0) >> 1) ^ xort0) & 0x7ff;
    uint32_t taddr1 = ((((tbase0 << 4) + s1) >> 1) ^ xort0) & 0x7ff;
    uint32_t taddr2 = ((((tbase2 << 4) + s0) >> 1) ^ xort2) & 0x7ff;
    uint32_t taddr3 = ((((tbase2 << 4) + s1) >> 1) ^ xort2) & 0x7ff;
    uint32_t p0 = state[wid].tmem[taddr0];
    uint32_t p1 = state[wid].tmem[taddr1];
    uint32_t p2 = state[wid].tmem[taddr2];
    uint32_t p3 = state[wid].tmem[taddr3];
    uint32_t c0 = (s0 & 1) ? (p0 & 0xf) : (p0 >> 4);
    uint32_t c1 = (s1 & 1) ? (p1 & 0xf) : (p1 >> 4);
    uint32_t c2 = (s0 & 1) ? (p2 & 0xf) : (p2 >> 4);
    uint32_t c3 = (s1 & 1) ? (p3 & 0xf) : (p3 >> 4);
    uint32_t ta0 = (tpal | c0) << 2;
    uint32_t ta1 = ((tpal | c1) << 2) + 1;
    uint32_t ta2 = ((tpal | c2) << 2) + 2;
    uint32_t ta3 = ((tpal | c3) << 2) + 3;
    uint32_t xorrg = isupperrg ? (WORD_ADDR_XOR ^ 3) : WORD_ADDR_XOR;

    texel_quad_tlut_finish_simd(wid, TEX, ta0, ta1, ta2, ta3, xorrg, sfrac, tfrac, upper, center);
}

/* Fused fetch + lerp for nearest-sampled TLUT CI textures: a single
 * index per pixel (CI4 nibble + palette merge, or CI8 byte), but the
 * hardware still reads one entry from each of the four replicated
 * TLUT banks at that index -- the bank copies are not guaranteed
 * coherent, so the quad is fetched for real and runs the real lerp
 * through the shared TLUT finish rather than collapsing to the
 * texel. Equivalent to fetch_texel_entlut_quadro_nearest for
 * tlutswitch 0..2 / 4..6 (note the unmasked t coordinate). The body
 * is ISA-independent. */
static STRICTINLINE void texture_quadro_lerp_cinear_simd(uint32_t wid, struct color* TEX, int s0, int t0, uint32_t tilenum, int sfrac, int tfrac, int upper, int center, int isupperrg, int is_ci4)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * t0 + state[wid].tile[tilenum].tmem;
    uint32_t xort0 = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
    uint32_t xorrg = isupperrg ? (WORD_ADDR_XOR ^ 3) : WORD_ADDR_XOR;
    uint32_t ta0;

    if (is_ci4)
    {
        uint32_t tpal = state[wid].tile[tilenum].palette << 4;
        uint32_t taddr0 = ((((tbase0 << 4) + s0) >> 1) ^ xort0) & 0x7ff;
        uint32_t p0 = state[wid].tmem[taddr0];
        uint32_t c0 = (s0 & 1) ? (p0 & 0xf) : (p0 >> 4);
        ta0 = (tpal | c0) << 2;
    }
    else
    {
        uint32_t taddr0 = (((tbase0 << 3) + s0) ^ xort0) & 0x7ff;
        ta0 = (uint32_t)state[wid].tmem[taddr0] << 2;
    }

    texel_quad_tlut_finish_simd(wid, TEX, ta0, ta0 + 1, ta0 + 2, ta0 + 3, xorrg, sfrac, tfrac, upper, center);
}

#endif

static INLINE void fetch_texel_entlut_quadro(uint32_t wid, struct color *color0, struct color *color1, struct color *color2, struct color *color3, int s0, int sdiff, int t0, int tdiff, uint32_t tilenum, int isupper, int isupperrg)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * (t0 & 0xff) + state[wid].tile[tilenum].tmem;
    int t1 = (t0 & 0xff) + tdiff;
    int s1;

    uint32_t tbase2 = state[wid].tile[tilenum].line * t1 + state[wid].tile[tilenum].tmem;
    uint32_t tpal = state[wid].tile[tilenum].palette << 4;
    uint32_t xort, ands;

    uint32_t taddr0, taddr1, taddr2, taddr3;
    uint16_t c0, c1, c2, c3;




    uint32_t xorupperrg = isupperrg ? (WORD_ADDR_XOR ^ 3) : WORD_ADDR_XOR;



    switch(state[wid].tile[tilenum].f.tlutswitch)
    {
    case 0:
    case 1:
    case 2:
        {
            s1 = s0 + sdiff;
            taddr0 = ((tbase0 << 4) + s0) >> 1;
            taddr1 = ((tbase0 << 4) + s1) >> 1;
            taddr2 = ((tbase2 << 4) + s0) >> 1;
            taddr3 = ((tbase2 << 4) + s1) >> 1;
            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            ands = s0 & 1;
            c0 = state[wid].tmem[taddr0 & 0x7ff];
            c0 = (ands) ? (c0 & 0xf) : (c0 >> 4);
            taddr0 = (tpal | c0) << 2;
            c2 = state[wid].tmem[taddr2 & 0x7ff];
            c2 = (ands) ? (c2 & 0xf) : (c2 >> 4);
            taddr2 = ((tpal | c2) << 2) + 2;

            ands = s1 & 1;
            c1 = state[wid].tmem[taddr1 & 0x7ff];
            c1 = (ands) ? (c1 & 0xf) : (c1 >> 4);
            taddr1 = ((tpal | c1) << 2) + 1;
            c3 = state[wid].tmem[taddr3 & 0x7ff];
            c3 = (ands) ? (c3 & 0xf) : (c3 >> 4);
            taddr3 = ((tpal | c3) << 2) + 3;
        }
        break;
    case 3:
        {
            s1 = s0 + (sdiff << 1);
            taddr0 = (tbase0 << 3) + s0;
            taddr1 = (tbase0 << 3) + s1;
            taddr2 = (tbase2 << 3) + s0;
            taddr3 = (tbase2 << 3) + s1;

            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            c0 = state[wid].tmem[taddr0 & 0x7ff];
            c0 >>= 4;
            taddr0 = (tpal | c0) << 2;
            c2 = state[wid].tmem[taddr2 & 0x7ff];
            c2 >>= 4;
            taddr2 = ((tpal | c2) << 2) + 2;

            c1 = state[wid].tmem[taddr1 & 0x7ff];
            c1 >>= 4;
            taddr1 = ((tpal | c1) << 2) + 1;
            c3 = state[wid].tmem[taddr3 & 0x7ff];
            c3 >>= 4;
            taddr3 = ((tpal | c3) << 2) + 3;
        }
        break;
    case 4:
    case 5:
    case 6:
        {
            s1 = s0 + sdiff;
            taddr0 = (tbase0 << 3) + s0;
            taddr1 = (tbase0 << 3) + s1;
            taddr2 = (tbase2 << 3) + s0;
            taddr3 = (tbase2 << 3) + s1;

            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            c0 = state[wid].tmem[taddr0 & 0x7ff];
            taddr0 = c0 << 2;
            c2 = state[wid].tmem[taddr2 & 0x7ff];
            taddr2 = (c2 << 2) + 2;
            c1 = state[wid].tmem[taddr1 & 0x7ff];
            taddr1 = (c1 << 2) + 1;
            c3 = state[wid].tmem[taddr3 & 0x7ff];
            taddr3 = (c3 << 2) + 3;
        }
        break;
    case 7:
        {
            s1 = s0 + (sdiff << 1);
            taddr0 = (tbase0 << 3) + s0;
            taddr1 = (tbase0 << 3) + s1;
            taddr2 = (tbase2 << 3) + s0;
            taddr3 = (tbase2 << 3) + s1;

            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            c0 = state[wid].tmem[taddr0 & 0x7ff];
            taddr0 = c0 << 2;
            c2 = state[wid].tmem[taddr2 & 0x7ff];
            taddr2 = (c2 << 2) + 2;
            c1 = state[wid].tmem[taddr1 & 0x7ff];
            taddr1 = (c1 << 2) + 1;
            c3 = state[wid].tmem[taddr3 & 0x7ff];
            taddr3 = (c3 << 2) + 3;
        }
        break;
    case 8:
    case 9:
    case 10:
        {
            s1 = s0 + sdiff;
            taddr0 = (tbase0 << 2) + s0;
            taddr1 = (tbase0 << 2) + s1;
            taddr2 = (tbase2 << 2) + s0;
            taddr3 = (tbase2 << 2) + s1;
            xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            c0 = tc16[taddr0 & 0x3ff];
            taddr0 = (c0 >> 6) & ~3;
            c1 = tc16[taddr1 & 0x3ff];
            taddr1 = ((c1 >> 6) & ~3) + 1;
            c2 = tc16[taddr2 & 0x3ff];
            taddr2 = ((c2 >> 6) & ~3) + 2;
            c3 = tc16[taddr3 & 0x3ff];
            taddr3 = (c3 >> 6) | 3;
        }
        break;
    case 11:
        {
            s1 = s0 + (sdiff << 1);
            taddr0 = (tbase0 << 3) + s0;
            taddr1 = (tbase0 << 3) + s1;
            taddr2 = (tbase2 << 3) + s0;
            taddr3 = (tbase2 << 3) + s1;

            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            c0 = state[wid].tmem[taddr0 & 0x7ff];
            taddr0 = c0 << 2;
            c2 = state[wid].tmem[taddr2 & 0x7ff];
            taddr2 = (c2 << 2) + 2;
            c1 = state[wid].tmem[taddr1 & 0x7ff];
            taddr1 = (c1 << 2) + 1;
            c3 = state[wid].tmem[taddr3 & 0x7ff];
            taddr3 = (c3 << 2) + 3;
        }
        break;
    case 12:
    case 13:
    case 14:
        {
            s1 = s0 + sdiff;
            taddr0 = (tbase0 << 2) + s0;
            taddr1 = (tbase0 << 2) + s1;
            taddr2 = (tbase2 << 2) + s0;
            taddr3 = (tbase2 << 2) + s1;

            xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            c0 = tc16[taddr0 & 0x3ff];
            taddr0 = (c0 >> 6) & ~3;
            c1 = tc16[taddr1 & 0x3ff];
            taddr1 = ((c1 >> 6) & ~3) + 1;
            c2 = tc16[taddr2 & 0x3ff];
            taddr2 = ((c2 >> 6) & ~3) + 2;
            c3 = tc16[taddr3 & 0x3ff];
            taddr3 = (c3 >> 6) | 3;
        }
        break;
    case 15:
    default:
        {
            s1 = s0 + (sdiff << 1);
            taddr0 = (tbase0 << 3) + s0;
            taddr1 = (tbase0 << 3) + s1;
            taddr2 = (tbase2 << 3) + s0;
            taddr3 = (tbase2 << 3) + s1;

            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;
            taddr1 ^= xort;
            xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr2 ^= xort;
            taddr3 ^= xort;

            c0 = state[wid].tmem[taddr0 & 0x7ff];
            taddr0 = c0 << 2;
            c2 = state[wid].tmem[taddr2 & 0x7ff];
            taddr2 = (c2 << 2) + 2;
            c1 = state[wid].tmem[taddr1 & 0x7ff];
            taddr1 = (c1 << 2) + 1;
            c3 = state[wid].tmem[taddr3 & 0x7ff];
            taddr3 = (c3 << 2) + 3;
        }
        break;
    }

    c0 = tlut[taddr0 ^ xorupperrg];
    c2 = tlut[taddr2 ^ xorupperrg];
    c1 = tlut[taddr1 ^ xorupperrg];
    c3 = tlut[taddr3 ^ xorupperrg];

    if (!state[wid].other_modes.tlut_type)
    {
        color0->r = GET_HI_RGBA16_TMEM(c0);
        color0->g = GET_MED_RGBA16_TMEM(c0);
        color1->r = GET_HI_RGBA16_TMEM(c1);
        color1->g = GET_MED_RGBA16_TMEM(c1);
        color2->r = GET_HI_RGBA16_TMEM(c2);
        color2->g = GET_MED_RGBA16_TMEM(c2);
        color3->r = GET_HI_RGBA16_TMEM(c3);
        color3->g = GET_MED_RGBA16_TMEM(c3);

        if (isupper == isupperrg)
        {
            color0->b = GET_LOW_RGBA16_TMEM(c0);
            color0->a = (c0 & 1) ? 0xff : 0;
            color1->b = GET_LOW_RGBA16_TMEM(c1);
            color1->a = (c1 & 1) ? 0xff : 0;
            color2->b = GET_LOW_RGBA16_TMEM(c2);
            color2->a = (c2 & 1) ? 0xff : 0;
            color3->b = GET_LOW_RGBA16_TMEM(c3);
            color3->a = (c3 & 1) ? 0xff : 0;
        }
        else
        {
            color0->b = GET_LOW_RGBA16_TMEM(c3);
            color0->a = (c3 & 1) ? 0xff : 0;
            color1->b = GET_LOW_RGBA16_TMEM(c2);
            color1->a = (c2 & 1) ? 0xff : 0;
            color2->b = GET_LOW_RGBA16_TMEM(c1);
            color2->a = (c1 & 1) ? 0xff : 0;
            color3->b = GET_LOW_RGBA16_TMEM(c0);
            color3->a = (c0 & 1) ? 0xff : 0;
        }
    }
    else
    {
        color0->r = color0->g = c0 >> 8;
        color1->r = color1->g = c1 >> 8;
        color2->r = color2->g = c2 >> 8;
        color3->r = color3->g = c3 >> 8;

        if (isupper == isupperrg)
        {
            color0->b = c0 >> 8;
            color0->a = c0 & 0xff;
            color1->b = c1 >> 8;
            color1->a = c1 & 0xff;
            color2->b = c2 >> 8;
            color2->a = c2 & 0xff;
            color3->b = c3 >> 8;
            color3->a = c3 & 0xff;
        }
        else
        {
            color0->b = c3 >> 8;
            color0->a = c3 & 0xff;
            color1->b = c2 >> 8;
            color1->a = c2 & 0xff;
            color2->b = c1 >> 8;
            color2->a = c1 & 0xff;
            color3->b = c0 >> 8;
            color3->a = c0 & 0xff;
        }
    }
}

static INLINE void fetch_texel_entlut_quadro_nearest(uint32_t wid, struct color *color0, struct color *color1, struct color *color2, struct color *color3, int s0, int t0, uint32_t tilenum, int isupper, int isupperrg)
{
    uint32_t tbase0 = state[wid].tile[tilenum].line * t0 + state[wid].tile[tilenum].tmem;
    uint32_t tpal = state[wid].tile[tilenum].palette << 4;
    uint32_t xort, ands;

    uint32_t taddr0 = 0;
    uint16_t c0, c1, c2, c3;

    uint32_t xorupperrg = isupperrg ? (WORD_ADDR_XOR ^ 3) : WORD_ADDR_XOR;

    switch(state[wid].tile[tilenum].f.tlutswitch)
    {
    case 0:
    case 1:
    case 2:
        {
            taddr0 = ((tbase0 << 4) + s0) >> 1;
            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;

            ands = s0 & 1;
            c0 = state[wid].tmem[taddr0 & 0x7ff];
            c0 = (ands) ? (c0 & 0xf) : (c0 >> 4);

            taddr0 = (tpal | c0) << 2;
        }
        break;
    case 3:
        {
            taddr0 = (tbase0 << 3) + s0;
            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;

            c0 = state[wid].tmem[taddr0 & 0x7ff];

            c0 >>= 4;

            taddr0 = (tpal | c0) << 2;
        }
        break;
    case 4:
    case 5:
    case 6:
        {
            taddr0 = (tbase0 << 3) + s0;
            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;

            c0 = state[wid].tmem[taddr0 & 0x7ff];

            taddr0 = c0 << 2;
        }
        break;
    case 7:
        {
            taddr0 = (tbase0 << 3) + s0;
            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;

            c0 = state[wid].tmem[taddr0 & 0x7ff];

            taddr0 = c0 << 2;
        }
        break;
    case 8:
    case 9:
    case 10:
        {
            taddr0 = (tbase0 << 2) + s0;
            xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr0 ^= xort;

            c0 = tc16[taddr0 & 0x3ff];

            taddr0 = (c0 >> 6) & ~3;
        }
        break;
    case 11:
        {
            taddr0 = (tbase0 << 3) + s0;
            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;

            c0 = state[wid].tmem[taddr0 & 0x7ff];

            taddr0 = c0 << 2;
        }
        break;
    case 12:
    case 13:
    case 14:
        {
            taddr0 = (tbase0 << 2) + s0;
            xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
            taddr0 ^= xort;

            c0 = tc16[taddr0 & 0x3ff];

            taddr0 = (c0 >> 6) & ~3;
        }
        break;
    case 15:
    default:
        {
            taddr0 = (tbase0 << 3) + s0;
            xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
            taddr0 ^= xort;

            c0 = state[wid].tmem[taddr0 & 0x7ff];

            taddr0 = c0 << 2;
        }
        break;
    }

    c0 = tlut[taddr0 ^ xorupperrg];
    c1 = tlut[(taddr0 + 1) ^ xorupperrg];
    c2 = tlut[(taddr0 + 2) ^ xorupperrg];
    c3 = tlut[(taddr0 + 3) ^ xorupperrg];

    if (!state[wid].other_modes.tlut_type)
    {
        color0->r = GET_HI_RGBA16_TMEM(c0);
        color0->g = GET_MED_RGBA16_TMEM(c0);
        color1->r = GET_HI_RGBA16_TMEM(c1);
        color1->g = GET_MED_RGBA16_TMEM(c1);
        color2->r = GET_HI_RGBA16_TMEM(c2);
        color2->g = GET_MED_RGBA16_TMEM(c2);
        color3->r = GET_HI_RGBA16_TMEM(c3);
        color3->g = GET_MED_RGBA16_TMEM(c3);

        if (isupper == isupperrg)
        {
            color0->b = GET_LOW_RGBA16_TMEM(c0);
            color0->a = (c0 & 1) ? 0xff : 0;
            color1->b = GET_LOW_RGBA16_TMEM(c1);
            color1->a = (c1 & 1) ? 0xff : 0;
            color2->b = GET_LOW_RGBA16_TMEM(c2);
            color2->a = (c2 & 1) ? 0xff : 0;
            color3->b = GET_LOW_RGBA16_TMEM(c3);
            color3->a = (c3 & 1) ? 0xff : 0;
        }
        else
        {
            color0->b = GET_LOW_RGBA16_TMEM(c3);
            color0->a = (c3 & 1) ? 0xff : 0;
            color1->b = GET_LOW_RGBA16_TMEM(c2);
            color1->a = (c2 & 1) ? 0xff : 0;
            color2->b = GET_LOW_RGBA16_TMEM(c1);
            color2->a = (c1 & 1) ? 0xff : 0;
            color3->b = GET_LOW_RGBA16_TMEM(c0);
            color3->a = (c0 & 1) ? 0xff : 0;
        }
    }
    else
    {
        color0->r = color0->g = c0 >> 8;
        color1->r = color1->g = c1 >> 8;
        color2->r = color2->g = c2 >> 8;
        color3->r = color3->g = c3 >> 8;

        if (isupper == isupperrg)
        {
            color0->b = c0 >> 8;
            color0->a = c0 & 0xff;
            color1->b = c1 >> 8;
            color1->a = c1 & 0xff;
            color2->b = c2 >> 8;
            color2->a = c2 & 0xff;
            color3->b = c3 >> 8;
            color3->a = c3 & 0xff;
        }
        else
        {
            color0->b = c3 >> 8;
            color0->a = c3 & 0xff;
            color1->b = c2 >> 8;
            color1->a = c2 & 0xff;
            color2->b = c1 >> 8;
            color2->a = c1 & 0xff;
            color3->b = c0 >> 8;
            color3->a = c0 & 0xff;
        }
    }
}

static void get_tmem_idx(uint32_t wid, int s, int t, uint32_t tilenum, uint32_t* idx0, uint32_t* idx1, uint32_t* idx2, uint32_t* idx3, uint32_t* bit3flipped, uint32_t* hibit)
{
    uint32_t tbase = (state[wid].tile[tilenum].line * t) & 0x1ff;
    tbase += state[wid].tile[tilenum].tmem;
    uint32_t tsize = state[wid].tile[tilenum].size;
    uint32_t tformat = state[wid].tile[tilenum].format;
    uint32_t sshorts = 0;


    if (tsize == PIXEL_SIZE_8BIT || tformat == FORMAT_YUV)
        sshorts = s >> 1;
    else if (tsize >= PIXEL_SIZE_16BIT)
        sshorts = s;
    else
        sshorts = s >> 2;
    sshorts &= 0x7ff;

    *bit3flipped = ((sshorts & 2) != 0) ^ (t & 1);

    int tidx_a = ((tbase << 2) + sshorts) & 0x7fd;
    int tidx_b = (tidx_a + 1) & 0x7ff;
    int tidx_c = (tidx_a + 2) & 0x7ff;
    int tidx_d = (tidx_a + 3) & 0x7ff;

    *hibit = (tidx_a & 0x400) != 0;

    if (t & 1)
    {
        tidx_a ^= 2;
        tidx_b ^= 2;
        tidx_c ^= 2;
        tidx_d ^= 2;
    }


    sort_tmem_idx(idx0, tidx_a, tidx_b, tidx_c, tidx_d, 0);
    sort_tmem_idx(idx1, tidx_a, tidx_b, tidx_c, tidx_d, 1);
    sort_tmem_idx(idx2, tidx_a, tidx_b, tidx_c, tidx_d, 2);
    sort_tmem_idx(idx3, tidx_a, tidx_b, tidx_c, tidx_d, 3);
}

static void read_tmem_copy(uint32_t wid, int s, int s1, int s2, int s3, int t, uint32_t tilenum, uint32_t* sortshort, int* hibits, int* lowbits)
{
    uint32_t tbase = (state[wid].tile[tilenum].line * t) & 0x1ff;
    tbase += state[wid].tile[tilenum].tmem;
    uint32_t tsize = state[wid].tile[tilenum].size;
    uint32_t tformat = state[wid].tile[tilenum].format;
    uint32_t shbytes = 0, shbytes1 = 0, shbytes2 = 0, shbytes3 = 0;
    int32_t delta = 0;
    uint32_t sortidx[8];


    if (tsize == PIXEL_SIZE_8BIT || tformat == FORMAT_YUV)
    {
        shbytes = s << 1;
        shbytes1 = s1 << 1;
        shbytes2 = s2 << 1;
        shbytes3 = s3 << 1;
    }
    else if (tsize >= PIXEL_SIZE_16BIT)
    {
        shbytes = s << 2;
        shbytes1 = s1 << 2;
        shbytes2 = s2 << 2;
        shbytes3 = s3 << 2;
    }
    else
    {
        shbytes = s;
        shbytes1 = s1;
        shbytes2 = s2;
        shbytes3 = s3;
    }

    shbytes &= 0x1fff;
    shbytes1 &= 0x1fff;
    shbytes2 &= 0x1fff;
    shbytes3 &= 0x1fff;

    int tidx_a, tidx_blow, tidx_bhi, tidx_c, tidx_dlow, tidx_dhi;

    tbase <<= 4;
    tidx_a = (tbase + shbytes) & 0x1fff;
    tidx_bhi = (tbase + shbytes1) & 0x1fff;
    tidx_c = (tbase + shbytes2) & 0x1fff;
    tidx_dhi = (tbase + shbytes3) & 0x1fff;

    if (tformat == FORMAT_YUV)
    {
        delta = shbytes1 - shbytes;
        tidx_blow = (tidx_a + (delta << 1)) & 0x1fff;
        tidx_dlow = (tidx_blow + shbytes3 - shbytes) & 0x1fff;
    }
    else
    {
        tidx_blow = tidx_bhi;
        tidx_dlow = tidx_dhi;
    }

    if (t & 1)
    {
        tidx_a ^= 8;
        tidx_blow ^= 8;
        tidx_bhi ^= 8;
        tidx_c ^= 8;
        tidx_dlow ^= 8;
        tidx_dhi ^= 8;
    }

    hibits[0] = (tidx_a & 0x1000) != 0;
    hibits[1] = (tidx_blow & 0x1000) != 0;
    hibits[2] = (tidx_bhi & 0x1000) != 0;
    hibits[3] = (tidx_c & 0x1000) != 0;
    hibits[4] = (tidx_dlow & 0x1000) != 0;
    hibits[5] = (tidx_dhi & 0x1000) != 0;
    lowbits[0] = tidx_a & 0xf;
    lowbits[1] = tidx_blow & 0xf;
    lowbits[2] = tidx_bhi & 0xf;
    lowbits[3] = tidx_c & 0xf;
    lowbits[4] = tidx_dlow & 0xf;
    lowbits[5] = tidx_dhi & 0xf;

    uint32_t short0, short1, short2, short3;


    tidx_a >>= 2;
    tidx_blow >>= 2;
    tidx_bhi >>= 2;
    tidx_c >>= 2;
    tidx_dlow >>= 2;
    tidx_dhi >>= 2;


    sort_tmem_idx(&sortidx[0], tidx_a, tidx_blow, tidx_c, tidx_dlow, 0);
    sort_tmem_idx(&sortidx[1], tidx_a, tidx_blow, tidx_c, tidx_dlow, 1);
    sort_tmem_idx(&sortidx[2], tidx_a, tidx_blow, tidx_c, tidx_dlow, 2);
    sort_tmem_idx(&sortidx[3], tidx_a, tidx_blow, tidx_c, tidx_dlow, 3);

    short0 = tmem16[sortidx[0] ^ WORD_ADDR_XOR];
    short1 = tmem16[sortidx[1] ^ WORD_ADDR_XOR];
    short2 = tmem16[sortidx[2] ^ WORD_ADDR_XOR];
    short3 = tmem16[sortidx[3] ^ WORD_ADDR_XOR];


    sort_tmem_shorts_lowhalf(&sortshort[0], short0, short1, short2, short3, lowbits[0] >> 2);
    sort_tmem_shorts_lowhalf(&sortshort[1], short0, short1, short2, short3, lowbits[1] >> 2);
    sort_tmem_shorts_lowhalf(&sortshort[2], short0, short1, short2, short3, lowbits[3] >> 2);
    sort_tmem_shorts_lowhalf(&sortshort[3], short0, short1, short2, short3, lowbits[4] >> 2);

    if (state[wid].other_modes.en_tlut)
    {

        compute_color_index(wid, &short0, sortshort[0], lowbits[0] & 3, tilenum);
        compute_color_index(wid, &short1, sortshort[1], lowbits[1] & 3, tilenum);
        compute_color_index(wid, &short2, sortshort[2], lowbits[3] & 3, tilenum);
        compute_color_index(wid, &short3, sortshort[3], lowbits[4] & 3, tilenum);


        sortidx[4] = (short0 << 2);
        sortidx[5] = (short1 << 2) | 1;
        sortidx[6] = (short2 << 2) | 2;
        sortidx[7] = (short3 << 2) | 3;
    }
    else
    {
        sort_tmem_idx(&sortidx[4], tidx_a, tidx_bhi, tidx_c, tidx_dhi, 0);
        sort_tmem_idx(&sortidx[5], tidx_a, tidx_bhi, tidx_c, tidx_dhi, 1);
        sort_tmem_idx(&sortidx[6], tidx_a, tidx_bhi, tidx_c, tidx_dhi, 2);
        sort_tmem_idx(&sortidx[7], tidx_a, tidx_bhi, tidx_c, tidx_dhi, 3);
    }

    short0 = tmem16[(sortidx[4] | 0x400) ^ WORD_ADDR_XOR];
    short1 = tmem16[(sortidx[5] | 0x400) ^ WORD_ADDR_XOR];
    short2 = tmem16[(sortidx[6] | 0x400) ^ WORD_ADDR_XOR];
    short3 = tmem16[(sortidx[7] | 0x400) ^ WORD_ADDR_XOR];



    if (state[wid].other_modes.en_tlut)
    {
        sort_tmem_shorts_lowhalf(&sortshort[4], short0, short1, short2, short3, 0);
        sort_tmem_shorts_lowhalf(&sortshort[5], short0, short1, short2, short3, 1);
        sort_tmem_shorts_lowhalf(&sortshort[6], short0, short1, short2, short3, 2);
        sort_tmem_shorts_lowhalf(&sortshort[7], short0, short1, short2, short3, 3);
    }
    else
    {
        sort_tmem_shorts_lowhalf(&sortshort[4], short0, short1, short2, short3, lowbits[0] >> 2);
        sort_tmem_shorts_lowhalf(&sortshort[5], short0, short1, short2, short3, lowbits[2] >> 2);
        sort_tmem_shorts_lowhalf(&sortshort[6], short0, short1, short2, short3, lowbits[3] >> 2);
        sort_tmem_shorts_lowhalf(&sortshort[7], short0, short1, short2, short3, lowbits[5] >> 2);
    }
}

static void tmem_init_lut(void)
{
    int i;
    for (i = 0; i < 32; i++)
        replicated_rgba[i] = (i << 3) | ((i >> 2) & 7);
}
