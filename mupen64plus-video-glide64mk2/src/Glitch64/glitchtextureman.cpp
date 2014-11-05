/*
* Glide64 - Glide video plugin for Nintendo 64 emulators.
* Copyright (c) 2002  Dave2001
* Copyright (c) 2003-2009  Sergey 'Gonetz' Lipski
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#ifdef _WIN32
#include <windows.h>
#else // _WIN32
#include <stdlib.h>
#endif // _WIN32
#include "glide.h"
#include "main.h"
#include <stdio.h>

typedef struct _texlist
{
  unsigned int id;
  struct _texlist *next;
} texlist;

static int nbTex = 0;
static texlist *list = NULL;

void remove_tex(unsigned int idmin, unsigned int idmax)
{
  unsigned int *t;
  int n = 0;
  texlist *aux = list;
  int sz = nbTex;
  if (aux == NULL) return;
  t = (unsigned int*)malloc(sz * sizeof(int));
  while (aux && aux->id >= idmin && aux->id < idmax)
  {
    if (n >= sz)
      t = (unsigned int *) realloc(t, ++sz*sizeof(int));
    t[n++] = aux->id;
    aux = aux->next;
    free(list);
    list = aux;
    nbTex--;
  }
  while (aux != NULL && aux->next != NULL)
  {
    if (aux->next->id >= idmin && aux->next->id < idmax)
    {
      texlist *aux2 = aux->next->next;
      if (n >= sz)
        t = (unsigned int *) realloc(t, ++sz*sizeof(int));
      t[n++] = aux->next->id;
      free(aux->next);
      aux->next = aux2;
      nbTex--;
    }
    aux = aux->next;
  }
  glDeleteTextures(n, t);
  free(t);
  //printf("RMVTEX nbtex is now %d (%06x - %06x)\n", nbTex, idmin, idmax);
}

void add_tex(unsigned int id)
{
  texlist *aux = list;
  texlist *aux2;
  //printf("ADDTEX nbtex is now %d (%06x)\n", nbTex, id);
  if (list == NULL || id < list->id)
  {
    nbTex++;
    list = (texlist*)malloc(sizeof(texlist));
    list->next = aux;
    list->id = id;
    return;
  }
  while (aux->next != NULL && aux->next->id < id) aux = aux->next;
  // ZIGGY added this test so that add_tex now accept re-adding an existing texture
  if (aux->next != NULL && aux->next->id == id) return;
  nbTex++;
  aux2 = aux->next;
  aux->next = (texlist*)malloc(sizeof(texlist));
  aux->next->id = id;
  aux->next->next = aux2;
}

GLuint get_tex_id(unsigned int id)
{
   return id;
}
