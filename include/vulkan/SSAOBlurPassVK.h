#pragma once

#include "vulkan/renderPassVK.h"

namespace MiniEngine
{
    struct Runtime;
    class MeshVK;
    typedef std::shared_ptr<MeshVK> MeshVKPtr;

    class SSAOBlurPassVK final : public RenderPassVK
    {
    public:
        SSAOBlurPassVK(
            const Runtime& i_runtime, 
            const ImageBlock& i_in_ssao_attachment,
            const ImageBlock& i_blur_texture);

        virtual ~SSAOBlurPassVK();

        bool            initialize() override;
        void            shutdown  () override;
        VkCommandBuffer draw      ( const Frame& i_frame ) override;


    private:
        SSAOBlurPassVK( const SSAOBlurPassVK& ) = delete;
        SSAOBlurPassVK& operator=(const SSAOBlurPassVK& ) = delete;

        void createFbo             ();
        void createRenderPass      ();
        void createPipelines       ();
        void createDescriptorLayout();
        void createDescriptors     ();

        struct DescriptorsSets
        {
            VkDescriptorSet m_textures_descriptor;
        };

        VkPipeline                                                         m_pipeline;
        VkPipelineLayout                                                   m_pipeline_layouts;
        std::array<VkDescriptorSetLayout          , 1                    > m_descriptor_set_layout;
        std::array<DescriptorsSets                , 3                    > m_descriptor_sets;
        std::array<VkPipelineShaderStageCreateInfo, 2                    > m_shader_stages;
       
        VkRenderPass                   m_render_pass;
        std::array<VkCommandBuffer, 3> m_command_buffer;
        std::array<VkFramebuffer  , 3> m_fbos;
        VkDescriptorPool               m_descriptor_pool;

        MeshVKPtr m_plane;

        const ImageBlock m_in_ssao_attachment;
        const ImageBlock m_blur_texture;
    };
};