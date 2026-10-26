#version 450
layout(location = 0) in vec2 vMotion;
layout(location = 0) out vec2 FragMotion;

void main()
{
	FragMotion = vMotion;
}
