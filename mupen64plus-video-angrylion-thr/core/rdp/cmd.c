static uint32_t rdp_cmd_data[0x10000];
static uint32_t rdp_cmd_ptr = 0;
static uint32_t rdp_cmd_cur = 0;
static uint32_t ptr_onstart = 0;

#define CMD_BUFFER_COUNT 1024

static uint32_t rdp_cmd_buf[CMD_BUFFER_COUNT][CMD_MAX_INTS];
static uint32_t rdp_cmd_buf_pos;

static void rdp_invalid(const uint32_t* args);
static void rdp_noop(const uint32_t* args);
static void rdp_tri_noshade(const uint32_t* args);
static void rdp_tri_noshade_z(const uint32_t* args);
static void rdp_tri_tex(const uint32_t* args);
static void rdp_tri_tex_z(const uint32_t* args);
static void rdp_tri_shade(const uint32_t* args);
static void rdp_tri_shade_z(const uint32_t* args);
static void rdp_tri_texshade(const uint32_t* args);
static void rdp_tri_texshade_z(const uint32_t* args);
static void rdp_tex_rect(const uint32_t* args);
static void rdp_tex_rect_flip(const uint32_t* args);
static void rdp_sync_load(const uint32_t* args);
static void rdp_sync_pipe(const uint32_t* args);
static void rdp_sync_tile(const uint32_t* args);
static void rdp_sync_full(const uint32_t* args);
static void rdp_set_key_gb(const uint32_t* args);
static void rdp_set_key_r(const uint32_t* args);
static void rdp_set_convert(const uint32_t* args);
static void rdp_set_scissor(const uint32_t* args);
static void rdp_set_prim_depth(const uint32_t* args);
static void rdp_set_other_modes(const uint32_t* args);
static void rdp_set_tile_size(const uint32_t* args);
static void rdp_load_block(const uint32_t* args);
static void rdp_load_tlut(const uint32_t* args);
static void rdp_load_tile(const uint32_t* args);
static void rdp_set_tile(const uint32_t* args);
static void rdp_fill_rect(const uint32_t* args);
static void rdp_set_fill_color(const uint32_t* args);
static void rdp_set_fog_color(const uint32_t* args);
static void rdp_set_blend_color(const uint32_t* args);
static void rdp_set_prim_color(const uint32_t* args);
static void rdp_set_env_color(const uint32_t* args);
static void rdp_set_combine(const uint32_t* args);
static void rdp_set_texture_image(const uint32_t* args);
static void rdp_set_mask_image(const uint32_t* args);
static void rdp_set_color_image(const uint32_t* args);

static const struct
{
    void (*handler)(const uint32_t*);   // command handler function pointer
    uint32_t length;                    // command data length in bytes
    bool singlethread;                  // run in main thread
    bool multithread;                   // run in worker threads
    bool sync;                          // synchronize all workers before execution
    char name[32];                      // descriptive name for debugging
} rdp_commands[] = {
    {rdp_noop,              8,   true,  false, false, "No_Op"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_tri_noshade,       32,  false, true,  false, "Fill_Triangle"},
    {rdp_tri_noshade_z,     48,  false, true,  false, "Fill_ZBuffer_Triangle"},
    {rdp_tri_tex,           96,  false, true,  false, "Texture_Triangle"},
    {rdp_tri_tex_z,         112, false, true,  false, "Texture_ZBuffer_Triangle"},
    {rdp_tri_shade,         96,  false, true,  false, "Shade_Triangle"},
    {rdp_tri_shade_z,       112, false, true,  false, "Shade_ZBuffer_Triangle"},
    {rdp_tri_texshade,      160, false, true,  false, "Shade_Texture_Triangle"},
    {rdp_tri_texshade_z,    176, false, true,  false, "Shade_Texture_Z_Buffer_Triangle"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_tex_rect,          16,  false, true,  false, "Texture_Rectangle"},
    {rdp_tex_rect_flip,     16,  false, true,  false, "Texture_Rectangle_Flip"},
    {rdp_sync_load,         8,   true,  false, false, "Sync_Load"},
    {rdp_sync_pipe,         8,   true,  false, false, "Sync_Pipe"},
    {rdp_sync_tile,         8,   true,  false, false, "Sync_Tile"},
    {rdp_sync_full,         8,   true,  false, true,  "Sync_Full"},
    {rdp_set_key_gb,        8,   false, true,  false, "Set_Key_GB"},
    {rdp_set_key_r,         8,   false, true,  false, "Set_Key_R"},
    {rdp_set_convert,       8,   false, true,  false, "Set_Convert"},
    {rdp_set_scissor,       8,   false, true,  false, "Set_Scissor"},
    {rdp_set_prim_depth,    8,   false, true,  false, "Set_Prim_Depth"},
    {rdp_set_other_modes,   8,   false, true,  false, "Set_Other_Modes"},
    {rdp_load_tlut,         8,   false, true,  false, "Load_TLUT"},
    {rdp_invalid,           8,   true,  false, false, "???"},
    {rdp_set_tile_size,     8,   false, true,  false, "Set_Tile_Size"},
    {rdp_load_block,        8,   false, true,  false, "Load_Block"},
    {rdp_load_tile,         8,   false, true,  false, "Load_Tile"},
    {rdp_set_tile,          8,   false, true,  false, "Set_Tile"},
    {rdp_fill_rect,         8,   false, true,  false, "Fill_Rectangle"},
    {rdp_set_fill_color,    8,   false, true,  false, "Set_Fill_Color"},
    {rdp_set_fog_color,     8,   false, true,  false, "Set_Fog_Color"},
    {rdp_set_blend_color,   8,   false, true,  false, "Set_Blend_Color"},
    {rdp_set_prim_color,    8,   false, true,  false, "Set_Prim_Color"},
    {rdp_set_env_color,     8,   false, true,  false, "Set_Env_Color"},
    {rdp_set_combine,       8,   false, true,  false, "Set_Combine"},
    {rdp_set_texture_image, 8,   false, true,  false, "Set_Texture_Image"},
    {rdp_set_mask_image,    8,   true,  true,  true,  "Set_Mask_Image"},
    {rdp_set_color_image,   8,   false, true,  true,  "Set_Color_Image"}
};

static void rdp_cmd_run(const uint32_t* arg)
{
    uint32_t cmd_id = CMD_ID(arg);
    rdp_commands[cmd_id].handler(arg);
}

static void rdp_cmd_run_buffered(void)
{
    for (uint32_t pos = 0; pos < rdp_cmd_buf_pos; pos++) {
        rdp_cmd_run(rdp_cmd_buf[pos]);
    }
}

static void rdp_cmd_flush(void)
{
    // only run if there's something buffered
    if (rdp_cmd_buf_pos) {
        // let workers run all buffered commands in parallel
        parallel_run(rdp_cmd_run_buffered);

        // reset buffer by starting from the beginning
        rdp_cmd_buf_pos = 0;
    }
}

static void rdp_cmd_push(const uint32_t* arg, uint32_t length)
{
    // copy command data to current buffer position
    memcpy(rdp_cmd_buf + rdp_cmd_buf_pos, arg, length * sizeof(uint32_t));

    // increment buffer position and flush buffer when it is full
    if (++rdp_cmd_buf_pos >= CMD_BUFFER_COUNT) {
        rdp_cmd_flush();
    }
}

void rdp_cmd(const uint32_t* arg, uint32_t length)
{
    uint32_t cmd_id = CMD_ID(arg);

    if (rdp_commands[cmd_id].sync && config->parallel) {
        rdp_cmd_flush();
    }

    if (rdp_commands[cmd_id].singlethread || !config->parallel) {
        rdp_cmd_run(arg);
    }

    if (rdp_commands[cmd_id].multithread && config->parallel) {
        rdp_cmd_push(arg, length);
    }
}

void rdp_update(void)
{
    int i, length;
    uint32_t cmd, cmd_length;

    uint32_t** dp_reg = plugin_get_dp_registers();
    uint32_t dp_current_al = *dp_reg[DP_CURRENT] & ~7, dp_end_al = *dp_reg[DP_END] & ~7;

    *dp_reg[DP_STATUS] &= ~DP_STATUS_FREEZE;







    if (dp_end_al <= dp_current_al)
    {






        return;
    }

    length = (dp_end_al - dp_current_al) >> 2;

    ptr_onstart = rdp_cmd_ptr;









    uint32_t remaining_length = length;


    dp_current_al >>= 2;

    while (remaining_length)
    {

    int toload = remaining_length > 0x10000 ? 0x10000 : remaining_length;

    if (*dp_reg[DP_STATUS] & DP_STATUS_XBUS_DMA)
    {
        uint32_t* dmem = (uint32_t*)plugin_get_dmem();
        for (i = 0; i < toload; i ++)
        {
            rdp_cmd_data[rdp_cmd_ptr] = dmem[dp_current_al & 0x3ff];
            rdp_cmd_ptr++;
            dp_current_al++;
        }
    }
    else
    {
        for (i = 0; i < toload; i ++)
        {
            RREADIDX32(rdp_cmd_data[rdp_cmd_ptr], dp_current_al);





            rdp_cmd_ptr++;
            dp_current_al++;
        }
    }

    remaining_length -= toload;

    while (rdp_cmd_cur < rdp_cmd_ptr && !rdp_pipeline_crashed)
    {
        cmd = CMD_ID(rdp_cmd_data + rdp_cmd_cur);
        cmd_length = rdp_commands[cmd].length >> 2;



        if ((rdp_cmd_ptr - rdp_cmd_cur) < cmd_length)
        {
            if (!remaining_length)
            {

                *dp_reg[DP_START] = *dp_reg[DP_CURRENT] = *dp_reg[DP_END];
                return;
            }
            else
            {
                dp_current_al -= (rdp_cmd_ptr - rdp_cmd_cur);
                remaining_length += (rdp_cmd_ptr - rdp_cmd_cur);
                break;
            }
        }

        rdp_cmd(rdp_cmd_data + rdp_cmd_cur, cmd_length);

        if (trace_write_is_open()) {
            trace_write_cmd(rdp_cmd_data + rdp_cmd_cur, cmd_length);
        }

        rdp_cmd_cur += cmd_length;
    };
    rdp_cmd_ptr = 0;
    rdp_cmd_cur = 0;
    };

    *dp_reg[DP_START] = *dp_reg[DP_CURRENT] = *dp_reg[DP_END];
}
