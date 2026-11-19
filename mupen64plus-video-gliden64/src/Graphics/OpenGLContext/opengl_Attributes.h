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
