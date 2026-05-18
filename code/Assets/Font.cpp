#include "Font.h"
#include "AssetManager.h"
#include "Image.h"

#include "../Core/Allocators.h"
#include "../Core/JsonParser.h"

#include "../Common/VirtualFS.h"

static constexpr uint32 FONT_ASSET_TYPE = MakeFourCC('F', 'O', 'N', 'T');
static constexpr uint32 FONT_ASSET_CACHE_VERSION = 1;

struct AssetFontImpl final : AssetTypeImplBase
{
    bool Bake(const AssetParams& params, AssetData* data, const Span<uint8>& srcData, String<256>* outErrorDesc) override;
    bool Reload(void* newData, void* oldData) override;
};

struct AssetFontManager
{
    AssetFontImpl fontImpl;
    FontData* blankFont;
};

static AssetFontManager gFontMgr;

bool Font::InitializeManager()
{
    static uint8 blankFontData[sizeof(FontData) + sizeof(FontGlyph) + 32];
    MemSingleShotMalloc<FontData> fontDataMallocator;
    fontDataMallocator.AddMemberArray<uint16>(offsetof(FontData, glyphIds), 1, true, 8);
    fontDataMallocator.AddMemberArray<FontGlyph>(offsetof(FontData, glyphs), 1, true, 8);
    gFontMgr.blankFont = fontDataMallocator.Calloc(blankFontData, sizeof(blankFontData));
    Str::Copy(gFontMgr.blankFont->name, sizeof(gFontMgr.blankFont->name), "Blank");
    gFontMgr.blankFont->size = 12;
    gFontMgr.blankFont->atlasWidth = 1;
    gFontMgr.blankFont->atlasHeight = 1;
    gFontMgr.blankFont->numGlyphs = 1;
    gFontMgr.blankFont->glyphIds[0] = uint16(-1);

    AssetTypeDesc assetDesc {
        .name = "Font",
        .fourcc = FONT_ASSET_TYPE,
        .cacheVersion = FONT_ASSET_CACHE_VERSION,
        .impl = &gFontMgr.fontImpl,
        .failedObj = gFontMgr.blankFont,
        .asyncObj = gFontMgr.blankFont        
    };
    Asset::RegisterType(assetDesc);
    
    return true;
}

void Font::ReleaseManager()
{
    Asset::UnregisterType(FONT_ASSET_TYPE);
}

AssetHandleFont Font::Load(const char* path, const AssetGroup& group)
{
    AssetParams assetParams {
        .typeId = FONT_ASSET_TYPE,
        .path = path
    };

    return group.AddToLoadQueue(assetParams);
}

bool AssetFontImpl::Bake(const AssetParams& params, AssetData* data, const Span<uint8>& srcData, String<256>* outErrorDesc)
{
    MemTempAllocator tempAlloc;
    JsonErrorLocation jsonErrorLoc;
    JsonContext* jctx = Json::Parse((const char*)srcData.Ptr(), srcData.Count(), &jsonErrorLoc, &tempAlloc);
    if (jctx == nullptr) {
        outErrorDesc->FormatSelf("Loading JSON data from '%s' failed (%u:%u)", params.path.CStr(), jsonErrorLoc.line, jsonErrorLoc.col);
        return false;
    }

    Path filename = params.path.GetFileName();

    JsonNode jroot(jctx);
    FontData* font = tempAlloc.MallocZeroTyped<FontData>();
    jroot.GetChildValueString("name", font->name, sizeof(font->name), filename.CStr());
    
    {
        JsonNode jatlas = jroot.GetChild("atlas");
        font->size = jatlas.GetChildValue("size", 0u);
        font->atlasWidth = jatlas.GetChildValue("width", 0u);
        font->atlasHeight = jatlas.GetChildValue("height", 0u);
        
        // Validation
        char origin[32];
        jatlas.GetChildValueString("yOrigin", origin, sizeof(origin), "bottom");
        if (!Str::IsEqual(origin, "top")) {
            outErrorDesc->FormatSelf("Error loading font '%s'. Origin should be 'top'", params.path.CStr());
            return false;
        }
    }

    {
        JsonNode jmetrics = jroot.GetChild("metrics");
        font->lineHeight = jmetrics.GetChildValue("lineHeight", 0.0f);
        font->ascender = jmetrics.GetChildValue("ascender", 0.0f);
        font->descender = jmetrics.GetChildValue("descender", 0.0f);
        font->underlineY = jmetrics.GetChildValue("underlineY", 0.0f);
    }

    {
        JsonNode jglyphs = jroot.GetChild("glyphs");
        font->numGlyphs = jglyphs.GetArrayCount();
        if (font->numGlyphs == 0) {
            outErrorDesc->FormatSelf("Error loading font '%s'. No glyphs in the font", params.path.CStr());
            return false;
        }
        font->glyphIds = tempAlloc.MallocZeroTyped<uint16>(font->numGlyphs);
        font->glyphs = tempAlloc.MallocZeroTyped<FontGlyph>(font->numGlyphs);

        for (uint32 i = 0; i < font->numGlyphs; i++) {
            JsonNode jglyph = jglyphs.GetArrayItem(i);
            FontGlyph& glyph = font->glyphs[i];
            font->glyphIds[i] = (uint16)jglyph.GetChildValue("unicode", uint32(-1));
            glyph.xadvance = jglyph.GetChildValue("advance", 0.0f);
            
            JsonNode jpb = jglyph.GetChild("planeBounds");
            glyph.planeBounds = RectFloat(
                jpb.GetChildValue("left", 0.0f),
                jpb.GetChildValue("top", 0.0f),
                jpb.GetChildValue("right", 0.0f),
                jpb.GetChildValue("bottom", 0.0f)
            );
            
            JsonNode jab = jglyph.GetChild("atlasBounds");
            glyph.uvBounds = RectFloat(
                jab.GetChildValue("left", 0.0f),
                jab.GetChildValue("top", 0.0f),
                jab.GetChildValue("right", 0.0f),
                jab.GetChildValue("bottom", 0.0f)
            );
        }
    }

    {
        JsonNode jkerns = jroot.GetChild("kerning");
        font->numKernings = jkerns.GetArrayCount();
        if (font->numKernings) {
            font->kernings = tempAlloc.MallocZeroTyped<FontKerning>(font->numKernings);

            for (uint32 i = 0; i < font->numKernings; i++) {
                JsonNode jkern = jkerns.GetArrayItem(i);
                font->kernings[i] = FontKerning {
                    .firstId = (uint16)jkern.GetChildValue("unicode1", 0u),
                    .secondId = (uint16)jkern.GetChildValue("unicode2", 0u),
                    .xadvance = jkern.GetChildValue("advance", 0.0f)
                };
            }
        }
    }

    // Try to load the source font file
    Path fileDir = params.path.GetDirectory();
    Path sourceFontTTF = Path::JoinUnix(fileDir, filename).Append(".ttf");
    if (!Vfs::FileExists(sourceFontTTF.CStr())) 
        sourceFontTTF = Path::JoinUnix(fileDir, filename).Append(".otf");
    Blob sourceFontData = Vfs::ReadFile(sourceFontTTF.CStr(), VfsFlags::None, &tempAlloc);
    if (sourceFontData.IsValid()) {
        ASSERT(sourceFontData.Size() <= UINT32_MAX);
        font->fontSourceSize = (uint32)sourceFontData.Size();
        font->fontSourceData = (uint8*)sourceFontData.Data();
    }

    size_t fontDataSize = tempAlloc.GetOffset() - tempAlloc.GetPointerOffset(font);
    ASSERT(fontDataSize <= UINT32_MAX);
    data->SetObjData(font, (uint32)fontDataSize);

    // Dependencies (Textures)
    ImageLoadParams atlasLoadParams {};
    AssetParams atlasParams {
        .typeId = IMAGE_ASSET_TYPE,
        .path = Path::Join(fileDir, filename).Append(".png"),
        .platform = params.platform,
        .extraParams = &atlasLoadParams
    };
    data->AddDependency(&font->atlas, atlasParams);

    return true;
}

bool AssetFontImpl::Reload(void*, void*)
{
    return false;
}