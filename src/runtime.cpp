#include "runtime.h"
#include "vulkan/rendererVK.h"
#include "vulkan/deviceVK.h"
#include "vulkan/utilsVK.h"
#include "frame.h"
#include "meshRegistry.h" 
#include "scene.h"

using namespace MiniEngine;


void Runtime::createResources()
{
    for( uint32_t id = 0; id < m_per_frame_buffer.size(); id++ )
    {
        if( VK_NULL_HANDLE == m_per_frame_buffer[ id ] )
        {
            UtilsVK::createBuffer( *m_renderer->getDevice(), sizeof( PerFrameData ), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_per_frame_buffer[ id ], m_per_frame_buffer_memory[ id ] );
        }

        if( VK_NULL_HANDLE == m_per_object_buffer[ id ] )
        {
            UtilsVK::createBuffer( *m_renderer->getDevice(), sizeof( PerObjectData ) * kMAX_NUMBER_OF_OBJECTS, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, m_per_object_buffer[ id ], m_per_object_buffer_memory[ id ] );
        }

    }

    UtilsVK::createTLAS(
        *m_renderer->getDevice(),
        m_scene->getTransforms(),// array de matrices de transformacion de cada una de las mallas
        m_mesh_registry->getMeshesBLAS(),// array de vkAccellerationStructures de las mallas
        m_tlas,
        m_tlas_buffer,
        m_tlas_memory);
}


void Runtime::freeResources()
{
    for( uint32_t id = 0; id < m_per_frame_buffer.size(); id++ )
    {
        if( VK_NULL_HANDLE != m_per_frame_buffer[ id ] )
        {
            vkDestroyBuffer( m_renderer->getDevice()->getLogicalDevice(), m_per_frame_buffer       [ id ], nullptr );
            vkFreeMemory   ( m_renderer->getDevice()->getLogicalDevice(), m_per_frame_buffer_memory[ id ], nullptr );

            m_per_frame_buffer[ id ] = VK_NULL_HANDLE;
        }

        if( VK_NULL_HANDLE != m_per_object_buffer[ id ] )
        {
            vkDestroyBuffer( m_renderer->getDevice()->getLogicalDevice(), m_per_object_buffer       [ id ], nullptr );
            vkFreeMemory   ( m_renderer->getDevice()->getLogicalDevice(), m_per_object_buffer_memory[ id ], nullptr );

            m_per_object_buffer[ id ] = VK_NULL_HANDLE;
        }
    }
}