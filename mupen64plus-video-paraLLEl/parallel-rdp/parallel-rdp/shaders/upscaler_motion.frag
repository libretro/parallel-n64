#version 450
layout(location = 0) in vec2 vMotion;
layout(location = 0) out vec2 FragMotion;
/* 1 where a translucent primitive is the last drawn */
layout(location = 1) out float FragTranslucent;

layout(push_constant, std430) uniform Registers
{
	layout(offset = 8) float translucent;
} registers;

void main()
{
	FragMotion = vMotion;
	FragTranslucent = registers.translucent;
}
