static TLS struct
{
    int32_t *i1a_r[2];
    int32_t *i1a_g[2];
    int32_t *i1a_b[2];
    int32_t *i1b_a[2];
    int32_t *i2a_r[2];
    int32_t *i2a_g[2];
    int32_t *i2a_b[2];
    int32_t *i2b_a[2];
} blender;

static TLS struct color blend_color;
static TLS struct color fog_color;
static TLS struct color inv_pixel_color;
static TLS struct color blended_pixel_color;

static int32_t blenderone = 0xff;

static uint8_t bldiv_hwaccurate_table[0x8000];

static INLINE void set_blender_input(int cycle, int which, int32_t **input_r, int32_t **input_g, int32_t **input_b, int32_t **input_a, int a, int b)
{

    switch (a & 0x3)
    {
        case 0:
        {
            if (cycle == 0)
            {
                *input_r = &pixel_color.r;
                *input_g = &pixel_color.g;
                *input_b = &pixel_color.b;
            }
            else
            {
                *input_r = &blended_pixel_color.r;
                *input_g = &blended_pixel_color.g;
                *input_b = &blended_pixel_color.b;
            }
            break;
        }

        case 1:
        {
            *input_r = &memory_color.r;
            *input_g = &memory_color.g;
            *input_b = &memory_color.b;
            break;
        }

        case 2:
        {
            *input_r = &blend_color.r;      *input_g = &blend_color.g;      *input_b = &blend_color.b;
            break;
        }

        case 3:
        {
            *input_r = &fog_color.r;        *input_g = &fog_color.g;        *input_b = &fog_color.b;
            break;
        }
    }

    if (which == 0)
    {
        switch (b & 0x3)
        {
            case 0:     *input_a = &pixel_color.a; break;
            case 1:     *input_a = &fog_color.a; break;
            case 2:     *input_a = &shade_color.a; break;
            case 3:     *input_a = &zero_color; break;
        }
    }
    else
    {
        switch (b & 0x3)
        {
            case 0:     *input_a = &inv_pixel_color.a; break;
            case 1:     *input_a = &memory_color.a; break;
            case 2:     *input_a = &blenderone; break;
            case 3:     *input_a = &zero_color; break;
        }
    }
}

static STRICTINLINE int alpha_compare(int32_t comb_alpha)
{
    int32_t threshold;
    if (!other_modes.alpha_compare_en)
        return 1;
    else
    {
        if (!other_modes.dither_alpha_en)
            threshold = blend_color.a;
        else
            threshold = irand() & 0xff;


        if (comb_alpha >= threshold)
            return 1;
        else
            return 0;
    }
}

static STRICTINLINE void blender_equation_cycle0(int* r, int* g, int* b)
{
    int blend1a, blend2a;
    int blr, blg, blb, sum;
    blend1a = *blender.i1b_a[0] >> 3;
    blend2a = *blender.i2b_a[0] >> 3;

    int mulb;



    if (blender.i2b_a[0] == &memory_color.a)
    {
        blend1a = (blend1a >> blshifta) & 0x3C;
        blend2a = (blend2a >> blshiftb) | 3;
    }

    mulb = blend2a + 1;


    blr = (*blender.i1a_r[0]) * blend1a + (*blender.i2a_r[0]) * mulb;
    blg = (*blender.i1a_g[0]) * blend1a + (*blender.i2a_g[0]) * mulb;
    blb = (*blender.i1a_b[0]) * blend1a + (*blender.i2a_b[0]) * mulb;



    if (!other_modes.force_blend)
    {





        sum = ((blend1a & ~3) + (blend2a & ~3) + 4) << 9;
        *r = bldiv_hwaccurate_table[sum | ((blr >> 2) & 0x7ff)];
        *g = bldiv_hwaccurate_table[sum | ((blg >> 2) & 0x7ff)];
        *b = bldiv_hwaccurate_table[sum | ((blb >> 2) & 0x7ff)];
    }
    else
    {
        *r = (blr >> 5) & 0xff;
        *g = (blg >> 5) & 0xff;
        *b = (blb >> 5) & 0xff;
    }
}

static STRICTINLINE void blender_equation_cycle0_2(int* r, int* g, int* b)
{
    int blend1a, blend2a;
    blend1a = *blender.i1b_a[0] >> 3;
    blend2a = *blender.i2b_a[0] >> 3;

    if (blender.i2b_a[0] == &memory_color.a)
    {
        blend1a = (blend1a >> pastblshifta) & 0x3C;
        blend2a = (blend2a >> pastblshiftb) | 3;
    }

    blend2a += 1;
    *r = (((*blender.i1a_r[0]) * blend1a + (*blender.i2a_r[0]) * blend2a) >> 5) & 0xff;
    *g = (((*blender.i1a_g[0]) * blend1a + (*blender.i2a_g[0]) * blend2a) >> 5) & 0xff;
    *b = (((*blender.i1a_b[0]) * blend1a + (*blender.i2a_b[0]) * blend2a) >> 5) & 0xff;
}

static STRICTINLINE void blender_equation_cycle1(int* r, int* g, int* b)
{
    int blend1a, blend2a;
    int blr, blg, blb, sum;
    blend1a = *blender.i1b_a[1] >> 3;
    blend2a = *blender.i2b_a[1] >> 3;

    int mulb;
    if (blender.i2b_a[1] == &memory_color.a)
    {
        blend1a = (blend1a >> blshifta) & 0x3C;
        blend2a = (blend2a >> blshiftb) | 3;
    }

    mulb = blend2a + 1;
    blr = (*blender.i1a_r[1]) * blend1a + (*blender.i2a_r[1]) * mulb;
    blg = (*blender.i1a_g[1]) * blend1a + (*blender.i2a_g[1]) * mulb;
    blb = (*blender.i1a_b[1]) * blend1a + (*blender.i2a_b[1]) * mulb;

    if (!other_modes.force_blend)
    {
        sum = ((blend1a & ~3) + (blend2a & ~3) + 4) << 9;
        *r = bldiv_hwaccurate_table[sum | ((blr >> 2) & 0x7ff)];
        *g = bldiv_hwaccurate_table[sum | ((blg >> 2) & 0x7ff)];
        *b = bldiv_hwaccurate_table[sum | ((blb >> 2) & 0x7ff)];
    }
    else
    {
        *r = (blr >> 5) & 0xff;
        *g = (blg >> 5) & 0xff;
        *b = (blb >> 5) & 0xff;
    }
}

static STRICTINLINE int blender_1cycle(uint32_t* fr, uint32_t* fg, uint32_t* fb, int dith, uint32_t blend_en, uint32_t prewrap, uint32_t curpixel_cvg, uint32_t curpixel_cvbit)
{
    int r, g, b, dontblend;


    if (alpha_compare(pixel_color.a))
    {






        if (other_modes.antialias_en ? curpixel_cvg : curpixel_cvbit)
        {

            if (!other_modes.color_on_cvg || prewrap)
            {
                dontblend = (other_modes.f.partialreject_1cycle && pixel_color.a >= 0xff);
                if (!blend_en || dontblend)
                {
                    r = *blender.i1a_r[0];
                    g = *blender.i1a_g[0];
                    b = *blender.i1a_b[0];
                }
                else
                {
                    inv_pixel_color.a =  (~(*blender.i1b_a[0])) & 0xff;





                    blender_equation_cycle0(&r, &g, &b);
                }
            }
            else
            {
                r = *blender.i2a_r[0];
                g = *blender.i2a_g[0];
                b = *blender.i2a_b[0];
            }

            if (other_modes.rgb_dither_sel != 3)
                rgb_dither(&r, &g, &b, dith);

            *fr = r;
            *fg = g;
            *fb = b;
            return 1;
        }
        else
            return 0;
        }
    else
        return 0;
}

static STRICTINLINE int blender_2cycle(uint32_t* fr, uint32_t* fg, uint32_t* fb, int dith, uint32_t blend_en, uint32_t prewrap, uint32_t curpixel_cvg, uint32_t curpixel_cvbit, int32_t acalpha)
{
    int r, g, b, dontblend;


    if (alpha_compare(acalpha))
    {
        if (other_modes.antialias_en ? (curpixel_cvg) : (curpixel_cvbit))
        {

            inv_pixel_color.a =  (~(*blender.i1b_a[0])) & 0xff;
            blender_equation_cycle0_2(&r, &g, &b);


            memory_color = pre_memory_color;

            blended_pixel_color.r = r;
            blended_pixel_color.g = g;
            blended_pixel_color.b = b;
            blended_pixel_color.a = pixel_color.a;

            if (!other_modes.color_on_cvg || prewrap)
            {
                dontblend = (other_modes.f.partialreject_2cycle && pixel_color.a >= 0xff);
                if (!blend_en || dontblend)
                {
                    r = *blender.i1a_r[1];
                    g = *blender.i1a_g[1];
                    b = *blender.i1a_b[1];
                }
                else
                {
                    inv_pixel_color.a =  (~(*blender.i1b_a[1])) & 0xff;
                    blender_equation_cycle1(&r, &g, &b);
                }
            }
            else
            {
                r = *blender.i2a_r[1];
                g = *blender.i2a_g[1];
                b = *blender.i2a_b[1];
            }


            if (other_modes.rgb_dither_sel != 3)
                rgb_dither(&r, &g, &b, dith);
            *fr = r;
            *fg = g;
            *fb = b;
            return 1;
        }
        else
        {
            memory_color = pre_memory_color;
            return 0;
        }
    }
    else
    {
        memory_color = pre_memory_color;
        return 0;
    }
}

static void blender_init(void)
{
    int d = 0, n = 0, temp = 0, res = 0, invd = 0, nbit = 0;
    int ps[9];
    for (int i = 0; i < 0x8000; i++)
    {
        res = 0;
        d = (i >> 11) & 0xf;
        n = i & 0x7ff;
        invd = (~d) & 0xf;


        temp = invd + (n >> 8) + 1;
        ps[0] = temp & 7;
        for (int k = 0; k < 8; k++)
        {
            nbit = (n >> (7 - k)) & 1;
            if (res & (0x100 >> k))
                temp = invd + (ps[k] << 1) + nbit + 1;
            else
                temp = d + (ps[k] << 1) + nbit;
            ps[k + 1] = temp & 7;
            if (temp & 0x10)
                res |= (1 << (7 - k));
        }
        bldiv_hwaccurate_table[i] = res;
    }
}

static void rdp_set_fog_color(const uint32_t* args)
{
    fog_color.r = (args[1] >> 24) & 0xff;
    fog_color.g = (args[1] >> 16) & 0xff;
    fog_color.b = (args[1] >>  8) & 0xff;
    fog_color.a = (args[1] >>  0) & 0xff;
}

static void rdp_set_blend_color(const uint32_t* args)
{
    blend_color.r = (args[1] >> 24) & 0xff;
    blend_color.g = (args[1] >> 16) & 0xff;
    blend_color.b = (args[1] >>  8) & 0xff;
    blend_color.a = (args[1] >>  0) & 0xff;
}
