#if defined(AUREA_TEST_VULKAN) && !defined(AUREA_TEST_GLES)
AUREA_TEST(VulkanDescriptorsGpu, FallbackBufferSupportsStorageAndDynamicUniformDescriptors) {
    AUREA_REQUIRE_GPU();
    auto& backend = gpu().backend;
    const u32 errors = vk::Backend::validation_errors();
    const bool validation = backend.capabilities().validationEnabled;
    std::printf("    Vulkan descriptor validation: %s\n", validation ? "enabled" : "unavailable");

    // Exercise the actual fallback through Vulkan's descriptor update contract,
    // without an earlier image conversion supplying a real uniform buffer.
    const VkDescriptorType types[] = {
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
    };
    VkDescriptorSetLayoutBinding bindings[2]{};
    VkDescriptorPoolSize sizes[2]{};
    for (u32 i = 0; i < 2; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = types[i];
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        sizes[i] = {types[i], 1};
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    AUREA_CHECK_EQ(vk::vkCreateDescriptorSetLayout(backend.device(), &layoutInfo, nullptr, &layout), VK_SUCCESS);
    if (!layout) return;
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    AUREA_CHECK_EQ(vk::vkCreateDescriptorPool(backend.device(), &poolInfo, nullptr, &pool), VK_SUCCESS);
    if (pool) {
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = pool;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &layout;
        VkDescriptorSet set = VK_NULL_HANDLE;
        AUREA_CHECK_EQ(vk::vkAllocateDescriptorSets(backend.device(), &allocate, &set), VK_SUCCESS);
        if (set) {
            const VkDescriptorBufferInfo buffer{backend.dummy_buffer().buffer, 0, 16};
            VkWriteDescriptorSet writes[2]{};
            for (u32 i = 0; i < 2; ++i) {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = set;
                writes[i].dstBinding = i;
                writes[i].descriptorType = types[i];
                writes[i].descriptorCount = 1;
                writes[i].pBufferInfo = &buffer;
            }
            vk::vkUpdateDescriptorSets(backend.device(), 2, writes, 0, nullptr);
        }
        vk::vkDestroyDescriptorPool(backend.device(), pool, nullptr);
    }
    vk::vkDestroyDescriptorSetLayout(backend.device(), layout, nullptr);
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - errors, 0u);
}
AUREA_TEST(VulkanDescriptorsGpu, ColdPbrDrawRemainsValidAcrossProjectResets) {
    AUREA_REQUIRE_GPU();
    Engine engine;
    EngineConfig config;
    config.backend = new vk::Backend();
    config.backendConfig.enableValidation = true;
    config.disableAutosave = true;
    config.workerCount = 2;
    AUREA_CHECK(engine.initialize(config).ok());
    const u32 errors = vk::Backend::validation_errors();
    for (int reset = 0; reset < 3; ++reset) {
        AUREA_CHECK(engine.new_project(160, 90, 30., "cold PBR").ok());
        AUREA_CHECK(engine.add_shape3d(0, "cube without HDR").ok());
        Image8 image;
        AUREA_CHECK(engine.capture_frame_rgba(160, image.rgba, image.width, image.height).ok());
        AUREA_CHECK_EQ(image.width, 160u);
        AUREA_CHECK_EQ(image.height, 90u);
        const f32 visible = coverage(image);
        std::printf("    cold PBR reset %d: coverage %.3f, validation errors %u\n",
                    reset, visible, vk::Backend::validation_errors() - errors);
        AUREA_CHECK(visible > .01f);
        AUREA_CHECK_EQ(vk::Backend::validation_errors() - errors, 0u);
    }
    engine.shutdown();
    AUREA_CHECK_EQ(vk::Backend::validation_errors() - errors, 0u);
}
#endif
