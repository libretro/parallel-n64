#version 450
/* The motion vector pass: a primitive of the frame where it was drawn,
 * in pixels of the render size, its depth, and how far each vertex
 * moved, in render pixels, from where it is to where it was. */
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aMotion;
layout(location = 0) out vec2 vMotion;

layout(push_constant, std430) uniform Registers
{
	vec2 inv_resolution;
} registers;

void main()
{
	vec2 ndc = aPosition.xy * registers.inv_resolution * 2.0 - 1.0;
	gl_Position = vec4(ndc, aPosition.z, 1.0);
	vMotion = aMotion;
}
