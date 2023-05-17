#include "Base.h"
#define CJ5_IMPLEMENT
#define CJ5_ASSERT(e) ASSERT(e)
#define CJ5_SKIP_ASAN NO_ASAN
#include "External/cj5/cj5.h"
#undef CJ5_IMPLEMENT

// Include jsonParser after, because we are already including cj5.h in the header
#include "JsonParser.h"

#include "Buffers.h"
#include "String.h"

uint32 jsonGetTokenCount(const char* json5, uint32 json5Len)
{
    json5Len = json5Len == 0 ? json5Len : strLen(json5);

    cj5_token tokens[1];
    cj5_result r = cj5_parse(json5, (int)json5Len, tokens, (int)CountOf(tokens));
    return (r.error == CJ5_ERROR_OVERFLOW) ? static_cast<uint32>(r.num_tokens) : 0u;
}

bool jsonParse(JsonContext* ctx, const char* json5, uint32 json5Len, Allocator* alloc)
{
    ASSERT(json5Len < INT32_MAX);
    ASSERT(alloc);

    *ctx = JsonContext {};

    Array<cj5_token> tokens(alloc);
    tokens.Reserve(64);

    auto CreateToken = [](void* user)->cj5_token* { return ((Array<cj5_token>*)user)->Push(); };
    auto GetAll = [](void* user)->cj5_token* { return ((Array<cj5_token>*)user)->Ptr(); };
    cj5_factory factory {
        .create_token = CreateToken,
        .get_all = GetAll,
        .user_data = &tokens
    };

    json5Len = json5Len == 0 ? json5Len : strLen(json5);
    ctx->r = cj5_parse_with_factory(json5, (int)json5Len, factory);

    if (ctx->r.error == CJ5_ERROR_NONE) {
        ctx->alloc = alloc;
        return true;
    }
    else {
        tokens.Free();
        return false;
    }
}

bool jsonParse(JsonContext* ctx, const char* json5, uint32 json5Len, cj5_token* tokens, uint32 maxTokens)
{
    ASSERT(json5Len < INT32_MAX);
    ASSERT(maxTokens && maxTokens < INT32_MAX);

    *ctx = JsonContext {};

    json5Len = json5Len == 0 ? json5Len : strLen(json5);
    ctx->r = cj5_parse(json5, (int)json5Len, tokens, (int)maxTokens);

    return ctx->r.error != CJ5_ERROR_NONE;
}

void jsonDestroy(JsonContext* ctx)
{
    ASSERT(ctx);
    if (ctx->alloc)
        memFree(const_cast<cj5_token*>(ctx->r.tokens), ctx->alloc);
    memset(ctx, 0x0, sizeof(JsonContext));
}

JsonErrorLocation jsonParseGetErrorLocation(JsonContext* ctx)
{
    return JsonErrorLocation {
        .line = (uint32)ctx->r.error_line,
        .col = (uint32)ctx->r.error_col
    };
}

uint32 JsonNode::GetChildCount() const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(ctx);

    const cj5_token* tok = &r->tokens[tokenId];
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
    cj5_result* r = reinterpret_cast<cj5_result*>(ctx);
    const cj5_token* tok = &r->tokens[tokenId];
    ASSERT(tok->type == CJ5_TOKEN_ARRAY);
    return tok->size;
}

bool JsonNode::IsArray() const
{
    return ctx->r.tokens[tokenId].type == CJ5_TOKEN_ARRAY;
}

bool JsonNode::IsObject() const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(ctx);
    
    const cj5_token* tok = &r->tokens[tokenId];
    if (tok->type == CJ5_TOKEN_OBJECT)
        return true;

    if (tok->parent_id != -1)
        return r->tokens[tok->parent_id].type == CJ5_TOKEN_OBJECT;

    return false;
}

const char* JsonNode::GetKey(char* outKey, uint32 keySize) const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(ctx);
    ASSERT(tokenId > 0 && tokenId < r->num_tokens);
    ASSERT(r->tokens[tokenId].parent_id != -1);
    const cj5_token* tok = &r->tokens[r->tokens[tokenId].parent_id];   // get the 'key' token (parent)
    ASSERT(tok->type == CJ5_TOKEN_STRING);
    return cj5__strcpy(outKey, keySize, &r->json5[tok->start], tok->end - tok->start);
}

const char* JsonNode::GetValue(char* outValue, uint32 valueSize) const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(ctx);
    CJ5_ASSERT(tokenId >= 0 && tokenId < r->num_tokens);
    const cj5_token* tok = &r->tokens[tokenId];
    return cj5__strcpy(outValue, valueSize, &r->json5[tok->start], tok->end - tok->start);

}

JsonNode JsonNode::GetChildItem(uint32 _index) const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(ctx);
    const cj5_token* tok = &r->tokens[tokenId];
    int index = (int)_index;

    ASSERT(tok->type == CJ5_TOKEN_OBJECT);
    ASSERT(index < tok->size);
    
    for (int i = tokenId + 1, count = 0, ic = r->num_tokens; i < ic && count < tok->size; i+=2) {
        ASSERT(r->tokens[i].type == CJ5_TOKEN_STRING);
        if (r->tokens[i].parent_id == tokenId) {
            if (count == index)
                return JsonNode(*ctx, i + 1, index);       // get next 'value' token
            count++;
        }
    }
    return JsonNode(*ctx, -1);
}

JsonNode JsonNode::GetNextChildItem(const JsonNode& curChildItem) const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(ctx);
    const cj5_token* tok = &r->tokens[tokenId];

    ASSERT(curChildItem.itemIndex < tok->size);

    int nextIndex = curChildItem.itemIndex + 1;
    if (nextIndex == tok->size) 
        return JsonNode(*ctx, -1);

    for (int i = curChildItem.tokenId + 1, ic = r->num_tokens; i < ic; i+=2) {
        if (r->tokens[i].parent_id == tokenId)
            return JsonNode(*ctx, i + 1, nextIndex);
    }

    return JsonNode(*ctx, -1);
}

JsonNode JsonNode::GetArrayItem(uint32 _index) const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(ctx);
    const cj5_token* tok = &r->tokens[tokenId];
    int index = (int)_index;
    ASSERT(tok->type == CJ5_TOKEN_ARRAY);
    ASSERT(index < tok->size);
    for (int i = tokenId + 1, count = 0, ic = r->num_tokens; i < ic && count < tok->size; i++) {
        if (r->tokens[i].parent_id == tokenId) {
            if (count == index)
                return JsonNode(*ctx, i, _index);
            count++;
        }
    }
    return JsonNode(*ctx, -1);
}

JsonNode JsonNode::GetNextArrayItem(const JsonNode& curItem) const
{
    cj5_result* r = reinterpret_cast<cj5_result*>(ctx);
    const cj5_token* tok = &r->tokens[tokenId];
    int index = curItem.itemIndex + 1;
    ASSERT(tok->type == CJ5_TOKEN_ARRAY);

    if (index == tok->size) 
        return JsonNode(*ctx, -1);

    int startId = curItem.tokenId <= 0 ? (tokenId + 1) : (curItem.tokenId + 1);
    for (int i = startId, ic = r->num_tokens; i < ic; i++) {
        if (r->tokens[i].parent_id == tokenId)
            return JsonNode(*ctx, i, index);
    }
    return JsonNode(*ctx, -1);
}
