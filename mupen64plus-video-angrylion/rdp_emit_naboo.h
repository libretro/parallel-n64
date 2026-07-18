#ifndef RDP_EMIT_NABOO_H
#define RDP_EMIT_NABOO_H

#include "rdp_emit_f3dex2.h"

#define NABOO_R_DONE       0
#define NABOO_R_FALLBACK  (-1)

void naboo_set_rdram(unsigned char *rdram);
void naboo_set_rdram_size(unsigned int size);
void naboo_task_reset(unsigned int dl);
int  naboo_run_dl(RdpFifo *fifo, unsigned int dl_addr, int resume);

#endif
