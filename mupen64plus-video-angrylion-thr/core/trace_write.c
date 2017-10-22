#include "trace_write.h"
#include "core.h"
#include "msg.h"
#include "rdram.h"

#include <stdio.h>
#include <memory.h>

static FILE* fp;
static uint32_t trace_rdram[0x200000];

static void trace_write_id(char id)
{
    fputc(id, fp);
}

bool trace_write_open(const char* path)
{
    fp = fopen(path, "wb");

    return trace_write_is_open();
}

bool trace_write_is_open(void)
{
    return fp != NULL;
}

void trace_write_header(uint32_t rdram_size)
{
    fwrite(TRACE_HEADER, 4, 1, fp);
    fwrite(&rdram_size, sizeof(rdram_size), 1, fp);
}

void trace_write_cmd(uint32_t* cmd, uint32_t length)
{
    trace_write_id(TRACE_CMD);
    fputc(length & 0xff, fp);
    fwrite(cmd, sizeof(*cmd), length, fp);
}

void trace_write_rdram(uint32_t offset, uint32_t length)
{
    // drop invalid addresses
    if (!rdram_valid_idx32(offset) || !rdram_valid_idx32(offset + length - 1)) {
        msg_debug("trace_write_rdram: invalid RDRAM offset (%x) or length (%d)", offset, length);
        return;
    }

    // only write changes that aren't covered by the trace file yet
    bool changed = false;
    for (uint32_t i = offset; i < offset + length; i++) {
        uint32_t v = rdram_read_idx32(i);
        changed = changed || trace_rdram[i] != v;
        trace_rdram[i] = v;
    }

    if (!changed) {
        return;
    }

    // write rdram block
    trace_write_id(TRACE_RDRAM);
    fwrite(&offset, sizeof(offset), 1, fp);
    fwrite(&length, sizeof(length), 1, fp);

    for (uint32_t i = offset; i < offset + length; i++) {
        uint32_t v = rdram_read_idx32(i);
        fwrite(&v, sizeof(v), 1, fp);
    }
}

void trace_write_vi(uint32_t** vi_reg)
{
    trace_write_id(TRACE_VI);

    for (int32_t i = 0; i < VI_NUM_REG; i++) {
        fwrite(vi_reg[i], sizeof(uint32_t), 1, fp);
    }
}

void trace_write_reset(void)
{
    memset(trace_rdram, 0, sizeof(trace_rdram));
}

void trace_write_close(void)
{
    fclose(fp);
    fp = NULL;
}
