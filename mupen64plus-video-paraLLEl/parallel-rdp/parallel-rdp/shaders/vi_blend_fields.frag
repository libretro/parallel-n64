#version 450
#extension GL_EXT_samplerless_texture_functions : require

layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform texture2D uImage;

void main()
{
    // A persistent pixel does not propagate more than one frame.
    vec4 input_pixel = texelFetch(uImage, ivec2(gl_FragCoord.xy), 0);
    FragColor = vec4(input_pixel.rgb * input_pixel.a, 0.0);
}