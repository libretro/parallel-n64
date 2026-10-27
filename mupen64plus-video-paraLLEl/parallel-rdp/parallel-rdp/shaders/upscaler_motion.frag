#version 450
layout(location = 0) in vec2 vMotion;
layout(location = 1) flat in vec3 vPlaneA0;
layout(location = 2) flat in vec3 vPlaneA1;
layout(location = 3) flat in vec3 vPlaneAW;
layout(location = 4) flat in vec3 vPlaneD0;
layout(location = 5) flat in vec3 vPlaneD1;
layout(location = 6) flat in vec3 vPlaneDW;
layout(location = 0) out vec2 FragMotion;
/* 1 where a translucent primitive is the last drawn */
layout(location = 1) out float FragTranslucent;

layout(push_constant, std430) uniform Registers
{
	layout(offset = 8) float translucent;
	/* the pixel the planes are from: the centre, jittered */
	layout(offset = 16) vec2 origin;
	/* farther than this, in pixels, nothing is the same thing */
	layout(offset = 24) float farthest;
} registers;

void main()
{
	vec2 motion = vMotion;
	if (vPlaneAW != vec3(0.0))
	{
		/* The pixel was where the primitive before, planes A + D, gave
		 * the attributes over w it has here, u and v: on the two lines
		 * (A + D - u AW - u DW) . x' = 0. With x' = x + d and
		 * (A - u AW) . x = 0 that is a 2x2 system in d, exact in it,
		 * and only the differences D carry the motion. */
		vec3 x = vec3(gl_FragCoord.xy - registers.origin, 1.0);
		float w = dot(vPlaneAW, x);
		if (w != 0.0)
		{
			float u = dot(vPlaneA0, x) / w;
			float v = dot(vPlaneA1, x) / w;
			vec3 l0 = vPlaneA0 + vPlaneD0 - u * (vPlaneAW + vPlaneDW);
			vec3 l1 = vPlaneA1 + vPlaneD1 - v * (vPlaneAW + vPlaneDW);
			float r0 = dot(vPlaneD0 - u * vPlaneDW, x);
			float r1 = dot(vPlaneD1 - v * vPlaneDW, x);
			float det = l0.x * l1.y - l0.y * l1.x;
			if (det != 0.0)
			{
				vec2 d = vec2(r1 * l0.y - r0 * l1.y, r0 * l1.x - r1 * l0.x) / det;
				if (abs(d.x) <= registers.farthest && abs(d.y) <= registers.farthest)
					motion = d;
			}
		}
	}
	FragMotion = motion;
	FragTranslucent = registers.translucent;
}
