#pragma once

#include "Blobs.h"

typedef struct ini_t ini_t;

struct INIFileProperty
{
    void SetName(const char* name);
    void SetValue(const char* value);

    const char* GetName();
    const char* GetValue();

    void Delete();

    inline bool IsValid() const    { return id != -1; }

    ini_t* ini = nullptr;
    int sectionId = -1;
    int id = -1;
};

struct INIFileSection
{
    uint32 GetPropertyCount();
    INIFileProperty GetProperty(uint32 index);
    const char* GetPropertyName(uint32 index);

    INIFileProperty NewProperty(const char* name, const char* value);
    INIFileProperty FindProperty(const char* name);

    void SetName(const char* name);
    const char* GetName();

    void Delete();

    inline bool IsValid() const    { return id != -1; }

    ini_t* ini = nullptr;
    int id = -1;
};

struct INIFileContext
{
    uint32 GetSectionCount() const;
    INIFileSection GetSection(uint32 index) const;
    const char* GetSectionName(uint32 index) const;

    INIFileSection GetRootSection() const;

    INIFileSection NewSection(const char* name) const;
    INIFileSection FindSection(const char* name) const;

    inline bool IsValid() const    { return ini != nullptr; }

    void Destroy();

    ini_t* ini = nullptr;
};

namespace INIFile
{
    API INIFileContext Create(MemAllocator* alloc = Mem::GetDefaultAlloc());
    API INIFileContext Load(const char* filepath, MemAllocator* alloc = Mem::GetDefaultAlloc());
    API INIFileContext LoadFromString(const char* data, MemAllocator* alloc = Mem::GetDefaultAlloc());
    API bool Save(const INIFileContext& ini, const char* filepath);
    API Blob SaveToMem(const INIFileContext& ini, MemAllocator* alloc);
}

