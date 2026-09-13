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


# Shared semaphore (re)creation, used on both first init and every recreate
rep("""    static bool _ResizeSwapchain(GfxBackendSwapchain* swapchain, Int2 size, bool forceSRGB)
    {""",
    """    // A present that fails with OUT_OF_DATE does not necessarily consume its wait semaphore, so a
    // renderFinished semaphore can survive signalled. Signalling an already-signalled binary semaphore
    // on the next frame wedges the queue, so they are all rebuilt whenever the swapchain is.
    // Caller must have made the device idle first
    static void _RecreateSwapchainSemaphores(GfxBackendSwapchain* swapchain)
    {
        VkSemaphoreCreateInfo semCreateInfo { .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

        for (uint32 i = 0; i < GFXBACKEND_BACKBUFFER_COUNT; i++) {
            if (swapchain->renderFinishedSemaphores[i])
                vkDestroySemaphore(gBackendVk.device, swapchain->renderFinishedSemaphores[i], gBackendVk.vkAlloc);
            vkCreateSemaphore(gBackendVk.device, &semCreateInfo, gBackendVk.vkAlloc, &swapchain->renderFinishedSemaphores[i]);
        }

        for (uint32 i = 0; i < GFXBACKEND_FRAMES_IN_FLIGHT; i++) {
            if (swapchain->imageReadySemaphores[i])
                vkDestroySemaphore(gBackendVk.device, swapchain->imageReadySemaphores[i], gBackendVk.vkAlloc);
            vkCreateSemaphore(gBackendVk.device, &semCreateInfo, gBackendVk.vkAlloc, &swapchain->imageReadySemaphores[i]);
        }
    }

    static bool _ResizeSwapchain(GfxBackendSwapchain* swapchain, Int2 size, bool forceSRGB)
    {""")

rep("""        swapchain->format = chosenFormat;
        swapchain->resize = false;
        memset(swapchain->imageStates, 0x0, sizeof(swapchain->imageStates));

        return true;
    }""",
    """        swapchain->format = chosenFormat;
        swapchain->resize = false;
        memset(swapchain->imageStates, 0x0, sizeof(swapchain->imageStates));
        _RecreateSwapchainSemaphores(swapchain);

        return true;
    }""")

rep("""        if (!_ResizeSwapchain(swapchain, size, forceSRGB))
            return false;

        // Semaphores
        VkSemaphoreCreateInfo semCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        };

        for (uint32 i = 0; i < GFXBACKEND_BACKBUFFER_COUNT; i++) {
            vkCreateSemaphore(gBackendVk.device, &semCreateInfo, gBackendVk.vkAlloc, &swapchain->renderFinishedSemaphores[i]);
        }

        for (uint32 i = 0; i < GFXBACKEND_FRAMES_IN_FLIGHT; i++) {
            vkCreateSemaphore(gBackendVk.device, &semCreateInfo, gBackendVk.vkAlloc, &swapchain->imageReadySemaphores[i]);
        }

        return true;
    }""",
    """        // _ResizeSwapchain creates the semaphores as part of building the swapchain
        if (!_ResizeSwapchain(swapchain, size, forceSRGB))
            return false;

        return true;
    }""")

io.open(path, 'w', encoding='utf-8', newline='').write(s)
print('semaphore recreation ok')
