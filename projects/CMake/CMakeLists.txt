cmake_minimum_required(VERSION 3.10)

project(Junkyard)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
string(REPLACE "/GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Junkyard.lib
file(GLOB SOURCE_Core ../../code/Core/*.cpp)
file(GLOB_RECURSE HEADERS_Core ../../code/Core/*.h)

if (WIN32)
    file(GLOB SOURCE_Common ../../code/Common/*.cpp)
    file(GLOB HEADERS_Common ../../code/Common/*.h)

    file(GLOB SOURCE_DebugTools ../../code/DebugTools/*.cpp)
    file(GLOB HEADERS_DebugTools ../../code/DebugTools/*.h)

    file(GLOB SOURCE_DebugTools ../../code/DebugTools/*.cpp)
    file(GLOB HEADERS_DebugTools ../../code/DebugTools/*.h)

    file(GLOB SOURCE_Graphics ../../code/Graphics/*.cpp)
    file(GLOB HEADERS_Graphics ../../code/Graphics/*.h)

    file(GLOB SOURCE_ImGui ../../code/ImGui/*.cpp)
    file(GLOB HEADERS_ImGui ../../code/ImGui/*.h)

    file(GLOB SOURCE_Tool ../../code/Tool/*.cpp)
    file(GLOB HEADERS_Tool ../../code/Tool/*.h)

    file(GLOB SOURCE_Assets ../../code/Assets/*.cpp)
    file(GLOB HEADERS_Assets ../../code/Assets/*.h)

    file(GLOB SOURCE_Shaders ../../code/Shaders/*.hlsl)

    file(GLOB_RECURSE SOURCE_External ../../code/External/*.cpp)
    file(GLOB_RECURSE HEADERS_External ../../code/External/*.h)

    file(GLOB SOURCE_Root ../../code/*.cpp)
    file(GLOB HEADERS_Root ../../code/*.h)

    add_library(Junkyard STATIC 
        ${SOURCE_Core} ${HEADERS_Core}
        ${SOURCE_Common} ${HEADERS_Core} 
        ${SOURCE_DebugTools} ${HEADERS_DebugTools}
        ${SOURCE_Graphics} ${HEADERS_Graphics} 
        ${SOURCE_ImGui} ${HEADERS_ImGui}
        ${SOURCE_Tool} ${HEADERS_Tool}
        ${SOURCE_Assets} ${HEADERS_Assets}
        ${SOURCE_External} ${HEADERS_External}
        ${SOURCE_Root} ${HEADERS_Root})

    target_include_directories(Junkyard SYSTEM PUBLIC ../../code/External/vulkan/include)
    target_compile_definitions(Junkyard PUBLIC -D_DEBUG -D_ITERATOR_DEBUG_LEVEL=0 -D_CRT_SECURE_NO_WARNINGS -DCONFIG_TOOLMODE=1)
    target_compile_options(Junkyard PUBLIC /Zi /GR-)
    target_link_options(Junkyard PUBLIC /DEBUG:FULL /INCREMENTAL:NO)

    # LivePP
    # CL: Enable Minimal Rebuild (/Gm-), Function level linking (/Gy), Optimize Global Data (/Gw)
    # Link: NoReferences (/OPT:NOREF), Disable COMDAT Folding (/OPT:NOICF), Full Debug info (Already added), CreateHotpackableImage (/FUNCTIONPADMIN)
    target_compile_options(Junkyard PUBLIC /Gm- /Gy /Gw)
    target_link_options(Junkyard PUBLIC /OPT:NOREF /OPT:NOICF /FUNCTIONPADMIN) 

    target_link_directories(Junkyard INTERFACE 
        ../../code/External/slang/lib 
        ../../code/External/ispc_texcomp/lib/win64 
        ../../code/External/meshoptimizer/lib/win64)
    target_link_libraries(Junkyard INTERFACE ispc_texcomp slang meshoptimizer)

    # TestBasicGfx
    add_executable(TestBasicGfx ../../code/Tests/TestBasicGfx.cpp)
    target_link_libraries(TestBasicGfx Junkyard)
else()
    add_library(Junkyard STATIC ${SOURCE_Core} ${HEADERS_Core})
endif()



