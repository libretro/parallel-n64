#define VI_COMPARE(x)                                               \
{                                                                   \
    addr = (x);                                                     \
    RREADIDX16(pix, addr);                                          \
    tempr = (pix >> 11) & 0x1f;                                     \
    tempg = (pix >> 6) & 0x1f;                                      \
    tempb = (pix >> 1) & 0x1f;                                      \
    rend += redptr[tempr];                                          \
    gend += greenptr[tempg];                                        \
    bend += blueptr[tempb];                                         \
}

#define VI_COMPARE_OPT(x)                                           \
{                                                                   \
    pix = rdram_read_idx16_fast((x));                               \
    tempr = (pix >> 11) & 0x1f;                                     \
    tempg = (pix >> 6) & 0x1f;                                      \
    tempb = (pix >> 1) & 0x1f;                                      \
    rend += redptr[tempr];                                          \
    gend += greenptr[tempg];                                        \
    bend += blueptr[tempb];                                         \
}

#define VI_COMPARE32(x)                                                 \
{                                                                       \
    addr = (x);                                                         \
    RREADIDX32(pix, addr);                                              \
    tempr = (pix >> 27) & 0x1f;                                         \
    tempg = (pix >> 19) & 0x1f;                                         \
    tempb = (pix >> 11) & 0x1f;                                         \
    rend += redptr[tempr];                                              \
    gend += greenptr[tempg];                                            \
    bend += blueptr[tempb];                                             \
}

#define VI_COMPARE32_OPT(x)                                             \
{                                                                       \
    pix = rdram_read_idx32_fast((x));                                   \
    tempr = (pix >> 27) & 0x1f;                                         \
    tempg = (pix >> 19) & 0x1f;                                         \
    tempb = (pix >> 11) & 0x1f;                                         \
    rend += redptr[tempr];                                              \
    gend += greenptr[tempg];                                            \
    bend += blueptr[tempb];                                             \
}

static int vi_restore_table[0x400];

static STRICTINLINE void restore_filter16(int* r, int* g, int* b, uint32_t fboffset, uint32_t num, uint32_t hres, uint32_t fetchbugstate)
{
    uint32_t idx = (fboffset >> 1) + num;

    uint32_t toleftpix = idx - 1;

    uint32_t leftuppix, leftdownpix, maxpix;

    leftuppix = idx - hres - 1;

    if (fetchbugstate != 1)
    {
        leftdownpix = idx + hres - 1;
        maxpix = idx + hres + 1;
    }
    else
    {
        leftdownpix = toleftpix;
        maxpix = toleftpix + 2;
    }

    int rend = *r;
    int gend = *g;
    int bend = *b;
    const int* redptr = &vi_restore_table[(rend << 2) & 0x3e0];
    const int* greenptr = &vi_restore_table[(gend << 2) & 0x3e0];
    const int* blueptr = &vi_restore_table[(bend << 2) & 0x3e0];

    uint32_t tempr, tempg, tempb;
    uint16_t pix;
    uint32_t addr;

    if (rdram_valid_idx16(maxpix) && rdram_valid_idx16(leftuppix))
    {
        VI_COMPARE_OPT(leftuppix);
        VI_COMPARE_OPT(leftuppix + 1);
        VI_COMPARE_OPT(leftuppix + 2);
        VI_COMPARE_OPT(leftdownpix);
        VI_COMPARE_OPT(leftdownpix + 1);
        VI_COMPARE_OPT(maxpix);
        VI_COMPARE_OPT(toleftpix);
        VI_COMPARE_OPT(toleftpix + 2);
    }
    else
    {
        VI_COMPARE(leftuppix);
        VI_COMPARE(leftuppix + 1);
        VI_COMPARE(leftuppix + 2);
        VI_COMPARE(leftdownpix);
        VI_COMPARE(leftdownpix + 1);
        VI_COMPARE(maxpix);
        VI_COMPARE(toleftpix);
        VI_COMPARE(toleftpix + 2);
    }


    *r = rend;
    *g = gend;
    *b = bend;
}

static STRICTINLINE void restore_filter32(int* r, int* g, int* b, uint32_t fboffset, uint32_t num, uint32_t hres, uint32_t fetchbugstate)
{
    uint32_t idx = (fboffset >> 2) + num;

    uint32_t toleftpix = idx - 1;

    uint32_t leftuppix, leftdownpix, maxpix;

    leftuppix = idx - hres - 1;

    if (fetchbugstate != 1)
    {
        leftdownpix = idx + hres - 1;
        maxpix = idx +hres + 1;
    }
    else
    {
        leftdownpix = toleftpix;
        maxpix = toleftpix + 2;
    }

    int rend = *r;
    int gend = *g;
    int bend = *b;
    const int* redptr = &vi_restore_table[(rend << 2) & 0x3e0];
    const int* greenptr = &vi_restore_table[(gend << 2) & 0x3e0];
    const int* blueptr = &vi_restore_table[(bend << 2) & 0x3e0];

    uint32_t tempr, tempg, tempb;
    uint32_t pix, addr;

    if (rdram_valid_idx32(maxpix) && rdram_valid_idx32(leftuppix))
    {
        VI_COMPARE32_OPT(leftuppix);
        VI_COMPARE32_OPT(leftuppix + 1);
        VI_COMPARE32_OPT(leftuppix + 2);
        VI_COMPARE32_OPT(leftdownpix);
        VI_COMPARE32_OPT(leftdownpix + 1);
        VI_COMPARE32_OPT(maxpix);
        VI_COMPARE32_OPT(toleftpix);
        VI_COMPARE32_OPT(toleftpix + 2);
    }
    else
    {
        VI_COMPARE32(leftuppix);
        VI_COMPARE32(leftuppix + 1);
        VI_COMPARE32(leftuppix + 2);
        VI_COMPARE32(leftdownpix);
        VI_COMPARE32(leftdownpix + 1);
        VI_COMPARE32(maxpix);
        VI_COMPARE32(toleftpix);
        VI_COMPARE32(toleftpix + 2);
    }

    *r = rend;
    *g = gend;
    *b = bend;
}

void vi_restore_init()
{
    for (int i = 0; i < 0x400; i++)
    {
        if (((i >> 5) & 0x1f) < (i & 0x1f))
            vi_restore_table[i] = 1;
        else if (((i >> 5) & 0x1f) > (i & 0x1f))
            vi_restore_table[i] = -1;
        else
            vi_restore_table[i] = 0;
    }
}
