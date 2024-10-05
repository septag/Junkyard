#include "IniParser.h"

#include "StringUtil.h"
#include "System.h"
#include "Allocators.h"

#define INI_IMPLEMENTATION
#define INI_MALLOC(ctx, size)       Mem::Alloc(size, (MemAllocator*)ctx)
#define INI_FREE(ctx, ptr)          Mem::Free(ptr, (MemAllocator*)ctx)
#define INI_MEMCPY(dst, src, cnt)   memcpy(dst, src, cnt)
#define INI_STRLEN(s)               Str::Len(s)
#define INI_STRNICMP(s1, s2, cnt)   (Str::IsEqualNoCaseCount(s1, s2, cnt) ? 0 : 1)

PRAGMA_DIAGNOSTIC_PUSH()
PRAGMA_DIAGNOSTIC_IGNORED_CLANG_GCC("-Wsign-compare")
#include "External/mgustavsson/ini.h"
PRAGMA_DIAGNOSTIC_POP()

typedef struct ini_t ini_t;

INIFileContext INIFile::Create(MemAllocator* alloc)
{
    return INIFileContext { .ini = ini_create(alloc) };
}

INIFileContext INIFile::Load(const char* filepath, MemAllocator* alloc)
{
    ASSERT_MSG(alloc->GetType() != MemAllocatorType::Temp, "alloc cannot be temp. Because the code below also has temp alloc and breaks the stack");

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
        return INIFileContext {};
    }

    void* data;
    size_t size;
    blob.Detach(&data, &size);
    return INIFileContext { .ini = ini_load((const char*)data, alloc) };
}

INIFileContext INIFile::LoadFromString(const char* data, MemAllocator* alloc)
{
    return INIFileContext { .ini = ini_load(data, alloc) };
}

bool INIFile::Save(const INIFileContext& ini, const char* filepath)
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

Blob INIFile::SaveToMem(const INIFileContext& ini, MemAllocator* alloc)
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

uint32 INIFileContext::GetSectionCount() const
{
    ASSERT(this->ini);
    return static_cast<uint32>(ini_section_count(this->ini));
}

INIFileSection INIFileContext::GetSection(uint32 index) const
{
    ASSERT(this->ini);
    return INIFileSection { .ini = this->ini, .id = static_cast<int>(index) };
}

const char* INIFileContext::GetSectionName(uint32 index) const
{
    ASSERT(this->ini);
    return ini_section_name(this->ini, static_cast<int>(index));
}

INIFileSection INIFileContext::GetRootSection() const
{
    ASSERT(this->ini);
    return INIFileSection { .ini = this->ini, .id = INI_GLOBAL_SECTION };
}

INIFileSection INIFileContext::NewSection(const char* name) const
{
    ASSERT(this->ini);
    return INIFileSection { .ini = this->ini, .id = ini_section_add(this->ini, name, Str::Len(name)) };
}

INIFileSection INIFileContext::FindSection(const char* name) const
{
    ASSERT(this->ini);
    return INIFileSection { .ini = this->ini, .id = ini_find_section(this->ini, name, Str::Len(name)) };
}

void INIFileContext::Destroy()
{
    if (this->ini) 
        ini_destroy(this->ini);
    this->ini = nullptr;
}

uint32 INIFileSection::GetPropertyCount()
{
    ASSERT(this->id != INI_NOT_FOUND);
    return static_cast<uint32>(ini_property_count(this->ini, this->id));
}

INIFileProperty INIFileSection::GetProperty(uint32 index)
{
    return { .ini = this->ini, .sectionId = this->id, .id = static_cast<int>(index) };
}

const char* INIFileSection::GetPropertyName(uint32 index)
{
    ASSERT(this->id != INI_NOT_FOUND);
    return ini_property_name(this->ini, this->id, static_cast<int>(index));
}

INIFileProperty INIFileSection::NewProperty(const char* name, const char* value)
{
    ASSERT(this->id != INI_NOT_FOUND);
    ini_property_add(this->ini, this->id, name, Str::Len(name), value, Str::Len(value));
    return INIFileProperty { 
        .ini = this->ini, 
        .sectionId = this->id,
        .id = ini_property_count(this->ini, this->id) - 1 
    };
}

INIFileProperty INIFileSection::FindProperty(const char* name)
{
    ASSERT(this->id != INI_NOT_FOUND);
    return INIFileProperty { 
        .ini = this->ini, 
        .sectionId = this->id,
        .id = ini_find_property(this->ini, this->id, name, Str::Len(name)) 
    };
}

void INIFileSection::SetName(const char* name)
{
    ASSERT(this->id != INI_NOT_FOUND);
    ini_section_name_set(this->ini, this->id, name, Str::Len(name));
}

const char* INIFileSection::GetName()
{
    ASSERT(this->id != INI_NOT_FOUND);
    return ini_section_name(this->ini, this->id);
}

void INIFileSection::Delete()
{
    ASSERT(this->id != INI_NOT_FOUND);
    ini_section_remove(this->ini, this->id);
}

void INIFileProperty::SetName(const char* name)
{
    ASSERT(this->id != INI_NOT_FOUND);
    ini_property_name_set(this->ini, this->sectionId, this->id, name, (int)Str::Len(name));
}

void INIFileProperty::SetValue(const char* value)
{
    ASSERT(this->id != INI_NOT_FOUND);
    ini_property_value_set(this->ini, this->sectionId, this->id, value, (int)Str::Len(value));
}

const char* INIFileProperty::GetName()
{
    ASSERT(this->id != INI_NOT_FOUND);
    return ini_property_name(this->ini, this->sectionId, this->id);
}

const char* INIFileProperty::GetValue()
{
    ASSERT(this->id != INI_NOT_FOUND);
    return ini_property_value(this->ini, this->sectionId, this->id);
}

void INIFileProperty::Delete()
{
    ASSERT(this->id != INI_NOT_FOUND);
    ini_property_remove(this->ini, this->sectionId, this->id);
}
