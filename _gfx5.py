import io
import re

path = 'code/Graphics/GfxBackend.cpp'
s = io.open(path, encoding='utf-8', newline='').read()


def rep(a, b, n=1):
    global s
    pat = re.compile(r'[ \t]*\r\n'.join(re.escape(x) for x in a.split('\n')))
    b2 = b.replace('\n', '\r\n')
    found = len(pat.findall(s))
    assert found == n, (found, n, a[:110])
    s = pat.sub(lambda m: b2, s, count=n)


# Attachment builder needs to know *which* swapchain, not just whether
rep("    static VkRenderingAttachmentInfo _TransitionAndMakeAttachment(const GfxRenderPassAttachment& attachment, VkCommandBuffer cmdVk, bool toSwapchain)",
    "    static VkRenderingAttachmentInfo _TransitionAndMakeAttachment(const GfxRenderPassAttachment& attachment, VkCommandBuffer cmdVk, GfxSwapchainHandle toSwapchain)")

rep("            viewHandle = _MainSwapchain().GetImageView();",
    "            viewHandle = gBackendVk.swapchains.Data(toSwapchain).GetImageView();")
rep("            resolveImageView = _MainSwapchain().GetImageView();",
    "            resolveImageView = gBackendVk.swapchains.Data(toSwapchain).GetImageView();")

rep("        depthAttachment = GfxBackend::_TransitionAndMakeAttachment(pass.depthAttachment, cmdVk, false);",
    "        depthAttachment = GfxBackend::_TransitionAndMakeAttachment(pass.depthAttachment, cmdVk, GfxSwapchainHandle());")

# --- BeginRenderPass
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

rep("""            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = _MainSwapchain().GetImage(),""",
    """            .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .image = target.GetImage(),""")

rep("""        gBackendVk.queueMan.GetQueue(mQueueIndex).internalDependents |= GfxQueueType::Present;
        mDrawsToSwapchain = true;
    }""",
    """        gBackendVk.queueMan.GetQueue(mQueueIndex).internalDependents |= GfxQueueType::Present;

        ASSERT_MSG(!mSwapchainTarget.IsValid() || mSwapchainTarget == pass.swapchain,
                   "A CommandBuffer can only present to a single swapchain");
        mSwapchainTarget = pass.swapchain;
    }""")

# --- EndCommandBuffer present transition follows the command buffer's own target
rep("""    if (cmdBuffer.mDrawsToSwapchain) {
        // Transition the swapchain to PRESENT layout if we have drawn to it
        GfxBackendSwapchain::ImageState& state = _MainSwapchain().GetImageState();""",
    """    if (cmdBuffer.mSwapchainTarget.IsValid()) {
        // Transition the swapchain to PRESENT layout if we have drawn to it
        GfxBackendSwapchain& target = gBackendVk.swapchains.Data(cmdBuffer.mSwapchainTarget);
        GfxBackendSwapchain::ImageState& state = target.GetImageState();""")

rep("""            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = _MainSwapchain().GetImage(),""",
    """            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .image = target.GetImage(),""")

# --- ClearSwapchainColor / CopyImageToSwapchain always mean the main swapchain
rep("    mDrawsToSwapchain = true;", "    mSwapchainTarget = gBackendVk.mainSwapchain;", 2)

# --- Orientation transform helpers
rep("                                                                                         mDrawsToSwapchain);",
    "                                                                                         DrawsToSwapchain());")
rep("            mDrawsToSwapchain);", "            DrawsToSwapchain());")

io.open(path, 'w', encoding='utf-8', newline='').write(s)
print('remaining mDrawsToSwapchain:', s.count('mDrawsToSwapchain'))
print('gfx step3 cpp ok')
