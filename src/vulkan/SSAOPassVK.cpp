#include <random>

#include "common.h"
#include "vulkan/utilsVK.h"
#include "vulkan/SSAOPassVK.h"
#include "vulkan/rendererVK.h"
#include "vulkan/deviceVK.h"
#include "vulkan/windowVK.h"
#include "runtime.h"
#include "frame.h"
#include "shaderRegistry.h"
#include "meshRegistry.h"
#include "entity.h"
#include "vulkan/meshVK.h"
#include "material.h"


using namespace MiniEngine;


SSAOPassVK::SSAOPassVK(
    const Runtime& i_runtime,
    const ImageBlock& i_normals_attachment,
    const ImageBlock& i_position_attachment,
    const ImageBlock& i_ssao_attachment) :
    RenderPassVK(i_runtime),
    m_normals_attachment(i_normals_attachment),
    m_position_attachment(i_position_attachment),
    m_ssao_attachment(i_ssao_attachment)
{
    for( auto cmd : m_command_buffer )
    {
        cmd = VK_NULL_HANDLE;
    }
}               


SSAOPassVK::~SSAOPassVK()
{
}


bool SSAOPassVK::initialize()
{
    RendererVK& renderer = *m_runtime.m_renderer;

    // load plane
    m_plane = m_runtime.m_mesh_registry->loadMesh("./scenes/quad.obj");

    assert(m_plane != nullptr);

    //SHADER STAGES
    {
        VkShaderModule vert_module        = m_runtime.m_shader_registry->loadShader( "./shaders/SSAO_v.spv"       , VK_SHADER_STAGE_VERTEX_BIT   );
        VkShaderModule frag_module     = m_runtime.m_shader_registry->loadShader( "./shaders/SSAO_f.spv"    , VK_SHADER_STAGE_FRAGMENT_BIT );

        { // difuse
            VkPipelineShaderStageCreateInfo vert_shader{};
            vert_shader.sType   = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            vert_shader.stage   = VK_SHADER_STAGE_VERTEX_BIT;
            vert_shader.module  = vert_module;
            vert_shader.pName   = "main";

            VkPipelineShaderStageCreateInfo frag_shader{};
            frag_shader.sType   = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            frag_shader.stage   = VK_SHADER_STAGE_FRAGMENT_BIT;
            frag_shader.module  = frag_module;
            frag_shader.pName   = "main";

            m_shader_stages[ 0 ] = vert_shader;
            m_shader_stages[ 1 ] = frag_shader;
        }
    }

    createRenderPass();
    createPipelines ();
    createFbo       ();

    VkCommandBufferAllocateInfo commandBufferAllocateInfo{};

    commandBufferAllocateInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandBufferAllocateInfo.commandPool        = renderer.getDevice()->getCommandPool();
    commandBufferAllocateInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandBufferAllocateInfo.commandBufferCount = 3;
    
    vkAllocateCommandBuffers( renderer.getDevice()->getLogicalDevice(), &commandBufferAllocateInfo, m_command_buffer.data() );

    return true;
}


void SSAOPassVK::shutdown()
{
    RendererVK& renderer = *m_runtime.m_renderer;

    vkFreeCommandBuffers( renderer.getDevice()->getLogicalDevice(), renderer.getDevice()->getCommandPool(), m_command_buffer.size(), m_command_buffer.data() );
    
    vkDestroyDescriptorPool     ( renderer.getDevice()->getLogicalDevice(), m_descriptor_pool, nullptr );

    vkDestroyDescriptorSetLayout( renderer.getDevice()->getLogicalDevice(), m_descriptor_set_layout[ 0 ], nullptr );
    vkDestroyPipeline           ( renderer.getDevice()->getLogicalDevice(), m_pipeline                  , nullptr );
    vkDestroyPipelineLayout     ( renderer.getDevice()->getLogicalDevice(), m_pipeline_layouts          , nullptr );
    

    for( uint32 id = 0; id < static_cast<uint32>( renderer.getWindow().getImageCount() ); id++ )
    {
        vkDestroyFramebuffer   ( renderer.getDevice()->getLogicalDevice(), m_fbos[ id ], nullptr );
    }


    vkDestroyRenderPass( renderer.getDevice()->getLogicalDevice(), m_render_pass, nullptr );
}


VkCommandBuffer SSAOPassVK::draw( const Frame& i_frame)
{
    RendererVK& renderer = *m_runtime.m_renderer;

    VkCommandBuffer& current_cmd = m_command_buffer[ renderer.getWindow().getCurrentImageId() ];

    if( current_cmd != VK_NULL_HANDLE )
    {
        VkCommandBufferResetFlags flags{};        
        vkResetCommandBuffer( current_cmd, flags );
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

    uint32_t width = 0, height = 0;
    renderer.getWindow().getWindowSize( width, height );

    VkRenderPassBeginInfo render_pass_info{};
    render_pass_info.sType                = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_info.renderPass           = m_render_pass;
    render_pass_info.framebuffer          = m_fbos[ renderer.getWindow().getCurrentImageId()];
    render_pass_info.renderArea.offset    = { 0, 0 };
    render_pass_info.renderArea.extent    = { width, height };

    std::array<VkClearValue, 1> clear_values;
    clear_values[ 0 ].color          = { { 0.0f, 0.0f, 0.0f, 0.0f } };

    render_pass_info.clearValueCount = static_cast<uint32_t>( clear_values.size() );
    render_pass_info.pClearValues    = clear_values.data();

    if( vkBeginCommandBuffer( current_cmd, &begin_info ) != VK_SUCCESS )
    {
        throw MiniEngineException( "failed to begin recording command buffer!" );
    }
    
    UtilsVK::beginRegion( current_cmd, "SSSAO Pass", Vector4f( 0.0f, 0.5f, 0.0f, 1.0f ) );
    vkCmdBeginRenderPass( current_cmd, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE );

    vkCmdBindPipeline(current_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(current_cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layouts, 0, 1, &m_descriptor_sets[renderer.getWindow().getCurrentImageId()].m_textures_descriptor, 0, nullptr);

    m_plane->draw(current_cmd, 0);
    
    vkCmdEndRenderPass(current_cmd);
    UtilsVK::endRegion(current_cmd);

    if( vkEndCommandBuffer( current_cmd ) != VK_SUCCESS )
    {
        throw MiniEngineException( "failed to record command buffer!" );
    }

    return current_cmd;
}


void SSAOPassVK::createFbo()
{
    RendererVK& renderer = *m_runtime.m_renderer;

    uint32_t width = 0, height = 0;
    renderer.getWindow().getWindowSize( width, height );

    for( size_t i = 0; i < m_fbos.size(); i++ )
    {
        std::array<VkImageView, 1> attachments;
        attachments[ 0 ] = m_ssao_attachment.m_image_view;     // SSAO attachment

        VkFramebufferCreateInfo framebuffer_create_info = {};
        framebuffer_create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        // All frame buffers use the same renderpass setup
        framebuffer_create_info.renderPass      = m_render_pass;
        framebuffer_create_info.attachmentCount = static_cast< uint32_t >( attachments.size() );
        framebuffer_create_info.pAttachments    = attachments.data();
        framebuffer_create_info.width           = width;
        framebuffer_create_info.height          = height;
        framebuffer_create_info.layers          = 1;
        // Create the framebuffer

        if( vkCreateFramebuffer( renderer.getDevice()->getLogicalDevice(), &framebuffer_create_info, nullptr, &m_fbos[ i ] ) )
        {
            throw MiniEngineException( "failed to create fbos" );
        }
    }
}



void SSAOPassVK::createRenderPass()
{
    RendererVK& renderer = *m_runtime.m_renderer;

    std::array<VkAttachmentDescription, 1> attachments = {};

    // Color  attachment
    attachments[ 0 ].format = m_ssao_attachment.m_format;
    attachments[ 0 ].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[ 0 ].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[ 0 ].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[ 0 ].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[ 0 ].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[ 0 ].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[ 0 ].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference ssao_reference = {};
    ssao_reference.attachment = 0;
    ssao_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    std::array<VkAttachmentReference, 1> attachments_references = { ssao_reference};

    VkSubpassDescription subpass_description = {};
    subpass_description.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass_description.colorAttachmentCount    = attachments_references.size();
    subpass_description.pColorAttachments       = attachments_references.data();
    subpass_description.inputAttachmentCount    = 0;
    subpass_description.pInputAttachments       = nullptr;
    subpass_description.preserveAttachmentCount = 0;
    subpass_description.pPreserveAttachments    = nullptr;
    subpass_description.pResolveAttachments     = nullptr;

    // Subpass dependencies for layout transitions
    std::array<VkSubpassDependency, 1> dependencies;

    dependencies[ 0 ].srcSubpass        = VK_SUBPASS_EXTERNAL;
    dependencies[ 0 ].dstSubpass        = 0;
    dependencies[ 0 ].srcStageMask      = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[ 0 ].dstStageMask      = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[ 0 ].srcAccessMask     = VK_ACCESS_SHADER_READ_BIT;
    dependencies[ 0 ].dstAccessMask     = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[ 0 ].dependencyFlags   = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo render_pass_info = {};
    render_pass_info.sType            = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount  = static_cast< uint32_t >( attachments.size() );
    render_pass_info.pAttachments     = attachments.data();
    render_pass_info.subpassCount     = dependencies.size();
    render_pass_info.pSubpasses       = &subpass_description;
    render_pass_info.dependencyCount  = static_cast< uint32_t >( dependencies.size() );
    render_pass_info.pDependencies    = dependencies.data();

    if( vkCreateRenderPass( renderer.getDevice()->getLogicalDevice(), &render_pass_info, nullptr, &m_render_pass ) )
    {
        throw MiniEngineException("Failed to create empty render pass");
    }
}


void SSAOPassVK::createPipelines()
{
    RendererVK& renderer = *m_runtime.m_renderer;
    
    VkVertexInputBindingDescription binding_vertex_descrition{};
    binding_vertex_descrition.binding   = 0;
    binding_vertex_descrition.stride    = sizeof(Vertex);
    binding_vertex_descrition.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 3> attribute_descriptions{};

    attribute_descriptions[ 0 ].binding   = 0;
    attribute_descriptions[ 0 ].location  = 0;
    attribute_descriptions[ 0 ].format    = VK_FORMAT_R32G32B32A32_SFLOAT;
    attribute_descriptions[ 0 ].offset    = offsetof(Vertex, m_position);
                              
    attribute_descriptions[ 1 ].binding   = 0;
    attribute_descriptions[ 1 ].location  = 1;
    attribute_descriptions[ 1 ].format    = VK_FORMAT_R32G32B32_SFLOAT;
    attribute_descriptions[ 1 ].offset    = offsetof(Vertex, m_normal);
                              
    attribute_descriptions[ 2 ].binding   = 0;
    attribute_descriptions[ 2 ].location  = 2;
    attribute_descriptions[ 2 ].format    = VK_FORMAT_R32G32_SFLOAT;
    attribute_descriptions[ 2 ].offset    = offsetof(Vertex, m_uv );

    VkPipelineVertexInputStateCreateInfo vertex_input_info{};
    vertex_input_info.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex_input_info.vertexBindingDescriptionCount   = 1;
    vertex_input_info.vertexAttributeDescriptionCount = static_cast< uint32_t >( attribute_descriptions.size() );
    vertex_input_info.pVertexBindingDescriptions      = &binding_vertex_descrition;
    vertex_input_info.pVertexAttributeDescriptions    = attribute_descriptions.data();
    vertex_input_info.flags                           = 0;

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType                    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology                 = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    input_assembly.primitiveRestartEnable   = VK_FALSE;
    input_assembly.flags                    = 0;

    VkPipelineDepthStencilStateCreateInfo depth_stencil{};
    depth_stencil.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_stencil.depthTestEnable       = VK_FALSE;
    depth_stencil.depthWriteEnable      = VK_FALSE;
    depth_stencil.depthCompareOp        = VK_COMPARE_OP_LESS;
    depth_stencil.depthBoundsTestEnable = VK_FALSE;
    depth_stencil.stencilTestEnable     = VK_FALSE;
    depth_stencil.flags                 = 0;


    VkPipelineRasterizationStateCreateInfo raster_info{};
    raster_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster_info.pNext                   = VK_NULL_HANDLE;
    raster_info.flags                   = 0;
    raster_info.depthClampEnable        = VK_FALSE;
    raster_info.rasterizerDiscardEnable = VK_FALSE;
    raster_info.polygonMode             = VkPolygonMode::VK_POLYGON_MODE_FILL;
    raster_info.cullMode                = VK_CULL_MODE_NONE;
    raster_info.frontFace               = VK_FRONT_FACE_CLOCKWISE;
    raster_info.depthBiasEnable         = VK_FALSE;
    raster_info.depthBiasConstantFactor = 0.f;
    raster_info.depthBiasClamp          = VK_FALSE;
    raster_info.depthBiasSlopeFactor    = 0.f;
    raster_info.lineWidth               = 1.f;
    
    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    color_blend_attachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo color_blending{};
    color_blending.sType                = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blending.logicOpEnable        = VK_FALSE;
    color_blending.logicOp              = VK_LOGIC_OP_COPY;
    color_blending.attachmentCount      = 1;
    color_blending.pAttachments         = &color_blend_attachment;
    color_blending.blendConstants[0]    = 0.0f;
    color_blending.blendConstants[1]    = 0.0f;
    color_blending.blendConstants[2]    = 0.0f;
    color_blending.blendConstants[3]    = 0.0f;
    color_blending.flags                = 0;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable   = VK_FALSE;
    multisampling.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;
    multisampling.flags                 = 0;


    uint32 width = 0, height = 0;
    renderer.getWindow().getWindowSize( width, height );
    VkExtent2D extend{ width, height };

    VkViewport viewport{};
    viewport.x          = 0.0f;
    viewport.y          = 0.0f;
    viewport.width      = (float) width;
    viewport.height     = (float) height;
    viewport.minDepth   = 0.0f;
    viewport.maxDepth   = 1.0f;
    
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extend;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType            = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount    = 1;
    viewport_state.pViewports       = &viewport;
    viewport_state.scissorCount     = 1;
    viewport_state.pScissors        = &scissor;
    viewport_state.flags            = 0;

    //create unfiorms 
    createDescriptorLayout();

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount         = m_descriptor_set_layout.size();
    pipeline_layout_info.pSetLayouts            = m_descriptor_set_layout.data();
    pipeline_layout_info.pPushConstantRanges    = VK_NULL_HANDLE;
    pipeline_layout_info.pushConstantRangeCount = 0;
    pipeline_layout_info.flags                  = 0;

    std::vector<VkGraphicsPipelineCreateInfo> graphic_pipelines;

    
    if( vkCreatePipelineLayout( renderer.getDevice()->getLogicalDevice(), &pipeline_layout_info, nullptr, &m_pipeline_layouts ) != VK_SUCCESS) 
    {
        throw MiniEngineException("failed to create pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType                 = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.layout                = m_pipeline_layouts;
    pipeline_info.renderPass            = m_render_pass;
    pipeline_info.basePipelineIndex     = -1;
    pipeline_info.basePipelineHandle    = VK_NULL_HANDLE;
    pipeline_info.pInputAssemblyState   = &input_assembly;
    pipeline_info.pRasterizationState   = &raster_info;
    pipeline_info.pColorBlendState      = &color_blending;
    pipeline_info.pMultisampleState     = &multisampling;
    pipeline_info.pViewportState        = &viewport_state;
    pipeline_info.pDepthStencilState    = &depth_stencil;
    pipeline_info.pDynamicState         = VK_NULL_HANDLE;
    pipeline_info.stageCount            = m_shader_stages.size();
    pipeline_info.pStages               = m_shader_stages.data();
    pipeline_info.flags                 = 0;
    pipeline_info.pVertexInputState     = &vertex_input_info;
    pipeline_info.subpass               = 0;
    
    graphic_pipelines.push_back( pipeline_info );
    

    if( vkCreateGraphicsPipelines( renderer.getDevice()->getLogicalDevice(), VK_NULL_HANDLE, 1, graphic_pipelines.data(), nullptr, &m_pipeline ) )
    {
        throw MiniEngineException("Error creating the pipeline");
    }

    createDescriptors();
}


void SSAOPassVK::createDescriptorLayout()
{
    // SSAO
    std::array<VkDescriptorSetLayoutBinding, 5> ssao_bindings = {};
    ssao_bindings[ 0 ].binding = 0;
    ssao_bindings[ 0 ].descriptorCount = 1;
    ssao_bindings[ 0 ].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ssao_bindings[ 0 ].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    ssao_bindings[ 1 ].binding = 1;
    ssao_bindings[ 1 ].descriptorCount = 1;
    ssao_bindings[ 1 ].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ssao_bindings[ 1 ].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    ssao_bindings[ 2 ].binding = 2;
    ssao_bindings[ 2 ].descriptorCount = 1;
    ssao_bindings[ 2 ].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssao_bindings[ 2 ].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    ssao_bindings[ 3 ].binding = 3;
    ssao_bindings[ 3 ].descriptorCount = 1;
    ssao_bindings[ 3 ].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssao_bindings[ 3 ].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
                   
    ssao_bindings[ 4 ].binding = 4;
    ssao_bindings[ 4 ].descriptorCount = 1;
    ssao_bindings[ 4 ].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ssao_bindings[ 4 ].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo set_ssao_info = {};
    set_ssao_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_ssao_info.pNext = nullptr;
    set_ssao_info.bindingCount = ssao_bindings.size();
    set_ssao_info.pBindings = ssao_bindings.data();

    if (VK_SUCCESS != vkCreateDescriptorSetLayout(m_runtime.m_renderer->getDevice()->getLogicalDevice(), &set_ssao_info, nullptr, &m_descriptor_set_layout[ 0 ]))
    {
        throw MiniEngineException("Error creating descriptor set");
    }
}


void SSAOPassVK::createDescriptors()
{
    std::vector<VkDescriptorPoolSize> sizes =
    {
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 30 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 30 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = 0;
    pool_info.maxSets = 30;
    pool_info.poolSizeCount = (uint32_t)sizes.size();
    pool_info.pPoolSizes = sizes.data();

    if (VK_SUCCESS != vkCreateDescriptorPool(m_runtime.m_renderer->getDevice()->getLogicalDevice(), &pool_info, nullptr, &m_descriptor_pool))
    {
        throw MiniEngineException("Error creating descriptor pool");
    }

    VkBuffer kernel = CreateKernel();
    ImageBlock noise = CreateNoiseTexture();

    for (uint32_t id = 0; id < m_runtime.m_renderer->getWindow().getImageCount(); id++)
    {
        //ssao
        //allocate one descriptor set for each frame
        VkDescriptorSetAllocateInfo alloc_ssao_info = {};
        alloc_ssao_info.pNext = nullptr;
        alloc_ssao_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_ssao_info.descriptorPool = m_descriptor_pool;
        alloc_ssao_info.descriptorSetCount = 1;
        alloc_ssao_info.pSetLayouts = &m_descriptor_set_layout[0];
        vkAllocateDescriptorSets(m_runtime.m_renderer->getDevice()->getLogicalDevice(), &alloc_ssao_info, &m_descriptor_sets[id].m_textures_descriptor);


        //information about the buffer we want to point at in the descriptor
        VkDescriptorBufferInfo binfo[2];

        // perframe
        binfo[0].buffer = m_runtime.getPerFrameBuffer()[id];
        binfo[0].offset = 0;
        binfo[0].range = sizeof(PerFrameData);

        // kernel    
        binfo[1].buffer = kernel;
        binfo[1].offset = 0;
        binfo[1].range = sizeof(SSAOKernelData);

        // noise
        VkDescriptorImageInfo noise_info;
        noise_info.sampler = noise.m_sampler;
        noise_info.imageView = noise.m_image_view;
        noise_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // position
        VkDescriptorImageInfo pos_info;
        pos_info.sampler = m_position_attachment.m_sampler;
        pos_info.imageView = m_position_attachment.m_image_view;
        pos_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        // normals
        VkDescriptorImageInfo normals_info;
        normals_info.sampler = m_normals_attachment.m_sampler;
        normals_info.imageView = m_normals_attachment.m_image_view;
        normals_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 5> set_write;

        set_write[0] = {};
        set_write[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        set_write[0].pNext = nullptr;
        set_write[0].dstSet = m_descriptor_sets[id].m_textures_descriptor;
        set_write[0].dstBinding = 0;
        set_write[0].descriptorCount = 1;
        set_write[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        set_write[0].pBufferInfo = &binfo[0];

        set_write[1] = {};
        set_write[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        set_write[1].pNext = nullptr;
        set_write[1].dstSet = m_descriptor_sets[id].m_textures_descriptor;
        set_write[1].dstBinding = 1;
        set_write[1].descriptorCount = 1;
        set_write[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        set_write[1].pBufferInfo = &binfo[1];

        set_write[2] = {};
        set_write[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        set_write[2].pNext = nullptr;
        set_write[2].dstSet = m_descriptor_sets[id].m_textures_descriptor;
        set_write[2].dstBinding = 2;
        set_write[2].descriptorCount = 1;
        set_write[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        set_write[2].pImageInfo = &noise_info;

        set_write[3] = {};
        set_write[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        set_write[3].pNext = nullptr;
        set_write[3].dstSet = m_descriptor_sets[id].m_textures_descriptor;
        set_write[3].dstBinding = 3;
        set_write[3].descriptorCount = 1;
        set_write[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        set_write[3].pImageInfo = &pos_info;

        set_write[4] = {};
        set_write[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        set_write[4].pNext = nullptr;
        set_write[4].dstSet = m_descriptor_sets[id].m_textures_descriptor;
        set_write[4].dstBinding = 4;
        set_write[4].descriptorCount = 1;
        set_write[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        set_write[4].pImageInfo = &normals_info;

        vkUpdateDescriptorSets(
            m_runtime.m_renderer->getDevice()->getLogicalDevice(),
            set_write.size(), 
            set_write.data(),
            0, 
            nullptr);
    }
}

    ImageBlock SSAOPassVK::CreateNoiseTexture() {
        std::default_random_engine generator;
        std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
        std::array<Vector4f, 16> m_noise;
        for (uint32_t idx = 0; idx < m_noise.size(); idx++) {
            Vector4f sample = Vector4f(
                randomFloats(generator) * 2.0 - 1.0,
                randomFloats(generator) * 2.0 - 1.0,
                0.0, 0.0
            );
            m_noise[idx] = sample;
        }

        ImageBlock img;

        UtilsVK::TextureFromBuffer(
            *m_runtime.m_renderer->getDevice(),
            m_noise.data(),
            sizeof(m_noise),
            VK_FORMAT_R32G32B32A32_SFLOAT,
            4, 4,
            img,
            VK_FILTER_NEAREST,
            VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
        
        return img;
    }

    VkBuffer SSAOPassVK::CreateKernel() {
        // create CPU array
        std::array<Vector4f, 64> kernel_data{};
        std::default_random_engine generator;
        std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
        for (uint32_t idx = 0; idx < kernel_data.size(); idx++)
        {
            Vector4f sample = Vector4f(
                randomFloats(generator) * 2.0 - 1.0,
                randomFloats(generator) * 2.0 - 1.0,
                randomFloats(generator),
                0.0f
            );
            sample = glm::normalize(sample);
            sample *= randomFloats(generator);
            float scale = (float)idx / kernel_data.size(); // to distribute more kernel samples closer to the origin
            scale = 0.1f + (scale * scale) * (1.0f - 0.1f);
            sample *= scale;
            kernel_data[idx] = sample;
        }

        // create temporal Staging Buffer
        VkDeviceSize bufferSize = sizeof(SSAOKernelData);
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        UtilsVK::createBuffer(
            *m_runtime.m_renderer->getDevice(),
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            stagingBuffer,
            stagingBufferMemory
        );

        // copy array to Staging Buffer
        void* data;
        vkMapMemory(m_runtime.m_renderer->getDevice()->getLogicalDevice(), stagingBufferMemory, 0, bufferSize, 0, &data);
        memcpy(data, &kernel_data, (size_t)bufferSize);
        vkUnmapMemory(m_runtime.m_renderer->getDevice()->getLogicalDevice(), stagingBufferMemory);

        // create GPU buffer
        VkBuffer kernel_buffer = VK_NULL_HANDLE;
        VkDeviceMemory kernel_buffer_memory;
        UtilsVK::createBuffer(
            *m_runtime.m_renderer->getDevice(),
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            kernel_buffer,        
            kernel_buffer_memory  
        );

        // transfer Staging to GPU buffer
        UtilsVK::copyBuffer(
            *m_runtime.m_renderer->getDevice(),
            stagingBuffer,
            kernel_buffer, 
            bufferSize);

        // destroy Staging buffer
        vkDestroyBuffer(m_runtime.m_renderer->getDevice()->getLogicalDevice(), stagingBuffer, nullptr);
        vkFreeMemory(m_runtime.m_renderer->getDevice()->getLogicalDevice(), stagingBufferMemory, nullptr);

        return kernel_buffer;
    }   