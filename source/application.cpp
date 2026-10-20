#include "application.hpp"
#include "graphics_internal.hpp"
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>

VkPipeline pipelineFill = VK_NULL_HANDLE;
VkPipeline pipelineWire = VK_NULL_HANDLE;
VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

VkBuffer vertexBuffer, uniformBuffer;
VmaAllocation vertexAlloc, uniformAlloc;
void* uniformBufferMapped = nullptr;
uint32_t vertexCount = 0;

struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

void createDodecahedron(std::vector<float>& vertices, std::vector<uint32_t>& indices) {
    const float phi = (1.0f + std::sqrt(5.0f)) / 2.0f;
    const float inv_phi = 1.0f / phi;

    std::vector<glm::vec3> pos = {
        { -1, -1, -1 },{ -1, -1,  1 },{ -1,  1, -1 },{ -1,  1,  1 },
        { 1, -1, -1 },{ 1, -1,  1 },{ 1,  1, -1 },{ 1,  1,  1 },
        { 0, -inv_phi, -phi },{ 0, -inv_phi,  phi },{ 0,  inv_phi, -phi },{ 0,  inv_phi,  phi },
        { -inv_phi, -phi, 0 },{ -inv_phi,  phi, 0 },{ inv_phi, -phi, 0 },{ inv_phi,  phi, 0 },
        { -phi, 0, -inv_phi },{ phi, 0, -inv_phi },{ -phi, 0,  inv_phi },{ phi, 0,  inv_phi }
    };

    indices = {
        3, 11, 7,   3, 7, 15,   3, 15, 13,
        7, 19, 17,  7, 17, 6,   7, 6, 15,
        17, 4, 8,   17, 8, 10,  17, 10, 6,
        8, 0, 16,   8, 16, 2,   8, 2, 10,
        0, 12, 1,   0, 1, 18,   0, 18, 16,
        6, 10, 2,   6, 2, 13,   6, 13, 15,
        2, 16, 18,  2, 18, 3,   2, 3, 13,
        18, 1, 9,   18, 9, 11,  18, 11, 3,
        4, 14, 12,  4, 12, 0,   4, 0, 8,
        11, 9, 5,   11, 5, 19,  11, 19, 7,
        19, 5, 14,  19, 14, 4,  19, 4, 17,
        1, 12, 14,  1, 14, 5,   1, 5, 9
    };

    for (size_t i = 0; i < indices.size(); i += 3) {
        glm::vec3 p0 = pos[indices[i]];
        glm::vec3 p1 = pos[indices[i + 1]];
        glm::vec3 p2 = pos[indices[i + 2]];

        glm::vec3 normal = glm::normalize(glm::cross(p1 - p0, p2 - p0));

        size_t faceIndex = i / 9;

        float r = 0.2f + 0.8f * ((faceIndex * 3) % 7) / 7.0f;
        float g = 0.2f + 0.8f * ((faceIndex * 5) % 7) / 7.0f;
        float b = 0.2f + 0.8f * ((faceIndex * 7) % 7) / 7.0f;

        for (int j = 0; j < 3; ++j) {
            glm::vec3 p = pos[indices[i + j]];
            vertices.push_back(p.x); vertices.push_back(p.y); vertices.push_back(p.z);
            vertices.push_back(normal.x); vertices.push_back(normal.y); vertices.push_back(normal.z);
            vertices.push_back(r); vertices.push_back(g); vertices.push_back(b);
        }
    }
    vertexCount = static_cast<uint32_t>(vertices.size() / 9);
}

std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) throw std::runtime_error("failed to open file!");
    size_t fileSize = (size_t)file.tellg();
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    return buffer;
}

VkShaderModule createShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shaderModule;
    vkCreateShaderModule(graphics::internal::context.device, &createInfo, nullptr, &shaderModule);
    return shaderModule;
}

VkPipeline createPipeline(VkShaderModule vertModule, VkShaderModule fragModule, VkPolygonMode polyMode) {
    auto& ctx = graphics::internal::context;

    VkPipelineShaderStageCreateInfo shaderStages[2]{};
    shaderStages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertModule;
    shaderStages[0].pName = "main";

    shaderStages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragModule;
    shaderStages[1].pName = "main";

    VkVertexInputBindingDescription bindingDesc{ 0, 9 * sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription attrDesc[3]{};
    attrDesc[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
    attrDesc[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, 3 * sizeof(float) };
    attrDesc[2] = { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, 6 * sizeof(float) };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
    vertexInputInfo.vertexAttributeDescriptionCount = 3;
    vertexInputInfo.pVertexAttributeDescriptions = attrDesc;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rasterizer.polygonMode = polyMode;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = (polyMode == VK_POLYGON_MODE_LINE) ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = 0xF;

    VkPipelineColorBlendStateCreateInfo colorBlending{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkGraphicsPipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = ctx.render_pass;
    pipelineInfo.subpass = 0;

    VkPipeline pipeline;
    if (vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        std::cerr << "Failed to create pipeline!" << std::endl;
    }
    return pipeline;
}

namespace application {

    bool initialize() {
        auto& ctx = graphics::internal::context;

        std::vector<float> vertices;
        std::vector<uint32_t> indices;
        createDodecahedron(vertices, indices);

        VkBufferCreateInfo bufferInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = vertices.size() * sizeof(float);
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        vmaCreateBuffer(ctx.allocator, &bufferInfo, &allocInfo, &vertexBuffer, &vertexAlloc, nullptr);

        void* data;
        vmaMapMemory(ctx.allocator, vertexAlloc, &data);
        memcpy(data, vertices.data(), bufferInfo.size);
        vmaUnmapMemory(ctx.allocator, vertexAlloc);

        VkBufferCreateInfo uboInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        uboInfo.size = sizeof(UniformBufferObject);
        uboInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        VmaAllocationCreateInfo uboAlloc{};
        uboAlloc.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

        vmaCreateBuffer(ctx.allocator, &uboInfo, &uboAlloc, &uniformBuffer, &uniformAlloc, nullptr);
        vmaMapMemory(ctx.allocator, uniformAlloc, &uniformBufferMapped);

        VkDescriptorSetLayoutBinding uboLayoutBinding{};
        uboLayoutBinding.binding = 0;
        uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboLayoutBinding.descriptorCount = 1;
        uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layoutInfo.bindingCount = 1;
        layoutInfo.pBindings = &uboLayoutBinding;
        vkCreateDescriptorSetLayout(ctx.device, &layoutInfo, nullptr, &descriptorSetLayout);

        VkDescriptorPoolSize poolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
        VkDescriptorPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        poolInfo.maxSets = 1;
        vkCreateDescriptorPool(ctx.device, &poolInfo, nullptr, &descriptorPool);

        VkDescriptorSetAllocateInfo allocSetInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        allocSetInfo.descriptorPool = descriptorPool;
        allocSetInfo.descriptorSetCount = 1;
        allocSetInfo.pSetLayouts = &descriptorSetLayout;
        vkAllocateDescriptorSets(ctx.device, &allocSetInfo, &descriptorSet);

        VkDescriptorBufferInfo bufferDescInfo{ uniformBuffer, 0, sizeof(UniformBufferObject) };
        VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        descriptorWrite.dstSet = descriptorSet;
        descriptorWrite.dstBinding = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pBufferInfo = &bufferDescInfo;
        vkUpdateDescriptorSets(ctx.device, 1, &descriptorWrite, 0, nullptr);

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
        vkCreatePipelineLayout(ctx.device, &pipelineLayoutInfo, nullptr, &pipelineLayout);

        auto vertCode = readFile("shaders/dodecahedron.vert.spv");
        auto fragCode = readFile("shaders/dodecahedron.frag.spv");
        auto wireCode = readFile("shaders/wireframe.frag.spv");

        VkShaderModule vertModule = createShaderModule(vertCode);
        VkShaderModule fragModule = createShaderModule(fragCode);
        VkShaderModule wireModule = createShaderModule(wireCode);

        pipelineFill = createPipeline(vertModule, fragModule, VK_POLYGON_MODE_FILL);
        pipelineWire = createPipeline(vertModule, wireModule, VK_POLYGON_MODE_LINE);

        vkDestroyShaderModule(ctx.device, wireModule, nullptr);
        vkDestroyShaderModule(ctx.device, fragModule, nullptr);
        vkDestroyShaderModule(ctx.device, vertModule, nullptr);

        return true;
    }

    void shutdown() {
        auto& ctx = graphics::internal::context;
        vkQueueWaitIdle(ctx.graphics_queue);

        vkDestroyPipeline(ctx.device, pipelineFill, nullptr);
        vkDestroyPipeline(ctx.device, pipelineWire, nullptr);
        vkDestroyPipelineLayout(ctx.device, pipelineLayout, nullptr);
        vkDestroyDescriptorPool(ctx.device, descriptorPool, nullptr);
        vkDestroyDescriptorSetLayout(ctx.device, descriptorSetLayout, nullptr);

        vmaDestroyBuffer(ctx.allocator, vertexBuffer, vertexAlloc);
        vmaDestroyBuffer(ctx.allocator, uniformBuffer, uniformAlloc);
    }

    void update([[maybe_unused]] double time) {
        static float rotationAngle = 0.0f;
        static float scaleFactor = 0.6f;

        ImGui::Begin("Dodecahedron Controls");
        ImGui::SliderFloat("Rotation", &rotationAngle, 0.0f, 6.28f);
        ImGui::SliderFloat("Scale", &scaleFactor, 0.1f, 2.0f);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::End();

        rotationAngle += 0.01f;

        auto& ctx = graphics::internal::context;
        float aspect = (float)ctx.swapchain_extent.width / (float)ctx.swapchain_extent.height;

        UniformBufferObject ubo{};
        ubo.model = glm::rotate(glm::mat4(1.0f), rotationAngle, glm::vec3(0.0f, 1.0f, 0.0f));
        ubo.model = glm::rotate(ubo.model, rotationAngle * 0.5f, glm::vec3(1.0f, 0.0f, 0.0f));
        ubo.model = glm::scale(ubo.model, glm::vec3(scaleFactor));
        ubo.view = glm::lookAt(glm::vec3(0.0f, 0.0f, 4.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f), aspect, 0.1f, 100.0f);
        ubo.proj[1][1] *= -1.0f;

        memcpy(uniformBufferMapped, &ubo, sizeof(ubo));
        vmaFlushAllocation(ctx.allocator, uniformAlloc, 0, VK_WHOLE_SIZE);
    }

    void render(const graphics::internal::FrameData& fd) {
        auto& ctx = graphics::internal::context;
        VkCommandBuffer cmd = fd.command_buffer;

        VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkClearValue clearValues[2];
        clearValues[0].color = { { 1.0f, 1.0f, 1.0f, 1.0f } };
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo renderPassInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
        renderPassInfo.renderPass = ctx.render_pass;
        renderPassInfo.framebuffer = fd.framebuffer;
        renderPassInfo.renderArea.offset = { 0, 0 };
        renderPassInfo.renderArea.extent = ctx.swapchain_extent;
        renderPassInfo.clearValueCount = 2;
        renderPassInfo.pClearValues = clearValues;

        vkCmdBeginRenderPass(cmd, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{ 0.0f, 0.0f, (float)ctx.swapchain_extent.width, (float)ctx.swapchain_extent.height, 0.0f, 1.0f };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{ { 0, 0 }, ctx.swapchain_extent };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, offsets);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineFill);
        vkCmdDraw(cmd, vertexCount, 1, 0, 0);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineWire);
        vkCmdDraw(cmd, vertexCount, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
        vkEndCommandBuffer(cmd);
    }
}