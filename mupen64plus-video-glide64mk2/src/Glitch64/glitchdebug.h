#ifndef _GLITCH_DEBUG_VP_H
#define _GLITCH_DEBUG_VP_H

void dump_start(void);

void dump_stop(void);

void dump_tex(int id);

void bufferswap_vpdebug(void);

extern int dumping;

#endif
