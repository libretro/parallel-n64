static void fbwrite_4(uint32_t curpixel, uint32_t r, uint32_t g, uint32_t b, uint32_t blend_en, uint32_t curpixel_cvg, uint32_t curpixel_memcvg);
static void fbwrite_8(uint32_t curpixel, uint32_t r, uint32_t g, uint32_t b, uint32_t blend_en, uint32_t curpixel_cvg, uint32_t curpixel_memcvg);
static void fbwrite_16(uint32_t curpixel, uint32_t r, uint32_t g, uint32_t b, uint32_t blend_en, uint32_t curpixel_cvg, uint32_t curpixel_memcvg);
static void fbwrite_32(uint32_t curpixel, uint32_t r, uint32_t g, uint32_t b, uint32_t blend_en, uint32_t curpixel_cvg, uint32_t curpixel_memcvg);
static void fbread_4(uint32_t num, uint32_t* curpixel_memcvg);
static void fbread_8(uint32_t num, uint32_t* curpixel_memcvg);
static void fbread_16(uint32_t num, uint32_t* curpixel_memcvg);
static void fbread_32(uint32_t num, uint32_t* curpixel_memcvg);
static void fbread2_4(uint32_t num, uint32_t* curpixel_memcvg);
static void fbread2_8(uint32_t num, uint32_t* curpixel_memcvg);
static void fbread2_16(uint32_t num, uint32_t* curpixel_memcvg);
static void fbread2_32(uint32_t num, uint32_t* curpixel_memcvg);

static void (*fbread_func[4])(uint32_t, uint32_t*) =
{
    fbread_4, fbread_8, fbread_16, fbread_32
};

static void (*fbread2_func[4])(uint32_t, uint32_t*) =
{
    fbread2_4, fbread2_8, fbread2_16, fbread2_32
};

static void (*fbwrite_func[4])(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) =
{
    fbwrite_4, fbwrite_8, fbwrite_16, fbwrite_32
};

static TLS void (*fbread1_ptr)(uint32_t, uint32_t*);
static TLS void (*fbread2_ptr)(uint32_t, uint32_t*);
static TLS void (*fbwrite_ptr)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

static TLS int fb_format;
static TLS int fb_size;
static TLS int fb_width;
static TLS uint32_t fb_address;
static TLS uint32_t fill_color;

static void fbwrite_4(uint32_t curpixel, uint32_t r, uint32_t g, uint32_t b, uint32_t blend_en, uint32_t curpixel_cvg, uint32_t curpixel_memcvg)
{
    uint32_t fb = fb_address + curpixel;
    RWRITEADDR8(fb, 0);
}

static void fbwrite_8(uint32_t curpixel, uint32_t r, uint32_t g, uint32_t b, uint32_t blend_en, uint32_t curpixel_cvg, uint32_t curpixel_memcvg)
{
    uint32_t fb = fb_address + curpixel;
    PAIRWRITE8(fb, r & 0xff, (r & 1) ? 3 : 0);
}

static void fbwrite_16(uint32_t curpixel, uint32_t r, uint32_t g, uint32_t b, uint32_t blend_en, uint32_t curpixel_cvg, uint32_t curpixel_memcvg)
{
#undef CVG_DRAW
#ifdef CVG_DRAW
    int covdraw = (curpixel_cvg - 1) << 5;
    r=covdraw; g=covdraw; b=covdraw;
#endif

    uint32_t fb;
    uint16_t rval;
    uint8_t hval;
    fb = (fb_address >> 1) + curpixel;

    int32_t finalcvg = finalize_spanalpha(blend_en, curpixel_cvg, curpixel_memcvg);
    int16_t finalcolor;

    if (fb_format == FORMAT_RGBA)
    {
        finalcolor = ((r & ~7) << 8) | ((g & ~7) << 3) | ((b & ~7) >> 2);
    }
    else
    {
        finalcolor = (r << 8) | (finalcvg << 5);
        finalcvg = 0;
    }


    rval = finalcolor|(finalcvg >> 2);
    hval = finalcvg & 3;
    PAIRWRITE16(fb, rval, hval);
}

static void fbwrite_32(uint32_t curpixel, uint32_t r, uint32_t g, uint32_t b, uint32_t blend_en, uint32_t curpixel_cvg, uint32_t curpixel_memcvg)
{
    uint32_t fb = (fb_address >> 2) + curpixel;

    int32_t finalcolor;
    int32_t finalcvg = finalize_spanalpha(blend_en, curpixel_cvg, curpixel_memcvg);

    finalcolor = (r << 24) | (g << 16) | (b << 8);
    finalcolor |= (finalcvg << 5);

    PAIRWRITE32(fb, finalcolor, (g & 1) ? 3 : 0, 0);
}

static void fbfill_4(uint32_t curpixel)
{
    rdp_pipeline_crashed = 1;
}

static void fbfill_8(uint32_t curpixel)
{
    uint32_t fb = fb_address + curpixel;
    uint32_t val = (fill_color >> (((fb & 3) ^ 3) << 3)) & 0xff;
    uint8_t hval = ((val & 1) << 1) | (val & 1);
    PAIRWRITE8(fb, val, hval);
}

static void fbfill_16(uint32_t curpixel)
{
    uint16_t val;
    uint8_t hval;
    uint32_t fb = (fb_address >> 1) + curpixel;
    if (fb & 1)
        val = fill_color & 0xffff;
    else
        val = (fill_color >> 16) & 0xffff;
    hval = ((val & 1) << 1) | (val & 1);
    PAIRWRITE16(fb, val, hval);
}

static void fbfill_32(uint32_t curpixel)
{
    uint32_t fb = (fb_address >> 2) + curpixel;
    PAIRWRITE32(fb, fill_color, (fill_color & 0x10000) ? 3 : 0, (fill_color & 0x1) ? 3 : 0);
}

static void fbread_4(uint32_t curpixel, uint32_t* curpixel_memcvg)
{
    memory_color.r = memory_color.g = memory_color.b = 0;

    *curpixel_memcvg = 7;
    memory_color.a = 0xe0;
}

static void fbread2_4(uint32_t curpixel, uint32_t* curpixel_memcvg)
{
    pre_memory_color.r = pre_memory_color.g = pre_memory_color.b = 0;
    pre_memory_color.a = 0xe0;
    *curpixel_memcvg = 7;
}

static void fbread_8(uint32_t curpixel, uint32_t* curpixel_memcvg)
{
    uint8_t mem;
    uint32_t addr = fb_address + curpixel;
    RREADADDR8(mem, addr);
    memory_color.r = memory_color.g = memory_color.b = mem;
    *curpixel_memcvg = 7;
    memory_color.a = 0xe0;
}

static void fbread2_8(uint32_t curpixel, uint32_t* curpixel_memcvg)
{
    uint8_t mem;
    uint32_t addr = fb_address + curpixel;
    RREADADDR8(mem, addr);
    pre_memory_color.r = pre_memory_color.g = pre_memory_color.b = mem;
    pre_memory_color.a = 0xe0;
    *curpixel_memcvg = 7;
}

static void fbread_16(uint32_t curpixel, uint32_t* curpixel_memcvg)
{
    uint16_t fword;
    uint8_t hbyte;
    uint32_t addr = (fb_address >> 1) + curpixel;

    uint8_t lowbits;


    if (other_modes.image_read_en)
    {
        PAIRREAD16(fword, hbyte, addr);

        if (fb_format == FORMAT_RGBA)
        {
            memory_color.r = GET_HI(fword);
            memory_color.g = GET_MED(fword);
            memory_color.b = GET_LOW(fword);
            lowbits = ((fword & 1) << 2) | hbyte;
        }
        else
        {
            memory_color.r = memory_color.g = memory_color.b = fword >> 8;
            lowbits = (fword >> 5) & 7;
        }

        *curpixel_memcvg = lowbits;
        memory_color.a = lowbits << 5;
    }
    else
    {
        RREADIDX16(fword, addr);

        if (fb_format == FORMAT_RGBA)
        {
            memory_color.r = GET_HI(fword);
            memory_color.g = GET_MED(fword);
            memory_color.b = GET_LOW(fword);
        }
        else
            memory_color.r = memory_color.g = memory_color.b = fword >> 8;

        *curpixel_memcvg = 7;
        memory_color.a = 0xe0;
    }
}

static void fbread2_16(uint32_t curpixel, uint32_t* curpixel_memcvg)
{
    uint16_t fword;
    uint8_t hbyte;
    uint32_t addr = (fb_address >> 1) + curpixel;

    uint8_t lowbits;

    if (other_modes.image_read_en)
    {
        PAIRREAD16(fword, hbyte, addr);

        if (fb_format == FORMAT_RGBA)
        {
            pre_memory_color.r = GET_HI(fword);
            pre_memory_color.g = GET_MED(fword);
            pre_memory_color.b = GET_LOW(fword);
            lowbits = ((fword & 1) << 2) | hbyte;
        }
        else
        {
            pre_memory_color.r = pre_memory_color.g = pre_memory_color.b = fword >> 8;
            lowbits = (fword >> 5) & 7;
        }

        *curpixel_memcvg = lowbits;
        pre_memory_color.a = lowbits << 5;
    }
    else
    {
        RREADIDX16(fword, addr);

        if (fb_format == FORMAT_RGBA)
        {
            pre_memory_color.r = GET_HI(fword);
            pre_memory_color.g = GET_MED(fword);
            pre_memory_color.b = GET_LOW(fword);
        }
        else
            pre_memory_color.r = pre_memory_color.g = pre_memory_color.b = fword >> 8;

        *curpixel_memcvg = 7;
        pre_memory_color.a = 0xe0;
    }

}

static void fbread_32(uint32_t curpixel, uint32_t* curpixel_memcvg)
{
    uint32_t mem, addr = (fb_address >> 2) + curpixel;
    RREADIDX32(mem, addr);
    memory_color.r = (mem >> 24) & 0xff;
    memory_color.g = (mem >> 16) & 0xff;
    memory_color.b = (mem >> 8) & 0xff;
    if (other_modes.image_read_en)
    {
        *curpixel_memcvg = (mem >> 5) & 7;
        memory_color.a = mem & 0xe0;
    }
    else
    {
        *curpixel_memcvg = 7;
        memory_color.a = 0xe0;
    }
}

static INLINE void fbread2_32(uint32_t curpixel, uint32_t* curpixel_memcvg)
{
    uint32_t mem, addr = (fb_address >> 2) + curpixel;
    RREADIDX32(mem, addr);
    pre_memory_color.r = (mem >> 24) & 0xff;
    pre_memory_color.g = (mem >> 16) & 0xff;
    pre_memory_color.b = (mem >> 8) & 0xff;
    if (other_modes.image_read_en)
    {
        *curpixel_memcvg = (mem >> 5) & 7;
        pre_memory_color.a = mem & 0xe0;
    }
    else
    {
        *curpixel_memcvg = 7;
        pre_memory_color.a = 0xe0;
    }
}

static void rdp_set_color_image(const uint32_t* args)
{
    fb_format   = (args[0] >> 21) & 0x7;
    fb_size     = (args[0] >> 19) & 0x3;
    fb_width    = (args[0] & 0x3ff) + 1;
    fb_address  = args[1] & 0x0ffffff;


    fbread1_ptr = fbread_func[fb_size];
    fbread2_ptr = fbread2_func[fb_size];
    fbwrite_ptr = fbwrite_func[fb_size];
}

static void rdp_set_fill_color(const uint32_t* args)
{
    fill_color = args[1];
}

static void fb_init()
{
    fb_format = FORMAT_RGBA;
    fb_size = PIXEL_SIZE_4BIT;
    fb_width = 0;
    fb_address = 0;


    fbread1_ptr = fbread_func[fb_size];
    fbread2_ptr = fbread2_func[fb_size];
    fbwrite_ptr = fbwrite_func[fb_size];
}
