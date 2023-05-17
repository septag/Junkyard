#pragma once

#include "External/cj5/cj5.h"

#include "Base.h"
#include "Memory.h"

#include "MathTypes.h"

struct JsonContext;

struct JsonErrorLocation
{
    uint32 line;
    uint32 col;
};

struct JsonContext
{
    cj5_result r;
    Allocator* alloc;
};

// Returns number of tokens needed for 'jsonParse'
// Returns 0 if there was an error parsing the file
API uint32 jsonGetTokenCount(const char* json5, uint32 json5Len);

API bool jsonParse(JsonContext* ctx, const char* json5, uint32 json5Len, Allocator* alloc = memDefaultAlloc());
API bool jsonParse(JsonContext* ctx, const char* json5, uint32 json5Len, cj5_token* tokens, uint32 maxTokens);
API void jsonDestroy(JsonContext* ctx);

API JsonErrorLocation jsonParseGetErrorLocation(JsonContext* ctx);

struct JsonNode
{
    JsonNode() = default;
    explicit JsonNode(JsonContext& _ctx);
    explicit JsonNode(JsonContext& _ctx, int _tokenId, int _itemIndex = 0);

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
    JsonContext* ctx = nullptr;
    int tokenId = -1;
    int itemIndex = 0;
};

//------------------------------------------------------------------------
inline JsonNode::JsonNode(JsonContext& _ctx) : ctx(&_ctx), tokenId(0), itemIndex(0) {}
inline JsonNode::JsonNode(JsonContext& _ctx, int _tokenId, int _itemIndex) : ctx(&_ctx), tokenId(_tokenId), itemIndex(_itemIndex) {}

inline bool JsonNode::HasChild(const char* _childNode) const
{
    return cj5_seek(reinterpret_cast<cj5_result*>(ctx), tokenId, _childNode) != -1;
}

inline JsonNode JsonNode::GetChild(const char* _childNode) const
{
    int id = cj5_seek(reinterpret_cast<cj5_result*>(ctx), tokenId, _childNode);
    return JsonNode(*ctx, id);
}

inline bool JsonNode::IsValid() const
{
    return ctx && tokenId > -1;
}

template <> inline uint32 JsonNode::GetArrayValues(uint32* _values, uint32 _maxValues) const
{
    return (uint32)cj5_seekget_array_uint(reinterpret_cast<cj5_result*>(ctx), tokenId, nullptr, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetArrayValues(float* _values, uint32 _maxValues) const
{
    return (uint32)cj5_seekget_array_float(reinterpret_cast<cj5_result*>(ctx), tokenId, nullptr, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetArrayValues(double* _values, uint32 _maxValues) const
{
    return (uint32)cj5_seekget_array_double(reinterpret_cast<cj5_result*>(ctx), tokenId, nullptr, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetArrayValues(uint64* _values, uint32 _maxValues) const
{
    return (uint32)cj5_seekget_array_uint64(reinterpret_cast<cj5_result*>(ctx), tokenId, nullptr, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetArrayValues(int* _values, uint32 _maxValues) const
{
    return (uint32)cj5_seekget_array_int(reinterpret_cast<cj5_result*>(ctx), tokenId, nullptr, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetArrayValues(bool* _values, uint32 _maxValues) const
{
    return (uint32)cj5_seekget_array_bool(reinterpret_cast<cj5_result*>(ctx), tokenId, nullptr, _values, _maxValues);
}

template <> inline bool JsonNode::GetValue() const
{
    return cj5_get_bool(reinterpret_cast<cj5_result*>(ctx), tokenId);
}

template <> inline int JsonNode::GetValue() const
{
    return cj5_get_int(reinterpret_cast<cj5_result*>(ctx), tokenId);
}

template <> inline float JsonNode::GetValue() const
{
    return cj5_get_float(reinterpret_cast<cj5_result*>(ctx), tokenId);
}

template <> inline double JsonNode::GetValue() const
{
    return cj5_get_double(reinterpret_cast<cj5_result*>(ctx), tokenId);
}

template <> inline uint32 JsonNode::GetValue() const
{
    return cj5_get_uint(reinterpret_cast<cj5_result*>(ctx), tokenId);
}

template <> inline uint64 JsonNode::GetValue() const
{
    return cj5_get_uint64(reinterpret_cast<cj5_result*>(ctx), tokenId);
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
    return cj5_seekget_uint(reinterpret_cast<cj5_result*>(ctx), tokenId, _childNode, _defaultValue);
}

template <> inline uint64 JsonNode::GetChildValue(const char* _childNode, uint64 _defaultValue)
{
    return cj5_seekget_uint64(reinterpret_cast<cj5_result*>(ctx), tokenId, _childNode, _defaultValue);
}

template <> inline int JsonNode::GetChildValue(const char* _childNode, int _defaultValue)
{
    return cj5_seekget_int(reinterpret_cast<cj5_result*>(ctx), tokenId, _childNode, _defaultValue);
}

template <> inline float JsonNode::GetChildValue(const char* _childNode, float _defaultValue)
{
    return cj5_seekget_float(reinterpret_cast<cj5_result*>(ctx), tokenId, _childNode, _defaultValue);
}

template <> inline double JsonNode::GetChildValue(const char* _childNode, double _defaultValue)
{
    return cj5_seekget_double(reinterpret_cast<cj5_result*>(ctx), tokenId, _childNode, _defaultValue);
}

template <> inline bool JsonNode::GetChildValue(const char* _childNode, bool _defaultValue)
{
    return cj5_seekget_bool(reinterpret_cast<cj5_result*>(ctx), tokenId, _childNode, _defaultValue);
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
    return (uint32)cj5_seekget_array_uint(reinterpret_cast<cj5_result*>(ctx), tokenId, _childNode, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetChildArrayValues(const char* _childNode, int* _values, uint32 _maxValues)
{
    return (uint32)cj5_seekget_array_int(reinterpret_cast<cj5_result*>(ctx), tokenId, _childNode, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetChildArrayValues(const char* _childNode, uint64* _values, uint32 _maxValues)
{
    return (uint32)cj5_seekget_array_uint64(reinterpret_cast<cj5_result*>(ctx), tokenId, _childNode, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetChildArrayValues(const char* _childNode, bool* _values, uint32 _maxValues)
{
    return (uint32)cj5_seekget_array_bool(reinterpret_cast<cj5_result*>(ctx), tokenId, _childNode, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetChildArrayValues(const char* _childNode, double* _values, uint32 _maxValues)
{
    return (uint32)cj5_seekget_array_double(reinterpret_cast<cj5_result*>(ctx), tokenId, _childNode, _values, _maxValues);
}

template <> inline uint32 JsonNode::GetChildArrayValues(const char* _childNode, float* _values, uint32 _maxValues)
{
    return (uint32)cj5_seekget_array_float(reinterpret_cast<cj5_result*>(ctx), tokenId, _childNode, _values, _maxValues);
}



