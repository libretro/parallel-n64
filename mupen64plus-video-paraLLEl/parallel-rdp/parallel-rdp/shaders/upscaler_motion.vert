#version 450
/* The motion vector pass: a primitive of the frame where it was drawn,
 * in pixels of the render size, its depth, and how far each vertex
 * moved, in render pixels, from where it is to where it was; and the
 * attribute planes of the primitive and of the one it was, which say
 * where each of its pixels was. */
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aMotion;
layout(location = 2) in vec3 aPlaneA0;
layout(location = 3) in vec3 aPlaneA1;
layout(location = 4) in vec3 aPlaneAW;
layout(location = 5) in vec3 aPlaneB0;
layout(location = 6) in vec3 aPlaneB1;
layout(location = 7) in vec3 aPlaneBW;
layout(location = 0) out vec2 vMotion;
layout(location = 1) flat out vec3 vPlaneA0;
layout(location = 2) flat out vec3 vPlaneA1;
layout(location = 3) flat out vec3 vPlaneAW;
layout(location = 4) flat out vec3 vPlaneB0;
layout(location = 5) flat out vec3 vPlaneB1;
layout(location = 6) flat out vec3 vPlaneBW;

layout(push_constant, std430) uniform Registers
{
	vec2 inv_resolution;
} registers;

void main()
{
	vec2 ndc = aPosition.xy * registers.inv_resolution * 2.0 - 1.0;
	gl_Position = vec4(ndc, aPosition.z, 1.0);
	vMotion = aMotion;
	vPlaneA0 = aPlaneA0;
	vPlaneA1 = aPlaneA1;
	vPlaneAW = aPlaneAW;
	vPlaneB0 = aPlaneB0;
	vPlaneB1 = aPlaneB1;
	vPlaneBW = aPlaneBW;
}
