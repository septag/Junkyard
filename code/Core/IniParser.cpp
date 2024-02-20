#include "IniParser.h"

#include "StringUtil.h"
#include "System.h"
#include "Allocators.h"

#define INI_IMPLEMENTATION
#define INI_MALLOC(ctx, size)       memAlloc(size, (Allocator*)ctx)
#define INI_FREE(ctx, ptr)          memFree(ptr, (Allocator*)ctx)
#define INI_MEMCPY(dst, src, cnt)   memcpy(dst, src, cnt)
#define INI_STRLEN(s)               strLen(s)
#define INI_STRNICMP(s1, s2, cnt)   (strIsEqualNoCaseCount(s1, s2, cnt) ? 0 : 1)

PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wsign-compare")
#include "External/mgustavsson/ini.h"
PRAGMA_DIAGNOSTIC_POP()

typedef struct ini_t ini_t;

IniContext iniCreateContext(Allocator* alloc)
{
    return IniContext { .ini = ini_create(alloc) };
}

IniContext iniLoad(const char* filepath, Allocator* alloc)
{
    ASSERT_MSG(alloc->GetType() != AllocatorType::Temp, "alloc cannot be temp. Because the code below also has temp alloc and breaks the stack");

    File f;

    MemTempAllocator tmpAlloc;
    Blob blob(&tmpAlloc);
    if (f.Open(filepath, FileOpenFlags::Read | FileOpenFlags::SeqScan)) {
        size_t size = f.GetSize();
        blob.Reserve(size + 1);
        f.Read(const_cast<void*>(blob.Data()), size);
        blob.SetSize(size);
        blob.Write<char>('\0');
        f.Close();
    }
    else {
        return IniContext {};
    }

    void* data;
    size_t size;
    blob.Detach(&data, &size);
    return IniContext { .ini = ini_load((const char*)data, alloc) };
}

IniContext iniLoadFromString(const char* data, Allocator* alloc)
{
    return IniContext { .ini = ini_load(data, alloc) };
}

bool iniSave(const IniContext& ini, const char* filepath)
{
    int size = ini_save(ini.ini, nullptr, 0);
    if (size > 0) {
        MemTempAllocator tmpAlloc;
        char* data = tmpAlloc.MallocTyped<char>(size);
        ini_save(ini.ini, data, size);

        File f;
        if (f.Open(filepath, FileOpenFlags::Write)) {
            f.Write(data, size);
            f.Close();
            return true;
        }
        else {
            return false;
        }        
    }
    else {
        return false;
    }
}

Blob iniSaveToMem(const IniContext& ini, Allocator* alloc)
{
    int size = ini_save(ini.ini, nullptr, 0);
    if (size > 0) {
        Blob blob(alloc);
        blob.Reserve(size);
        ini_save(ini.ini, (char*)blob.Data(), (int)blob.Size());
        return blob;
    }
    else {
        return Blob {};
    }
}

uint32 IniContext::GetSectionCount() const
{
    ASSERT(this->ini);
    return static_cast<uint32>(ini_section_count(this->ini));
}

IniSection IniContext::GetSection(uint32 index) const
{
    ASSERT(this->ini);
    return IniSection { .ini = this->ini, .id = static_cast<int>(index) };
}

const char* IniContext::GetSectionName(uint32 index) const
{
    ASSERT(this->ini);
    return ini_section_name(this->ini, static_cast<int>(index));
}

IniSection IniContext::GetRootSection() const
{
    ASSERT(this->ini);
    return IniSection { .ini = this->ini, .id = INI_GLOBAL_SECTION };
}

IniSection IniContext::NewSection(const char* name) const
{
    ASSERT(this->ini);
    return IniSection { .ini = this->ini, .id = ini_section_add(this->ini, name, strLen(name)) };
}

IniSection IniContext::FindSection(const char* name) const
{
    ASSERT(this->ini);
    return IniSection { .ini = this->ini, .id = ini_find_section(this->ini, name, strLen(name)) };
}

void IniContext::Destroy()
{
    if (this->ini) 
        ini_destroy(this->ini);
    this->ini = nullptr;
}

uint32 IniSection::GetPropertyCount()
{
    ASSERT(this->id != INI_NOT_FOUND);
    return static_cast<uint32>(ini_property_count(this->ini, this->id));
}

IniProperty IniSection::GetProperty(uint32 index)
{
    return { .ini = this->ini, .sectionId = this->id, .id = static_cast<int>(index) };
}

const char* IniSection::GetPropertyName(uint32 index)
{
    ASSERT(this->id != INI_NOT_FOUND);
    return ini_property_name(this->ini, this->id, static_cast<int>(index));
}

IniProperty IniSection::NewProperty(const char* name, const char* value)
{
    ASSERT(this->id != INI_NOT_FOUND);
    ini_property_add(this->ini, this->id, name, strLen(name), value, strLen(value));
    return IniProperty { 
        .ini = this->ini, 
        .sectionId = this->id,
        .id = ini_property_count(this->ini, this->id) - 1 
    };
}

IniProperty IniSection::FindProperty(const char* name)
{
    ASSERT(this->id != INI_NOT_FOUND);
    return IniProperty { 
        .ini = this->ini, 
        .sectionId = this->id,
        .id = ini_find_property(this->ini, this->id, name, strLen(name)) 
    };
}

void IniSection::SetName(const char* name)
{
    ASSERT(this->id != INI_NOT_FOUND);
    ini_section_name_set(this->ini, this->id, name, strLen(name));
}

const char* IniSection::GetName()
{
    ASSERT(this->id != INI_NOT_FOUND);
    return ini_section_name(this->ini, this->id);
}

void IniSection::Delete()
{
    ASSERT(this->id != INI_NOT_FOUND);
    ini_section_remove(this->ini, this->id);
}

void IniProperty::SetName(const char* name)
{
    ASSERT(this->id != INI_NOT_FOUND);
    ini_property_name_set(this->ini, this->sectionId, this->id, name, (int)strLen(name));
}

void IniProperty::SetValue(const char* value)
{
    ASSERT(this->id != INI_NOT_FOUND);
    ini_property_value_set(this->ini, this->sectionId, this->id, value, (int)strLen(value));
}

const char* IniProperty::GetName()
{
    ASSERT(this->id != INI_NOT_FOUND);
    return ini_property_name(this->ini, this->sectionId, this->id);
}

const char* IniProperty::GetValue()
{
    ASSERT(this->id != INI_NOT_FOUND);
    return ini_property_value(this->ini, this->sectionId, this->id);
}

void IniProperty::Delete()
{
    ASSERT(this->id != INI_NOT_FOUND);
    ini_property_remove(this->ini, this->sectionId, this->id);
}
