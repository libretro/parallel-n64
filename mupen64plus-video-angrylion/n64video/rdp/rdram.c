//
// rdram.c: RDRAM memory interface
//

#define RDRAM_MASK 0x00ffffff

// macros used to interface with AL's code
#define RREADADDR8(rdst, in) {(rdst) = rdram_read_idx8((in));}
#define RREADIDX16(rdst, in) {(rdst) = rdram_read_idx16((in));}
#define RREADIDX32(rdst, in) {(rdst) = rdram_read_idx32((in));}

#define RWRITEADDR8(in, val) rdram_write_idx8((in), (val))
#define RWRITEIDX16(in, val) rdram_write_idx16((in), (val))
#define RWRITEIDX32(in, val) rdram_write_idx32((in), (val))

#define PAIRREAD16(rdst, hdst, in) rdram_read_pair16(&rdst, &hdst, (in))

#define PAIRWRITE16(in, rval, hval) rdram_write_pair16((in), (rval), (hval))

#define PAIRWRITE32(in, rval, hval0, hval1) rdram_write_pair32((in), (rval), (hval0), (hval1))

#define PAIRWRITE8(in, rval, hval) rdram_write_pair8((in), (rval), (hval))

// pointer indexing limits for aliasing RDRAM reads and writes
static uint32_t idxlim8;
static uint32_t idxlim16;
static uint32_t idxlim32;

static uint32_t* rdram32;
static uint16_t* rdram16;
static uint8_t* rdram8;
static uint8_t rdram_hidden[RDRAM_MAX_SIZE / 2];

/* The pixel domain: what the colour and depth buffers are read from and
 * written to. It is RDRAM itself at 1x, and a buffer of its own holding
 * a factor-by-factor block of subpixels per RDRAM pixel when the
 * renderer is upscaling - the same arrangement parallel-RDP uses, an
 * RDRAM-shaped domain rather than a framebuffer-shaped one, so an
 * address in it is the RDRAM address with the subpixel index folded in.
 * Texture loads and everything else keep reading RDRAM through the
 * plain index accessors below. At 1x these pointers are the RDRAM
 * pointers and the limits are the RDRAM limits, so nothing about the
 * pixel path changes and nothing extra is allocated. */
static uint32_t* px32;
static uint16_t* px16;
static uint8_t* px8;
static uint8_t* px_hidden;
static uint32_t pxlim8, pxlim16, pxlim32;
static uint32_t px_mask8, px_mask16, px_mask32;

static uint16_t* px_alloc16;
static uint8_t* px_alloc_hidden;
static uint32_t px_scale = 1;

static void rdram_close(void)
{
    if (px_alloc16)
        free(px_alloc16);
    if (px_alloc_hidden)
        free(px_alloc_hidden);
    px_alloc16 = NULL;
    px_alloc_hidden = NULL;
    px_scale = 1;
}

static void rdram_init(void)
{
    uint32_t scale = config.upscale ? config.upscale : 1;

    idxlim8 = config.gfx.rdram_size - 1;
    idxlim16 = (idxlim8 >> 1) & 0xffffffu;
    idxlim32 = (idxlim8 >> 2) & 0xffffffu;

    rdram32 = (uint32_t*)config.gfx.rdram;
    rdram16 = (uint16_t*)config.gfx.rdram;
    rdram8 = config.gfx.rdram;

    memset(rdram_hidden, 3, sizeof(rdram_hidden));

    rdram_close();
    px_scale = scale;

    if (scale > 1)
    {
        size_t samples = (size_t)scale * scale;
        size_t bytes   = (size_t)config.gfx.rdram_size * samples;

        px_alloc16      = (uint16_t*)calloc(bytes, 1);
        px_alloc_hidden = (uint8_t*)malloc(bytes >> 1);

        if (px_alloc16 && px_alloc_hidden)
        {
            memset(px_alloc_hidden, 3, bytes >> 1);
            px32      = (uint32_t*)px_alloc16;
            px16      = px_alloc16;
            px8       = (uint8_t*)px_alloc16;
            px_hidden = px_alloc_hidden;
            pxlim8    = (uint32_t)(bytes - 1);
            pxlim16   = (uint32_t)((bytes >> 1) - 1);
            pxlim32   = (uint32_t)((bytes >> 2) - 1);
            /* RDRAM_MASK covers the console's address space; the pixel
             * domain is that many samples times the factor squared */
            px_mask8  = (RDRAM_MASK + 1) * (uint32_t)samples - 1;
            px_mask16 = px_mask8 >> 1;
            px_mask32 = px_mask8 >> 2;
            return;
        }
        /* out of memory: render at 1x rather than not at all */
        rdram_close();
    }

    px32      = rdram32;
    px16      = rdram16;
    px8       = rdram8;
    px_hidden = rdram_hidden;
    pxlim8    = idxlim8;
    pxlim16   = idxlim16;
    pxlim32   = idxlim32;
    px_mask8  = RDRAM_MASK;
    px_mask16 = RDRAM_MASK >> 1;
    px_mask32 = RDRAM_MASK >> 2;
}

void *n64video_pixel_domain(void)
{
    return px16;
}

static STRICTINLINE bool px_valid_idx8(uint32_t in)
{
    return in <= pxlim8;
}

static STRICTINLINE bool px_valid_idx16(uint32_t in)
{
    return in <= pxlim16;
}

static STRICTINLINE bool px_valid_idx32(uint32_t in)
{
    return in <= pxlim32;
}

static STRICTINLINE bool rdram_valid_idx8(uint32_t in)
{
    return in <= idxlim8;
}

static STRICTINLINE bool rdram_valid_idx16(uint32_t in)
{
    return in <= idxlim16;
}

static STRICTINLINE bool rdram_valid_idx32(uint32_t in)
{
    return in <= idxlim32;
}

static STRICTINLINE uint8_t rdram_read_idx8(uint32_t in)
{
    in &= RDRAM_MASK;
    return rdram_valid_idx8(in) ? rdram8[in ^ BYTE_ADDR_XOR] : 0;
}

static STRICTINLINE uint8_t rdram_read_idx8_fast(uint32_t in)
{
    return rdram8[in ^ BYTE_ADDR_XOR];
}

static STRICTINLINE uint16_t rdram_read_idx16(uint32_t in)
{
    in &= RDRAM_MASK >> 1;
    return rdram_valid_idx16(in) ? rdram16[in ^ WORD_ADDR_XOR] : 0;
}

static STRICTINLINE uint16_t rdram_read_idx16_fast(uint32_t in)
{
    return rdram16[in ^ WORD_ADDR_XOR];
}

static STRICTINLINE uint32_t rdram_read_idx32(uint32_t in)
{
    in &= RDRAM_MASK >> 2;
    return rdram_valid_idx32(in) ? rdram32[in] : 0;
}

static STRICTINLINE uint32_t rdram_read_idx32_fast(uint32_t in)
{
    return rdram32[in];
}

static STRICTINLINE void rdram_write_idx8(uint32_t in, uint8_t val)
{
    in &= RDRAM_MASK;
    if (rdram_valid_idx8(in)) {
        rdram8[in ^ BYTE_ADDR_XOR] = val;
    }
}

static STRICTINLINE void rdram_write_idx16(uint32_t in, uint16_t val)
{
    in &= RDRAM_MASK >> 1;
    if (rdram_valid_idx16(in)) {
        rdram16[in ^ WORD_ADDR_XOR] = val;
    }
}

static STRICTINLINE void rdram_write_idx32(uint32_t in, uint32_t val)
{
    in &= RDRAM_MASK >> 2;
    if (rdram_valid_idx32(in)) {
        rdram32[in] = val;
    }
}

static STRICTINLINE void rdram_read_pair16(uint16_t* rdst, uint8_t* hdst, uint32_t in)
{
    in &= px_mask16;
    if (px_valid_idx16(in)) {
        *rdst = px16[in ^ WORD_ADDR_XOR];
        *hdst = px_hidden[in];
    } else {
        *rdst = *hdst = 0;
    }
}

static STRICTINLINE void rdram_write_pair8(uint32_t in, uint8_t rval, uint8_t hval)
{
    in &= px_mask8;
    if (px_valid_idx8(in)) {
        px8[in ^ BYTE_ADDR_XOR] = rval;
        if (in & 1) {
            px_hidden[in >> 1] = hval;
        }
    }
}

static STRICTINLINE void rdram_write_pair16(uint32_t in, uint16_t rval, uint8_t hval)
{
    in &= px_mask16;
    if (px_valid_idx16(in)) {
        px16[in ^ WORD_ADDR_XOR] = rval;
        px_hidden[in] = hval;
    }
}

static STRICTINLINE void rdram_write_pair32(uint32_t in, uint32_t rval, uint8_t hval0, uint8_t hval1)
{
    in &= px_mask32;
    if (px_valid_idx32(in)) {
        px32[in] = rval;
        px_hidden[in << 1] = hval0;
        px_hidden[(in << 1) + 1] = hval1;
    }
}
