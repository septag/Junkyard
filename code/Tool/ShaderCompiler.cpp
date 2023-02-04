#include "ShaderCompiler.h"

#if CONFIG_TOOLMODE

#include <stdio.h>

#include "../External/slang/slang.h"

#include "../Core/Buffers.h"
#include "../Core/Log.h"

struct ShaderCompiler
{
    SlangSession* slang;
};

static ShaderCompiler gShaderCompiler;

static ShaderStage shaderTranslateStage(SlangStage stage)
{
    switch (stage) {
    case SLANG_STAGE_VERTEX:            return ShaderStage::Vertex;
    case SLANG_STAGE_FRAGMENT:          return ShaderStage::Fragment;
    case SLANG_STAGE_COMPUTE:           return ShaderStage::Compute;
    default:                            ASSERT_MSG(false, "Stage not supported"); return ShaderStage::Unknown;
    }
}

static GfxFormat shaderTranslateVertexInputFormat([[maybe_unused]] uint32 rows, uint32 cols, 
                                                  slang::TypeReflection::ScalarType scalarType, slang::TypeReflection::Kind kind)
{
    auto translateVector2 = [](slang::TypeReflection::ScalarType scalarType)->GfxFormat {
        switch (scalarType) {
        case slang::TypeReflection::Float32:     return GfxFormat::R32G32_SFLOAT;
        case slang::TypeReflection::Int32:       return GfxFormat::R32G32_SINT;
        case slang::TypeReflection::UInt32:      return GfxFormat::R32G32_UINT;
        default:                                 ASSERT_MSG(0, "Vertex scalar types other than float/uint/int are not supported");
        }
        return GfxFormat::Undefined;
    };
    auto translateVector3 = [](slang::TypeReflection::ScalarType scalarType)->GfxFormat {
        switch (scalarType) {
        case slang::TypeReflection::Float32:     return GfxFormat::R32G32B32_SFLOAT;
        case slang::TypeReflection::Int32:       return GfxFormat::R32G32B32_SINT;
        case slang::TypeReflection::UInt32:      return GfxFormat::R32G32B32_UINT;
        default:                                 ASSERT_MSG(0, "Vertex scalar types other than float/uint/int are not supported");
        }
        return GfxFormat::Undefined;
    };
    auto translateVector4 = [](slang::TypeReflection::ScalarType scalarType)->GfxFormat {
        switch (scalarType) {
            case slang::TypeReflection::Float32:     return GfxFormat::R32G32B32A32_SFLOAT;
            case slang::TypeReflection::Int32:       return GfxFormat::R32G32B32A32_SINT;
            case slang::TypeReflection::UInt32:      return GfxFormat::R32G32B32A32_UINT;
            default:                                 ASSERT_MSG(0, "Vertex scalar types other than float/uint/int are not supported");
        }
        return GfxFormat::Undefined;
    };
    
    // We only support scalars and vectors
    ASSERT(rows == 1);

    if (kind == slang::TypeReflection::Kind::Scalar) {
        ASSERT(cols == 1);
        switch (scalarType) {
        case slang::TypeReflection::Float32:     return GfxFormat::R32_SFLOAT;
        case slang::TypeReflection::Int32:       return GfxFormat::R32_SINT;
        case slang::TypeReflection::UInt32:      return GfxFormat::R32_UINT; 
        default:                                 ASSERT_MSG(0, "Vertex scalar types other than float/uint/int are not supported");
        }
    } 
    else if (kind == slang::TypeReflection::Kind::Vector) {
        switch (cols) {
        case 2:         return translateVector2(scalarType);
        case 3:         return translateVector3(scalarType);
        case 4:         return translateVector4(scalarType);
        default:        ASSERT_MSG(0, "Input vector vertex attributes other than float2/float3/float4 is not supported");   break;
        }
    }
    else {
        ASSERT_MSG(0, "Only vector and scalar vertex inputs are supported");
    }

    return GfxFormat::Undefined;
}


Pair<Shader*, uint32> shaderCompile(const Blob& blob, const char* filepath, const ShaderCompileDesc& desc, 
                                    char* errorDiag, uint32 errorDiagSize, Allocator* alloc)
{
    ASSERT(gShaderCompiler.slang);

    SlangCompileRequest* req = spCreateCompileRequest(gShaderCompiler.slang);

    spSetCodeGenTarget(req, SLANG_SPIRV);
    spSetMatrixLayoutMode(req, SLANG_MATRIX_LAYOUT_COLUMN_MAJOR);

    if (desc.debug) {
        spSetDebugInfoLevel(req, SLANG_DEBUG_INFO_LEVEL_MAXIMAL);
        spSetOptimizationLevel(req, SLANG_OPTIMIZATION_LEVEL_NONE);
    }
    
    if (desc.dumpIntermediates) {
        char filename[64];
        pathFileName(filepath, filename, sizeof(filename));
        spSetDumpIntermediates(req, true);
        spSetDumpIntermediatePrefix(req, filename);
    }

    for (uint32 i = 0; i < desc.numIncludeDirs; i++)
        spAddSearchPath(req, desc.includeDirs[i].includeDir.CStr());
    
    for (uint32 i = 0; i < desc.numDefines; i++) 
        spAddPreprocessorDefine(req, desc.defines[i].define.CStr(), desc.defines[i].value.CStr());
        
    int translationUnitIdx = spAddTranslationUnit(req, SLANG_SOURCE_LANGUAGE_HLSL, "");
    spAddTranslationUnitSourceStringSpan(req, translationUnitIdx, filepath, 
        reinterpret_cast<const char*>(blob.Data()),
        reinterpret_cast<const char*>(blob.Data()) + blob.Size());
    
    int result = spCompile(req);
    
    const char* diag = spGetDiagnosticOutput(req);
    if (result != 0) {
        if (errorDiag)
            strCopy(errorDiag, errorDiagSize, diag);
        else 
            logError(diag);
        return {};
    }

    slang::ShaderReflection* refl = slang::ShaderReflection::get(req);
    ASSERT(refl);

    uint32 numVertexAttributes = 0;
    uint32 vertexInputParamIdx = UINT32_MAX;
    for (uint32 i = 0; i < refl->getEntryPointCount() && vertexInputParamIdx == UINT32_MAX; i++) {
        slang::EntryPointReflection* entryPoint = refl->getEntryPointByIndex(i);
        if (entryPoint->getStage() == SLANG_STAGE_VERTEX) {
            for (uint32 p = 0; p < entryPoint->getParameterCount(); p++) {
                slang::VariableLayoutReflection* param = entryPoint->getParameterByIndex(p);
                if (param->getCategory() == slang::VaryingInput) {
                    numVertexAttributes = param->getTypeLayout()->getFieldCount();
                    vertexInputParamIdx = p;
                    break;
                }
            }
        }
    }

    BuffersAllocPOD<Shader> podAlloc;
    podAlloc.AddMemberField<ShaderStageInfo>(offsetof(Shader, stages), refl->getEntryPointCount());
    podAlloc.AddMemberField<ShaderParameterInfo>(offsetof(Shader, params), refl->getParameterCount());
    if (numVertexAttributes)
        podAlloc.AddMemberField<ShaderVertexAttributeInfo>(offsetof(Shader, vertexAttributes), numVertexAttributes);
    Shader* shaderInfo = podAlloc.Calloc(alloc);
    
    pathFileName(filepath, shaderInfo->name, sizeof(shaderInfo->name));
    shaderInfo->alloc = alloc;
    shaderInfo->numStages = static_cast<uint32>(refl->getEntryPointCount());
    shaderInfo->numVertexAttributes = numVertexAttributes;

    for (uint32 i = 0; i < shaderInfo->numStages; i++) {
        slang::EntryPointReflection* entryPoint = refl->getEntryPointByIndex(i);
        ShaderStageInfo* stageInfo = &shaderInfo->stages[i];

        stageInfo->stage = shaderTranslateStage(entryPoint->getStage());
        strCopy(stageInfo->entryName, sizeof(stageInfo->entryName), entryPoint->getName()); 

        size_t dataSize;
        const void* data = spGetEntryPointCode(req, i, &dataSize);

        if (dataSize > 0) {
            stageInfo->blob.data = memAlloc(dataSize, alloc);
            stageInfo->blob.size = dataSize;
            memcpy(stageInfo->blob.data, data, dataSize);
        }

        // Vertex inputs
        if (stageInfo->stage == ShaderStage::Vertex && vertexInputParamIdx != UINT32_MAX) {
            ASSERT(shaderInfo->vertexAttributes[0].format == GfxFormat::Undefined);

            slang::VariableLayoutReflection* vertexInputParam = entryPoint->getParameterByIndex(vertexInputParamIdx);
            slang::TypeLayoutReflection* typeLayout = vertexInputParam->getTypeLayout();
            for (uint32 f = 0, fc = typeLayout->getFieldCount(); f < fc; f++) {
                ShaderVertexAttributeInfo* vertexAtt = &shaderInfo->vertexAttributes[f];

                slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(f);
                slang::TypeReflection::ScalarType scalarType = field->getType()->getScalarType();
                slang::TypeReflection::Kind   kind = field->getType()->getKind();
                uint32 numRows = field->getTypeLayout()->getRowCount();
                uint32 numCols = field->getTypeLayout()->getColumnCount();

                strCopy(vertexAtt->name, sizeof(vertexAtt->name), field->getName());
                strCopy(vertexAtt->semantic, sizeof(vertexAtt->semantic), field->getSemanticName());
                vertexAtt->semanticIdx = static_cast<uint32>(field->getSemanticIndex());
                vertexAtt->location = field->getBindingIndex();
                vertexAtt->format = shaderTranslateVertexInputFormat(numRows, numCols, scalarType, kind);
            } 
        }
    }
    
    shaderInfo->numParams = (uint32)refl->getParameterCount();
    for (uint32 i = 0; i < refl->getParameterCount(); i++) {
        ShaderParameterInfo* paramInfo = &shaderInfo->params[i];

        slang::VariableLayoutReflection* param = refl->getParameterByIndex(i);
        slang::TypeReflection* type = param->getType();

        slang::ParameterCategory cat = param->getCategory();
        if (cat == slang::PushConstantBuffer) {
            paramInfo->isPushConstant = true;
        }
        else {
            ASSERT(param->getCategory() == slang::DescriptorTableSlot);
        }
        
        strCopy(paramInfo->name, sizeof(paramInfo->name), param->getName());
        // TODO: get the stage that this parameter is being used (param->getStage() returns nothing)
        //paramInfo->stage = shaderTranslateStage(param->getStage());
        paramInfo->bindingIdx = param->getBindingIndex();

        switch (type->getKind()) {
        case slang::TypeReflection::Kind::ConstantBuffer:    paramInfo->type = ShaderParameterType::Uniformbuffer;  break;
        case slang::TypeReflection::Kind::SamplerState:      paramInfo->type = ShaderParameterType::Samplerstate;  break;
        case slang::TypeReflection::Kind::Resource:          paramInfo->type = ShaderParameterType::Resource; break;
        default:                                             ASSERT_MSG(false, "Shader parameter type is not supported");   break;
        }
    }

    return Pair<Shader*, uint32>(shaderInfo, uint32(podAlloc.GetSize()));
}

bool _private::shaderInitializeCompiler()
{
    gShaderCompiler.slang = spCreateSession();
    if (!gShaderCompiler.slang)
        return false;
    return true;
}

void _private::shaderReleaseCompiler()
{
    if (gShaderCompiler.slang)
        spDestroySession(gShaderCompiler.slang);
}

#endif
