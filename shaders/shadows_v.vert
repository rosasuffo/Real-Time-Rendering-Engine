#version 460

#extension GL_ARB_shader_draw_parameters : enable

//inputs
layout( location = 0 ) in vec3 v_positions;
layout( location = 1 ) in vec3 v_normals;
layout( location = 2 ) in vec2 v_uvs;

//globals
struct LightData
{
    vec4 m_light_pos;
    vec4 m_radiance;
    vec4 m_attenuattion;
    mat4 m_view_projection;
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
    vec4      m_clipping_planes; // near, far, 0, 0
    LightData m_lights[ 10 ];
    uint      m_number_of_lights;
} per_frame_data;


struct ObjectData
{
    mat4 m_model;
    vec4 m_albedo; 
    vec4 m_metallic_roughness;
};

//all object matrices
layout(std140,set = 1, binding = 0) readonly buffer ObjectBufferData
{
    ObjectData objects[];
} per_object_data;


layout( location = 0 ) out vec3 f_position;
layout( location = 1 ) out vec3 f_normal;
layout( location = 2 ) out vec2 f_uv;
layout( location = 3 ) out flat int f_instance;

mat4 lightProjection(float left, float right, float bottom, float top, float near, float far){
    mat4 res = mat4(1.0);
    
    res[0][0] = 2.0 / (right - left);
    res[1][1] = 2.0 / (bottom - top); // Nótese el cambio para invertir el eje Y de Vulkan
    res[2][2] = 1.0 / (far - near);
    
    res[3][0] = -(right + left) / (right - left);
    res[3][1] = -(bottom + top) / (bottom - top);
    res[3][2] = -near / (far - near);
    
    return res;
}

mat4 lightLookAt(vec3 eye, vec3 center, vec3 up){
    vec3 f = normalize(center - eye);  // forward
    vec3 r = normalize(cross(f, up));  // right
    vec3 u = cross(r, f);              // up

    mat4 res = mat4(1.0);
    
    res[0][0] = r.x;
    res[0][1] = u.x;
    res[0][2] = -f.x;
    res[0][3] = 0.0;
    
    res[1][0] = r.y;
    res[1][1] = u.y;
    res[1][2] = -f.y;
    res[1][3] = 0.0;
    
    res[2][0] = r.z;
    res[2][1] = u.z;
    res[2][2] = -f.z;
    res[2][3] = 0.0;
    
    res[3][0] = -dot(r, eye);
    res[3][1] = -dot(u, eye);
    res[3][2] = dot(f, eye);
    res[3][3] = 1.0;
    
    return res;
}

void main() {
    // orthographic proyection matrix
    mat4 lightProj = lightProjection(-10.0,10.0,-10.0,10.0,per_frame_data.m_clipping_planes.x,per_frame_data.m_clipping_planes.y);

    // light lookat
    vec3 lightPos = vec3(0.0, 1.0, 2.0);
    vec3 target   = vec3(0.0, 0.0, 0.0);
    vec3 up       = vec3(0.0, 1.0, 0.0);
    mat4 lightView = lightLookAt(lightPos,target,up);

    // light space transformation matrix that transforms each world-space vector into the space as visible from the light source
    mat4 lightSpaceMatrix = lightProj * lightView; 


    //pos in world space
    vec4 pos = per_object_data.objects[ gl_BaseInstance ].m_model * vec4(v_positions, 1.0);
    //pos in light space
    pos = lightSpaceMatrix * pos;
    f_position = pos.xyz;
    

    //normal in world space
    mat3 normal_matrix = transpose( inverse( mat3( per_object_data.objects[ gl_BaseInstance ].m_model ) ) );    
    f_normal = normal_matrix * v_normals;

    // uv
    f_uv = v_uvs;

    //progate the id
    f_instance = gl_BaseInstance;

    gl_Position = per_frame_data.m_projection * per_frame_data.m_view * pos;
}