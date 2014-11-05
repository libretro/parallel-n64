#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#else
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <SDL.h>
#endif // _WIN32
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <math.h>
#include "glide.h"
#include "g3ext.h"
#include "main.h"
#include "m64p.h"
#include "glitchlog.h"

#include <IL/il.h>

#include "glitchdebug.h"

// VP debug
int dumping = 0;
static int tl_i;
static int tl[10240];

void dump_start(void)
{
  static int init;
  if (!init) {
    init = 1;
    ilInit();
    ilEnable(IL_FILE_OVERWRITE);
  }
  dumping = 1;
  tl_i = 0;
}

void dump_stop(void)
{
  if (!dumping)
     return;

  int i, j;
  for (i=0; i<nb_fb; i++) {
    dump_tex(fbs[i].texid);
  }
  dump_tex(default_texture);
  dump_tex(depth_texture);

  dumping = 0;

  glReadBuffer(GL_FRONT);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, frameBuffer);
  ilTexImage(width, height, 1, 4, IL_RGBA, IL_UNSIGNED_BYTE, frameBuffer);
  ilSaveImage("dump/framecolor.png");
  glReadPixels(0, 0, width, height, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, depthBuffer);
  //   FILE * fp = fopen("glide_depth1.bin", "rb");
  //   fread(depthBuffer, 2, width*height, fp);
  //   fclose(fp);
  for (j=0; j<height; j++) {
    for (i=0; i<width; i++) {
      //uint16_t d = ( (uint16_t *)depthBuffer )[i+(height-1-j)*width]/2 + 0x8000;
      uint16_t d = ( (uint16_t *)depthBuffer )[i+j*width];
      uint32_t c = ( (uint32_t *)frameBuffer )[i+j*width];
      ( (unsigned char *)frameBuffer )[(i+j*width)*3] = d&0xff;
      ( (unsigned char *)frameBuffer )[(i+j*width)*3+1] = d>>8;
      ( (unsigned char *)frameBuffer )[(i+j*width)*3+2] = c&0xff;
    }
  }
  ilTexImage(width, height, 1, 3, IL_RGB, IL_UNSIGNED_BYTE, frameBuffer);
  ilSaveImage("dump/framedepth.png");

  for (i=0; i<tl_i; i++) {
    glBindTexture(GL_TEXTURE_2D, tl[i]);
    GLint w, h, fmt;
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmt);
    fprintf(stderr, "Texture %d %dx%d fmt %x\n", tl[i], (int)w, (int)h, (int) fmt);

    uint32_t * pixels = (uint32_t *) malloc(w*h*4);
    // 0x1902 is another constant meaning GL_DEPTH_COMPONENT
    // (but isn't defined in gl's headers !!)
    if (fmt != GL_DEPTH_COMPONENT && fmt != 0x1902) {
      glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
      ilTexImage(w, h, 1, 4, IL_RGBA, IL_UNSIGNED_BYTE, pixels);
    } else {
      glGetTexImage(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, pixels);
      int i;
      for (i=0; i<w*h; i++)
        ((unsigned char *)frameBuffer)[i] = ((unsigned short *)pixels)[i]/256;
      ilTexImage(w, h, 1, 1, IL_LUMINANCE, IL_UNSIGNED_BYTE, frameBuffer);
    }
    char name[128];
    //     sprintf(name, "mkdir -p dump ; rm -f dump/tex%04d.png", i);
    //     system(name);
    sprintf(name, "dump/tex%04d.png", i);
    fprintf(stderr, "Writing '%s'\n", name);
    ilSaveImage(name);

    //     SDL_FreeSurface(surf);
    free(pixels);
  }
  glBindTexture(GL_TEXTURE_2D, default_texture);
}

void dump_tex(int id)
{
  if (!dumping) return;

  int n;
  // yes, it's inefficient
  for (n=0; n<tl_i; n++)
    if (tl[n] == id)
      return;

  tl[tl_i++] = id;

  int i = tl_i-1;
}

void bufferswap_vpdebug(void)
{
   dump_stop();
   SDL_Event event;
   while (SDL_PollEvent(&event))
   {
      switch (event.type)
      {
         case SDL_KEYDOWN:
            switch (event.key.keysym.sym)
            {
               case 'd':
                  printf("Dumping !\n");
                  dump_start();
                  break;
               case 'w':
                  {
                     static int wireframe;
                     wireframe = !wireframe;
                     glPolygonMode(GL_FRONT_AND_BACK, wireframe? GL_LINE : GL_FILL);
                     break;
                  }
            }
            break;
      }
   }
}
