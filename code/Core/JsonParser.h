#pragma once

#include "Base.h"
#include "MathTypes.h"

struct JsonContext;

struct JsonErrorLocation
{
    uint32 line;
    uint32 col;
};

API JsonContext* jsonParse(const char* json5, uint32 json5Len, JsonErrorLocation* outErrLoc, Allocator* alloc = memDefaultAlloc());
API void jsonDestroy(JsonContext* ctx);

struct JsonNode
{
    JsonNode() = default;
    explicit JsonNode(JsonContext* _ctx);
    explicit JsonNode(JsonContext* _ctx, int _tokenId, int _itemIndex = 0);

    bool HasChild(const char* _childNode) const;    

    JsonNode GetChild(const char* _childNode) const;
    JsonNode GetChildItem(uint32 _index = 0) const;
    JsonNode GetNextChildItem(const JsonNode& curChildItem) const;
    uint32 GetChildCount() const;

    JsonNode GetArrayItem(uint32 _index = 0) const;
    JsonNode GetNextArrayItem(const JsonNode& curItem) const;
    uint32 GetArrayCount() const;

    bool IsValid() const;
    bool IsObject() const;
    bool IsArray() const;

    const char* GetKey(char* outKey, uint32 keySize) const;
    const char* GetValue(char* outValue, uint32 valueSize) const;

    template <typename _T> _T GetValue() const;
    template <typename _T> uint32 GetArrayValues(_T* _values, uint32 _maxValues) const;

    template <typename _T> _T GetChildValue(const char* _childNode, _T _defaultValue);
    template <typename _T> uint32 GetChildArrayValues(const char* _childNode, _T* _values, uint32 _maxValues);

private:
    JsonContext* mCtx = nullptr;
    int mTokenId = -1;
    int mItemIndex = 0;
};

//----------------------------------------------------------------------------------------------------------------------
// fwd decls from cj5.h
typedef struct cj5_result cj5_result;
extern "C" {
    int cj5_seek(cj5_result* r, int parent_id, const char* key);
    int cj5_seek_hash(cj5_result* r, int parent_id, const uint32_t key_hash);
    int cj5_seek_recursive(cj5_result* r, int parent_id, const char* key);
    const char* cj5_get_string(cj5_result* r, int id, char* str, int max_str);
    double cj5_get_double(cj5_result* r, int id);
    float cj5_get_float(cj5_result* r, int id);
    int cj5_get_int(cj5_result* r, int id);
    uint32_t cj5_get_uint(cj5_result* r, int id);
    uint64_t cj5_get_uint64(cj5_result* r, int id);
    int64_t cj5_get_int64(cj5_result* r, int id);
    bool cj5_get_bool(cj5_result* r, int id);
    double cj5_seekget_double(cj5_result* r, int parent_id, const char* key, double def_val);
    float cj5_seekget_float(cj5_result* r, int parent_id, const char* key, float def_val);
    int cj5_seekget_array_int16(cj5_result* r, int parent_id, const char* key, int16_t* values, int max_values);
    int cj5_seekget_array_uint16(cj5_result* r, int parent_id, const char* key, uint16_t* values, int max_values);
    int cj5_seekget_int(cj5_result* r, int parent_id, const char* key, int def_val);
    uint32_t cj5_seekget_uint(cj5_result* r, int parent_id, const char* key, uint32_t def_val);
    uint64_t cj5_seekget_uint64(cj5_result* r, int parent_id, const char* key, uint64_t def_val);
    int64_t cj5_seekget_int64(cj5_result* r, int parent_id, const char* key, int64_t def_val);
    bool cj5_seekget_bool(cj5_result* r, int parent_id, const char* key, bool def_val);
    const char* cj5_seekget_string(cj5_result* r, int parent_id, const char* key, char* str, int max_str, const char* def_val);
    int cj5_seek(cj5_result* r, int parent_id, const char* key);
    int cj5_seekget_array_double(cj5_result* r, int parent_id, const char* key, double* values, int max_values);
    int cj5_seekget_array_float(cj5_result* r, int parent_id, const char* key, float* values, int max_values);
    int cj5_seekget_array_int(cj5_result* r, int parent_id, const char* key, int* values, int max_values);
    int cj5_seekget_array_uint(cj5_result* r, int parent_id, const char* key, uint32_t* values, int max_values);
    int cj5_seekget_array_uint64(cj5_result* r, int parent_id, const char* key, uint64_t* values, int max_values);
    int cj5_seekget_array_int64(cj5_result* r, int parent_id, const char* key, int64_t* values, int max_values);
    int cj5_seekget_array_bool(cj5_result* r, int parent_id, const char* key, bool* values, int max_values);
    int cj5_seekget_array_string(cj5_result* r, int parent_id, const char* key, char** strs, int max_str, int max_values);
    int cj5_get_array_elem(cj5_result* r, int id, int index);
    int cj5_get_array_elem_incremental(cj5_result* r, int id, int index, int prev_elem);
}

inline JsonNode::JsonNode(JsonContext* _ctx) : mCtx(_ctx), mTokenId(0), mItemIndex(0) 
{
    ASSERT(_ctx);
}

inline JsonNode::JsonNode(JsonContext* _ctx, int _tokenId, int _itemIndex) : mCtx(_ctx), mTokenId(_tokenId), mItemIndex(_itemIndex) 
{
    ASSERT(_ctx);
}

inline bool JsonNode::HasChild(const char* _childNode) const
{
    return cj5_seek(reinterpret_cast<cj5_result*>(mCtx), mTokenId, _childNode) != -1;
}

inline JsonNode JsonNode::GetChild(const char* _childNode) const
{
    int id = cj5_seek(reinterpret_cast<cj5_result*>(mCtx), mTokenId, _childNode);
    return JsonNode(mCtx, id);
}

inline bool JsonNode::IsValid() const
{
    return mCtx && mTokenId > -1;
}

template <> inline uint32 JsonNode::GetArrayValues(uint32* _values, uint32 _maxValues) const
{
    return (uint32)cj5_seekget_array_uint(reinterpret_cast<cj5_result*>(mCtx), mTokenId, nullptr, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetArrayValues(float* _values, uint32 _maxValues) const
{
    return (uint32)cj5_seekget_array_float(reinterpret_cast<cj5_result*>(mCtx), mTokenId, nullptr, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetArrayValues(double* _values, uint32 _maxValues) const
{
    return (uint32)cj5_seekget_array_double(reinterpret_cast<cj5_result*>(mCtx), mTokenId, nullptr, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetArrayValues(uint64* _values, uint32 _maxValues) const
{
    return (uint32)cj5_seekget_array_uint64(reinterpret_cast<cj5_result*>(mCtx), mTokenId, nullptr, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetArrayValues(int* _values, uint32 _maxValues) const
{
    return (uint32)cj5_seekget_array_int(reinterpret_cast<cj5_result*>(mCtx), mTokenId, nullptr, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetArrayValues(bool* _values, uint32 _maxValues) const
{
    return (uint32)cj5_seekget_array_bool(reinterpret_cast<cj5_result*>(mCtx), mTokenId, nullptr, _values, _maxValues);
}

template <> inline bool JsonNode::GetValue() const
{
    return cj5_get_bool(reinterpret_cast<cj5_result*>(mCtx), mTokenId);
}

template <> inline int JsonNode::GetValue() const
{
    return cj5_get_int(reinterpret_cast<cj5_result*>(mCtx), mTokenId);
}

template <> inline float JsonNode::GetValue() const
{
    return cj5_get_float(reinterpret_cast<cj5_result*>(mCtx), mTokenId);
}

template <> inline double JsonNode::GetValue() const
{
    return cj5_get_double(reinterpret_cast<cj5_result*>(mCtx), mTokenId);
}

template <> inline uint32 JsonNode::GetValue() const
{
    return cj5_get_uint(reinterpret_cast<cj5_result*>(mCtx), mTokenId);
}

template <> inline uint64 JsonNode::GetValue() const
{
    return cj5_get_uint64(reinterpret_cast<cj5_result*>(mCtx), mTokenId);
}

template <> inline Float4 JsonNode::GetValue() const
{
    Float4 v;
    [[maybe_unused]] uint32 n = GetArrayValues<float>(v.f, 4);
    ASSERT(n == 4);
    return v;
}

template <> inline Float2 JsonNode::GetValue() const
{
    Float2 v;
    [[maybe_unused]] uint32 n = GetArrayValues<float>(v.f, 2);
    ASSERT(n == 2);
    return v;
}

template <> inline Float3 JsonNode::GetValue() const
{
    Float3 v;
    [[maybe_unused]] uint32 n = GetArrayValues<float>(v.f, 3);
    ASSERT(n == 3);
    return v;
}

template <> inline Int2 JsonNode::GetValue() const
{
    Int2 v;
    [[maybe_unused]] uint32 n = GetArrayValues<int>(v.n, 2);
    ASSERT(n == 2);
    return v;
}

template <> inline uint32 JsonNode::GetChildValue(const char* _childNode, uint32 _defaultValue)
{
    return cj5_seekget_uint(reinterpret_cast<cj5_result*>(mCtx), mTokenId, _childNode, _defaultValue);
}

template <> inline uint64 JsonNode::GetChildValue(const char* _childNode, uint64 _defaultValue)
{
    return cj5_seekget_uint64(reinterpret_cast<cj5_result*>(mCtx), mTokenId, _childNode, _defaultValue);
}

template <> inline int JsonNode::GetChildValue(const char* _childNode, int _defaultValue)
{
    return cj5_seekget_int(reinterpret_cast<cj5_result*>(mCtx), mTokenId, _childNode, _defaultValue);
}

template <> inline float JsonNode::GetChildValue(const char* _childNode, float _defaultValue)
{
    return cj5_seekget_float(reinterpret_cast<cj5_result*>(mCtx), mTokenId, _childNode, _defaultValue);
}

template <> inline double JsonNode::GetChildValue(const char* _childNode, double _defaultValue)
{
    return cj5_seekget_double(reinterpret_cast<cj5_result*>(mCtx), mTokenId, _childNode, _defaultValue);
}

template <> inline bool JsonNode::GetChildValue(const char* _childNode, bool _defaultValue)
{
    return cj5_seekget_bool(reinterpret_cast<cj5_result*>(mCtx), mTokenId, _childNode, _defaultValue);
}

template <> inline Float4 JsonNode::GetChildValue(const char* _childNode, Float4 _defaultValue)
{
    JsonNode jArr = GetChild(_childNode);
    return jArr.IsValid() && jArr.IsArray() ? jArr.GetValue<Float4>() : _defaultValue;
}

template <> inline Float3 JsonNode::GetChildValue(const char* _childNode, Float3 _defaultValue)
{
    JsonNode jArr = GetChild(_childNode);
    return jArr.IsValid() && jArr.IsArray() ? jArr.GetValue<Float3>() : _defaultValue;
}

template <> inline Float2 JsonNode::GetChildValue(const char* _childNode, Float2 _defaultValue)
{
    JsonNode jArr = GetChild(_childNode);
    return jArr.IsValid() && jArr.IsArray() ? jArr.GetValue<Float2>() : _defaultValue;
}

template <> inline Int2 JsonNode::GetChildValue(const char* _childNode, Int2 _defaultValue)
{
    JsonNode jArr = GetChild(_childNode);
    return jArr.IsValid() && jArr.IsArray() ? jArr.GetValue<Int2>() : _defaultValue;
}

template <> inline uint32 JsonNode::GetChildArrayValues(const char* _childNode, uint32* _values, uint32 _maxValues)
{
    return (uint32)cj5_seekget_array_uint(reinterpret_cast<cj5_result*>(mCtx), mTokenId, _childNode, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetChildArrayValues(const char* _childNode, int* _values, uint32 _maxValues)
{
    return (uint32)cj5_seekget_array_int(reinterpret_cast<cj5_result*>(mCtx), mTokenId, _childNode, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetChildArrayValues(const char* _childNode, uint64* _values, uint32 _maxValues)
{
    return (uint32)cj5_seekget_array_uint64(reinterpret_cast<cj5_result*>(mCtx), mTokenId, _childNode, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetChildArrayValues(const char* _childNode, bool* _values, uint32 _maxValues)
{
    return (uint32)cj5_seekget_array_bool(reinterpret_cast<cj5_result*>(mCtx), mTokenId, _childNode, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetChildArrayValues(const char* _childNode, double* _values, uint32 _maxValues)
{
    return (uint32)cj5_seekget_array_double(reinterpret_cast<cj5_result*>(mCtx), mTokenId, _childNode, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetChildArrayValues(const char* _childNode, float* _values, uint32 _maxValues)
{
    return (uint32)cj5_seekget_array_float(reinterpret_cast<cj5_result*>(mCtx), mTokenId, _childNode, _values, _maxValues);
}



