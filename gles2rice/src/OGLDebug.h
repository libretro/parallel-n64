/* OGLDebug.h
Copyright (C) 2009 Richard Goedeken

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#if !defined(OPENGL_DEBUG_H)
#define OPENGL_DEBUG_H

#if defined(OPENGL_DEBUG)
    #define OPENGL_CHECK_ERRORS { const GLenum errcode = glGetError(); if (errcode != GL_NO_ERROR) fprintf(stderr, "OpenGL Error code %i in '%s' line %i\n", errcode, __FILE__, __LINE__-1); }
#else
    #define OPENGL_CHECK_ERRORS
#endif

#endif /* OPENGL_DEBUG_H */
