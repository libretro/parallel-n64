static TLS struct
{
    int sub_a_rgb0;
    int sub_b_rgb0;
    int mul_rgb0;
    int add_rgb0;
    int sub_a_a0;
    int sub_b_a0;
    int mul_a0;
    int add_a0;

    int sub_a_rgb1;
    int sub_b_rgb1;
    int mul_rgb1;
    int add_rgb1;
    int sub_a_a1;
    int sub_b_a1;
    int mul_a1;
    int add_a1;
} combine;

static TLS struct
{
    int32_t *rgbsub_a_r[2];
    int32_t *rgbsub_a_g[2];
    int32_t *rgbsub_a_b[2];
    int32_t *rgbsub_b_r[2];
    int32_t *rgbsub_b_g[2];
    int32_t *rgbsub_b_b[2];
    int32_t *rgbmul_r[2];
    int32_t *rgbmul_g[2];
    int32_t *rgbmul_b[2];
    int32_t *rgbadd_r[2];
    int32_t *rgbadd_g[2];
    int32_t *rgbadd_b[2];

    int32_t *alphasub_a[2];
    int32_t *alphasub_b[2];
    int32_t *alphamul[2];
    int32_t *alphaadd[2];
} combiner;

static TLS struct color prim_color;
static TLS struct color env_color;
static TLS struct color key_scale;
static TLS struct color key_center;
static TLS struct color key_width;

static TLS int32_t keyalpha;

static uint32_t special_9bit_clamptable[512];
static int32_t special_9bit_exttable[512];

static INLINE void set_suba_rgb_input(int32_t **input_r, int32_t **input_g, int32_t **input_b, int code)
{
    switch (code & 0xf)
    {
        case 0:     *input_r = &combined_color.r;   *input_g = &combined_color.g;   *input_b = &combined_color.b;   break;
        case 1:     *input_r = &texel0_color.r;     *input_g = &texel0_color.g;     *input_b = &texel0_color.b;     break;
        case 2:     *input_r = &texel1_color.r;     *input_g = &texel1_color.g;     *input_b = &texel1_color.b;     break;
        case 3:     *input_r = &prim_color.r;       *input_g = &prim_color.g;       *input_b = &prim_color.b;       break;
        case 4:     *input_r = &shade_color.r;      *input_g = &shade_color.g;      *input_b = &shade_color.b;      break;
        case 5:     *input_r = &env_color.r;        *input_g = &env_color.g;        *input_b = &env_color.b;        break;
        case 6:     *input_r = &one_color;          *input_g = &one_color;          *input_b = &one_color;      break;
        case 7:     *input_r = &noise;              *input_g = &noise;              *input_b = &noise;              break;
        case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
        {
            *input_r = &zero_color;     *input_g = &zero_color;     *input_b = &zero_color;     break;
        }
    }
}

static INLINE void set_subb_rgb_input(int32_t **input_r, int32_t **input_g, int32_t **input_b, int code)
{
    switch (code & 0xf)
    {
        case 0:     *input_r = &combined_color.r;   *input_g = &combined_color.g;   *input_b = &combined_color.b;   break;
        case 1:     *input_r = &texel0_color.r;     *input_g = &texel0_color.g;     *input_b = &texel0_color.b;     break;
        case 2:     *input_r = &texel1_color.r;     *input_g = &texel1_color.g;     *input_b = &texel1_color.b;     break;
        case 3:     *input_r = &prim_color.r;       *input_g = &prim_color.g;       *input_b = &prim_color.b;       break;
        case 4:     *input_r = &shade_color.r;      *input_g = &shade_color.g;      *input_b = &shade_color.b;      break;
        case 5:     *input_r = &env_color.r;        *input_g = &env_color.g;        *input_b = &env_color.b;        break;
        case 6:     *input_r = &key_center.r;       *input_g = &key_center.g;       *input_b = &key_center.b;       break;
        case 7:     *input_r = &k4;                 *input_g = &k4;                 *input_b = &k4;                 break;
        case 8: case 9: case 10: case 11: case 12: case 13: case 14: case 15:
        {
            *input_r = &zero_color;     *input_g = &zero_color;     *input_b = &zero_color;     break;
        }
    }
}

static INLINE void set_mul_rgb_input(int32_t **input_r, int32_t **input_g, int32_t **input_b, int code)
{
    switch (code & 0x1f)
    {
        case 0:     *input_r = &combined_color.r;   *input_g = &combined_color.g;   *input_b = &combined_color.b;   break;
        case 1:     *input_r = &texel0_color.r;     *input_g = &texel0_color.g;     *input_b = &texel0_color.b;     break;
        case 2:     *input_r = &texel1_color.r;     *input_g = &texel1_color.g;     *input_b = &texel1_color.b;     break;
        case 3:     *input_r = &prim_color.r;       *input_g = &prim_color.g;       *input_b = &prim_color.b;       break;
        case 4:     *input_r = &shade_color.r;      *input_g = &shade_color.g;      *input_b = &shade_color.b;      break;
        case 5:     *input_r = &env_color.r;        *input_g = &env_color.g;        *input_b = &env_color.b;        break;
        case 6:     *input_r = &key_scale.r;        *input_g = &key_scale.g;        *input_b = &key_scale.b;        break;
        case 7:     *input_r = &combined_color.a;   *input_g = &combined_color.a;   *input_b = &combined_color.a;   break;
        case 8:     *input_r = &texel0_color.a;     *input_g = &texel0_color.a;     *input_b = &texel0_color.a;     break;
        case 9:     *input_r = &texel1_color.a;     *input_g = &texel1_color.a;     *input_b = &texel1_color.a;     break;
        case 10:    *input_r = &prim_color.a;       *input_g = &prim_color.a;       *input_b = &prim_color.a;       break;
        case 11:    *input_r = &shade_color.a;      *input_g = &shade_color.a;      *input_b = &shade_color.a;      break;
        case 12:    *input_r = &env_color.a;        *input_g = &env_color.a;        *input_b = &env_color.a;        break;
        case 13:    *input_r = &lod_frac;           *input_g = &lod_frac;           *input_b = &lod_frac;           break;
        case 14:    *input_r = &primitive_lod_frac; *input_g = &primitive_lod_frac; *input_b = &primitive_lod_frac; break;
        case 15:    *input_r = &k5;                 *input_g = &k5;                 *input_b = &k5;                 break;
        case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23:
        case 24: case 25: case 26: case 27: case 28: case 29: case 30: case 31:
        {
            *input_r = &zero_color;     *input_g = &zero_color;     *input_b = &zero_color;     break;
        }
    }
}

static INLINE void set_add_rgb_input(int32_t **input_r, int32_t **input_g, int32_t **input_b, int code)
{
    switch (code & 0x7)
    {
        case 0:     *input_r = &combined_color.r;   *input_g = &combined_color.g;   *input_b = &combined_color.b;   break;
        case 1:     *input_r = &texel0_color.r;     *input_g = &texel0_color.g;     *input_b = &texel0_color.b;     break;
        case 2:     *input_r = &texel1_color.r;     *input_g = &texel1_color.g;     *input_b = &texel1_color.b;     break;
        case 3:     *input_r = &prim_color.r;       *input_g = &prim_color.g;       *input_b = &prim_color.b;       break;
        case 4:     *input_r = &shade_color.r;      *input_g = &shade_color.g;      *input_b = &shade_color.b;      break;
        case 5:     *input_r = &env_color.r;        *input_g = &env_color.g;        *input_b = &env_color.b;        break;
        case 6:     *input_r = &one_color;          *input_g = &one_color;          *input_b = &one_color;          break;
        case 7:     *input_r = &zero_color;         *input_g = &zero_color;         *input_b = &zero_color;         break;
    }
}

static INLINE void set_sub_alpha_input(int32_t **input, int code)
{
    switch (code & 0x7)
    {
        case 0:     *input = &combined_color.a; break;
        case 1:     *input = &texel0_color.a; break;
        case 2:     *input = &texel1_color.a; break;
        case 3:     *input = &prim_color.a; break;
        case 4:     *input = &shade_color.a; break;
        case 5:     *input = &env_color.a; break;
        case 6:     *input = &one_color; break;
        case 7:     *input = &zero_color; break;
    }
}

static INLINE void set_mul_alpha_input(int32_t **input, int code)
{
    switch (code & 0x7)
    {
        case 0:     *input = &lod_frac; break;
        case 1:     *input = &texel0_color.a; break;
        case 2:     *input = &texel1_color.a; break;
        case 3:     *input = &prim_color.a; break;
        case 4:     *input = &shade_color.a; break;
        case 5:     *input = &env_color.a; break;
        case 6:     *input = &primitive_lod_frac; break;
        case 7:     *input = &zero_color; break;
    }
}

static STRICTINLINE int32_t color_combiner_equation(int32_t a, int32_t b, int32_t c, int32_t d)
{





    a = special_9bit_exttable[a];
    b = special_9bit_exttable[b];
    c = SIGNF(c, 9);
    d = special_9bit_exttable[d];
    a = ((a - b) * c) + (d << 8) + 0x80;
    return (a & 0x1ffff);
}

static STRICTINLINE int32_t alpha_combiner_equation(int32_t a, int32_t b, int32_t c, int32_t d)
{
    a = special_9bit_exttable[a];
    b = special_9bit_exttable[b];
    c = SIGNF(c, 9);
    d = special_9bit_exttable[d];
    a = (((a - b) * c) + (d << 8) + 0x80) >> 8;
    return (a & 0x1ff);
}

static STRICTINLINE int32_t chroma_key_min(struct color* col)
{
    int32_t redkey, greenkey, bluekey, keyalpha;




    redkey = SIGN(col->r, 17);
    if (redkey > 0)
        redkey = ((redkey & 0xf) == 8) ? (-redkey + 0x10) : (-redkey);

    redkey = (key_width.r << 4) + redkey;

    greenkey = SIGN(col->g, 17);
    if (greenkey > 0)
        greenkey = ((greenkey & 0xf) == 8) ? (-greenkey + 0x10) : (-greenkey);

    greenkey = (key_width.g << 4) + greenkey;

    bluekey = SIGN(col->b, 17);
    if (bluekey > 0)
        bluekey = ((bluekey & 0xf) == 8) ? (-bluekey + 0x10) : (-bluekey);

    bluekey = (key_width.b << 4) + bluekey;

    keyalpha = (redkey < greenkey) ? redkey : greenkey;
    keyalpha = (bluekey < keyalpha) ? bluekey : keyalpha;
    keyalpha = clamp(keyalpha, 0, 0xff);
    return keyalpha;
}

static STRICTINLINE void combiner_1cycle(int adseed, uint32_t* curpixel_cvg)
{

    int32_t keyalpha, temp;
    struct color chromabypass;

    if (other_modes.key_en)
    {
        chromabypass.r = *combiner.rgbsub_a_r[1];
        chromabypass.g = *combiner.rgbsub_a_g[1];
        chromabypass.b = *combiner.rgbsub_a_b[1];
    }






    if (combiner.rgbmul_r[1] != &zero_color)
    {
















        combined_color.r = color_combiner_equation(*combiner.rgbsub_a_r[1],*combiner.rgbsub_b_r[1],*combiner.rgbmul_r[1],*combiner.rgbadd_r[1]);
        combined_color.g = color_combiner_equation(*combiner.rgbsub_a_g[1],*combiner.rgbsub_b_g[1],*combiner.rgbmul_g[1],*combiner.rgbadd_g[1]);
        combined_color.b = color_combiner_equation(*combiner.rgbsub_a_b[1],*combiner.rgbsub_b_b[1],*combiner.rgbmul_b[1],*combiner.rgbadd_b[1]);
    }
    else
    {
        combined_color.r = ((special_9bit_exttable[*combiner.rgbadd_r[1]] << 8) + 0x80) & 0x1ffff;
        combined_color.g = ((special_9bit_exttable[*combiner.rgbadd_g[1]] << 8) + 0x80) & 0x1ffff;
        combined_color.b = ((special_9bit_exttable[*combiner.rgbadd_b[1]] << 8) + 0x80) & 0x1ffff;
    }

    if (combiner.alphamul[1] != &zero_color)
        combined_color.a = alpha_combiner_equation(*combiner.alphasub_a[1],*combiner.alphasub_b[1],*combiner.alphamul[1],*combiner.alphaadd[1]);
    else
        combined_color.a = special_9bit_exttable[*combiner.alphaadd[1]] & 0x1ff;

    pixel_color.a = special_9bit_clamptable[combined_color.a];
    if (pixel_color.a == 0xff)
        pixel_color.a = 0x100;

    if (!other_modes.key_en)
    {

        combined_color.r >>= 8;
        combined_color.g >>= 8;
        combined_color.b >>= 8;
        pixel_color.r = special_9bit_clamptable[combined_color.r];
        pixel_color.g = special_9bit_clamptable[combined_color.g];
        pixel_color.b = special_9bit_clamptable[combined_color.b];
    }
    else
    {
        keyalpha = chroma_key_min(&combined_color);



        pixel_color.r = special_9bit_clamptable[chromabypass.r];
        pixel_color.g = special_9bit_clamptable[chromabypass.g];
        pixel_color.b = special_9bit_clamptable[chromabypass.b];


        combined_color.r >>= 8;
        combined_color.g >>= 8;
        combined_color.b >>= 8;
    }


    if (other_modes.cvg_times_alpha)
    {
        temp = (pixel_color.a * (*curpixel_cvg) + 4) >> 3;
        *curpixel_cvg = (temp >> 5) & 0xf;
    }

    if (!other_modes.alpha_cvg_select)
    {
        if (!other_modes.key_en)
        {
            pixel_color.a += adseed;
            if (pixel_color.a & 0x100)
                pixel_color.a = 0xff;
        }
        else
            pixel_color.a = keyalpha;
    }
    else
    {
        if (other_modes.cvg_times_alpha)
            pixel_color.a = temp;
        else
            pixel_color.a = (*curpixel_cvg) << 5;
        if (pixel_color.a > 0xff)
            pixel_color.a = 0xff;
    }

    shade_color.a += adseed;
    if (shade_color.a & 0x100)
        shade_color.a = 0xff;
}

static STRICTINLINE void combiner_2cycle(int adseed, uint32_t* curpixel_cvg, int32_t* acalpha)
{
    int32_t keyalpha, temp;
    struct color chromabypass;

    if (combiner.rgbmul_r[0] != &zero_color)
    {
        combined_color.r = color_combiner_equation(*combiner.rgbsub_a_r[0],*combiner.rgbsub_b_r[0],*combiner.rgbmul_r[0],*combiner.rgbadd_r[0]);
        combined_color.g = color_combiner_equation(*combiner.rgbsub_a_g[0],*combiner.rgbsub_b_g[0],*combiner.rgbmul_g[0],*combiner.rgbadd_g[0]);
        combined_color.b = color_combiner_equation(*combiner.rgbsub_a_b[0],*combiner.rgbsub_b_b[0],*combiner.rgbmul_b[0],*combiner.rgbadd_b[0]);
    }
    else
    {
        combined_color.r = ((special_9bit_exttable[*combiner.rgbadd_r[0]] << 8) + 0x80) & 0x1ffff;
        combined_color.g = ((special_9bit_exttable[*combiner.rgbadd_g[0]] << 8) + 0x80) & 0x1ffff;
        combined_color.b = ((special_9bit_exttable[*combiner.rgbadd_b[0]] << 8) + 0x80) & 0x1ffff;
    }

    if (combiner.alphamul[0] != &zero_color)
        combined_color.a = alpha_combiner_equation(*combiner.alphasub_a[0],*combiner.alphasub_b[0],*combiner.alphamul[0],*combiner.alphaadd[0]);
    else
        combined_color.a = special_9bit_exttable[*combiner.alphaadd[0]] & 0x1ff;



    if (other_modes.alpha_compare_en)
    {
        if (other_modes.key_en)
            keyalpha = chroma_key_min(&combined_color);

        int32_t preacalpha = special_9bit_clamptable[combined_color.a];
        if (preacalpha == 0xff)
            preacalpha = 0x100;

        if (other_modes.cvg_times_alpha)
            temp = (preacalpha * (*curpixel_cvg) + 4) >> 3;

        if (!other_modes.alpha_cvg_select)
        {
            if (!other_modes.key_en)
            {
                preacalpha += adseed;
                if (preacalpha & 0x100)
                    preacalpha = 0xff;
            }
            else
                preacalpha = keyalpha;
        }
        else
        {
            if (other_modes.cvg_times_alpha)
                preacalpha = temp;
            else
                preacalpha = (*curpixel_cvg) << 5;
            if (preacalpha > 0xff)
                preacalpha = 0xff;
        }

        *acalpha = preacalpha;
    }





    combined_color.r >>= 8;
    combined_color.g >>= 8;
    combined_color.b >>= 8;


    texel0_color = texel1_color;
    texel1_color = nexttexel_color;









    if (other_modes.key_en)
    {
        chromabypass.r = *combiner.rgbsub_a_r[1];
        chromabypass.g = *combiner.rgbsub_a_g[1];
        chromabypass.b = *combiner.rgbsub_a_b[1];
    }

    if (combiner.rgbmul_r[1] != &zero_color)
    {
        combined_color.r = color_combiner_equation(*combiner.rgbsub_a_r[1],*combiner.rgbsub_b_r[1],*combiner.rgbmul_r[1],*combiner.rgbadd_r[1]);
        combined_color.g = color_combiner_equation(*combiner.rgbsub_a_g[1],*combiner.rgbsub_b_g[1],*combiner.rgbmul_g[1],*combiner.rgbadd_g[1]);
        combined_color.b = color_combiner_equation(*combiner.rgbsub_a_b[1],*combiner.rgbsub_b_b[1],*combiner.rgbmul_b[1],*combiner.rgbadd_b[1]);
    }
    else
    {
        combined_color.r = ((special_9bit_exttable[*combiner.rgbadd_r[1]] << 8) + 0x80) & 0x1ffff;
        combined_color.g = ((special_9bit_exttable[*combiner.rgbadd_g[1]] << 8) + 0x80) & 0x1ffff;
        combined_color.b = ((special_9bit_exttable[*combiner.rgbadd_b[1]] << 8) + 0x80) & 0x1ffff;
    }

    if (combiner.alphamul[1] != &zero_color)
        combined_color.a = alpha_combiner_equation(*combiner.alphasub_a[1],*combiner.alphasub_b[1],*combiner.alphamul[1],*combiner.alphaadd[1]);
    else
        combined_color.a = special_9bit_exttable[*combiner.alphaadd[1]] & 0x1ff;

    if (!other_modes.key_en)
    {

        combined_color.r >>= 8;
        combined_color.g >>= 8;
        combined_color.b >>= 8;

        pixel_color.r = special_9bit_clamptable[combined_color.r];
        pixel_color.g = special_9bit_clamptable[combined_color.g];
        pixel_color.b = special_9bit_clamptable[combined_color.b];
    }
    else
    {
        keyalpha = chroma_key_min(&combined_color);



        pixel_color.r = special_9bit_clamptable[chromabypass.r];
        pixel_color.g = special_9bit_clamptable[chromabypass.g];
        pixel_color.b = special_9bit_clamptable[chromabypass.b];


        combined_color.r >>= 8;
        combined_color.g >>= 8;
        combined_color.b >>= 8;
    }

    pixel_color.a = special_9bit_clamptable[combined_color.a];
    if (pixel_color.a == 0xff)
        pixel_color.a = 0x100;


    if (other_modes.cvg_times_alpha)
    {
        temp = (pixel_color.a * (*curpixel_cvg) + 4) >> 3;

        *curpixel_cvg = (temp >> 5) & 0xf;


    }

    if (!other_modes.alpha_cvg_select)
    {
        if (!other_modes.key_en)
        {
            pixel_color.a += adseed;
            if (pixel_color.a & 0x100)
                pixel_color.a = 0xff;
        }
        else
            pixel_color.a = keyalpha;
    }
    else
    {
        if (other_modes.cvg_times_alpha)
            pixel_color.a = temp;
        else
            pixel_color.a = (*curpixel_cvg) << 5;
        if (pixel_color.a > 0xff)
            pixel_color.a = 0xff;
    }

    shade_color.a += adseed;
    if (shade_color.a & 0x100)
        shade_color.a = 0xff;
}

static void combiner_init(void)
{
    combiner.rgbsub_a_r[0] = combiner.rgbsub_a_r[1] = &one_color;
    combiner.rgbsub_a_g[0] = combiner.rgbsub_a_g[1] = &one_color;
    combiner.rgbsub_a_b[0] = combiner.rgbsub_a_b[1] = &one_color;
    combiner.rgbsub_b_r[0] = combiner.rgbsub_b_r[1] = &one_color;
    combiner.rgbsub_b_g[0] = combiner.rgbsub_b_g[1] = &one_color;
    combiner.rgbsub_b_b[0] = combiner.rgbsub_b_b[1] = &one_color;
    combiner.rgbmul_r[0] = combiner.rgbmul_r[1] = &one_color;
    combiner.rgbmul_g[0] = combiner.rgbmul_g[1] = &one_color;
    combiner.rgbmul_b[0] = combiner.rgbmul_b[1] = &one_color;
    combiner.rgbadd_r[0] = combiner.rgbadd_r[1] = &one_color;
    combiner.rgbadd_g[0] = combiner.rgbadd_g[1] = &one_color;
    combiner.rgbadd_b[0] = combiner.rgbadd_b[1] = &one_color;

    combiner.alphasub_a[0] = combiner.alphasub_a[1] = &one_color;
    combiner.alphasub_b[0] = combiner.alphasub_b[1] = &one_color;
    combiner.alphamul[0] = combiner.alphamul[1] = &one_color;
    combiner.alphaadd[0] = combiner.alphaadd[1] = &one_color;

    for(int i = 0; i < 0x200; i++)
    {
        switch((i >> 7) & 3)
        {
        case 0:
        case 1:
            special_9bit_clamptable[i] = i & 0xff;
            break;
        case 2:
            special_9bit_clamptable[i] = 0xff;
            break;
        case 3:
            special_9bit_clamptable[i] = 0;
            break;
        }
    }

    for (int i = 0; i < 0x200; i++)
    {
        special_9bit_exttable[i] = ((i & 0x180) == 0x180) ? (i | ~0x1ff) : (i & 0x1ff);
    }
}

static void rdp_set_prim_color(const uint32_t* args)
{
    min_level = (args[0] >> 8) & 0x1f;
    primitive_lod_frac = args[0] & 0xff;
    prim_color.r = (args[1] >> 24) & 0xff;
    prim_color.g = (args[1] >> 16) & 0xff;
    prim_color.b = (args[1] >>  8) & 0xff;
    prim_color.a = (args[1] >>  0) & 0xff;
}

static void rdp_set_env_color(const uint32_t* args)
{
    env_color.r = (args[1] >> 24) & 0xff;
    env_color.g = (args[1] >> 16) & 0xff;
    env_color.b = (args[1] >>  8) & 0xff;
    env_color.a = (args[1] >>  0) & 0xff;
}

static void rdp_set_combine(const uint32_t* args)
{
    combine.sub_a_rgb0  = (args[0] >> 20) & 0xf;
    combine.mul_rgb0    = (args[0] >> 15) & 0x1f;
    combine.sub_a_a0    = (args[0] >> 12) & 0x7;
    combine.mul_a0      = (args[0] >>  9) & 0x7;
    combine.sub_a_rgb1  = (args[0] >>  5) & 0xf;
    combine.mul_rgb1    = (args[0] >>  0) & 0x1f;

    combine.sub_b_rgb0  = (args[1] >> 28) & 0xf;
    combine.sub_b_rgb1  = (args[1] >> 24) & 0xf;
    combine.sub_a_a1    = (args[1] >> 21) & 0x7;
    combine.mul_a1      = (args[1] >> 18) & 0x7;
    combine.add_rgb0    = (args[1] >> 15) & 0x7;
    combine.sub_b_a0    = (args[1] >> 12) & 0x7;
    combine.add_a0      = (args[1] >>  9) & 0x7;
    combine.add_rgb1    = (args[1] >>  6) & 0x7;
    combine.sub_b_a1    = (args[1] >>  3) & 0x7;
    combine.add_a1      = (args[1] >>  0) & 0x7;


    set_suba_rgb_input(&combiner.rgbsub_a_r[0], &combiner.rgbsub_a_g[0], &combiner.rgbsub_a_b[0], combine.sub_a_rgb0);
    set_subb_rgb_input(&combiner.rgbsub_b_r[0], &combiner.rgbsub_b_g[0], &combiner.rgbsub_b_b[0], combine.sub_b_rgb0);
    set_mul_rgb_input(&combiner.rgbmul_r[0], &combiner.rgbmul_g[0], &combiner.rgbmul_b[0], combine.mul_rgb0);
    set_add_rgb_input(&combiner.rgbadd_r[0], &combiner.rgbadd_g[0], &combiner.rgbadd_b[0], combine.add_rgb0);
    set_sub_alpha_input(&combiner.alphasub_a[0], combine.sub_a_a0);
    set_sub_alpha_input(&combiner.alphasub_b[0], combine.sub_b_a0);
    set_mul_alpha_input(&combiner.alphamul[0], combine.mul_a0);
    set_sub_alpha_input(&combiner.alphaadd[0], combine.add_a0);

    set_suba_rgb_input(&combiner.rgbsub_a_r[1], &combiner.rgbsub_a_g[1], &combiner.rgbsub_a_b[1], combine.sub_a_rgb1);
    set_subb_rgb_input(&combiner.rgbsub_b_r[1], &combiner.rgbsub_b_g[1], &combiner.rgbsub_b_b[1], combine.sub_b_rgb1);
    set_mul_rgb_input(&combiner.rgbmul_r[1], &combiner.rgbmul_g[1], &combiner.rgbmul_b[1], combine.mul_rgb1);
    set_add_rgb_input(&combiner.rgbadd_r[1], &combiner.rgbadd_g[1], &combiner.rgbadd_b[1], combine.add_rgb1);
    set_sub_alpha_input(&combiner.alphasub_a[1], combine.sub_a_a1);
    set_sub_alpha_input(&combiner.alphasub_b[1], combine.sub_b_a1);
    set_mul_alpha_input(&combiner.alphamul[1], combine.mul_a1);
    set_sub_alpha_input(&combiner.alphaadd[1], combine.add_a1);

    other_modes.f.stalederivs = 1;
}

static void rdp_set_key_gb(const uint32_t* args)
{
    key_width.g = (args[0] >> 12) & 0xfff;
    key_width.b = args[0] & 0xfff;
    key_center.g = (args[1] >> 24) & 0xff;
    key_scale.g = (args[1] >> 16) & 0xff;
    key_center.b = (args[1] >> 8) & 0xff;
    key_scale.b = args[1] & 0xff;
}

static void rdp_set_key_r(const uint32_t* args)
{
    key_width.r = (args[1] >> 16) & 0xfff;
    key_center.r = (args[1] >> 8) & 0xff;
    key_scale.r = args[1] & 0xff;
}
