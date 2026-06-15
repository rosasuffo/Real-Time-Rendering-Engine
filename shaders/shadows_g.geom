#version 460

layout(triangles) in;
// max_vertices = (numero de vertices de la primitiva (3)) * (numero de capas/luces (10) * (cascades (4)))
layout(triangle_strip, max_vertices = 120) out;

#extension GL_ARB_shader_draw_parameters : enable

layout( location = 0 ) in vec3 g_position[];
layout( location = 1 ) in vec3 g_normal[];        // not used, but to prevent warnings
layout( location = 2 ) in vec2 g_uv[];            // not used, but to prevent warnings
layout( location = 3 ) in flat int g_instance[];  // not used, but to prevent warnings

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



void main() {
    int MAX_CASCADES = 4;

    for (int i = 0; i < per_frame_data.m_number_of_lights; ++i) {
        
        if(per_frame_data.m_lights[i].m_type != 2){ // if not ambient
            //CASCADES
            for(uint c = 0; c < per_frame_data.m_cascades_count; c++)
            {
                mat4 light_viewproj = per_frame_data.m_lights[i].m_cascades_view_proyection[c];

                for(int v = 0; v < 3; v++)
                {
                    gl_Layer = i * MAX_CASCADES + int(c);
                    gl_Position = light_viewproj * vec4(g_position[v],1.0);
                    EmitVertex();
                }
                EndPrimitive();
            }
        }
    }
}