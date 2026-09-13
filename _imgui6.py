import io
import re

path = 'code/ImGui/ImGuiMain.cpp'
s = io.open(path, encoding='utf-8', newline='').read()


def rep(a, b, n=1):
    global s
    pat = re.compile(r'[ \t]*\r\n'.join(re.escape(x) for x in a.split('\n')))
    b2 = b.replace('\n', '\r\n')
    found = len(pat.findall(s))
    assert found == n, (found, n, a[:110])
    s = pat.sub(lambda m: b2, s, count=n)


# --- DrawFrame now delegates the shared work
rep("""    // Fill the buffers
    if (drawData->TotalVtxCount) {
        _GrowGeometryBuffers(drawData->TotalVtxCount, drawData->TotalIdxCount);

        uint32 indexSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);
        uint32 vertexSize = drawData->TotalVtxCount * sizeof(ImDrawVert);

        GfxHelperBufferUpdateScope vertexBufferUpdate(cmd, gImGui.vertexBuffer, vertexSize, GfxShaderStage::Vertex);
        GfxHelperBufferUpdateScope indexBufferUpdate(cmd, gImGui.indexBuffer, indexSize, GfxShaderStage::Vertex);

        ImDrawVert* vertices = (ImDrawVert*)vertexBufferUpdate.mData;
        ImDrawIdx* indices = (ImDrawIdx*)indexBufferUpdate.mData;

        for (int i = 0; i < drawData->CmdLists.Size; i++) {
            const ImDrawList* cmdList = drawData->CmdLists[i];
            memcpy(vertices, cmdList->VtxBuffer.Data, cmdList->VtxBuffer.Size * sizeof(ImDrawVert));
            memcpy(indices, cmdList->IdxBuffer.Data, cmdList->IdxBuffer.Size * sizeof(ImDrawIdx));

            vertices += cmdList->VtxBuffer.Size;
            indices += cmdList->IdxBuffer.Size;
        }
    }
""",
    """    _UploadDrawData(cmd, drawData, &gImGui.vertexBuffer, &gImGui.maxVertices,
                    &gImGui.indexBuffer, &gImGui.maxIndices);
""")

# Replace the inlined draw loop with the extracted helper
i = s.find('    cmd.BeginRenderPass(pass);\r\n\r\n    // Draw\r\n')
assert i > 0, 'BeginRenderPass anchor not found'
j = s.find('    cmd.EndRenderPass();\r\n    return true;\r\n}', i)
assert j > i, 'EndRenderPass anchor not found'
s = s[:i] + ('    cmd.BeginRenderPass(pass);\r\n'
             '    _RecordDrawCommands(cmd, drawData, gImGui.vertexBuffer, gImGui.indexBuffer);\r\n') + s[j:]

# --- Renderer viewport callbacks
rep("""    static void _UpdateMonitors()""",
    """    static ImGuiViewportRenderData* _ViewportRenderData(ImGuiViewport* viewport)
    {
        return reinterpret_cast<ImGuiViewportRenderData*>(viewport->RendererUserData);
    }

    static void _RendererCreateWindow(ImGuiViewport* viewport)
    {
        void* nativeWindow = viewport->PlatformHandleRaw;
        if (nativeWindow == nullptr)
            return;

        Int2 size(int(viewport->Size.x), int(viewport->Size.y));
        GfxSwapchainHandle swapchain = GfxBackend::CreateSwapchain(nativeWindow, size);
        if (!swapchain.IsValid())
            return;

        ImGuiViewportRenderData* data = NEW(&gImGui.runtimeAlloc, ImGuiViewportRenderData);
        data->swapchain = swapchain;
        viewport->RendererUserData = data;
    }

    static void _RendererDestroyWindow(ImGuiViewport* viewport)
    {
        ImGuiViewportRenderData* data = _ViewportRenderData(viewport);
        if (data == nullptr)
            return;

        GfxBackend::DestroySwapchain(data->swapchain);
        GfxBackend::DestroyBuffer(data->vertexBuffer);
        GfxBackend::DestroyBuffer(data->indexBuffer);

        Mem::Free(data, &gImGui.runtimeAlloc);
        viewport->RendererUserData = nullptr;
    }

    static void _RendererSetWindowSize(ImGuiViewport* viewport, ImVec2 size)
    {
        if (ImGuiViewportRenderData* data = _ViewportRenderData(viewport))
            GfxBackend::ResizeSwapchain(data->swapchain, Int2(int(size.x), int(size.y)));
    }

    static void _RendererRenderWindow(ImGuiViewport* viewport, void*)
    {
        ImGuiViewportRenderData* data = _ViewportRenderData(viewport);
        ImDrawData* drawData = viewport->DrawData;
        if (data == nullptr || drawData == nullptr || drawData->CmdLists.Size == 0)
            return;

        GfxCommandBuffer cmd = GfxBackend::BeginCommandBuffer(GfxQueueType::Graphics);
        GPU_PROFILE_ZONE(cmd, "ImGuiViewport");

        _UploadDrawData(cmd, drawData, &data->vertexBuffer, &data->maxVertices,
                        &data->indexBuffer, &data->maxIndices);

        // Viewport swapchains are plain color targets: no MSAA, no depth, no resolve
        GfxBackendRenderPass pass {
            .colorAttachments = {{ .clear = !(viewport->Flags & ImGuiViewportFlags_NoRendererClear) }},
            .swapchain = data->swapchain
        };
        cmd.BeginRenderPass(pass);
        _RecordDrawCommands(cmd, drawData, data->vertexBuffer, data->indexBuffer);
        cmd.EndRenderPass();

        GfxBackend::EndCommandBuffer(cmd);
    }

    static void _UpdateMonitors()""")

rep("""        platformIO.Platform_GetWindowDpiScale = _PlatformGetWindowDpiScale;
""",
    """        platformIO.Platform_GetWindowDpiScale = _PlatformGetWindowDpiScale;

        platformIO.Renderer_CreateWindow = _RendererCreateWindow;
        platformIO.Renderer_DestroyWindow = _RendererDestroyWindow;
        platformIO.Renderer_SetWindowSize = _RendererSetWindowSize;
        platformIO.Renderer_RenderWindow = _RendererRenderWindow;
""")

# --- Viewport draws are recorded after the app already submitted, so they need their own submit
rep("""    UpdatePlatformWindows();

    // Creates/moves/destroys the platform windows. Nothing is drawn into secondary viewports yet,
    // because the graphics backend cannot make a swapchain per window
    RenderPlatformWindowsDefault();""",
    """    UpdatePlatformWindows();

    // Records a command buffer per viewport. The app already submitted its own work by now,
    // so these need a submit of their own before GfxBackend::End() presents everything
    RenderPlatformWindowsDefault();

    if (GetPlatformIO().Viewports.Size > 1)
        GfxBackend::SubmitQueue(GfxQueueType::Graphics);""")

io.open(path, 'w', encoding='utf-8', newline='').write(s)
print('imgui step5 part B ok')
