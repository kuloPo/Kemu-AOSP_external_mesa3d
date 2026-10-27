// Copyright 2026 The Android Open Source Project
// SPDX-License-Identifier: Apache-2.0
#include <vulkan/vulkan.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "push_descriptor_spv.h"

#define CHECK(call)                                                                 \
    do {                                                                            \
        VkResult r = (call);                                                        \
        if (r != VK_SUCCESS) {                                                      \
            std::fprintf(stderr, "%s failed: %d at line %d\n", #call, r, __LINE__); \
            std::exit(1);                                                           \
        }                                                                           \
    } while (0)
#define REQUIRE(test)                                                        \
    do {                                                                     \
        if (!(test)) {                                                       \
            std::fprintf(stderr, "%s failed at line %d\n", #test, __LINE__); \
            std::exit(1);                                                    \
        }                                                                    \
    } while (0)

struct Buffer {
    VkBuffer handle;
    VkDeviceMemory memory;
    uint32_t* data;
};

int main() {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "gfxstream_push_descriptor_test";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    VkInstance instance;
    CHECK(vkCreateInstance(&ici, nullptr, &instance));
    uint32_t count = 0;
    CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    REQUIRE(count > 0);
    std::vector<VkPhysicalDevice> devices(count);
    CHECK(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    VkPhysicalDevice physical = devices[0];
    CHECK(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr));
    std::vector<VkExtensionProperties> extensions(count);
    CHECK(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data()));
    bool supported = false;
    for (const auto& ext : extensions) {
        if (!std::strcmp(ext.extensionName, VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME)) {
            supported = true;
            std::printf("VK_KHR_push_descriptor revision %u\n", ext.specVersion);
        }
    }
    REQUIRE(supported);
    VkPhysicalDevicePushDescriptorPropertiesKHR pushProps{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PUSH_DESCRIPTOR_PROPERTIES_KHR};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    properties.pNext = &pushProps;
    vkGetPhysicalDeviceProperties2(physical, &properties);
    std::printf("Device: %s; maxPushDescriptors: %u\n", properties.properties.deviceName,
                pushProps.maxPushDescriptors);
    REQUIRE(pushProps.maxPushDescriptors >= 9);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    uint32_t family = 0;
    while (family < count && !(families[family].queueFlags & VK_QUEUE_COMPUTE_BIT)) ++family;
    REQUIRE(family < count);
    float priority = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    const char* enabledExtensions[] = {VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME,
                                       VK_KHR_DESCRIPTOR_UPDATE_TEMPLATE_EXTENSION_NAME};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = 2;
    dci.ppEnabledExtensionNames = enabledExtensions;
    VkDevice device;
    CHECK(vkCreateDevice(physical, &dci, nullptr, &device));
    auto push = reinterpret_cast<PFN_vkCmdPushDescriptorSetKHR>(
        vkGetDeviceProcAddr(device, "vkCmdPushDescriptorSetKHR"));
    auto pushTemplate = reinterpret_cast<PFN_vkCmdPushDescriptorSetWithTemplateKHR>(
        vkGetDeviceProcAddr(device, "vkCmdPushDescriptorSetWithTemplateKHR"));
    auto createTemplateKHR = reinterpret_cast<PFN_vkCreateDescriptorUpdateTemplateKHR>(
        vkGetDeviceProcAddr(device, "vkCreateDescriptorUpdateTemplateKHR"));
    REQUIRE(push && pushTemplate && createTemplateKHR);
    REQUIRE(vkGetInstanceProcAddr(instance, "vkCmdPushDescriptorSetKHR"));
    REQUIRE(vkGetInstanceProcAddr(instance, "vkCmdPushDescriptorSetWithTemplateKHR"));

    VkPhysicalDeviceMemoryProperties memoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
    auto allocate = [&](VkMemoryRequirements req, VkMemoryPropertyFlags flags) {
        uint32_t type = 0;
        for (; type < memoryProperties.memoryTypeCount; ++type) {
            if ((req.memoryTypeBits & (1u << type)) &&
                (memoryProperties.memoryTypes[type].propertyFlags & flags) == flags)
                break;
        }
        REQUIRE(type < memoryProperties.memoryTypeCount);
        VkMemoryAllocateInfo info{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        info.allocationSize = req.size;
        info.memoryTypeIndex = type;
        VkDeviceMemory result;
        CHECK(vkAllocateMemory(device, &info, nullptr, &result));
        return result;
    };
    auto createBuffer = [&](uint32_t value) {
        Buffer result{};
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = 256;
        info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                     VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;
        CHECK(vkCreateBuffer(device, &info, nullptr, &result.handle));
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(device, result.handle, &req);
        result.memory = allocate(
            req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        CHECK(vkBindBufferMemory(device, result.handle, result.memory, 0));
        CHECK(vkMapMemory(device, result.memory, 0, VK_WHOLE_SIZE, 0,
                          reinterpret_cast<void**>(&result.data)));
        std::memset(result.data, 0, 256);
        result.data[0] = value;
        return result;
    };
    Buffer output = createBuffer(0);
    Buffer inputs[] = {createBuffer(10), createBuffer(20), createBuffer(30), createBuffer(40)};
    Buffer texels = createBuffer(5);
    VkBufferViewCreateInfo bvci{VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO};
    bvci.buffer = texels.handle;
    bvci.format = VK_FORMAT_R32_UINT;
    bvci.range = 4;
    VkBufferView bufferView;
    CHECK(vkCreateBufferView(device, &bvci, nullptr, &bufferView));
    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R32_UINT;
    imageInfo.extent = {1, 1, 1};
    imageInfo.mipLevels = imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
    VkImage image;
    CHECK(vkCreateImage(device, &imageInfo, nullptr, &image));
    VkMemoryRequirements imageReq;
    vkGetImageMemoryRequirements(device, image, &imageReq);
    VkDeviceMemory imageMemory = allocate(imageReq, 0);
    CHECK(vkBindImageMemory(device, image, imageMemory, 0));
    VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    ivci.image = image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = VK_FORMAT_R32_UINT;
    ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView imageView;
    CHECK(vkCreateImageView(device, &ivci, nullptr, &imageView));
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.maxLod = 0;
    VkSampler sampler;
    CHECK(vkCreateSampler(device, &sci, nullptr, &sampler));
    VkDescriptorSetLayoutBinding bindings[] = {
        {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {5, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &sampler},
        {7, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, &sampler},
    };
    VkDescriptorSetLayoutCreateInfo slci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    slci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    slci.bindingCount = 8;
    slci.pBindings = bindings;
    VkDescriptorSetLayout setLayout;
    CHECK(vkCreateDescriptorSetLayout(device, &slci, nullptr, &setLayout));
    VkPushConstantRange pushRange{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
    VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &setLayout;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pushRange;
    VkPipelineLayout layout;
    CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &layout));
    // The pipeline layout must retain immutable-sampler metadata after this destroy.
    vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    smci.codeSize = sizeof(kPushDescriptorShader);
    smci.pCode = kPushDescriptorShader;
    VkShaderModule shader;
    CHECK(vkCreateShaderModule(device, &smci, nullptr, &shader));
    VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    cpci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    cpci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    cpci.stage.module = shader;
    cpci.stage.pName = "main";
    cpci.layout = layout;
    VkPipeline pipeline;
    CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &pipeline));
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = family;
    VkCommandPool pool;
    CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &pool));
    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = pool;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    CHECK(vkAllocateCommandBuffers(device, &cbai, &cmd));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(cmd, &begin));
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = ivci.subresourceRange;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    VkClearColorValue clear{};
    clear.uint32[0] = 7;
    vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_GENERAL, &clear, 1, &ivci.subresourceRange);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    VkDescriptorBufferInfo outputInfo{output.handle, 0, 256};
    VkDescriptorBufferInfo inputInfos[] = {{inputs[0].handle, 0, 16}, {inputs[1].handle, 0, 16}};
    const auto poison = uintptr_t(0x12345);
    VkDescriptorImageInfo storageInfo{reinterpret_cast<VkSampler>(poison), imageView,
                                      VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo samplerInfo{sampler, reinterpret_cast<VkImageView>(poison),
                                      VK_IMAGE_LAYOUT_UNDEFINED};
    VkWriteDescriptorSet writes[8]{};
    for (uint32_t i = 0; i < 8; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = reinterpret_cast<VkDescriptorSet>(poison);
        writes[i].dstBinding = i;
        writes[i].descriptorType = bindings[i].descriptorType;
        writes[i].descriptorCount = bindings[i].descriptorCount;
        writes[i].pImageInfo = reinterpret_cast<const VkDescriptorImageInfo*>(poison);
        writes[i].pBufferInfo = reinterpret_cast<const VkDescriptorBufferInfo*>(poison);
        writes[i].pTexelBufferView = reinterpret_cast<const VkBufferView*>(poison);
    }
    writes[0].pBufferInfo = &outputInfo;
    writes[1].pBufferInfo = inputInfos;
    writes[2].pTexelBufferView = &bufferView;
    writes[3].pImageInfo = &storageInfo;
    writes[4].pImageInfo = &storageInfo;
    writes[5].pImageInfo = &samplerInfo;
    writes[6].pImageInfo = &storageInfo;
    // Binding 7 is an immutable sampler: even pImageInfo is ignored.
    push(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 8, writes);
    auto dispatch = [&](uint32_t index) {
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &index);
        vkCmdDispatch(cmd, 1, 1, 1);
    };
    dispatch(0);
    VkDescriptorBufferInfo replacement{inputs[2].handle, 0, 16};
    VkWriteDescriptorSet partial = writes[1];
    partial.dstArrayElement = 1;
    partial.descriptorCount = 1;
    partial.pBufferInfo = &replacement;
    push(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &partial);
    dispatch(1);

    // Unaligned offsets, padding between elements, and ignored template handles.
    std::vector<uint8_t> payload(512, 0);
    VkDescriptorUpdateTemplateEntry entries[8]{};
    for (uint32_t i = 0; i < 8; ++i) {
        entries[i].dstBinding = i;
        entries[i].descriptorType = bindings[i].descriptorType;
        entries[i].descriptorCount = bindings[i].descriptorCount;
        entries[i].offset = 1 + i * 60;
        entries[i].stride = 28;
    }
    auto put = [&](uint32_t binding, uint32_t index, const auto& value) {
        std::memcpy(payload.data() + entries[binding].offset + index * entries[binding].stride,
                    &value, sizeof(value));
    };
    put(0, 0, outputInfo);
    VkDescriptorBufferInfo third{inputs[2].handle, 0, 16}, fourth{inputs[3].handle, 0, 16};
    put(1, 0, third);
    put(1, 1, fourth);
    put(2, 0, bufferView);
    put(3, 0, storageInfo);
    put(4, 0, storageInfo);
    put(5, 0, samplerInfo);
    put(6, 0, storageInfo);
    entries[7].offset = 0x1000000;  // Ignored immutable sampler data must not be read.
    VkDescriptorUpdateTemplateCreateInfo tci{
        VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO};
    tci.descriptorUpdateEntryCount = 8;
    tci.pDescriptorUpdateEntries = entries;
    tci.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;
    tci.descriptorSetLayout = reinterpret_cast<VkDescriptorSetLayout>(poison);
    tci.pipelineBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    tci.pipelineLayout = layout;
    VkDescriptorUpdateTemplate updateTemplate;
    CHECK(vkCreateDescriptorUpdateTemplate(device, &tci, nullptr, &updateTemplate));
    pushTemplate(cmd, updateTemplate, layout, 0, payload.data());
    dispatch(2);
    vkDestroyDescriptorUpdateTemplate(device, updateTemplate, nullptr);
    // Zero stride repeats the same element for the descriptor array.
    entries[1].stride = 0;
    CHECK(createTemplateKHR(device, &tci, nullptr, &updateTemplate));
    pushTemplate(cmd, updateTemplate, layout, 0, payload.data());
    dispatch(3);
    vkDestroyDescriptorUpdateTemplate(device, updateTemplate, nullptr);
    VkMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    hostBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                         1, &hostBarrier, 0, nullptr, 0, nullptr);
    CHECK(vkEndCommandBuffer(cmd));
    VkQueue queue;
    vkGetDeviceQueue(device, family, 0, &queue);
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));
    const uint32_t expected[] = {56, 66, 96, 86};
    for (uint32_t i = 0; i < 4; ++i) {
        std::printf("Case %u: actual=%u expected=%u\n", i, output.data[i], expected[i]);
        REQUIRE(output.data[i] == expected[i]);
    }
    // Re-recording the same command buffer must start a new push descriptor state.
    CHECK(vkResetCommandBuffer(cmd, 0));
    CHECK(vkBeginCommandBuffer(cmd, &begin));
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    push(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 8, writes);
    dispatch(4);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                         1, &hostBarrier, 0, nullptr, 0, nullptr);
    CHECK(vkEndCommandBuffer(cmd));
    CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));
    REQUIRE(output.data[4] == 56);

    VkCommandBuffer secondary;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
    CHECK(vkAllocateCommandBuffers(device, &cbai, &secondary));
    VkCommandBufferInheritanceInfo inheritance{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};
    begin.pInheritanceInfo = &inheritance;
    CHECK(vkBeginCommandBuffer(secondary, &begin));
    vkCmdBindPipeline(secondary, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    push(secondary, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 8, writes);
    uint32_t secondaryIndex = 5;
    vkCmdPushConstants(secondary, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &secondaryIndex);
    vkCmdDispatch(secondary, 1, 1, 1);
    CHECK(vkEndCommandBuffer(secondary));
    begin.pInheritanceInfo = nullptr;
    CHECK(vkResetCommandBuffer(cmd, 0));
    CHECK(vkBeginCommandBuffer(cmd, &begin));
    vkCmdExecuteCommands(cmd, 1, &secondary);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                         1, &hostBarrier, 0, nullptr, 0, nullptr);
    CHECK(vkEndCommandBuffer(cmd));
    CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));
    REQUIRE(output.data[5] == 56);

    // Regression: ordinary descriptor templates still work with the shared hooks.
    slci.flags = 0;
    CHECK(vkCreateDescriptorSetLayout(device, &slci, nullptr, &setLayout));
    VkPipelineLayout normalLayout;
    CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &normalLayout));
    VkPipeline normalPipeline;
    cpci.layout = normalLayout;
    CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &normalPipeline));
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1},  {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1},         {VK_DESCRIPTOR_TYPE_SAMPLER, 2},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};
    VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpci.maxSets = 1;
    dpci.poolSizeCount = 7;
    dpci.pPoolSizes = sizes;
    VkDescriptorPool descriptorPool;
    CHECK(vkCreateDescriptorPool(device, &dpci, nullptr, &descriptorPool));
    VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = descriptorPool;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &setLayout;
    VkDescriptorSet descriptorSet;
    CHECK(vkAllocateDescriptorSets(device, &dsai, &descriptorSet));
    entries[1].stride = 28;
    entries[7].offset = 421;
    VkDescriptorImageInfo cleanImage{VK_NULL_HANDLE, imageView, VK_IMAGE_LAYOUT_GENERAL};
    VkDescriptorImageInfo cleanSampler{sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
    put(1, 0, third);
    put(1, 1, fourth);
    put(3, 0, cleanImage);
    put(4, 0, cleanImage);
    put(5, 0, cleanSampler);
    put(6, 0, cleanImage);
    put(7, 0, cleanSampler);
    tci.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET;
    tci.descriptorSetLayout = setLayout;
    tci.pipelineLayout = reinterpret_cast<VkPipelineLayout>(poison);
    CHECK(vkCreateDescriptorUpdateTemplate(device, &tci, nullptr, &updateTemplate));
    vkUpdateDescriptorSetWithTemplate(device, descriptorSet, updateTemplate, payload.data());
    CHECK(vkResetCommandBuffer(cmd, 0));
    CHECK(vkBeginCommandBuffer(cmd, &begin));
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, normalPipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, normalLayout, 0, 1, &descriptorSet,
                            0, nullptr);
    uint32_t normalIndex = 6;
    vkCmdPushConstants(cmd, normalLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &normalIndex);
    vkCmdDispatch(cmd, 1, 1, 1);
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0,
                         1, &hostBarrier, 0, nullptr, 0, nullptr);
    CHECK(vkEndCommandBuffer(cmd));
    CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    CHECK(vkQueueWaitIdle(queue));
    REQUIRE(output.data[6] == 96);
    vkDestroyDescriptorUpdateTemplate(device, updateTemplate, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyPipeline(device, normalPipeline, nullptr);
    vkDestroyPipelineLayout(device, normalLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
    vkDestroyCommandPool(device, pool, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyShaderModule(device, shader, nullptr);
    vkDestroyPipelineLayout(device, layout, nullptr);
    vkDestroySampler(device, sampler, nullptr);
    vkDestroyImageView(device, imageView, nullptr);
    vkDestroyImage(device, image, nullptr);
    vkFreeMemory(device, imageMemory, nullptr);
    vkDestroyBufferView(device, bufferView, nullptr);
    auto destroyBuffer = [&](Buffer& buffer) {
        vkUnmapMemory(device, buffer.memory);
        vkDestroyBuffer(device, buffer.handle, nullptr);
        vkFreeMemory(device, buffer.memory, nullptr);
    };
    for (auto& buffer : inputs) destroyBuffer(buffer);
    destroyBuffer(output);
    destroyBuffer(texels);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    std::puts(
        "PASS: ordinary, partial, template, zero-stride, ignored fields, reset, secondary, "
        "normal-template");
    return 0;
}
