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


# --- 1) Per-viewport renderer data
rep("""struct ImGuiState
{""",
    """// Renderer side of a secondary viewport. Each one owns a swapchain and its own geometry buffers,
// because viewports are rendered in separate command buffers
struct ImGuiViewportRenderData
{
    GfxSwapchainHandle swapchain;
    GfxBufferHandle vertexBuffer;
    GfxBufferHandle indexBuffer;
    uint32 maxVertices;
    uint32 maxIndices;
};

struct ImGuiState
{""")

# --- 2) Generalize the geometry growth so viewports can reuse it
rep("""    static void _GrowGeometryBuffers(uint32 numVertices, uint32 numIndices)
    {
        if (numVertices > gImGui.maxVertices) {
            gImGui.maxVertices = AlignValue(numVertices, IMGUI_VERTICES_POOL_SIZE);
            GfxBackend::DestroyBuffer(gImGui.vertexBuffer);
            GfxBufferDesc vertexBufferDesc {
                .sizeBytes = gImGui.maxVertices*sizeof(ImDrawVert),
                .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Vertex,
                .perFrameUpdates = true
            };
            gImGui.vertexBuffer = GfxBackend::CreateBuffer(vertexBufferDesc);

            LOG_VERBOSE("ImGui vertex capacity increased to maximum %u vertices", gImGui.maxVertices);
        }

        if (numIndices > gImGui.maxIndices) {
            gImGui.maxIndices = AlignValue(numIndices, IMGUI_INDICES_POOL_SIZE);
            GfxBackend::DestroyBuffer(gImGui.indexBuffer);
            GfxBufferDesc indexBufferDesc {
                .sizeBytes = gImGui.maxIndices*sizeof(ImDrawIdx),
                .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Index,
                .perFrameUpdates = true
            };
            gImGui.indexBuffer = GfxBackend::CreateBuffer(indexBufferDesc);

            LOG_VERBOSE("ImGui index capacity increased to maximum %u indices", gImGui.maxIndices);
        }
    }""",
    """    static void _GrowGeometryBuffers(GfxBufferHandle* vertexBuffer, uint32* maxVertices,
                                     GfxBufferHandle* indexBuffer, uint32* maxIndices,
                                     uint32 numVertices, uint32 numIndices)
    {
        if (numVertices > *maxVertices) {
            *maxVertices = AlignValue(numVertices, IMGUI_VERTICES_POOL_SIZE);
            GfxBackend::DestroyBuffer(*vertexBuffer);
            GfxBufferDesc vertexBufferDesc {
                .sizeBytes = (*maxVertices)*sizeof(ImDrawVert),
                .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Vertex,
                .perFrameUpdates = true
            };
            *vertexBuffer = GfxBackend::CreateBuffer(vertexBufferDesc);

            LOG_VERBOSE("ImGui vertex capacity increased to maximum %u vertices", *maxVertices);
        }

        if (numIndices > *maxIndices) {
            *maxIndices = AlignValue(numIndices, IMGUI_INDICES_POOL_SIZE);
            GfxBackend::DestroyBuffer(*indexBuffer);
            GfxBufferDesc indexBufferDesc {
                .sizeBytes = (*maxIndices)*sizeof(ImDrawIdx),
                .usageFlags = GfxBufferUsageFlags::TransferDst|GfxBufferUsageFlags::Index,
                .perFrameUpdates = true
            };
            *indexBuffer = GfxBackend::CreateBuffer(indexBufferDesc);

            LOG_VERBOSE("ImGui index capacity increased to maximum %u indices", *maxIndices);
        }
    }

    // Grows and fills the geometry buffers. Must run outside of a RenderPass
    static void _UploadDrawData(GfxCommandBuffer cmd, ImDrawData* drawData,
                                GfxBufferHandle* vertexBuffer, uint32* maxVertices,
                                GfxBufferHandle* indexBuffer, uint32* maxIndices)
    {
        if (drawData->TotalVtxCount == 0)
            return;

        _GrowGeometryBuffers(vertexBuffer, maxVertices, indexBuffer, maxIndices,
                             drawData->TotalVtxCount, drawData->TotalIdxCount);

        uint32 indexSize = drawData->TotalIdxCount * sizeof(ImDrawIdx);
        uint32 vertexSize = drawData->TotalVtxCount * sizeof(ImDrawVert);

        GfxHelperBufferUpdateScope vertexBufferUpdate(cmd, *vertexBuffer, vertexSize, GfxShaderStage::Vertex);
        GfxHelperBufferUpdateScope indexBufferUpdate(cmd, *indexBuffer, indexSize, GfxShaderStage::Vertex);

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

    // Records the actual draw calls. Must run inside a RenderPass targeting `drawData`'s viewport
    static void _RecordDrawCommands(GfxCommandBuffer cmd, ImDrawData* drawData,
                                    GfxBufferHandle vertexBuffer, GfxBufferHandle indexBuffer)
    {
        Float2 displayPos = Float2(drawData->DisplayPos.x, drawData->DisplayPos.y);
        Float2 displaySize = Float2(drawData->DisplaySize.x, drawData->DisplaySize.y);

        // Origin stays at 0: the projection below already folds displayPos in, and with viewports enabled
        // displayPos is the window's desktop position, which must not offset the render target viewport
        GfxViewport viewport {
            .x = 0,
            .y = 0,
            .width = displaySize.x,
            .height = displaySize.y
        };

        uint64 offsets[] = {0};
        cmd.BindPipeline(gImGui.pipeline);
        cmd.SetViewports(0, 1, &viewport);
        cmd.BindVertexBuffers(0, 1, &vertexBuffer, offsets);
        cmd.BindIndexBuffer(indexBuffer, 0, GfxIndexType::Uint16);

        ImGuiShaderTransform transform {
            .projMat = GfxBackend::GetSwapchainTransformMat() * Mat4::OrthoOffCenter(displayPos.x, displayPos.y + displaySize.y,
                                                                                     displayPos.x + displaySize.x, displayPos.y,
                                                                                     -1.0f, 1.0f)
        };
        cmd.PushConstants<ImGuiShaderTransform>(gImGui.pipelineLayout, "Transform", transform);

        GfxImageHandle boundImage;
        uint32 globalVertexOffset = 0;
        uint32 globalIndexOffset = 0;
        for (int i = 0; i < drawData->CmdLists.Size; i++) {
            const ImDrawList* cmdList = drawData->CmdLists[i];

            for (int k = 0; k < cmdList->CmdBuffer.Size; k++) {
                const ImDrawCmd* drawCmd = &cmdList->CmdBuffer[k];

                if (drawCmd->UserCallback) {
                    drawCmd->UserCallback(cmdList, drawCmd);
                }
                else {
                    Float4 clipRect((drawCmd->ClipRect.x - displayPos.x), (drawCmd->ClipRect.y - displayPos.y),
                                    (drawCmd->ClipRect.z - displayPos.x), (drawCmd->ClipRect.w - displayPos.y));

                    if (clipRect.x < 0.0f) { clipRect.x = 0.0f; }
                    if (clipRect.y < 0.0f) { clipRect.y = 0.0f; }
                    if (clipRect.z > displaySize.x) { clipRect.z = displaySize.x; }
                    if (clipRect.w > displaySize.y) { clipRect.w = displaySize.y; }
                    if (clipRect.z <= clipRect.x || clipRect.w <= clipRect.y)
                        continue;

                    RectInt scissor(int(clipRect.x), int(clipRect.y), int(clipRect.z), int(clipRect.w));

                    GfxImageHandle img(uint32(drawCmd->GetTexID()));
                    if (img != boundImage) {
                        GfxBindingDesc bindings[] = {
                            {
                                .name = "MainTexture",
                                .image = img,
                                .sampler = gImGui.sampler
                            }
                        };
                        cmd.PushBindings(gImGui.pipelineLayout, CountOf(bindings), bindings);
                        boundImage = img;
                    }

                    cmd.SetScissors(0, 1, &scissor);
                    cmd.DrawIndexed(drawCmd->ElemCount, 1, drawCmd->IdxOffset + globalIndexOffset, drawCmd->VtxOffset + globalVertexOffset, 0);
                }
            }

            globalIndexOffset += cmdList->IdxBuffer.Size;
            globalVertexOffset += cmdList->VtxBuffer.Size;
        }

        RectInt rc(0, 0, int(displaySize.x), int(displaySize.y));
        cmd.SetScissors(0, 1, &rc);
    }""")

io.open(path, 'w', encoding='utf-8', newline='').write(s)
print('imgui step5 part A ok')
