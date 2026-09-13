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


rep("""    // Present
    {
        VkSemaphore waitSemaphore = _MainSwapchain().GetSwapchainRenderFinishedSemaphore();
        uint32 queueIndex = gBackendVk.queueMan.FindQueue(GfxQueueType::Present);
        ASSERT(queueIndex != -1);
        GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(queueIndex);

        VkPresentInfoKHR presentInfo {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1u,
            .pWaitSemaphores = &waitSemaphore,
            .swapchainCount = 1,
            .pSwapchains = &_MainSwapchain().handle,
            .pImageIndices = &_MainSwapchain().imageIndex
        };

        VkResult r = vkQueuePresentKHR(queue.handle, &presentInfo);
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            _MainSwapchain().resize = true;
        }
        else if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
            // TODO: VK_SUBOPTIMAL_KHR doc says " A swapchain no longer matches the surface properties exactly, but can still be used to present to the surface successfully."
            //       But I need to investigate a bit more on when this happens actually
            ASSERT_ALWAYS(false, "Gfx: Present swapchain failed");
        }
    }

    _CollectGarbage(false);

    if (_MainSwapchain().resize) {
        vkDeviceWaitIdle(gBackendVk.device);
        GfxBackend::_ResizeSwapchain(&_MainSwapchain(),
                                     Int2(App::GetFramebufferWidth(), App::GetFramebufferHeight()),
                                     SettingsJunkyard::Get().graphics.surfaceSRGB);
    }""",
    """    // Present every swapchain acquired this frame in a single call
    {
        uint32 numSwapchains = gBackendVk.numAcquiredSwapchains;
        ASSERT(numSwapchains);

        VkSemaphore waitSemaphores[GFXBACKEND_MAX_SWAPCHAIN_TARGETS];
        VkSwapchainKHR swapchainHandles[GFXBACKEND_MAX_SWAPCHAIN_TARGETS];
        uint32 imageIndices[GFXBACKEND_MAX_SWAPCHAIN_TARGETS];
        VkResult presentResults[GFXBACKEND_MAX_SWAPCHAIN_TARGETS];

        for (uint32 i = 0; i < numSwapchains; i++) {
            GfxBackendSwapchain& swapchain = gBackendVk.swapchains.Data(gBackendVk.acquiredSwapchains[i]);
            waitSemaphores[i] = swapchain.GetSwapchainRenderFinishedSemaphore();
            swapchainHandles[i] = swapchain.handle;
            imageIndices[i] = swapchain.imageIndex;
        }

        uint32 queueIndex = gBackendVk.queueMan.FindQueue(GfxQueueType::Present);
        ASSERT(queueIndex != -1);
        GfxBackendQueue& queue = gBackendVk.queueMan.GetQueue(queueIndex);

        VkPresentInfoKHR presentInfo {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = numSwapchains,
            .pWaitSemaphores = waitSemaphores,
            .swapchainCount = numSwapchains,
            .pSwapchains = swapchainHandles,
            .pImageIndices = imageIndices,
            .pResults = presentResults
        };

        VkResult r = vkQueuePresentKHR(queue.handle, &presentInfo);
        if (r != VK_SUCCESS && r != VK_ERROR_OUT_OF_DATE_KHR && r != VK_SUBOPTIMAL_KHR) {
            // TODO: VK_SUBOPTIMAL_KHR doc says " A swapchain no longer matches the surface properties exactly, but can still be used to present to the surface successfully."
            //       But I need to investigate a bit more on when this happens actually
            ASSERT_ALWAYS(false, "Gfx: Present swapchain failed");
        }

        // pResults reports per-swapchain status, so one stale window does not resize the others
        for (uint32 i = 0; i < numSwapchains; i++) {
            if (presentResults[i] == VK_ERROR_OUT_OF_DATE_KHR)
                gBackendVk.swapchains.Data(gBackendVk.acquiredSwapchains[i]).resize = true;
        }
    }

    _CollectGarbage(false);

    // Only the main swapchain is resized here. Secondary ones follow their window, so their owner
    // drives GfxBackend::ResizeSwapchain instead
    if (_MainSwapchain().resize) {
        vkDeviceWaitIdle(gBackendVk.device);
        GfxBackend::_ResizeSwapchain(&_MainSwapchain(),
                                     Int2(App::GetFramebufferWidth(), App::GetFramebufferHeight()),
                                     SettingsJunkyard::Get().graphics.surfaceSRGB);
    }""")

io.open(path, 'w', encoding='utf-8', newline='').write(s)
print('gfx step4 present ok')
