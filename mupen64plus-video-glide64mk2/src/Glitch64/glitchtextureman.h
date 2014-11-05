#ifndef _GLITCH_TEXTURE_MAN_H
#define _GLITCH_TEXTURE_MAN_H

void init_textureman(void);

void remove_tex(unsigned int idmin, unsigned int idmax);

void add_tex(unsigned int id);

GLuint get_tex_id(unsigned int id);

#endif
