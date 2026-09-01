/* Replay a frame captured by AL_CAPTURE (ALCAP4) at a chosen upscale
 * factor. The capture is self-contained: RDRAM, hidden RDRAM, VI regs,
 * and the exact RDP command stream of one real game frame.
 *
 *   angrylion_replay capture.alcap SCALE [dump.bin]
 */
#include "n64video.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

void msg_error(const char *e, ...) { (void)e; }
void msg_warning(const char *e, ...) { (void)e; }
void msg_debug(const char *e, ...) { (void)e; }
struct n64video_config;
void vdac_init(struct n64video_config *c) { (void)c; }
void vdac_write(void *fb, int w, int h, int p, int o) { (void)fb;(void)w;(void)h;(void)p;(void)o; }
void vdac_sync(int i) { (void)i; }
void vdac_close(void) {}
int aleck64_e90_overlay(void *a,int b,int c,int d){(void)a;(void)b;(void)c;(void)d;return 0;}

static uint8_t rdram[0x800000];
static uint8_t hidden[0x800000/2];
static uint32_t dp_reg_s[16], vi_reg_s[16];
static uint32_t *dp_reg[16], *vi_reg[16];
static uint32_t mi_intr;
static void dummy_intr(void) {}

int main(int argc, char **argv)
{
    FILE *f; char magic[6];
    uint32_t sz, i, n, scale, vr[16];
    uint32_t *cmds = NULL; size_t cap = 0, len = 0;
    struct n64video_config cfg;

    if (argc < 3) { fprintf(stderr,"usage: %s capture.alcap SCALE [dump.bin]\n",argv[0]); return 2; }
    scale = (uint32_t)atoi(argv[2]);
    f = fopen(argv[1],"rb"); if(!f){perror(argv[1]);return 1;}
    if (fread(magic,1,6,f)!=6 || memcmp(magic,"ALCAP5",6)){fprintf(stderr,"bad capture\n");return 1;}
    if (fread(&sz,4,1,f)!=1 || sz>sizeof(rdram)) return 1;
    { uint32_t hsz; if(fread(&hsz,4,1,f)!=1) return 1; if(hsz>sizeof(hidden)) return 1;
      if (fread(vr,4,16,f)!=16) return 1;
      if (fread(rdram,1,sz,f)!=sz) return 1;
      if (fread(hidden,1,hsz,f)!=hsz) return 1; }
    for (i=0;i<16;i++) vi_reg_s[i]=vr[i];
    { long here=ftell(f); fseek(f,0,SEEK_END); long end=ftell(f); fseek(f,here,SEEK_SET);
      len=(end-here)/4; if(len && ((uint32_t*)0), 1){ cmds=malloc(len*4); if(fread(cmds,4,len,f)!=len)return 1; }
      while(len>0 && cmds[len-1]==0) len--; }   /* drop trailing terminator */
    fclose(f);
    fprintf(stderr,"replay: %zu command words, scale %u, VI_ORIGIN=%06x WIDTH=%u\n", len, scale, vi_reg_s[1]&0xffffff, vi_reg_s[2]&0xfff);

    /* command list into a scratch area the frame does not draw into */
    { uint32_t scratch=0x780000; if(len*4 > 0x800000-scratch){fprintf(stderr,"cmd list too big\n");return 1;}
      memcpy(rdram+scratch, cmds, len*4);
      for(i=0;i<16;i++){dp_reg[i]=&dp_reg_s[i];vi_reg[i]=&vi_reg_s[i];}
      dp_reg_s[0]=scratch; dp_reg_s[1]=scratch; dp_reg_s[2]=scratch+len*4; dp_reg_s[3]=0; }

    memset(&cfg,0,sizeof(cfg));
    cfg.gfx.rdram=rdram; cfg.gfx.rdram_size=sz; cfg.gfx.dmem=rdram;
    cfg.gfx.dp_reg=dp_reg; cfg.gfx.vi_reg=vi_reg;
    cfg.gfx.mi_intr_reg=&mi_intr; cfg.gfx.mi_intr_cb=dummy_intr;
    cfg.parallel=true; cfg.num_workers=4; cfg.dp.compat=DP_COMPAT_HIGH; cfg.upscale=scale;

    n64video_init(&cfg);
    n64video_process_list();

    if (argc>3){
        FILE *d=fopen(argv[3],"wb");
        if(d){ uint32_t origin=vi_reg_s[1]&0xffffff, vw=vi_reg_s[2]&0xfff; if(!vw)vw=320;
               if(scale>1) n64video_resolve_for_display(origin);
               fwrite(rdram+origin,2,vw*240,d);
               fprintf(stderr,"dumped origin=%06x width=%u\n",origin,vw);
               fclose(d); }
    }
    n64video_close(); free(cmds); return 0;
}
