#ifndef NOISE_H_
#define NOISE_H_

#include "buffers.h"

// Just some random crap. Not really important for now.
int noise8(int cookie)
{
   vec2 uv = vec2(gl_GlobalInvocationID.xy) + vec2(float(constants.seed) * 1.135415, float(cookie) * 0.2378927);
   float i = dot(vec2(1.2379, 1.98295), uv);
   float c = fract(43758.5453123 * sin(i));
   return int(255.0 * c);
}

#endif
