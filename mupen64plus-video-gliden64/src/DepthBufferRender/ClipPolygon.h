#ifndef CLIP_POLYGON_H
#define CLIP_POLYGON_H

#define VISIBLE     0
#define LEFT        1
#define RIGHT       2
#define TOP         4
#define BOT         8

typedef struct vertexclip
{
	float x,y,z;
	int visible;
} vertexclip;

#ifdef __cplusplus
extern "C" {
#endif


/*
 *      vbp is a pointer to a vertex array. The first 3 vertices in that
 *      array is our source vertices. The rest of the array will be used
 *      to hold new vertices created during clipping.
 *
 *      you can then access the new vertices using the *final variable
 *
 *      function returns the number of vertices in the resulting polygon
 */
int gli_ClipPolygon(vertexclip *** final, vertexclip * vbp, int numVertices);

#ifdef __cplusplus
}
#endif

#endif /* CLIP_POLYGON_H */
