#version 460

#extension GL_ARB_shader_draw_parameters : enable

layout( location = 2 ) in vec2 f_uvs;

//globals
struct LightData
{
    vec4 m_light_pos;
    vec4 m_radiance;
    vec4 m_attenuattion;
    mat4 m_view_proyection;
};

layout( std140, set = 0, binding = 0 ) uniform PerFrameData
{
    vec4      m_camera_pos;
    mat4      m_view;
    mat4      m_projection;
    mat4      m_view_projection;
    mat4      m_inv_view;
    mat4      m_inv_projection;
    mat4      m_inv_view_projection;
    vec4      m_clipping_planes;
    LightData m_lights[ 10 ];
    uint      m_number_of_lights;
} per_frame_data;


layout(set = 0, binding = 1) uniform SSAOKernel {
    vec4 samples[64];
} i_kernel;

layout( set = 0, binding = 2) uniform sampler2D i_noise;
layout( set = 0, binding = 3) uniform sampler2D i_position_and_depth;
layout( set = 0, binding = 4) uniform sampler2D i_normal;

layout(location = 0) out vec4 out_ssao;


void main() {
    vec2 noiseScale = vec2(textureSize(i_position_and_depth, 0)) / vec2(textureSize(i_noise, 0));

    vec3 fragPosWS   = texture(i_position_and_depth, f_uvs).xyz;
    vec3 fragPosVS = (per_frame_data.m_view * vec4(fragPosWS, 1.0)).xyz;

    vec3 normalWS = normalize(texture(i_normal, f_uvs).rgb * 2.0 - 1.0);   
    vec3 normalVS = normalize(mat3(per_frame_data.m_view) * normalWS);


    vec3 randomVec = vec3(texture(i_noise, f_uvs * noiseScale).xy,0);  
    vec3 tangent   = normalize(randomVec - normalVS * dot(randomVec, normalVS));
    vec3 bitangent = cross(normalVS, tangent);
    mat3 TBN       = mat3(tangent, bitangent, normalVS);  

    float radius = 0.5;
    float bias = 0.025;

    float occlusion = 0.0;
    for(int i = 0; i < i_kernel.samples.length(); ++i)
    {
        // get sample position
        vec3 samplePos = TBN * i_kernel.samples[i].xyz; // from tangent to view-space
        samplePos = fragPosVS + samplePos * radius; 
    
        vec4 offset = vec4(samplePos, 1.0);
        offset      = per_frame_data.m_projection * offset;    // from view to clip-space
        offset.xyz /= offset.w;               // perspective divide
        offset.xyz  = offset.xyz * 0.5 + 0.5; // transform to range 0.0 - 1.0

        // get sample position depth from the viewer's perspective
        vec3 geoPosWS = texture(i_position_and_depth, offset.xy).xyz;
        vec3 geoPosVS = (per_frame_data.m_view * vec4(geoPosWS, 1.0)).xyz;
        float sampleDepth = geoPosVS.z; 

        // use a range to make sure the fragment contributes to the occlusion factor or if it's too far behind
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPosVS.z - sampleDepth));

        // check if sample depth is larger than current depth
        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;  
    }  

    occlusion = 1.0 - (occlusion / i_kernel.samples.length());
    out_ssao = vec4(occlusion.xxxx);
}