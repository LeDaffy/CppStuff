#include "nce/vke_macro.hxx"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <nce/vke.hxx>
#include <vulkan/vulkan_core.h>



[[nodiscard]] static auto read_file(std::filesystem::path shader_path) -> std::vector<std::byte> {
    std::ifstream file(shader_path, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        fmt::println("Failed to open {}", shader_path.c_str());
        std::abort();
    }

    std::size_t file_size = static_cast<size_t>(file.tellg());
    std::vector<std::byte> buffer(file_size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(file_size));
    fmt::println("Buffer size {}", buffer.size());
    file.close();
    return buffer;
}

namespace vke {
    //static members
    std::unique_ptr<VkInstance_T, VKEInstanceDeleter> Instance::instance(nullptr);
    std::unique_ptr<VkSurfaceKHR_T, VKESurfaceDeleter> Instance::surface(nullptr);
    VkPhysicalDevice Instance::physical_device(nullptr);
    std::unique_ptr<VkDevice_T, VKEDeviceDeleter> Instance::logical_device(nullptr);

    // function definitions
    
    void Instance::create_descriptor_sets() {
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptor_set_layout.get());
        VkDescriptorSetAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc_info.descriptorPool = descriptor_pool.get();
        alloc_info.descriptorSetCount = static_cast<u32>(MAX_FRAMES_IN_FLIGHT);
        alloc_info.pSetLayouts = layouts.data();

        descriptor_sets.resize(MAX_FRAMES_IN_FLIGHT);
        vke::Result result = vkAllocateDescriptorSets(logical_device.get(), &alloc_info, descriptor_sets.data());
        VKE_RESULT_CRASH(result);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
            VkDescriptorBufferInfo buffer_info{};
            buffer_info.buffer = uniform_buffers[i].get();
            buffer_info.offset = 0;
            buffer_info.range = sizeof(UniformBufferObject);

            VkWriteDescriptorSet descriptor_write{};
            descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptor_write.dstSet = descriptor_sets[i];
            descriptor_write.dstBinding = 0;
            descriptor_write.dstArrayElement = 0;
            descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            descriptor_write.descriptorCount = 1;
            descriptor_write.pBufferInfo = &buffer_info;
            descriptor_write.pImageInfo = nullptr; // Optional
            descriptor_write.pTexelBufferView = nullptr; // Optional

            vkUpdateDescriptorSets(logical_device.get(), 1, &descriptor_write, 0, nullptr);
        }

    }
    void Instance::create_descriptor_pool() {
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_size.descriptorCount = static_cast<u32>(MAX_FRAMES_IN_FLIGHT);

        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        pool_info.maxSets = static_cast<u32>(MAX_FRAMES_IN_FLIGHT);

        vke::Result result = vkCreateDescriptorPool(logical_device.get(), &pool_info, nullptr, reinterpret_cast<VkDescriptorPool*>(&descriptor_pool));
        VKE_RESULT_CRASH(result);
    }
    void Instance::update_uniform_buffer(u32 current_image) {
        static auto start_time = std::chrono::high_resolution_clock::now();

        auto current_time = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count();

        UniformBufferObject ubo{};
        ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f), static_cast<f32>(swapchain_extent.width) / static_cast<f32>(swapchain_extent.height), 0.1f, 10.0f);
        // ubo.model = glm::mat4(1.0);
        // ubo.view = glm::mat4(1.0);
        // ubo.proj = glm::mat4(1.0);

        ubo.proj[1][1] *= -1;
        memcpy(uniform_buffers_mapped[current_image], &ubo, sizeof(ubo));
    }
    void Instance::create_uniform_buffers() {
        VkDeviceSize buffer_size = sizeof(UniformBufferObject);
        uniform_buffers.resize(MAX_FRAMES_IN_FLIGHT);
        uniform_buffers_memory.resize(MAX_FRAMES_IN_FLIGHT);
        uniform_buffers_mapped.resize(MAX_FRAMES_IN_FLIGHT);

        for (const auto& [buffer, buffer_memory, buffer_map] : std::views::zip(uniform_buffers, uniform_buffers_memory, uniform_buffers_mapped)) {
            create_buffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT 
                    | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    buffer,
                    buffer_memory);

            vkMapMemory(logical_device.get(), buffer_memory.get(), 0, buffer_size, 0, &buffer_map);

        }
    }
    void Instance::create_descriptor_set_layout() {
        VkDescriptorSetLayoutBinding ubo_layout_binding{};
        ubo_layout_binding.binding = 0;
        ubo_layout_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ubo_layout_binding.descriptorCount = 1;
        ubo_layout_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layout_info{};
        layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_info.bindingCount = 1;
        layout_info.pBindings = &ubo_layout_binding;
        vke::Result result = vkCreateDescriptorSetLayout(logical_device.get(), &layout_info, nullptr, reinterpret_cast<VkDescriptorSetLayout*>(&descriptor_set_layout));
        VKE_RESULT_CRASH(result);
    };

    void Instance::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, std::unique_ptr<VkBuffer_T, VKEBufferDeleter>& buffer, std::unique_ptr<VkDeviceMemory_T, VKEMemoryDeleter>& buffer_memory) {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        buffer.reset(nullptr);
        vke::Result result = vkCreateBuffer(logical_device.get(), &buffer_info, nullptr, reinterpret_cast<VkBuffer*>(&buffer));
        VKE_RESULT_CRASH(result);

        VkMemoryRequirements mem_requirements;
        vkGetBufferMemoryRequirements(logical_device.get(), buffer.get(), &mem_requirements);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_requirements.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties);

        buffer_memory.reset(nullptr);
        result = vkAllocateMemory(logical_device.get(), &alloc_info, nullptr, reinterpret_cast<VkDeviceMemory*>(&buffer_memory));
        if (result != VK_SUCCESS) {
            fmt::println("failed to allocate vertex buffer memory!");
            VKE_RESULT_CRASH(result);
        }
        vkBindBufferMemory(logical_device.get(), buffer.get(), buffer_memory.get(), 0);

    }
    auto Instance::find_memory_type(u32 type_filter, VkMemoryPropertyFlags properties) const -> u32 {
        VkPhysicalDeviceMemoryProperties mem_properties;
        vkGetPhysicalDeviceMemoryProperties(physical_device, &mem_properties);

        for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
            if ((type_filter & (1 << i)) && (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }

        fmt::println("failed to find suitable memory type!");
        std::abort();

    }
    auto Instance::copy_buffer(VkBuffer src_buffer, VkBuffer dst_buffer, VkDeviceSize size) const -> void {
        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandPool = command_pool.get();
        alloc_info.commandBufferCount = 1;

        VkCommandBuffer command_buffer;
        vkAllocateCommandBuffers(logical_device.get(), &alloc_info, &command_buffer);

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(command_buffer, &begin_info);

        VkBufferCopy copy_region{};
        copy_region.srcOffset = 0; // Optional
        copy_region.dstOffset = 0; // Optional
        copy_region.size = size;
        vkCmdCopyBuffer(command_buffer, src_buffer, dst_buffer, 1, &copy_region);
        vkEndCommandBuffer(command_buffer);

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;

        vkQueueSubmit(graphics_queue, 1, &submit_info, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphics_queue);
        vkFreeCommandBuffers(logical_device.get(), command_pool.get(), 1, &command_buffer);


    }
    void Instance::create_index_buffer() {
        VkDeviceSize buffer_size = sizeof(indices[0]) * indices.size();
        std::unique_ptr<VkBuffer_T, VKEBufferDeleter> staging_buffer(nullptr);
        std::unique_ptr<VkDeviceMemory_T, VKEMemoryDeleter> staging_buffer_memory(nullptr);
        create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_buffer_memory);


        void* data;
        vkMapMemory(logical_device.get(), staging_buffer_memory.get(), 0, buffer_size, 0, &data); {
            memcpy(data, indices.data(), static_cast<size_t>(buffer_size));
        } vkUnmapMemory(logical_device.get(), staging_buffer_memory.get());

        create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, index_buffer, index_buffer_memory);

        copy_buffer(staging_buffer.get(), index_buffer.get(), buffer_size);
    }
    void Instance::create_vertex_buffer() {
        VkDeviceSize buffer_size = sizeof(vertices[0]) * vertices.size();
        std::unique_ptr<VkBuffer_T, VKEBufferDeleter> staging_buffer(nullptr);
        std::unique_ptr<VkDeviceMemory_T, VKEMemoryDeleter> staging_buffer_memory(nullptr);
        create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_buffer_memory);


        void* data;
        vkMapMemory(logical_device.get(), staging_buffer_memory.get(), 0, buffer_size, 0, &data); {
            memcpy(data, vertices.data(), static_cast<size_t>(buffer_size));
        } vkUnmapMemory(logical_device.get(), staging_buffer_memory.get());

        create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertex_buffer, vertex_buffer_memory);

        copy_buffer(staging_buffer.get(), vertex_buffer.get(), buffer_size);
    }

    void Instance::draw_frame() {
        vkWaitForFences(logical_device.get(), 1, 
                reinterpret_cast<const VkFence*>(&in_flight_fences[current_frame]),
                VK_TRUE, UINT64_MAX);

        u32 image_index;
        vke::Result result = vkAcquireNextImageKHR(logical_device.get(),
                swapchain.get(), UINT64_MAX,
                image_available_semaphores[current_frame].get(), 
                VK_NULL_HANDLE, &image_index);

        if (result == VK_ERROR_OUT_OF_DATE_KHR ) {
            frame_buffer_resized = false;
            recreate_swapchain();
            return;
        } else if (result != VK_SUCCESS) {
            VKE_RESULT_CRASH(result);
        }

        update_uniform_buffer(current_frame);
        vkResetFences(logical_device.get(), 1, 
                reinterpret_cast<const VkFence*>(&in_flight_fences[current_frame]));


        vkResetCommandBuffer(command_buffers[current_frame], 0);
        record_command_buffer(command_buffers[current_frame], image_index);

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore wait_semaphores[] = {image_available_semaphores[current_frame].get()};
        VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = wait_semaphores;
        submit_info.pWaitDstStageMask = wait_stages;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffers[current_frame];
        VkSemaphore signal_semaphores[] = {render_finished_semaphores[current_frame].get()};
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = signal_semaphores;
        result = vkQueueSubmit(graphics_queue, 1, &submit_info, in_flight_fences[current_frame].get());
        VKE_RESULT_CRASH(result);

        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        present_info.waitSemaphoreCount = 1;
        present_info.pWaitSemaphores = signal_semaphores;
        VkSwapchainKHR swapChains[] = {swapchain.get()};
        present_info.swapchainCount = 1;
        present_info.pSwapchains = swapChains;
        present_info.pImageIndices = &image_index;
        vkQueuePresentKHR(present_queue, &present_info);

        if (result == VK_ERROR_OUT_OF_DATE_KHR
                || result == VK_SUBOPTIMAL_KHR 
                || frame_buffer_resized) {
            frame_buffer_resized = false;
            recreate_swapchain();
        } else if (result != VK_SUCCESS) {
            VKE_RESULT_CRASH(result);
        }

        current_frame = (current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
    }
    void Instance::create_sync_objects() {
        image_available_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
        render_finished_semaphores.resize(MAX_FRAMES_IN_FLIGHT);
        in_flight_fences.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semaphore_info{};
        semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_info{};
        fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (const auto& [image_available_semaphore, render_finished_semaphore, in_flight_fence] : std::views::zip(image_available_semaphores, render_finished_semaphores, in_flight_fences)) {
            vke::Result result =  vkCreateSemaphore(logical_device.get(), &semaphore_info, nullptr, reinterpret_cast<VkSemaphore*>(&image_available_semaphore));
            VKE_RESULT_CRASH(result);
            result = vkCreateSemaphore(logical_device.get(), &semaphore_info, nullptr, reinterpret_cast<VkSemaphore*>(&render_finished_semaphore));
            VKE_RESULT_CRASH(result);
            result = vkCreateFence(logical_device.get(), &fence_info, nullptr, reinterpret_cast<VkFence*>(&in_flight_fence));
            VKE_RESULT_CRASH(result);
        }
    }
    void Instance::record_command_buffer(VkCommandBuffer command_buffer, u32 image_index) {
        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = 0; // Optional
        begin_info.pInheritanceInfo = nullptr; // Optional

        vke::Result result = vkBeginCommandBuffer(command_buffer, &begin_info);
        VKE_RESULT_CRASH(result);
        //fmt::println("failed to begin recording command buffer!");
        VkRenderPassBeginInfo render_pass_info{};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_info.renderPass = render_pass.get();
        render_pass_info.framebuffer = swapchain_framebuffers[image_index].get();
        render_pass_info.renderArea.offset = {0, 0};
        render_pass_info.renderArea.extent = swapchain_extent;

        VkClearValue clear_color = {{{0.2f, 0.4f, 0.6f, 1.0f}}};
        render_pass_info.clearValueCount = 1;
        render_pass_info.pClearValues = &clear_color;

        vkCmdBeginRenderPass(command_buffer, &render_pass_info, VK_SUBPASS_CONTENTS_INLINE);
        {
            vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphics_pipeline.get());

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<f32>(swapchain_extent.width);
            viewport.height = static_cast<f32>(swapchain_extent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(command_buffer, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = {0, 0};
            scissor.extent = swapchain_extent;
            vkCmdSetScissor(command_buffer, 0, 1, &scissor);

            VkBuffer vertex_buffers[] = {vertex_buffer.get()};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);
            vkCmdBindIndexBuffer(command_buffer, index_buffer.get(), 0, VK_INDEX_TYPE_UINT16);
            vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_layout.get(), 0, 1, &descriptor_sets[current_frame], 0, nullptr);
            vkCmdDrawIndexed(command_buffer, static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

        }
        vkCmdEndRenderPass(command_buffer);
        result = vkEndCommandBuffer(command_buffer);
        VKE_RESULT_CRASH(result);
        // "failed to record command buffer!"



    }
    void Instance::create_command_buffers() {
        command_buffers.resize(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = command_pool.get();
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = static_cast<u32>(command_buffers.size());

        vke::Result result = vkAllocateCommandBuffers(logical_device.get(), &alloc_info, command_buffers.data());
        if (!result) {
            fmt::println("failed to allocate command buffers!");
            VKE_RESULT_CRASH(result);
        }

    }
    void Instance::create_command_pool() {
        QueueFamilyIndices queue_family_indices = find_queue_families(physical_device);

        VkCommandPoolCreateInfo pool_info{};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = queue_family_indices.graphics_family.value();
        vke::Result result = vkCreateCommandPool(logical_device.get(), &pool_info, nullptr, reinterpret_cast<VkCommandPool*>(&command_pool));
    }
    void Instance::create_framebuffers() {
        swapchain_framebuffers.resize(swapchain_image_views.size());

        for (const auto& [index, image_view] : std::views::enumerate(swapchain_image_views)) {
            VkImageView attachments[] = {
                image_view.get()
            };
            VkFramebufferCreateInfo framebuffer_info{};
            framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebuffer_info.renderPass = render_pass.get();
            framebuffer_info.attachmentCount = 1;
            framebuffer_info.pAttachments = attachments;
            framebuffer_info.width = swapchain_extent.width;
            framebuffer_info.height = swapchain_extent.height;
            framebuffer_info.layers = 1;

            swapchain_framebuffers[static_cast<std::size_t>(index)].reset(nullptr);
            vke::Result result = vkCreateFramebuffer(logical_device.get(), &framebuffer_info, nullptr, reinterpret_cast<VkFramebuffer*>(&(swapchain_framebuffers[static_cast<std::size_t>(index)])));
            VKE_RESULT_CRASH(result);
        }
    };
    auto Instance::create_shader_module(const std::vector<std::byte>& shader_code) const -> std::unique_ptr<VkShaderModule_T, VKEShaderModuleDeleter> {
        VkShaderModuleCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        create_info.codeSize = shader_code.size();
        create_info.pCode = reinterpret_cast<const u32*>(shader_code.data());
        VkShaderModule shader_module;
        vke::Result result = vkCreateShaderModule(logical_device.get(), &create_info, nullptr, &shader_module);
        VKE_RESULT_CRASH(result);

        return std::unique_ptr<VkShaderModule_T, VKEShaderModuleDeleter>(shader_module);
    }


    void Instance::create_render_pass() {
        VkAttachmentDescription color_attachment{};
        color_attachment.format = swapchain_image_format;
        color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference color_attachment_ref{};
        color_attachment_ref.attachment = 0;
        color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &color_attachment_ref;


        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = 1;
        renderPassInfo.pAttachments = &color_attachment;
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        vke::Result result = vkCreateRenderPass(logical_device.get(), &renderPassInfo, nullptr, reinterpret_cast<VkRenderPass*>(&render_pass));
        VKE_RESULT_CRASH(result);
    }
    void Instance::create_graphics_pipeline() {
        auto vs_source = read_file("shaders/hello.vert.spv");
        auto fs_source = read_file("shaders/hello.frag.spv");

        auto vs_module = create_shader_module(vs_source);
        auto fs_module = create_shader_module(fs_source);

        VkPipelineShaderStageCreateInfo vs_stage_info{};
        vs_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vs_stage_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vs_stage_info.module = vs_module.get();
        vs_stage_info.pName = "main";
        VkPipelineShaderStageCreateInfo fs_stage_info{};
        fs_stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fs_stage_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fs_stage_info.module = fs_module.get();
        fs_stage_info.pName = "main";

        VkPipelineShaderStageCreateInfo shader_stages[] = {vs_stage_info, fs_stage_info};

        VkPipelineVertexInputStateCreateInfo vertex_input_info{};
        vertex_input_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        auto binding_description = Vertex::get_binding_description();
        auto attribute_descriptions = Vertex::get_attribute_desc();
        vertex_input_info.vertexBindingDescriptionCount = 1;
        vertex_input_info.vertexAttributeDescriptionCount = attribute_descriptions.size();
        vertex_input_info.pVertexBindingDescriptions = &binding_description;
        vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions.data(); 


        VkPipelineInputAssemblyStateCreateInfo input_assembly{};
        input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        input_assembly.primitiveRestartEnable = VK_FALSE;

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<f32>(swapchain_extent.width);
        viewport.height = static_cast<f32>(swapchain_extent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = swapchain_extent;

        std::vector<VkDynamicState> dynamic_states = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR
        };

        VkPipelineDynamicStateCreateInfo dynamic_state{};
        dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic_state.dynamicStateCount = static_cast<u32>(dynamic_states.size());
        dynamic_state.pDynamicStates = dynamic_states.data();

        VkPipelineViewportStateCreateInfo viewport_state{};
        viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport_state.viewportCount = 1;
        viewport_state.pViewports = &viewport;
        viewport_state.scissorCount = 1;
        viewport_state.pScissors = &scissor;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE; // VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; //VK_FRONT_FACE_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;
        rasterizer.depthBiasConstantFactor = 0.0f; // Optional
        rasterizer.depthBiasClamp = 0.0f; // Optional
        rasterizer.depthBiasSlopeFactor = 0.0f; // Optional

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        multisampling.minSampleShading = 1.0f; // Optional
        multisampling.pSampleMask = nullptr; // Optional
        multisampling.alphaToCoverageEnable = VK_FALSE; // Optional
        multisampling.alphaToOneEnable = VK_FALSE; // Optional

        VkPipelineColorBlendAttachmentState color_blend_attachment{};
        color_blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        color_blend_attachment.blendEnable = VK_FALSE;
        color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD; // Optional
        color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; // Optional
        color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; // Optional
        color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD; // Optional

        VkPipelineColorBlendStateCreateInfo color_blending{};
        color_blending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        color_blending.logicOpEnable = VK_FALSE;
        color_blending.logicOp = VK_LOGIC_OP_COPY; // Optional
        color_blending.attachmentCount = 1;
        color_blending.pAttachments = &color_blend_attachment;
        color_blending.blendConstants[0] = 0.0f; // Optional
        color_blending.blendConstants[1] = 0.0f; // Optional
        color_blending.blendConstants[2] = 0.0f; // Optional
        color_blending.blendConstants[3] = 0.0f; // Optional

        VkPipelineLayoutCreateInfo pipeline_layout_info{};
        pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout_info.setLayoutCount = 1;
        pipeline_layout_info.pSetLayouts = reinterpret_cast<VkDescriptorSetLayout*>(&descriptor_set_layout);
        pipeline_layout_info.pushConstantRangeCount = 0;
        pipeline_layout_info.pPushConstantRanges = nullptr;

        vke::Result result = vkCreatePipelineLayout(logical_device.get(), &pipeline_layout_info, nullptr, reinterpret_cast<VkPipelineLayout*>(&pipeline_layout));
        VKE_RESULT_CRASH(result);

        VkGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline_info.stageCount = 2;
        pipeline_info.pStages = shader_stages;
        pipeline_info.pVertexInputState = &vertex_input_info;
        pipeline_info.pInputAssemblyState = &input_assembly;
        pipeline_info.pViewportState = &viewport_state;
        pipeline_info.pRasterizationState = &rasterizer;
        pipeline_info.pMultisampleState = &multisampling;
        pipeline_info.pDepthStencilState = nullptr; // Optional
        pipeline_info.pColorBlendState = &color_blending;
        pipeline_info.pDynamicState = &dynamic_state;
        pipeline_info.layout = pipeline_layout.get();
        pipeline_info.renderPass = render_pass.get();
        pipeline_info.subpass = 0;
        pipeline_info.basePipelineHandle = VK_NULL_HANDLE; // Optional
        pipeline_info.basePipelineIndex = -1; // Optional

        result = vkCreateGraphicsPipelines(logical_device.get(), VK_NULL_HANDLE, 1, &pipeline_info, nullptr, reinterpret_cast<VkPipeline*>(&graphics_pipeline));
        VKE_RESULT_CRASH(result);
    }


    void Instance::create_swapchain() {
        SwapChainSupportDetails swapchain_support = query_swapchain_support(this->physical_device, this->surface.get());
        VkSurfaceFormatKHR surface_format = choose_swap_surface_format(swapchain_support.formats);
        VkPresentModeKHR present_mode = choose_swap_present_mode(swapchain_support.present_modes);
        VkExtent2D extent = choose_swap_extent(swapchain_support.capabilities);

        u32 image_count = swapchain_support.capabilities.minImageCount + 1;
        if (swapchain_support.capabilities.maxImageCount > 0 && image_count > swapchain_support.capabilities.maxImageCount) {
            image_count = swapchain_support.capabilities.maxImageCount;
        }
        VkSwapchainCreateInfoKHR create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create_info.surface = surface.get();
        create_info.minImageCount = image_count;
        create_info.imageFormat = surface_format.format;
        create_info.imageColorSpace = surface_format.colorSpace;
        create_info.imageExtent = extent;
        create_info.imageArrayLayers = 1;
        create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        QueueFamilyIndices indices = find_queue_families(this->physical_device);
        u32 queueFamilyIndices[] = {indices.graphics_family.value(), indices.present_family.value()};

        if (indices.graphics_family != indices.present_family) {
            create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            create_info.queueFamilyIndexCount = 2;
            create_info.pQueueFamilyIndices = queueFamilyIndices;
        } else {
            create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            create_info.queueFamilyIndexCount = 0; // Optional
            create_info.pQueueFamilyIndices = nullptr; // Optional
        }
        create_info.preTransform = swapchain_support.capabilities.currentTransform;
        create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

        create_info.presentMode = present_mode;
        create_info.clipped = VK_TRUE;
        create_info.oldSwapchain = swapchain.get();
        std::unique_ptr<VkSwapchainKHR_T, VKESwapChainDeleter> swapchain_temp(nullptr);
        vke::Result result =  vkCreateSwapchainKHR(logical_device.get(), &create_info, nullptr, reinterpret_cast<VkSwapchainKHR*>(&swapchain_temp));
        swapchain.swap(swapchain_temp);
        VKE_RESULT_CRASH(result);


        vkGetSwapchainImagesKHR(this->logical_device.get(), this->swapchain.get(), &image_count, nullptr);
        swapchain_images.resize(image_count);
        vkGetSwapchainImagesKHR(this->logical_device.get(), this->swapchain.get(), &image_count, swapchain_images.data());
        swapchain_image_format = surface_format.format;
        swapchain_extent = extent;
    }
    void Instance::create_image_views() {
        swapchain_image_views.resize(swapchain_images.size());
        for (const auto& [index, image] : std::views::enumerate(swapchain_image_views) ) {
            VkImageViewCreateInfo create_info{};
            create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            create_info.image = swapchain_images[static_cast<std::size_t>(index)];
            create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            create_info.format = swapchain_image_format;
            create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            create_info.subresourceRange.baseMipLevel = 0;
            create_info.subresourceRange.levelCount = 1;
            create_info.subresourceRange.baseArrayLayer = 0;
            create_info.subresourceRange.layerCount = 1;

            image.reset(nullptr);
            vke::Result result = vkCreateImageView(this->logical_device.get(), &create_info, nullptr, reinterpret_cast<VkImageView*>(&swapchain_image_views[static_cast<std::size_t>(index)]));

            if (!result) {
                fmt::println("Failed to create image view");
                VKE_RESULT_CRASH(result);
            }

        }
    }
    void Instance::create_instance() {
        auto extensions_available = available_extensions();

#if 0
        for (auto const& e : extensions_available) {
            LOGINFO(e.extensionName);
        }
#endif
        if (use_validation_layers && !check_validation_layer_support(this->validation_layers)) {
            LOGERROR("Validation layer not supported");
            std::abort();
        }

        // create vulkan instance
        if (use_validation_layers) {
            this->info_create.enabledLayerCount = static_cast<uint32_t>(this->validation_layers.size());
            this->info_create.ppEnabledLayerNames = this->validation_layers.data();

            instance.reset(nullptr);
            vke::Result result = vkCreateInstance(&this->info_create, nullptr, reinterpret_cast<VkInstance*>(&this->instance));
            VKE_RESULT_CRASH(result);
        } else {
            vke::Result result = vkCreateInstance(&this->info_create, nullptr, reinterpret_cast<VkInstance*>(&this->instance));
            VKE_RESULT_CRASH(result);
        }
    }
    void Instance::create_surface(const window::Window& window) {
        // create window surface
        VkXcbSurfaceCreateInfoKHR surface_create_info = {
            // VkStructureType               sType;
            // const void*                   pNext;
            // VkXcbSurfaceCreateFlagsKHR    flags;
            // xcb_connection_t*             connection;
            // xcb_window_t                  window;
        };
        surface_create_info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
        surface_create_info.connection = window.x_connection.get();
        surface_create_info.window = window.x_window;

        VkSurfaceKHR l_instance = nullptr;
        vke::Result result = vkCreateXcbSurfaceKHR(this->instance.get(), &surface_create_info, nullptr, &l_instance);
        VKE_RESULT_CRASH(result);
        if (l_instance == nullptr) {
            std::abort();
        }
        this->surface = std::unique_ptr<VkSurfaceKHR_T, VKESurfaceDeleter>(l_instance);
    }
    auto Instance::query_swapchain_support(VkPhysicalDevice device, NonOwningPtr<VkSurfaceKHR_T> surface) const -> SwapChainSupportDetails {
        SwapChainSupportDetails details;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

        u32 format_count;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, nullptr);

        if (format_count != 0) {
            details.formats.resize(format_count);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &format_count, details.formats.data());
        }

        u32 present_mode_count;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, nullptr);

        if (present_mode_count != 0) {
            details.present_modes.resize(present_mode_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &present_mode_count, details.present_modes.data());
        }

        return details;
    }

    void Instance::pick_physical_device() {
        auto devices = available_physical_devices();
        // Use an ordered map to automatically sort candidates by increasing score
        std::multimap<int, VkPhysicalDevice> candidates;
        for (auto d : devices) {
            u32 score = rate_device(d);
            if (is_physical_device_suitable(d)) {
                candidates.insert(std::make_pair(score, d));
            }
        }

        // Check if the best candidate is suitable at all
        if (candidates.rbegin()->first > 0) {
            this->physical_device = candidates.rbegin()->second;
        } else {
            LOGERROR("GPU Not Supported");
            std::abort();
        }
        if (this->physical_device == VK_NULL_HANDLE) {
            LOGERROR("GPU Not Supported");
            std::abort();
        }

        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(this->physical_device, &device_properties);
        VkPhysicalDeviceFeatures device_features;
        vkGetPhysicalDeviceFeatures(this->physical_device, &device_features);
        fmt::println("Selected Vulkan device: {}", device_properties.deviceName);

    }
    Instance::Instance(window::Window& window) : 
        info_app({
                VK_STRUCTURE_TYPE_APPLICATION_INFO, // VkStructureType    sType;
                nullptr,                            // const void* pNext;
                "NCAD 3D",                          // const char* pApplicationName;
                VK_MAKE_VERSION(1, 0, 0),           // uint32_t applicationVersion;
                nullptr,                            // const char* pEngineName;
                VK_MAKE_VERSION(1, 0, 0),           // uint32_t engineVersion;
                VK_API_VERSION_1_3                  // uint32_t apiVersion;
                }),
        info_create({
                VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, // VkStructureType sType;
                nullptr,                                // const void* pNext;
                0,                                      // VkInstanceCreateFlags flags;
                &(this->info_app),                            // const VkApplicationInfo* pApplicationInfo;
                0,                                      // uint32_t enabledLayerCount;
                nullptr,                                // const char* const* ppEnabledLayerNames;
                this->extensions.size(),                      // uint32_t enabledExtensionCount;
                this->extensions.data()                       // const char* const* ppEnabledExtensionNames;
                }),
        swapchain(nullptr),
        window(window)
        {
            window.user_data_ptr = this;
            create_instance();
            create_surface(window);
            pick_physical_device();
            create_logical_device();
            create_swapchain();
            create_image_views();
            create_render_pass();
            create_descriptor_set_layout();
            create_graphics_pipeline();
            create_framebuffers();
            create_command_pool();
            create_vertex_buffer();
            create_index_buffer();
            create_uniform_buffers();
            create_descriptor_pool();
            create_descriptor_sets();
            create_command_buffers();
            create_sync_objects();

        }

    void Instance::recreate_swapchain() {
        u32 width = 0, height = 0;
        width = window.attributes.dimensions.x;
        height = window.attributes.dimensions.y;
        while (width == 0 || height == 0) {
            width = window.attributes.dimensions.x;
            height = window.attributes.dimensions.y;
        }

        vkDeviceWaitIdle(logical_device.get());

        create_swapchain();
        create_image_views();
        create_framebuffers();
    }
    void Instance::create_logical_device() {
        // Specifying the queues to be created
        QueueFamilyIndices indices = find_queue_families(this->physical_device);

        std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
        std::set<u32> unique_queue_families = {indices.graphics_family.value(), indices.present_family.value()};

        const float queue_priority = 1.0f;
        for (auto queue_family : unique_queue_families) {
            VkDeviceQueueCreateInfo queue_create_info = {
                // VkStructureType             sType;
                // const void*                 pNext;
                // VkDeviceQueueCreateFlags    flags;
                // uint32_t                    queueFamilyIndex;
                // uint32_t                    queueCount;
                // const float*                pQueuePriorities;
            };
            queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_create_info.queueFamilyIndex = queue_family;
            queue_create_info.queueCount = 1;
            queue_create_info.pQueuePriorities = &queue_priority;
            queue_create_infos.push_back(queue_create_info);
        }

        // Specifying used device features
        VkPhysicalDeviceFeatures device_features = {};

        // Creating the logical device
        VkDeviceCreateInfo create_info = {
            // VkStructureType                    sType;
            // const void*                        pNext;
            // VkDeviceCreateFlags                flags;
            // uint32_t                           queueCreateInfoCount;
            // const VkDeviceQueueCreateInfo*     pQueueCreateInfos;
            // uint32_t                           enabledLayerCount;
            // const char* const*                 ppEnabledLayerNames;
            // uint32_t                           enabledExtensionCount;
            // const char* const*                 ppEnabledExtensionNames;
            // const VkPhysicalDeviceFeatures*    pEnabledFeatures;
        };
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.pQueueCreateInfos = queue_create_infos.data();
        create_info.queueCreateInfoCount = static_cast<u32>(queue_create_infos.size());
        create_info.pEnabledFeatures = &device_features;

        create_info.enabledExtensionCount = static_cast<u32>(this->device_extensions.size());
        create_info.ppEnabledExtensionNames = this->device_extensions.data();

        if (use_validation_layers) {
            create_info.enabledLayerCount = static_cast<u32>(this->validation_layers.size());
            create_info.ppEnabledLayerNames = this->validation_layers.data();
        } else {
            create_info.enabledLayerCount = 0;
        }


        //create logical device
        vke::Result result = vkCreateDevice(this->physical_device, &create_info, nullptr, reinterpret_cast<VkDevice*>(&this->logical_device));
        VKE_RESULT_CRASH(result)

            vkGetDeviceQueue(this->logical_device.get(), indices.graphics_family.value(), 0, &this->graphics_queue);
        vkGetDeviceQueue(this->logical_device.get(), indices.present_family.value(), 0, &this->present_queue);
    }
    auto Instance::find_queue_families(VkPhysicalDevice device) -> QueueFamilyIndices {
        QueueFamilyIndices indices;

        u32 queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties> queue_families;
        queue_families.resize(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

        VkBool32 present_support = 0;
        for (const auto& [index, queue_family] : std::views::enumerate(queue_families) ) {
            if (queue_family.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                indices.graphics_family = index;
            }
            vkGetPhysicalDeviceSurfaceSupportKHR(device, static_cast<u32>(index), this->surface.get(), &present_support);
            if (present_support) {
                indices.present_family = index;
            }
            if (indices.has_value()) { return indices; }
        }


        return indices;
    }
    auto Instance::rate_device(VkPhysicalDevice device) -> u32 {
        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(device, &device_properties);
        VkPhysicalDeviceFeatures device_features;
        vkGetPhysicalDeviceFeatures(device, &device_features);
        u32 score = 0;

        // Discrete GPUs have a significant performance advantage
        if (device_properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            score += 1000;
        }

        // Maximum possible size of textures affects graphics quality
        score += device_properties.limits.maxImageDimension2D;

        return score;
    }
    auto Instance::is_physical_device_suitable(VkPhysicalDevice device) -> bool {
        QueueFamilyIndices indices = find_queue_families(device);

        VkPhysicalDeviceProperties device_properties;
        vkGetPhysicalDeviceProperties(device, &device_properties);
        VkPhysicalDeviceFeatures device_features;
        vkGetPhysicalDeviceFeatures(device, &device_features);
        fmt::println("{}", device_properties.deviceName);

        bool extensions_supported = check_device_extension_support(device);

        bool swapchain_adequate = false;
        if (extensions_supported) {
            SwapChainSupportDetails swapchain_support = query_swapchain_support(device, this->surface.get());
            swapchain_adequate = !swapchain_support.formats.empty() && !swapchain_support.present_modes.empty();
        }

        return indices.has_value() && extensions_supported && swapchain_adequate;
    }
    auto Instance::check_device_extension_support(VkPhysicalDevice device) const -> bool {
        u32 extension_count;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, nullptr);

        std::vector<VkExtensionProperties> available_extensions;
        available_extensions.resize(extension_count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &extension_count, available_extensions.data());

        std::set<std::string> requiredExtensions(this->device_extensions.begin(), this->device_extensions.end());

        for (const auto& extension : available_extensions) {
            requiredExtensions.erase(extension.extensionName);
        }

        return requiredExtensions.empty();
    }
    auto Instance::choose_swap_surface_format(const std::vector<VkSurfaceFormatKHR>& available_formats) const -> VkSurfaceFormatKHR {
        for (const auto& available_format : available_formats) {
            if (available_format.format == VK_FORMAT_B8G8R8A8_SRGB && available_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return available_format;
            }
        }
        return available_formats[0];
    }

    auto Instance::choose_swap_present_mode(const std::vector<VkPresentModeKHR>& available_present_modes) const -> VkPresentModeKHR {
        // constexpr std::array<VkPresentModeKHR, 4> modes = {
        //     VK_PRESENT_MODE_IMMEDIATE_KHR, // no sync
        //     VK_PRESENT_MODE_FIFO_KHR, // vsync
        //     VK_PRESENT_MODE_FIFO_RELAXED_KHR, // 
        //     VK_PRESENT_MODE_MAILBOX_KHR // triple buffering
        // };
        VkPresentModeKHR desired = VK_PRESENT_MODE_FIFO_KHR; // guaranteed to exist
        for (const auto& mode : available_present_modes) {
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                desired = VK_PRESENT_MODE_IMMEDIATE_KHR;
            }
        }
        return desired;
    }

    auto Instance::choose_swap_extent(const VkSurfaceCapabilitiesKHR& capabilities) const -> VkExtent2D {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        } else {
            u32 width = window.attributes.dimensions.x;
            u32 height = window.attributes.dimensions.y;
            fmt::println(" Window swap extent size [{}, {}]", width, height);

            VkExtent2D actualExtent = { width, height };

            actualExtent.width = std::clamp(actualExtent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            actualExtent.height = std::clamp(actualExtent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

            return actualExtent;
        }
    }

    auto Instance::check_validation_layer_support(std::array<CString, 1> layers) -> bool {
        auto layers_available = available_validation_layers();
        for (auto layer_name : layers) {
            bool layer_found = false;
            for (auto layer_available : layers_available) {
                fmt::println("Layer_name: {}", layer_available.layerName);
                if (strcmp(layer_name, layer_available.layerName) == 0) {
                    layer_found = true;
                    break;
                }
            }
            if (!layer_found) {
                return false;
            }
        }
        return true;
    }
    auto Instance::available_physical_devices() const -> std::vector<VkPhysicalDevice> {
        u32 device_count;
        std::vector<VkPhysicalDevice> devices_available;
        vke::Result result = vkEnumeratePhysicalDevices(this->instance.get(), &device_count, nullptr);
        VKE_RESULT_CRASH(result);
        if (device_count == 0) {
            LOGERROR("Vulkan not supported on this device");
            std::abort();
        }
        devices_available.resize(device_count);
        result = vkEnumeratePhysicalDevices(this->instance.get(), &device_count, devices_available.data());
        VKE_RESULT_CRASH(result);
        return devices_available;
    }
    auto Instance::available_validation_layers() const -> std::vector<VkLayerProperties> {
        u32 layer_count = 0;
        vke::Result result = vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
        VKE_RESULT_CRASH(result);
        std::vector<VkLayerProperties> layers_available;
        layers_available.resize(layer_count);
        result = vkEnumerateInstanceLayerProperties(&layer_count, layers_available.data());
        VKE_RESULT_CRASH(result);
        return layers_available;
    }
    auto Instance::available_extensions() const -> std::vector<VkExtensionProperties> {
        u32 extension_count = 0;
        vke::Result result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, nullptr);
        VKE_RESULT_CRASH(result);
        std::vector<VkExtensionProperties> extensions_available;
        extensions_available.resize(extension_count);
        result = vkEnumerateInstanceExtensionProperties(nullptr, &extension_count, extensions_available.data());
        VKE_RESULT_CRASH(result);
        return extensions_available;
    }

    void VKEImageViewDeleter::operator()(VkImageView ptr){ vkDestroyImageView(Instance::logical_device.get(), ptr, nullptr); } 
    void VKESwapChainDeleter::operator()(VkSwapchainKHR_T* ptr){ vkDestroySwapchainKHR(Instance::logical_device.get(), ptr, nullptr); } 
    void VKESurfaceDeleter::operator()(VkSurfaceKHR_T* ptr){ vkDestroySurfaceKHR(Instance::instance.get(), ptr, nullptr); } 
    void VKEShaderModuleDeleter::operator()(VkShaderModule_T* ptr) { vkDestroyShaderModule(Instance::logical_device.get(), ptr, nullptr); }
    void VKEPipelineLayoutDeleter::operator()(VkPipelineLayout_T* ptr) { vkDestroyPipelineLayout(Instance::logical_device.get(), ptr, nullptr); }
    void VKERenderPassDeleter::operator()(VkRenderPass_T* ptr) { vkDestroyRenderPass(Instance::logical_device.get(), ptr, nullptr); }
    void VKEGraphicsPipelineDeleter::operator()(VkPipeline_T* ptr) { vkDestroyPipeline(Instance::logical_device.get(), ptr, nullptr); }
    void VKEFramebufferDeleter::operator()(VkFramebuffer_T* ptr) { vkDestroyFramebuffer(Instance::logical_device.get(), ptr, nullptr); }
    void VKECommandPoolDeleter::operator()(VkCommandPool_T* ptr) { vkDestroyCommandPool(Instance::logical_device.get(), ptr, nullptr); }
    void VKESemaphoreDeleter::operator()(VkSemaphore_T* ptr) { vkDestroySemaphore(Instance::logical_device.get(), ptr, nullptr); }
    void VKEFenceDeleter::operator()(VkFence_T* ptr) { vkDestroyFence(Instance::logical_device.get(), ptr, nullptr); }
    void VKEBufferDeleter::operator()(VkBuffer_T* ptr) { vkDestroyBuffer(Instance::logical_device.get(), ptr, nullptr); }
    void VKEMemoryDeleter::operator()(VkDeviceMemory_T* ptr) { vkFreeMemory(Instance::logical_device.get(), ptr, nullptr); }
    void VKEDescriptorSetLayoutDeleter::operator()(VkDescriptorSetLayout_T* ptr) { vkDestroyDescriptorSetLayout(Instance::logical_device.get(), ptr, nullptr); }
    void VKEDescriptorPoolDeleter::operator()(VkDescriptorPool_T* ptr) { vkDestroyDescriptorPool(Instance::logical_device.get(), ptr, nullptr); }
}
