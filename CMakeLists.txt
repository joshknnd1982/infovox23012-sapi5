cmake_minimum_required(VERSION 3.20)
project(Infovox23012SAPI VERSION 1.0.5 LANGUAGES CXX RC)

# The Infovox 230 engine is a 32-bit dll, so the worker that owns it is always
# x86. The SAPI5 engine itself is built twice -- 32-bit for 32-bit hosts and
# 64-bit for hosts like Narrator -- and both are thin clients of the one worker.
# Configure this file once per architecture; build_all.bat does both and stages
# the result for the installer.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

if(NOT MSVC)
    message(FATAL_ERROR "This project targets MSVC; SAPI5 and the engine are Windows-only.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(IVX_X64 TRUE)
    message(STATUS "Infovox23012SAPI: 64-bit SAPI5 engine")
else()
    set(IVX_X64 FALSE)
    message(STATUS "Infovox23012SAPI: 32-bit SAPI5 engine, worker and diagnostics")
endif()

add_compile_options(/W4 /MP /permissive- /EHsc)
add_compile_definitions(
    _CRT_SECURE_NO_WARNINGS
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

# Windows 7 SP1 is the oldest Windows this runs on, and the Windows headers are
# told so. Without this they declare whatever the installed SDK knows about, and
# calling one of those by name is not a warning or a runtime failure that can be
# handled -- it is an import the loader cannot resolve, so Windows 7 kills the
# process before its first instruction. That is exactly what happened to the
# worker in 1.0.3: it named a Windows 8 function, and on Windows 7 every attempt
# to speak started a process that vanished. Pinning the version turns that
# mistake into a compile error. Anything genuinely newer is looked up with
# GetProcAddress and does without on the versions that lack it.
add_compile_definitions(
    _WIN32_WINNT=0x0601
    WINVER=0x0601
    NTDDI_VERSION=0x06010000
)
# Statically linked so a missing Visual C++ redistributable can never be the
# reason a screen reader has no voice.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Stamps a target with the product version, its own name and a description, so
# Explorer's Properties -> Details says which build it is. src/ivx_version.h is
# the single place the number lives; see the note there about never replacing a
# release asset in place.
function(ivx_version_stamp target description is_dll)
    target_sources(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/ivx_version.rc)
    get_target_property(_output ${target} OUTPUT_NAME)
    if(NOT _output)
        set(_output ${target})
    endif()
    if(is_dll)
        set(_suffix ".dll")
        set(_dllflag IVX_IS_DLL)
    else()
        set(_suffix ".exe")
        set(_dllflag IVX_NOT_DLL)
    endif()
    target_compile_definitions(${target} PRIVATE
        IVX_FILE_NAME="${_output}${_suffix}"
        IVX_FILE_DESC="${description}"
        ${_dllflag}
    )
endfunction()

# ---------------------------------------------------------------------------
# Shared by every target.
# ---------------------------------------------------------------------------
add_library(ivx_common STATIC
    src/ivx_log.cpp
    src/ivx_paths.cpp
    src/ivx_catalog.cpp
    src/ivx_settings.cpp
    src/ivx_vregistry.cpp
)
target_include_directories(ivx_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ivx_common PUBLIC shlwapi advapi32)

# ---------------------------------------------------------------------------
# The SAPI5 engine: built for both architectures.
# ---------------------------------------------------------------------------
add_library(Infovox23012SAPI SHARED
    src/ivx_sapi_main.cpp
    src/ivx_sapi_tokens.cpp
    src/ivx_sapi_engine.cpp
    src/ivx_client.cpp
    src/ivx_com.cpp
    src/Infovox23012SAPI.def
)
target_link_libraries(Infovox23012SAPI PRIVATE ivx_common ole32 oleaut32)
set_target_properties(Infovox23012SAPI PROPERTIES OUTPUT_NAME "Infovox23012SAPI")
ivx_version_stamp(Infovox23012SAPI "Infovox 230 v1.12 SAPI5 speech engine" TRUE)

# Drives the finished engine through SAPI5 itself rather than through our own
# interfaces, which is the only way to test what applications actually do. Built
# for both architectures because the 64-bit path is the one that crosses a
# process boundary.
add_executable(Infovox23012SapiTest tools/ivx_sapi_test.cpp)
target_link_libraries(Infovox23012SapiTest PRIVATE ole32 oleaut32)
target_include_directories(Infovox23012SapiTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
ivx_version_stamp(Infovox23012SapiTest "Infovox 230 v1.12 SAPI5 test harness" FALSE)

# ---------------------------------------------------------------------------
# 32-bit only: the worker that owns the engine, and the diagnostics tool.
# ---------------------------------------------------------------------------
if(NOT IVX_X64)
    add_library(ivx_engine STATIC src/ivx_engine.cpp)
    target_link_libraries(ivx_engine PUBLIC ivx_common ole32 oleaut32 winmm)

    add_executable(Infovox23012Server src/ivx_server.cpp)
    target_link_libraries(Infovox23012Server PRIVATE ivx_engine)
    ivx_version_stamp(Infovox23012Server "Infovox 230 v1.12 speech engine worker" FALSE)

    # The configuration utility. 32-bit only and not because of the engine --
    # it never loads it -- but because there is no reason to ship two of a
    # program a person runs by hand, and the 32-bit one runs everywhere.
    #
    # It speaks through the worker like any other client, so previewing a voice
    # exercises the same path a screen reader does.
    add_executable(Infovox23012Config WIN32
        src/ivx_config_app.cpp
        src/ivx_config_model.cpp
        src/ivx_client.cpp
        src/ivx_config.rc
    )
    target_link_libraries(Infovox23012Config PRIVATE ivx_common comctl32 winmm shell32 ole32)
    ivx_version_stamp(Infovox23012Config "Infovox 230 v1.12 configuration utility" FALSE)
endif()

# Diagnostics are built for both architectures. The 32-bit one can drive the
# engine directly as well as through the worker; the 64-bit one can only go
# through the worker, which is precisely the path it needs to prove.
add_executable(Infovox23012Diag tools/ivx_probe.cpp src/ivx_client.cpp)
if(IVX_X64)
    target_link_libraries(Infovox23012Diag PRIVATE ivx_common ole32 oleaut32)
else()
    target_link_libraries(Infovox23012Diag PRIVATE ivx_engine)
    target_compile_definitions(Infovox23012Diag PRIVATE IVX_HAVE_ENGINE)
endif()
ivx_version_stamp(Infovox23012Diag "Infovox 230 v1.12 diagnostics" FALSE)

# ---------------------------------------------------------------------------
# Staging: everything the installer packages, in the layout it installs.
# ---------------------------------------------------------------------------
set(IVX_STAGE ${CMAKE_CURRENT_SOURCE_DIR}/output)
set(IVX_ENGINE_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/bin/engine)

if(IVX_X64)
    add_custom_command(TARGET Infovox23012SAPI POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${IVX_STAGE}/x64
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Infovox23012SAPI> ${IVX_STAGE}/x64/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Infovox23012Diag> ${IVX_STAGE}/x64/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Infovox23012SapiTest> ${IVX_STAGE}/x64/
        COMMENT "Staging the 64-bit SAPI5 engine")
else()
    add_custom_command(TARGET Infovox23012Server POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${IVX_STAGE}/engine
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Infovox23012SAPI> ${IVX_STAGE}/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Infovox23012Server> ${IVX_STAGE}/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Infovox23012Diag> ${IVX_STAGE}/
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Infovox23012SapiTest> ${IVX_STAGE}/
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${IVX_ENGINE_SOURCE} ${IVX_STAGE}/engine
        COMMENT "Staging the 32-bit engine, worker and voice data")

    add_custom_command(TARGET Infovox23012Config POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${IVX_STAGE}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Infovox23012Config> ${IVX_STAGE}/
        COMMENT "Staging the configuration utility")
endif()
