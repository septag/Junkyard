#include "../UnityBuild.inl"

#include "../Core/Log.h"
#include "../Core/MathAll.h"
#include "../Core/System.h"
#include "../Core/TracyHelper.h"

#include "../Common/VirtualFS.h"
#include "../Common/Application.h"
#include "../Common/Camera.h"
#include "../Common/JunkyardSettings.h"

#include "../ImGui/ImGuiMain.h"
#include "../ImGui/ImGuizmo.h"

#include "../DebugTools/DebugHud.h"

#include "../Engine.h"

#include "../GUI/GUI.h"

#include "../Assets/AssetManager.h"
#include "../Assets/Image.h"

static constexpr uint32 IMAGE_COUNT = 2048;
static constexpr uint32 IMAGE_GROUP_COUNT = 20;
static constexpr float THUMBNAIL_SIZE = 128.0f;
static constexpr uint16 THUMBNAIL_GAP = 8;
static constexpr uint32 THUMBNAIL_ROWS_FIRST_FRAME = 16; // On the very first frame Clay has no layout to query yet, so this is how many rows we assume are visible

struct TestUIApp final : AppCallbacks
{
    GfxImageHandle mProfileImage;
    AssetGroup mImageGroups[IMAGE_GROUP_COUNT];
    float mSideBarPercent = 0.2f;
    AssetHandleImage mImages[IMAGE_COUNT];
    GfxImageHandle mImageHandles[IMAGE_COUNT];  // mImages resolved for the current frame

    bool Initialize() override
    {
        if (!Engine::Initialize())
            return false;

        mProfileImage = Image::CreateCheckerTexture(32, 8, COLOR4U_WHITE, COLOR4U_BLACK);

        Vfs::MountLocal("data/TestUI", "data", true);

        for (uint32 i = 0; i < IMAGE_GROUP_COUNT; i++)
            mImageGroups[i] = Asset::CreateGroup();


        Path imagePath;
        uint32 chunkSize = M::CeilDiv(IMAGE_COUNT, IMAGE_GROUP_COUNT);

        for (uint32 groupIdx = 0; groupIdx < IMAGE_GROUP_COUNT; groupIdx++) {
            uint32 startIdx = groupIdx*chunkSize;
            uint32 imageCount = groupIdx == (IMAGE_GROUP_COUNT - 1) ? (IMAGE_COUNT - startIdx) : chunkSize;
            for (uint32 i = 0; i < imageCount; i++) {
                imagePath.FormatSelf("/data/%u.tga", i + startIdx + 1);
                mImages[i + startIdx] = Image::Load(imagePath.CStr(), ImageLoadParams(), mImageGroups[groupIdx]);
            }
        }

        return true;
    }

    void Cleanup() override
    {
        for (uint32 i = 0; i < IMAGE_GROUP_COUNT; i++) {
            if (mImageGroups[i].GetState() != AssetGroupState::Idle)
                mImageGroups[i].Unload();
            mImageGroups[i].WaitForIdle();
            Asset::DestroyGroup(mImageGroups[i]);
        }

        GfxBackend::DestroyImage(mProfileImage);

        Engine::Release();
    }

    INLINE bool IsPointerDown()
    {
        Clay_PointerDataInteractionState state = Clay_GetPointerState().state;
        return state == CLAY_POINTER_DATA_PRESSED_THIS_FRAME || state == CLAY_POINTER_DATA_PRESSED;
    }

    void DrawUI()
    {
        PROFILE_ZONE("DrawUI");

        constexpr Clay_Color COLOR_LIGHT = {224, 215, 210, 255};
        constexpr Clay_Color COLOR_RED = {168, 66, 28, 255};
        constexpr Clay_Color COLOR_ORANGE = {225, 138, 50, 255};
        constexpr Clay_Color COLOR_GREEN = {60, 160, 80, 255};
        constexpr Clay_Color COLOR_GREEN_HOVER = {85, 205, 110, 255};
        constexpr Clay_Color COLOR_BUTTON_PRESSED = {200, 50, 50, 255};

        // Everything below comes from the previous frame's layout, so a resize reflows one frame later
        const Clay_ScrollContainerData scrollData = Clay_GetScrollContainerData(CLAY_ID("MainContent"));
        const float rowStride = THUMBNAIL_SIZE + float(THUMBNAIL_GAP);

        // Clay has no flex-wrap for child elements (only text wraps), so thumbnail rows are chunked by hand
        uint32 thumbnailsPerRow = 1;
        if (scrollData.found) {
            float contentWidth = scrollData.scrollContainerDimensions.width - 2.0f*float(THUMBNAIL_GAP);
            float perRow = (contentWidth + float(THUMBNAIL_GAP))/rowStride;
            if (perRow >= 1.0f)
                thumbnailsPerRow = uint32(perRow);
        }
        const uint32 numThumbnailRows = (IMAGE_COUNT + thumbnailsPerRow - 1)/thumbnailsPerRow;

        // Only rows inside the visible window declare their thumbnails and resolve their images. Rows outside it
        // stay empty, they still reserve their fixed height so Clay's content size and scroll range stay correct
        uint32 firstVisibleRow = 0;
        uint32 lastVisibleRow = Min(numThumbnailRows, THUMBNAIL_ROWS_FIRST_FRAME);   // Exclusive
        if (scrollData.found) {
            // Clay scrolls by pushing children up, so scrollPosition.y is zero or negative
            float scrollY = -scrollData.scrollPosition->y;
            float firstRow = scrollY/rowStride - 1.0f;      // One row of margin to avoid popping at the edges
            float lastRow = (scrollY + scrollData.scrollContainerDimensions.height)/rowStride + 2.0f;

            firstVisibleRow = firstRow > 0 ? uint32(firstRow) : 0;
            lastVisibleRow = lastRow > 0 ? Min(numThumbnailRows, uint32(lastRow)) : 0;
        }

        CLAY(CLAY_ID("OuterContainer"), {
            .layout = { .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}, .padding = CLAY_PADDING_ALL(16), .childGap = 16 },
            .backgroundColor = {250,250,255,255} })
        {
            // Fixed 300px wide sidebar, stretched to the full height of the outer container
            CLAY(CLAY_ID("SideBar"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_PERCENT(mSideBarPercent), .height = CLAY_SIZING_GROW(0) },
                    .padding = CLAY_PADDING_ALL(16),
                    .childGap = 16,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM },
                .backgroundColor = COLOR_LIGHT
            }) {
                // Picture and the label next to it, vertically centered
                CLAY(CLAY_ID("ProfilePictureOuter"), {
                    .layout = {
                        .sizing = { .width = CLAY_SIZING_GROW(0) },
                        .padding = CLAY_PADDING_ALL(16),
                        .childGap = 16,
                        .childAlignment = { .y = CLAY_ALIGN_Y_CENTER } },
                        .backgroundColor = COLOR_RED })
                {
                    CLAY(CLAY_ID("ProfilePicture"), {
                        .layout = {.sizing = { .width = CLAY_SIZING_FIXED(60), .height = CLAY_SIZING_FIXED(60) }},
                        .image = { .imageData = &mProfileImage } }) {}
                    CLAY_TEXT(CLAY_STRING("UITest Application"), {
                        .textColor = {255, 255, 255, 255},
                        .fontSize = 24 });
                }

                // Rest of the sidebar
                CLAY(CLAY_ID("SideBarFiller"), {
                    .layout = {
                        .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
                        .padding = CLAY_PADDING_ALL(16),
                        .childGap = 8,
                        .layoutDirection = CLAY_TOP_TO_BOTTOM },
                    .backgroundColor = COLOR_ORANGE
                }) {
                    const Clay_String buttonLabels[] = {
                        CLAY_STRING("Button1"),
                        CLAY_STRING("Button2"),
                        CLAY_STRING("Button3")
                    };

                    for (uint32 i = 0; i < CountOf(buttonLabels); i++) {
                        CLAY(CLAY_IDI("Button", i), {
                            .layout = {
                                .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(40) },
                                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER } },
                            // Clay_Hovered() only refers to this button while it is the open element,
                            // so it has to be evaluated here inside the declaration
                            .backgroundColor = Clay_Hovered() ? (IsPointerDown() ? COLOR_BUTTON_PRESSED : COLOR_GREEN_HOVER)
                                                              : COLOR_GREEN,
                            .cornerRadius = CLAY_CORNER_RADIUS(8)
                        }) {
                            CLAY_TEXT(buttonLabels[i], {
                                .textColor = {255, 255, 255, 255},
                                .fontSize = 20 });
                        }
                    }
                }
            }

            // Takes up whatever horizontal space is left next to the sidebar.
            // .clip makes Clay wrap the thumbnails in SCISSOR_START/END and enables mouse-wheel scrolling
            uint32 lastLoadedGroup = uint32(-1);

            CLAY(CLAY_ID("MainContent"), {
                .layout = {
                    .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) },
                    .padding = CLAY_PADDING_ALL(THUMBNAIL_GAP),
                    .childGap = THUMBNAIL_GAP,
                    .layoutDirection = CLAY_TOP_TO_BOTTOM },
                .backgroundColor = COLOR_LIGHT,
                // .horizontal is not here to scroll sideways (childOffset.x stays 0), it's here so the thumbnail
                // rows stop propagating their minimum width up into this element. Without it MainContent can
                // never shrink below the widest row, so the column count would only ever grow on resize
                .clip = { .horizontal = true, .vertical = true, .childOffset = { 0, Clay_GetScrollOffset().y } }
            }) {
                for (uint32 row = 0; row < numThumbnailRows; row++) {
                    CLAY(CLAY_IDI("ThumbnailRow", row), {
                        .layout = {
                            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIXED(THUMBNAIL_SIZE) },
                            .childGap = THUMBNAIL_GAP }
                    }) {
                        if (row < firstVisibleRow || row >= lastVisibleRow)
                            continue;   // Inside the CLAY macro's for-loop this closes the element properly

                        uint32 chunkSize = M::CeilDiv(IMAGE_COUNT, IMAGE_GROUP_COUNT);
                        for (uint32 col = 0; col < thumbnailsPerRow; col++) {
                            uint32 index = row*thumbnailsPerRow + col;
                            if (index >= IMAGE_COUNT)
                                break;

                            uint32 groupIndex = index/chunkSize;
                            AssetGroupState groupState = mImageGroups[groupIndex].GetState();
                            if (groupState == AssetGroupState::Idle) {
                                if (lastLoadedGroup != groupIndex) {
                                    mImageGroups[groupIndex].Load();
                                    lastLoadedGroup = groupIndex;
                                }
                                continue;
                            }

                            // GUI::End() only reads the handle later in the frame, and the backend keeps image
                            // handles alive for a few frames, so the asset lock doesn't need to be held
                            AssetObjPtrScope<GfxImage> image(mImages[index]);
                            if (image.IsNull())
                                continue;
                            mImageHandles[index] = image->handle;

                            CLAY(CLAY_IDI("Thumbnail", index), {
                                .layout = { .sizing = { .width = CLAY_SIZING_FIXED(THUMBNAIL_SIZE),
                                                        .height = CLAY_SIZING_FIXED(THUMBNAIL_SIZE) } },
                                //.cornerRadius = CLAY_CORNER_RADIUS(4),
                                .image = { .imageData = &mImageHandles[index] }
                            }) {}
                        }
                    }
                }
            }
        }
    }

    void Update(float dt) override
    {
        Engine::BeginFrame(dt);
        GfxCommandBuffer cmd = GfxBackend::BeginCommandBuffer(GfxQueueType::Graphics);

        GfxBackendRenderPass pass { 
            .colorAttachments = {{ 
                .clear = true,
                .clearValue = {
                    .color = Color4u::ToFloat4(COLOR4U_BLACK)
                }
            }},
            .swapchain = true,
            .hasDepth = false
        };
        cmd.BeginRenderPass(pass);
        cmd.EndRenderPass();

        GUI::Begin();
        DrawUI();
        GUI::End(cmd);

        if (ImGui::IsEnabled()) {
            DebugHud::DrawDebugHud(dt, 20);

            ImGui::SetNextWindowSize(ImVec2(300, 200), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("UI")) {
                ImGui::SliderFloat("Sidebar", &mSideBarPercent, 0, 1, "%.2f");
            }
            ImGui::End();

            ImGui::DrawFrame(cmd);
        }
        
        GfxBackend::EndCommandBuffer(cmd);
        GfxBackend::SubmitQueue(GfxQueueType::Graphics);

        Engine::EndFrame();
    }

    void OnEvent(const AppEvent& ev) override
    {
        UNUSED(ev);
    }
};

int Main(int argc, char* argv[])
{
    SettingsJunkyard initSettings {
        .app = {
            .appName = "TestUI"
        }
    };
    SettingsJunkyard::Initialize(initSettings);

    Settings::InitializeFromINI("TestUI.ini");
    Settings::InitializeFromCommandLine(argc, argv);

    static TestUIApp impl;
    App::Run(AppDesc { 
        .callbacks = &impl, 
        .windowTitle = "Junkyard: UI test"
    });

    Settings::SaveToINI("TestUI.ini");
    Settings::Release();
    return 0;
}
