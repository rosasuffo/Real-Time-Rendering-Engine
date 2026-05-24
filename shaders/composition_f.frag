#version 460

#extension GL_ARB_shader_draw_parameters : enable
#define INV_PI 0.31830988618
#define PI   3.14159265358979323846264338327950288

layout( location = 0 ) in vec2 f_uvs;

//globals
struct LightData
{
    vec4 m_light_pos;
    vec4 m_radiance;
    vec4 m_attenuattion;
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

layout ( set = 0, binding = 1 ) uniform sampler2D i_albedo;
layout ( set = 0, binding = 2 ) uniform sampler2D i_position_and_depth;
layout ( set = 0, binding = 3 ) uniform sampler2D i_normal;
layout ( set = 0, binding = 4 ) uniform sampler2D i_material;
layout ( set = 0, binding = 5 ) uniform sampler2D i_ssao;
layout ( set = 0, binding = 6 ) uniform sampler2D i_shadowMap;


layout(location = 0) out vec4 out_color;


vec3 evalDiffuse()
{
    vec4  albedo       = texture( i_albedo  , f_uvs );
    vec3  n            = normalize( texture( i_normal, f_uvs ).rgb * 2.0 - 1.0 );    
    vec3  frag_pos     = texture( i_position_and_depth, f_uvs ).xyz;
    vec3  shading = vec3( 0.0 );


    for( uint id_light = 0; id_light < per_frame_data.m_number_of_lights; id_light++ )
    {
        LightData light = per_frame_data.m_lights[ id_light ];
        uint light_type = uint( floor( light.m_light_pos.a ) );

        switch( light_type )
        {
            case 0: //directional
            {
                vec3 l = normalize( light.m_light_pos.xyz );
                shading += max( dot( n, l ), 0.0 ) * albedo.rgb;
                break;
            }
            case 1: //point
            {
                vec3 l = light.m_light_pos.xyz - frag_pos;
                float dist = length( l );
                float att = 1.0 / (light.m_attenuattion.x + light.m_attenuattion.y * dist + light.m_attenuattion.z * dist * dist );
                vec3 radiance = light.m_radiance.rgb * att;

                shading += max( dot( n, l ), 0.0 ) * albedo.rgb * radiance;
                break;
            }
            case 2: //ambient
            {
                shading += light.m_radiance.rgb * albedo.rgb;
                break;
            }
        }
    }

    return shading;
}

vec3 evalMicrofacets(){
    vec3  pos          = texture( i_position_and_depth, f_uvs ).xyz;
    vec3  N            = normalize( texture( i_normal, f_uvs ).rgb * 2.0 - 1.0 );    
    vec3  V            = normalize(per_frame_data.m_camera_pos.xyz - pos);

    // material
    vec4 mat = texture(i_material, f_uvs);
    float alpha = mat.b * mat.b;
    float metallic = mat.g;
    vec4  albedo = texture( i_albedo  , f_uvs );

    vec3  shading      = vec3( 0.0 );
    for(int i = 0; i <per_frame_data.m_number_of_lights; i++){
        LightData light = per_frame_data.m_lights[i];
        uint light_type = uint( floor( light.m_light_pos.a ) );

        if(light_type == 2){
            shading += light.m_radiance.rgb * albedo.rgb;
            continue;
        }

        vec3 L = (light_type == 0) ? light.m_light_pos.xyz : light.m_light_pos.xyz - pos;
        L = normalize(L);
        vec3 H = normalize(L + V);

        float NdotH = max(dot(N,H), 0.0);
        float NdotL = max(dot(N,L), 0.0);
        float VdotH = max(dot(V,H), 0.0);
        float NdotV = max(dot(N,V), 0.0);

        // Normal distribution function
        float D = alpha / (PI * pow((pow(NdotH,2)) * (alpha-1) + 1, 2));

        // Geometric term
        float k = pow(mat.b + 1, 2) * 0.125;
        float Gl = NdotL / ((NdotL * (1-k)) + k);
        float Gv = NdotV / ((NdotV * (1-k)) + k);
        float G = Gl * Gv;

        // Fresnel with spheric gaussian
        vec3 F0 = mix(vec3(0.04), albedo.rgb, metallic);
        float p = -5.55473 * VdotH - 6.98316 * VdotH * VdotH;
        vec3 F = F0 + (1 - F0) * exp2(p);

        vec3 num = (D * F * G);
        float den = max(4 * NdotL * NdotV, 0.0001);
        vec3 specular = num / den;    

        // Lambert
        vec3 ks = F;
        vec3 kd = (1-ks) * (1-metallic);
        vec3 diffuse = (kd * albedo.rgb) / PI;

        shading += (diffuse + specular) * light.m_radiance.rgb * NdotL;
    }

    return shading;
}

void main() 
{
    float id_material = texture(i_material, f_uvs).r;

    float gamma = 2.2f;
    float exposure = 1.0f;
    vec3 mapped;

    if(id_material == 0.0f){
        mapped = vec3( 1.0f ) - exp(-evalDiffuse() * exposure);
    } else {
        vec3 diffuse = evalDiffuse();
        vec3 ks = vec3(0);
        vec3 spec = evalMicrofacets();
        vec3 kd = 1.0 - ks;

        vec3 light = spec;
        mapped = vec3( 1.0f ) - exp(light * exposure);
    }

    out_color = vec4( pow( mapped, vec3( 1.0f / gamma ) ), 1.0 );

    float ssao = texture(i_ssao, f_uvs).r;
    out_color *= ssao;
}