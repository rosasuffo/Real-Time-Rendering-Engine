#version 460

#extension GL_ARB_shader_draw_parameters : enable
#extension GL_EXT_ray_query : enable
#define INV_PI 0.31830988618
#define PI   3.14159265358979323846264338327950288

layout( location = 0 ) in vec2 f_uvs;

//globals
struct LightData
{
    vec4 m_light_pos;
    vec4 m_radiance;
    vec4 m_attenuattion;
    vec4 m_cascades_split_depth;
    mat4 m_cascades_view_proyection[ 4 ];
    uint m_type;
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
    uint      m_cascades_count;
} per_frame_data;

layout ( set = 0, binding = 1 ) uniform sampler2D i_albedo;
layout ( set = 0, binding = 2 ) uniform sampler2D i_position_and_depth;
layout ( set = 0, binding = 3 ) uniform sampler2D i_normal;
layout ( set = 0, binding = 4 ) uniform sampler2D i_material;
layout ( set = 0, binding = 5 ) uniform sampler2D i_ssao;
layout ( set = 0, binding = 6 ) uniform sampler2DArray i_shadowMap;
layout ( set = 0, binding = 7 ) uniform accelerationStructureEXT TLAS;

layout(location = 0) out vec4 out_color;

float evalVisibility(uint id_light)
{
    vec3 frag_pos = texture(i_position_and_depth,f_uvs).xyz;
    float depth = abs(frag_pos.z);

    uint cascade_idx = 0;
    for(uint c = 0; c < per_frame_data.m_cascades_count; c++){
        if(depth < per_frame_data.m_lights[id_light].m_cascades_split_depth[c]){
            cascade_idx = c;
            break;
        }
    }

    vec4 light_space_pos = per_frame_data.m_lights[ id_light ].m_cascades_view_proyection[cascade_idx] * vec4(frag_pos, 1.f);

    vec3 proj_coords = light_space_pos.xyz / light_space_pos.w; // Perspective divide
    proj_coords.xy = proj_coords.xy * 0.5 + 0.5; // Transform to [0,1] range

    // Sample the shadow map
    float csm_idx = id_light * per_frame_data.m_cascades_count + cascade_idx;
    float shadow = texture(i_shadowMap, vec3(proj_coords.xy, csm_idx)).r;
  
    // Compare the depth of the fragment with the depth stored in the shadow map
    float bias = 0.005; // Bias to prevent shadow acne

    //return shadow;
    return (proj_coords.z) > shadow ? 0.0 : 1.0;
}

float random(vec2 seed){
    return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
}

vec3 sampleCone(vec3 central_dir, float max_angle_radians, vec2 seed)
{
    // para definir una direccion tridimensional dentro de un cono, necesido 2 angulos
    // azimuth (entre 0 y 2PI) controla rotacion alrededor del eje central
    // theta (entre 0 y max_angle) controla la apertura
    float azimuth = 2 * PI * random(seed);
    float cos_theta = 1.0 - random(seed + vec2(1.0, 1.0)) * (1.0 - cos(max_angle_radians));
    float sin_theta = sqrt(1.0 - cos_theta * cos_theta);

    // vector en espacio local del cono alineado con el eje Z
    vec3 local_dir = vec3(cos(azimuth) * sin_theta, sin(azimuth) * sin_theta, cos_theta);

    // se construye una base ortonormal para poder transformar el cono a WS
    vec3 cz = central_dir;
    vec3 tangent = abs(cz.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 cx = normalize(cross(tangent, cz));
    vec3 cy = cross(cz, cx);

    return cx * local_dir.x + cy * local_dir.y + cz * local_dir.z;
}

float evalVisibilityRTX(uint id_light)
{
    rayQueryEXT rayQuery;

    vec3 light_pos = per_frame_data.m_lights[id_light].m_light_pos.xyz;
    vec3 frag_pos = texture(i_position_and_depth,f_uvs).xyz;

    vec3 fragment_to_light = light_pos - frag_pos;
    vec3 light_direction = normalize(fragment_to_light);
    float ray_max_distance = length(fragment_to_light);

    float shade = 0.0;
    uint num_rays = 64;
    float max_angle_radians = 0.05;
    for(uint i = 0; i < num_rays; i++)
    {
        vec2 seed = frag_pos.xy + vec2(float(i) * 5.23, float(i) * 7.89);
        vec3 ray_direction = sampleCone(light_direction, max_angle_radians, seed);

        rayQueryInitializeEXT(
            rayQuery,
            TLAS,    
            gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT | gl_RayFlagsSkipClosestHitShaderEXT, 
                // flags de optimizacion. TerminateOnFirstHit para detener la busqueda cuando hay un obstaculo.
                // Opaque para considerar toda la geometria como opaca. SkipClosestHitShader porque solo interesa
                // saber si hay colision, el resto de informacion da igual.
            0xFF, // cullMask. comprueba todas las geometrias visibles
            frag_pos, // origen rayo = posicion del fragmento
            0.001, // distancia minima del rayo
            ray_direction,
            ray_max_distance
        );

        // ver si ha colisionado con algo
        while(rayQueryProceedEXT(rayQuery)) {}
        uint intersection_type = rayQueryGetIntersectionTypeEXT(rayQuery, true);

            // si ha colisionado, sombra, return 0. si no, return 1.
        if(intersection_type == gl_RayQueryCommittedIntersectionNoneEXT){
            shade += 1.0;
        }
    }

    shade /= num_rays;
    return shade;
}

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
        vec3 value = vec3(0.0);

        switch( light_type )
        {
            case 0: //directional
            {
                vec3 l = normalize( light.m_light_pos.xyz );
                value = max( dot( n, l ), 0.0 ) * albedo.rgb;
                break;
            }
            case 1: //point
            {
                vec3 l = light.m_light_pos.xyz - frag_pos;
                float dist = length( l );
                float att = 1.0 / (light.m_attenuattion.x + light.m_attenuattion.y * dist + light.m_attenuattion.z * dist * dist );
                vec3 radiance = light.m_radiance.rgb * att;

                value = max( dot( n, l ), 0.0 ) * albedo.rgb * radiance;
                break;
            }
            case 2: //ambient
            {
                value = light.m_radiance.rgb * albedo.rgb;
                break;
            }
        }
        float visibility = evalVisibilityRTX(id_light);
        shading += value * visibility;
        //shading += value;
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
    for(uint i = 0; i <per_frame_data.m_number_of_lights; i++){
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

        vec3 value = (diffuse + specular) * light.m_radiance.rgb * NdotL;
        float visibility = evalVisibilityRTX(i);

        shading += value * visibility;

        //shading += (diffuse + specular) * light.m_radiance.rgb * NdotL;
    }

    return shading;
}

void main() 
{
    float id_material = texture(i_material, f_uvs).r;

    float gamma = 2.2f;
    float exposure = 0.0f;

    vec3 mapped;
    if(id_material == 0.0f){
        mapped = evalDiffuse();
    } else {
        mapped = evalMicrofacets();
    }

    out_color = vec4( pow( mapped, vec3( 1.0f / gamma ) ), 1.0 );

    float ssao = texture(i_ssao, f_uvs).r;
    //out_color *= ssao;
    //out_color = evalVisibility(0).xxxx;

}