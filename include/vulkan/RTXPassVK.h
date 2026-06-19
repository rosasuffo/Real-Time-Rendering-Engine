#pragma once

#include "vulkan/renderPassVK.h"

namespace MiniEngine
{
    struct Runtime;
    class MeshVK;
    typedef std::shared_ptr<MeshVK> MeshVKPtr;

    class RTXPassVK final : public RenderPassVK
    {
    public:
        RTXPassVK(
            const Runtime& i_runtime, 
            const ImageBlock& i_position_depth_attachment,
            const ImageBlock& o_rtx_attachment);

        virtual ~RTXPassVK();

        bool            initialize() override;
        void            shutdown  () override;
        VkCommandBuffer draw      ( const Frame& i_frame ) override;


    private:
        RTXPassVK( const RTXPassVK& ) = delete;
        RTXPassVK& operator=(const RTXPassVK& ) = delete;

        void createFbo             ();
        void createRenderPass      ();
        void createPipelines       ();
        void createDescriptorLayout();
        void createDescriptors     ();

        ImageBlock CreateNoiseTexture();
        VkBuffer CreateKernel();

        struct DescriptorsSets
        {
            VkDescriptorSet m_textures_descriptor;
        };
        const ImageBlock* m_noise_texture_ref = nullptr;

        VkPipeline                                                         m_pipeline;
        VkPipelineLayout                                                   m_pipeline_layouts;
        VkDescriptorSetLayout                                              m_descriptor_set_layout;
        std::array<DescriptorsSets                , 3                    > m_descriptor_sets;
        std::array<VkPipelineShaderStageCreateInfo, 2                    > m_shader_stages;
       
        VkRenderPass                   m_render_pass;
        std::array<VkCommandBuffer, 3> m_command_buffer;
        std::array<VkFramebuffer  , 3> m_fbos;
        VkDescriptorPool               m_descriptor_pool;

        MeshVKPtr m_plane;

        const ImageBlock m_in_position_depth_attachment;
        const ImageBlock m_out_rtx_attachment;
    };
};