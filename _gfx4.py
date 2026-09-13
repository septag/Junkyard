import io
import re

def patcher(path):
    s = io.open(path, encoding='utf-8', newline='').read()
    state = {'s': s}

    def rep(a, b, n=1):
        pat = re.compile(r'[ \t]*\r\n'.join(re.escape(x) for x in a.split('\n')))
        b2 = b.replace('\n', '\r\n')
        found = len(pat.findall(state['s']))
        assert found == n, (path, found, n, a[:110])
        state['s'] = pat.sub(lambda m: b2, state['s'], count=n)

    def save():
        io.open(path, 'w', encoding='utf-8', newline='').write(state['s'])

    return rep, save, state


# =====================================================================
# GfxBackendTypes.h : the render pass targets a specific swapchain
# =====================================================================
rep, save, _ = patcher('code/Graphics/GfxBackendTypes.h')
rep("""    GfxRenderPassAttachment stencilAttachment;
    bool swapchain;""",
    """    GfxRenderPassAttachment stencilAttachment;
    // Swapchain this pass renders into. Invalid means it renders into `colorAttachments` images instead.
    // Use GfxBackend::GetMainSwapchain() for the main window
    GfxSwapchainHandle swapchain;""")
save()

# =====================================================================
# GfxBackend.h : command buffers track which swapchain they touched
# =====================================================================
rep, save, _ = patcher('code/Graphics/GfxBackend.h')
rep("""    uint32 mGeneration;
    uint16 mCmdBufferIndex;
    uint8 mQueueIndex;
    uint8 mDrawsToSwapchain : 1;
    uint8 mIsRecording : 1;
    uint8 mIsInRenderPass : 1;
    uint8 mShouldSubmit : 1;
""",
    """    uint32 mGeneration;
    uint16 mCmdBufferIndex;
    uint8 mQueueIndex;
    uint8 mIsRecording : 1;
    uint8 mIsInRenderPass : 1;
    uint8 mShouldSubmit : 1;
    // A command buffer presents to at most one swapchain. Invalid when it renders to offscreen images only
    GfxSwapchainHandle mSwapchainTarget;

    bool DrawsToSwapchain() const { return mSwapchainTarget.IsValid(); }
""")
save()

# =====================================================================
# GfxBackend.cpp
# =====================================================================
rep, save, state = patcher('code/Graphics/GfxBackend.cpp')

# Attachment builder needs to know *which* swapchain, not just whether
rep("    static VkRenderingAttachmentInfo _TransitionAndMakeAttachment(const GfxRenderPassAttachment& attachment, VkCommandBuffer cmdVk, bool toSwapchain)",
    "    static VkRenderingAttachmentInfo _TransitionAndMakeAttachment(const GfxRenderPassAttachment& attachment, VkCommandBuffer cmdVk, GfxSwapchainHandle toSwapchain)")

rep("""            viewHandle = _MainSwapchain().GetImageView();""",
    """            viewHandle = gBackendVk.swapchains.Data(toSwapchain).GetImageView();""")

rep("""            resolveImageView = _MainSwapchain().GetImageView();    """,
    """            resolveImageView = gBackendVk.swapchains.Data(toSwapchain).GetImageView();    """)

rep("        depthAttachment = GfxBackend::_TransitionAndMakeAttachment(pass.depthAttachment, cmdVk, false);",
    "        depthAttachment = GfxBackend::_TransitionAndMakeAttachment(pass.depthAttachment, cmdVk, GfxSwapchainHandle());")

# BeginRenderPass: resolve the pass target once, then use it throughout
rep("    uint32 numColorAttachments = !pass.swapchain ? pass.numAttachments : 1;",
    "    uint32 numColorAttachments = !pass.swapchain.IsValid() ? pass.numAttachments : 1;")

rep("""            if (pass.swapchain) {
                width = uint16(_MainSwapchain().extent.width);
                height = uint16(_MainSwapchain().extent.height);
            }""",
    """            if (pass.swapchain.IsValid()) {
                const GfxBackendSwapchain& target = gBackendVk.swapchains.Data(pass.swapchain);
                width = uint16(target.extent.width);
                height = uint16(target.extent.height);
            }""")

rep("""    if (pass.swapchain || resolvedToSwapchain) {
        GfxBackendSwapchain::ImageState& state = _MainSwapchain().GetImageState();""",
    """    if (pass.swapchain.IsValid() || resolvedToSwapchain) {
        GfxBackendSwapchain& target = gBackendVk.swapchains.Data(pass.swapchain);
        GfxBackendSwapchain::ImageState& state = target.GetImageState();""")

rep("""            .image = _MainSwapchain().GetImage(),
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .layerCount = VK_REMAINING_ARRAY_LAYERS
            }""",
    """            .image = target.GetImage(),
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .levelCount = VK_REMAINING_MIP_LEVELS,
                .layerCount = VK_REMAINING_ARRAY_LAYERS
            }""")

rep("""        gBackendVk.queueMan.GetQueue(mQueueIndex).internalDependents |= GfxQueueType::Present;
        mDrawsToSwapchain = true;
    }""",
    """        gBackendVk.queueMan.GetQueue(mQueueIndex).internalDependents |= GfxQueueType::Present;

        ASSERT_MSG(!mSwapchainTarget.IsValid() || mSwapchainTarget == pass.swapchain,
                   "A CommandBuffer can only present to a single swapchain");
        mSwapchainTarget = pass.swapchain;
    }""")

# The remaining mDrawsToSwapchain sites
rep("""    mDrawsToSwapchain = true;""", """    mSwapchainTarget = gBackendVk.mainSwapchain;""", 2)
rep("    if (cmdBuffer.mDrawsToSwapchain) {", "    if (cmdBuffer.mSwapchainTarget.IsValid()) {")
rep("                                                                                         mDrawsToSwapchain);",
    "                                                                                         DrawsToSwapchain());")
rep("            mDrawsToSwapchain);", "            DrawsToSwapchain());")
save()

print('remaining mDrawsToSwapchain in GfxBackend.cpp:', state['s'].count('mDrawsToSwapchain'))
print('gfx step3 core ok')
