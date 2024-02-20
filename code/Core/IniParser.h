#pragma once

#include "Blobs.h"

typedef struct ini_t ini_t;

struct IniProperty
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

struct IniSection
{
    uint32 GetPropertyCount();
    IniProperty GetProperty(uint32 index);
    const char* GetPropertyName(uint32 index);

    IniProperty NewProperty(const char* name, const char* value);
    IniProperty FindProperty(const char* name);

    void SetName(const char* name);
    const char* GetName();

    void Delete();

    inline bool IsValid() const    { return id != -1; }

    ini_t* ini = nullptr;
    int id = -1;
};

struct IniContext
{
    uint32 GetSectionCount() const;
    IniSection GetSection(uint32 index) const;
    const char* GetSectionName(uint32 index) const;

    IniSection GetRootSection() const;

    IniSection NewSection(const char* name) const;
    IniSection FindSection(const char* name) const;

    inline bool IsValid() const    { return ini != nullptr; }

    void Destroy();

    ini_t* ini = nullptr;
};

API IniContext iniCreateContext(Allocator* alloc = memDefaultAlloc());
API IniContext iniLoad(const char* filepath, Allocator* alloc = memDefaultAlloc());
API IniContext iniLoadFromString(const char* data, Allocator* alloc = memDefaultAlloc());
API bool iniSave(const IniContext& ini, const char* filepath);
API Blob iniSaveToMem(const IniContext& ini, Allocator* alloc);
