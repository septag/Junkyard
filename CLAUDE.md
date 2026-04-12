# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build System

**Windows (primary platform)**

```bat
# Option 1: Visual Studio solution (recommended)
# Open projects/Windows/Junkyard.sln
# Configurations: Debug | ReleaseDev (Tracy profiler + assertions) | Release

# Option 2: Unity build scripts (no build system required)
scripts/Build/build-TestBasicGfx.bat
scripts/Build/build-TestCollision.bat
scripts/Build/build-TestRenderer.bat
scripts/Build/build-TestAsset.bat
scripts/Build/build-all.bat

# Option 3: CMake
cd projects/CMake
cmake -B build
cmake --build build
```

**Linux**: Build via VSCode + CMakeTools extension with Clang toolset (see `projects/CMake`).  
**macOS**: Xcode project in `projects/MacOS`.

**First-time setup** (fetches Slang, meshoptimizer, ISPC Texture Compressor, optionally Vulkan SDK):
```bat
Setup.bat   # Windows
Setup.sh    # Linux/macOS
```

**Running examples**: Working directory must be set to the project root so `data/` is accessible. Binaries output to `Bin/Debug/`, `Bin/ReleaseDev/`, or `Bin/Release/`.

## Architecture

The codebase is a portable C++ game/application framework built in minimal C++20 (no STL, no RTTI, no exceptions). Vulkan is the only graphics API.

**Module layout under `code/`:**

| Directory | Purpose |
|-----------|---------|
| `Core/` | Portable stdlib replacement: Array, HashTable, HandlePool, Memory allocators, Math, FileIO, Jobs (fiber-based), Atomic, Buffers, Log, JSON/INI parsing |
| `Graphics/` | Low-level Vulkan 1.3 backend (`GfxBackend.h/.cpp`) |
| `Renderer/` | High-level rendering: meshes, materials, lighting |
| `Assets/` | Async asset manager: image/model/shader loading and baking (PNG→BCn/ASTC, GLTF→meshoptimizer, HLSL→SPIRV) |
| `Common/` | Application entry, Camera (Orbital/FPS), Input, VirtualFS, Settings, RemoteServices, Profiler |
| `Collision/` | Physics/collision system |
| `ImGui/` | ImGui + ImGuizmo integration |
| `DebugTools/` | Debug draw, debug HUD |
| `Tool/` | Asset baking tools, JunkyardTool server for remote asset baking |
| `Tests/` | Demo/test apps: TestBasicGfx, TestRenderer, TestAsset, TestCollision |
| `Shaders/` | HLSL shader sources (compiled to SPIRV via Slang) |
| `External/` | Vendored third-party libs |

**Job system**: Fiber-based (Naughty Dog style) in `Core/Jobs.h`. Preferred over threads for parallel work.

**Asset pipeline**: Assets are baked on the PC. For platforms without local baking support, run `JunkyardTool` as a server and configure `Settings.ini` with `connectToServer=true` and `remoteServicesUrl=[host_ip]:6006`.

**Memory**: Only `Mem::Alloc/Free/Realloc` — never `malloc/free/new/delete`. For placement-new scenarios use `NEW`/`NEW_ARRAY`/`PLACEMENT_NEW` macros. Temp allocator (`Mem::GetTempAllocator()`) for short-lived frame allocations.

## Coding Conventions

From `doc/misc/CodingStandard.md`:

- **Namespaces**: One level only. Functions live inside namespaces; types live outside with namespace-name prefixes (e.g., `struct MemAllocator; namespace Mem { void* Alloc(MemAllocator*...); }`). Private helpers go in `_private` namespace.
- **Naming**: PascalCase for namespaces/functions/types/files; camelCase for local/argument vars; `mPascalCase` for member vars; `gPascalCase` for globals; `SCREAMING_SNAKE_CASE` for defines and constants.
- **Lifecycle**: `Initialize/Release` for subsystems, `Create/Destroy` for resources, `Begin/End` for paired operations.
- **No singletons**: Subsystem state lives as `static gMySubSystem;` inside its translation unit, not exported.
- **No inheritance** except pure-virtual interfaces (use `final` on implementors).
- **No heavy constructors or destructors**: allocations in ctors are forbidden; deallocations in dtors are forbidden.
- **Handles over pointers** for public APIs: use `DEFINE_HANDLE` macro + `HandlePool<>`.
- **POD structs preferred** with zero-initialization as default. Default member initializers for descriptor/parameter structs used with designated initializers.
- **`enum class` with explicit underlying type**; use `ENABLE_BITMASK()` for flag enums.
- **`#pragma once`** only (no include guards).
- **Formatting**: 4-space indentation, LF line endings, ~120 char line width, `//` comments only, opening brace on new line for functions/types/lambdas, same line for everything else.
- **Non-trivial code must not live in headers**; keep private types out of headers too.
- **Templates**: use sparingly, no nested/complex templates; heavy template bodies go in `_private::` functions inside `.cpp` files.
