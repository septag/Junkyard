#include "JsonParser.h"

#define CJ5_IMPLEMENT
#define CJ5_ASSERT(e) ASSERT(e)
#define CJ5_SKIP_ASAN NO_ASAN
#include "External/cj5/cj5.h"
#undef CJ5_IMPLEMENT

#include "StringUtil.h"
#include "Allocators.h"
#include "Arrays.h"

struct JsonContext
{
    cj5_result r;       // This should always come first, because the wrapper API casts JsonContext to cj5_result*
    uint32 numTokens;
    Allocator* alloc;
    cj5_token* tokens;
};

JsonContext* jsonParse(const char* json5, uint32 json5Len, JsonErrorLocation* outErrLoc, Allocator* alloc)
{
    ASSERT(json5);
    ASSERT(json5Len < INT32_MAX);
    ASSERT(alloc);

    bool mainAllocIsTemp = alloc->GetType() == AllocatorType::Temp;
    MemTempId tempMemId = mainAllocIsTemp ? ((MemTempAllocator*)alloc)->GetId() : memTempPushId();
    MemTempAllocator tmpAlloc(tempMemId);
    Array<cj5_token> tokens(&tmpAlloc);
    tokens.Reserve(64);

    auto CreateToken = [](void* user)->cj5_token* { return ((Array<cj5_token>*)user)->Push(); };
    auto GetAll = [](void* user)->cj5_token* { return ((Array<cj5_token>*)user)->Ptr(); };
    cj5_factory factory {
        .create_token = CreateToken,
        .get_all = GetAll,
        .user_data = &tokens
    };

    json5Len = json5Len == 0 ? json5Len : strLen(json5);
    cj5_result r = cj5_parse_with_factory(json5, (int)json5Len, factory);

    if (r.error == CJ5_ERROR_NONE) {
        ASSERT(tokens.Count());

        // TODO: the API and usage is a bit inconvenient. look for better solutions for cj5 API
        MemSingleShotMalloc<JsonContext> mallocator;
        mallocator.AddMemberField<cj5_token>(offsetof(JsonContext, tokens), tokens.Count());
        JsonContext* ctx = mallocator.Calloc(alloc);

        ctx->numTokens = tokens.Count();
        memcpy(ctx->tokens, r.tokens, tokens.Count());
        ctx->r.tokens = ctx->tokens;

        ctx->r = r;
        ctx->alloc = alloc;

        if (!mainAllocIsTemp)
            memTempPopId(tempMemId);
        return ctx;
    }
    else {
        if (outErrLoc) {
            *outErrLoc = JsonErrorLocation {
                .line = (uint32)r.error_line,
                .col = (uint32)r.error_col
            };
        }
        if (!mainAllocIsTemp)
            memTempPopId(tempMemId);
        return nullptr;
    }
}

void jsonDestroy(JsonContext* ctx)
{
    if (ctx && ctx->alloc) 
        MemSingleShotMalloc<JsonContext>::Free(ctx, ctx->alloc);
}

uint32 JsonNode::GetChildCount() const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(mCtx);

    const cj5_token* tok = &r->tokens[mTokenId];
    if (tok->parent_id != -1) {
        const cj5_token* parent = &r->tokens[tok->parent_id];
        if (parent->type == CJ5_TOKEN_STRING) {     // this would be the key
            return r->tokens[parent->parent_id].type == CJ5_TOKEN_OBJECT ? r->tokens[parent->parent_id].size : 0;
        }
        else {
            return 0;
        }
    }
    else {
        return tok->type == CJ5_TOKEN_OBJECT ? tok->size : 0;
    }
}

uint32 JsonNode::GetArrayCount() const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(mCtx);
    const cj5_token* tok = &r->tokens[mTokenId];
    ASSERT(tok->type == CJ5_TOKEN_ARRAY);
    return tok->size;
}

bool JsonNode::IsArray() const
{
    return mCtx->r.tokens[mTokenId].type == CJ5_TOKEN_ARRAY;
}

bool JsonNode::IsObject() const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(mCtx);
    
    const cj5_token* tok = &r->tokens[mTokenId];
    if (tok->type == CJ5_TOKEN_OBJECT)
        return true;

    if (tok->parent_id != -1)
        return r->tokens[tok->parent_id].type == CJ5_TOKEN_OBJECT;

    return false;
}

const char* JsonNode::GetKey(char* outKey, uint32 keySize) const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(mCtx);
    ASSERT(mTokenId > 0 && mTokenId < r->num_tokens);
    ASSERT(r->tokens[mTokenId].parent_id != -1);
    const cj5_token* tok = &r->tokens[r->tokens[mTokenId].parent_id];   // get the 'key' token (parent)
    ASSERT(tok->type == CJ5_TOKEN_STRING);
    return cj5__strcpy(outKey, keySize, &r->json5[tok->start], tok->end - tok->start);
}

const char* JsonNode::GetValue(char* outValue, uint32 valueSize) const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(mCtx);
    CJ5_ASSERT(mTokenId >= 0 && mTokenId < r->num_tokens);
    const cj5_token* tok = &r->tokens[mTokenId];
    return cj5__strcpy(outValue, valueSize, &r->json5[tok->start], tok->end - tok->start);

}

JsonNode JsonNode::GetChildItem(uint32 _index) const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(mCtx);
    const cj5_token* tok = &r->tokens[mTokenId];
    int index = (int)_index;

    ASSERT(tok->type == CJ5_TOKEN_OBJECT);
    ASSERT(index < tok->size);
    
    for (int i = mTokenId + 1, count = 0, ic = r->num_tokens; i < ic && count < tok->size; i+=2) {
        ASSERT(r->tokens[i].type == CJ5_TOKEN_STRING);
        if (r->tokens[i].parent_id == mTokenId) {
            if (count == index)
                return JsonNode(mCtx, i + 1, index);       // get next 'value' token
            count++;
        }
    }
    return JsonNode(mCtx, -1);
}

JsonNode JsonNode::GetNextChildItem(const JsonNode& curChildItem) const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(mCtx);
    const cj5_token* tok = &r->tokens[mTokenId];

    ASSERT(curChildItem.mItemIndex < tok->size);

    int nextIndex = curChildItem.mItemIndex + 1;
    if (nextIndex == tok->size) 
        return JsonNode(mCtx, -1);

    for (int i = curChildItem.mTokenId + 1, ic = r->num_tokens; i < ic; i+=2) {
        if (r->tokens[i].parent_id == mTokenId)
            return JsonNode(mCtx, i + 1, nextIndex);
    }

    return JsonNode(mCtx, -1);
}

JsonNode JsonNode::GetArrayItem(uint32 _index) const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(mCtx);
    const cj5_token* tok = &r->tokens[mTokenId];
    int index = (int)_index;
    ASSERT(tok->type == CJ5_TOKEN_ARRAY);
    ASSERT(index < tok->size);
    for (int i = mTokenId + 1, count = 0, ic = r->num_tokens; i < ic && count < tok->size; i++) {
        if (r->tokens[i].parent_id == mTokenId) {
            if (count == index)
                return JsonNode(mCtx, i, _index);
            count++;
        }
    }
    return JsonNode(mCtx, -1);
}

JsonNode JsonNode::GetNextArrayItem(const JsonNode& curItem) const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(mCtx);
    const cj5_token* tok = &r->tokens[mTokenId];
    int index = curItem.mItemIndex + 1;
    ASSERT(tok->type == CJ5_TOKEN_ARRAY);

    if (index == tok->size) 
        return JsonNode(mCtx, -1);

    int startId = curItem.mTokenId <= 0 ? (mTokenId + 1) : (curItem.mTokenId + 1);
    for (int i = startId, ic = r->num_tokens; i < ic; i++) {
        if (r->tokens[i].parent_id == mTokenId)
            return JsonNode(mCtx, i, index);
    }
    return JsonNode(mCtx, -1);
}
