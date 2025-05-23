cmake_minimum_required(VERSION 3.10)

project(Junkyard)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ../bin)
endif()

if (CMAKE_GENERATOR MATCHES "Visual Studio" OR CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    option(MSVC_MULTITHREADED_COMPILE "Multi-threaded compilation in MSVC." ON)

    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    string(REPLACE "/GR" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

    if (MSVC_MULTITHREADED_COMPILE AND NOT CMAKE_MAKE_PROGRAM MATCHES "ninja.exe")
        add_compile_options(/MP)    # multiprocessor compilation
    endif()
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "Error: Only clang compiler is supported on linux")
    endif()

    list(APPEND CMAKE_PREFIX_PATH "$ENV{VULKAN_SDK}/share/cmake")
endif()

# Junkyard.lib
file(GLOB SOURCE_Core ../../code/Core/*.cpp)
file(GLOB_RECURSE HEADERS_Core ../../code/Core/*.h)

file(GLOB SOURCE_Common ../../code/Common/*.cpp)
file(GLOB HEADERS_Common ../../code/Common/*.h)

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

if (WIN32)
    target_include_directories(Junkyard SYSTEM PUBLIC ../../code/External/vulkan/include)
    target_compile_definitions(Junkyard PUBLIC -D_ITERATOR_DEBUG_LEVEL=0 -D_CRT_SECURE_NO_WARNINGS -DCONFIG_TOOLMODE=1)
    target_compile_options(Junkyard PUBLIC /Zi /GR-)
    target_link_options(Junkyard PUBLIC /DEBUG:FULL /INCREMENTAL:NO)

    # Copy files after succesful build
    set (FILES_TO_COPY 
        ../code/External/slang/bin/slang.dll
        ../code/External/ispc_texcomp/lib/win64/ispc_texcomp.dll 
        ../code/External/dbghelp/dbghelp.dll
        ../code/External/meshoptimizer/lib/win64/meshoptimizer.dll)
    set (FILES_DEST_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/$<CONFIG>)

    add_custom_command(TARGET Junkyard POST_BUILD 
        COMMAND ${CMAKE_COMMAND} -E echo "Copying files to ${FILES_DEST_DIR}")
    foreach (FILE ${FILES_TO_COPY}) 
        get_filename_component(FILENAME ${FILE} NAME)
        add_custom_command(TARGET Junkyard POST_BUILD 
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FILE} "${FILES_DEST_DIR}/${FILENAME}")
    endforeach()

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

    get_filename_component(DEBUGGER_WORKING_DIR "${CMAKE_SOURCE_DIR}/../../" ABSOLUTE)

    # TestBasicGfx
    add_executable(TestBasicGfx ../../code/Tests/TestBasicGfx.cpp)
    target_link_libraries(TestBasicGfx Junkyard)
    set_property(TARGET TestBasicGfx PROPERTY VS_DEBUGGER_WORKING_DIRECTORY ${DEBUGGER_WORKING_DIR})

    set_property(GLOBAL PROPERTY VS_STARTUP_PROJECT TestBasicGfx)
else()
    target_compile_definitions(Junkyard PUBLIC -DCONFIG_TOOLMODE=1)
    target_compile_options(Junkyard PUBLIC -fno-rtti -fno-exceptions -Wall)
    target_compile_options(Junkyard PUBLIC -Wno-backslash-newline-escape)    # Because of some fancy ascii comments

    if (CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        target_compile_options(Junkyard PUBLIC -Werror)
    endif()

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_definitions(Junkyard PUBLIC -D_DEBUG)
    endif()

    if (CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        target_compile_definitions(Junkyard PUBLIC -DCONFIG_ENABLE_ASSERT=1 -DTRACY_ENABLE=1)
        target_compile_options(Junkyard PUBLIC -fno-omit-frame-pointer)
        target_link_options(Junkyard PUBLIC -rdynamic)
    endif()

    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_package(PkgConfig REQUIRED)

        pkg_check_modules(GLFW REQUIRED glfw3)
        target_include_directories(Junkyard PUBLIC ${GLFW_INCLUDE_DIRS})

        find_package(VulkanHeaders REQUIRED)
        target_link_libraries(Junkyard PUBLIC Vulkan::Headers)

        target_link_directories(Junkyard PUBLIC ${GLFW_LIBRARY_DIRS})
        target_link_libraries(Junkyard PUBLIC ${GLFW_LIBRARIES})
        target_link_libraries(Junkyard PUBLIC uuid m c++abi)

        # Copy files after succesful build
        set (FILES_TO_COPY 
            ../code/External/slang/lib/libslang.so
            ../code/External/ispc_texcomp/lib/libispc_texcomp.so 
            ../code/External/meshoptimizer/lib/linux-x86_64/libmeshoptimizer.so)
        set (FILES_DEST_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

        add_custom_command(TARGET Junkyard POST_BUILD 
            COMMAND ${CMAKE_COMMAND} -E echo "Copying files to ${FILES_DEST_DIR}")
        foreach (FILE ${FILES_TO_COPY}) 
            get_filename_component(FILENAME ${FILE} NAME)
            add_custom_command(TARGET Junkyard POST_BUILD 
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FILE} "${FILES_DEST_DIR}/${FILENAME}")
        endforeach()

        # External
        target_link_directories(Junkyard INTERFACE 
            ../../code/External/slang/lib 
            ../../code/External/ispc_texcomp/lib 
            ../../code/External/meshoptimizer/lib/linux-x86_64)
        target_link_libraries(Junkyard INTERFACE ispc_texcomp slang meshoptimizer)
    endif()

    add_executable(TestBasicGfx ../../code/Tests/TestBasicGfx.cpp)
    target_link_libraries(TestBasicGfx Junkyard)

endif()



