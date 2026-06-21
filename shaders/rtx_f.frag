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

layout ( set = 0, binding = 1 ) uniform sampler2D i_position_and_depth;
layout ( set = 0, binding = 2 ) uniform accelerationStructureEXT TLAS;

layout(location = 0) out vec4 out_color;

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

void main() 
{
    float visibility = 0.0;
    uint lights = 0;

    for( uint id_light = 0; id_light < per_frame_data.m_number_of_lights; id_light++ )
    {
        uint light_type = uint( floor( per_frame_data.m_lights[id_light].m_light_pos.a ) );
        if(light_type == 2) continue;
        visibility =+ evalVisibilityRTX(id_light);
        lights++;
    }

    out_color = vec4((visibility / lights).xxxx);
}