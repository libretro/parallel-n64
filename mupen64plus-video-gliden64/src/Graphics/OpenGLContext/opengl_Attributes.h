#pragma once
#include "GLFunctions.h"

/* The vertex attributes' locations, for the triangles and for the rects */
enum
{
	GLI_TRI_ATTRIB_POSITION = 0,
	GLI_TRI_ATTRIB_COLOR = 1,
	GLI_TRI_ATTRIB_TEXCOORD = 2,
	GLI_TRI_ATTRIB_NUMLIGHTS = 3,
	GLI_TRI_ATTRIB_MODIFY = 4,
	GLI_RECT_ATTRIB_POSITION = 5,
	GLI_RECT_ATTRIB_TEXCOORD0 = 6,
	GLI_RECT_ATTRIB_TEXCOORD1 = 7
};

#define MaxAttribIndex 8

#ifdef __cplusplus
namespace opengl {

	/* Triangle attributes */
	namespace triangleAttrib {
		static const GLuint position = GLI_TRI_ATTRIB_POSITION;
		static const GLuint color = GLI_TRI_ATTRIB_COLOR;
		static const GLuint texcoord = GLI_TRI_ATTRIB_TEXCOORD;
		static const GLuint numlights = GLI_TRI_ATTRIB_NUMLIGHTS;
		static const GLuint modify = GLI_TRI_ATTRIB_MODIFY;
	}

	/* Rect attributes */
	namespace rectAttrib {
		static const GLuint position = GLI_RECT_ATTRIB_POSITION;
		static const GLuint texcoord0 = GLI_RECT_ATTRIB_TEXCOORD0;
		static const GLuint texcoord1 = GLI_RECT_ATTRIB_TEXCOORD1;
	}
}
#endif
