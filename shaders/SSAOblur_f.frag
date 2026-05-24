#version 460

#extension GL_ARB_shader_draw_parameters : enable

layout( location = 2 ) in vec2 f_uvs;

layout( set = 0, binding = 0) uniform sampler2D i_ssao;

layout(location = 0) out float out_blur;

const vec2 noiseScale = vec2(800.0/4.0, 600.0/4.0); 

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(i_ssao, 0));
    float result = 0.0;
    for (int x = -2; x < 2; ++x) 
    {
        for (int y = -2; y < 2; ++y) 
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            result += texture(i_ssao, f_uvs + offset).r;
        }
    }
    out_blur = result / (4.0 * 4.0);
}