# Coding Standard

## Style
### Namespaces
Only one level namespaces are allowed. Nested namespaces are **not allowed**. 
- For **privately accessed functions and types** (usually within a single sub-system or template optimization), use `_private` namespace.
- **Only functions are allowed to be inside namespaces**
- **Public type declarations should always be outside of namespace**: Instead use the namespace name as prefix. Example: `struct MemAllocator; namespace Mem { void* Alloc(MemAllocator* ...); }`. As you can see here, types are prefixed with their corresponding sub-system/module and functions are encapsulated inside the namespace.

### Formatting
- **Code blocks for functions, types and lambdas**: Use open curly bracket in a new line.
- **Code blocks for everything else**: Use open curly bracket in the same line.
- **Not having brackets for *single line* blocks is allowed**
- **Only use relative paths for header includes**: Basically, using *include path* (`-I`) compiler flag is not allowed, except for some external dependencies like Vulkan headers.
- **Allowed line width is *roughly* 120 characters**: Break lines after 120 characters. By *roughly* I mean, there is some tolerance of course. You don't have to be super strict about it. Sometimes it's ok if it passes the threshold a couple of characters. Or for instance, sometimes for comments, we can write longer lines.
- **Only use LF for new lines**
- **Comments should only be indicated by double slashes (`//`)**
- **Indentations are 4-spaces**

### Naming
- **Namespaces, functions, type names, file names**: PascalCase
    - **static functions (privately accessed within a translation unit)**: _PascalCase
- **Local and argument variables**: camelCase
- **Member variables (if the type also includes functions that operate on them)**: mPascalCase
    - There are some exceptions in the core library (like math vector types) for conveniences of not addressing to `x, y, z` as `mX, mY, mZ`
- **Global variables**: gPascalCase
- **Defines**: SCREAMING_SNAKE_CASE. They usually start with their related namespace name. 
- **Constants and `constexpr` global vars**: SCREAMING_SNAKE_CASE. Same as above, they start with their parent module name or related namespace name. For example, anything related to GfxBackend, starts with `GFXBACKEND_` prefix.

## Coding 
- **Use of stl code is strictly prohibited**: There should be no `#include <some_stl_crap>` in the code. For some external libs, we *may* have no choice, but they should be absolutely avoided inside any publicly used headers.
- **Only use `#pragma once` for headers**
- **Prefer `enum class` with explicit types over plain `enum`**
    - Use `ENABLE_BITMASK()` macro for flag enum type to automatically implement `&|^~` operators
- **Use encapsulation carefully. Prefer POD (plain-old-data) structs and functions over encapsulation**
    - Unless there are some conveniences are involved and data is small/harmless enough to pass around. For example, look at `GfxCommandBuffer` in `GfxBackend.h`. It doesn't keep the actual data to CommandBuffers. It just holds a reference/handle to the internal data and can be more convenient that we pass it around as an object and use the member functions for drawing stuff. 
- **Inheritance is not allowed, *except* pure interface classes**: This also should be used rarely and only if necessary. Because it creates the *vtable*, breaks POD benefits and any parent data that embeds them. Also use `final` keyword for the inherited implementor class so that compiler may have a better chance to optimize it. 
- **Do not do anything heavy in constructors. Especially allocations are strictly prohibited**
    - Usually only `Scope` suffix types (Profiling, Mutex locking, ...) are allowed to do some light work in the constructor.
- **Prefer zero-initialization or default member initializers over classic constructors**: So the order of preference is:
    - *Best*: Default zero initialization: Everything works if member data is all zeros.
    - *Ok*: Default member initializers. Mostly used for input function arguments packed into structs. Descriptors or basically things that need default values when we use designated initializers.
    - *Meh*: Use explicit constructors to do some light work, assignment or whatever. But still, avoid any resource allocation. As said, some exceptions are `Scope` suffix classes that are effective only within their defined scope (profiling, mutex locking, etc.)
- **Do not release resources or deallocate in destructors**
- **Use `Create/Destroy` function pairs to initialize and uninitialize resources for objects**
- **Use `Initialize/Release` function pairs for sub-systems to initialize and uninitialize sub-systems and global data**
- **Use `Begin/End` function pairs for some work that needs preparation and post work**
- **Do not use singletons**: Instead keep each sub-system data inside the translation unit and use `static MySubSystem gMySubSystem;` inside that translation unit to access it's data. Data should not be accessed from outside the translation unit directly.
- **Using `malloc/free` or `new/delete` operators are not allowed**: We have replacements for all of those. Use `Mem::Alloc, Mem::Free, Mem::Realloc` family of functions. For `new/delete` (although should be rarely used) see `NEW/NEW_ARRAY/PLACEMENT_NEW/ALIGNED_NEW/PLACEMENT_NEW_ARRAY` macros, If you use plain-old-data types, you won't have the need for those.
- **Consider using struct to return multiple values from a function**
- **Non-trivial code should be out of headers**
- **Non-public data types and interfaces should be kept out of headers and inside source files**: There are a few exceptions where we use `_private` namespaces and those are usually for a few template optimizations, so that we can take non-trivial code out of the headers.
- **Prefer enums over booleans for function arguments**
- **Compile without warnings**: In release builds, warnings are actually treated as errors.
- **`const` reference types are preferred over non-const references for public functions**: Instead use pointer types to indicate that the data is mutable.
- **Use `using` C++ keyword instead of `typedef` for declaring callback functions**
- **For functions that accept lots of arguments. Pack arguments into a single `struct`**: You can use C++20 designated initializers to initialize their variables.
- **Prefer `constexpr` and `inline constepxr` over macros for global constants**
- **Use comments for rather big `#if/#else/#endif` blocks of code**
- **Prefer forward declarations in headers over including**
- **Prune unused/commented code as much as you can**
- **Use handles (`DEFINE_HANDLE` macro and `HandlePool<>`) and resolve data internally, instead of exporting pointer objects in public APIs**: [Handles are the better pointers](https://floooh.github.io/2018/06/17/handles-vs-pointers.html)
- **Adding non-POD data types as a member for other objects is not recommended**: Use of non-POD types, especially classes with virtual functions are only allowed inside sub-systems data and global data. Basically, in a data that is not copied and passed around. If you do that, then you have to get into C++ hell-hole and take care of manual construction like using NEW macros and such.
- **Templates should be used carefully and rarely**: 
    - They should not encapsulate large/heavy chunks of code. Only small/light functions and "helper" classes.
    - Should not be overused
    - Nested/complicated template classes are not allowed
    - If templates contain large non-trivial code, put them inside `_private::` functions, implement those functions inside the CPP file and call those functions inside template functions instead.

## C++ standard
General C++ flags and rules:
- **No Exceptions**
- **No RTTI**
- **No Stl**

These language standard are only used through-out the code. Other stuff is not allowed unless mentioned here:
- **C++20**:
    - *Designated initializers*: Still not as good as C99, but does the job and used extensively in the code.
    
- **C++17**:
    - `if constexpr()` - Rarely when writing multi-platform, config-dependent code and it makes sense to not litter the code with `#if/#endif` preprocessor.
    - `[[maybe_used]]`
    - `[[nodiscard]]`
    - `[[deprecated]]`
    - `inline constexpr variables`: For constant globals
     
- **C++14**:
    - *Functors*: Although rarely used
    - Generalized lambda expressions
    - `Extended capturing in lambdas`
    - `static_assert`
     
- **C++11/98**:
    - *lambda functions*: Limited use. Only if the body of the function is no more than a couple of lines.
    - `constexpr`
    - *operator overloading*: Limited use. Mainly for math types.
    - *default arguments*: Pass structs with default member initializers are preferred.
    - *Function overrides*: Limited use. It is mainly used for passing arguments with different types to a function that pretty much uses the same base signature and does the same thing (like variants for example). Otherwise, for functions that does drastically a different thing based on input arguments, you should use a different name instead.
    - *Return structs as value (C++ ABI)* 
    - `using` keyword: instead of C's `typedef`
    - *Pure virtual classes*: Any other kind of inheritance is prohibited
    - `enum class`
    - `namespace`/`using namespace`
    
