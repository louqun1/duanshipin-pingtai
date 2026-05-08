cmake_minimum_required(VERSION 3.16)

project(FlashpointShortVideos)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(FLASHPOINT_QT_ROOT "D:/itffmpeg/Qt/6.10.3/llvm-mingw_64" CACHE PATH
    "Qt 6 root used for FlashpointShortVideos")

if(EXISTS "${FLASHPOINT_QT_ROOT}/lib/cmake/Qt6/Qt6Config.cmake")
    list(PREPEND CMAKE_PREFIX_PATH "${FLASHPOINT_QT_ROOT}")
endif()

# Qt6Gui advertises optional Vulkan headers. On machines without a Vulkan SDK,
# pre-creating the nolink target keeps configure output clean while still using
# real headers if they are discoverable.
find_path(FLASHPOINT_VULKAN_INCLUDE_DIR
    NAMES vulkan/vulkan.h
)

if(NOT TARGET WrapVulkanHeaders::WrapVulkanHeaders)
    add_library(WrapVulkanHeaders::WrapVulkanHeaders INTERFACE IMPORTED)
    if(FLASHPOINT_VULKAN_INCLUDE_DIR)
        set_target_properties(WrapVulkanHeaders::WrapVulkanHeaders PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${FLASHPOINT_VULKAN_INCLUDE_DIR}"
        )
    endif()
    set_target_properties(WrapVulkanHeaders::WrapVulkanHeaders PROPERTIES
        _qt_is_nolink_target TRUE
        _qt_skip_include_dir_for_pri TRUE
    )
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network Sql OpenGL OpenGLWidgets)

add_subdirectory(backend)
add_subdirectory(frontend)

add_executable(QtFrontend
    app/main.cpp
    app/bootstrap/AppBootstrap.hpp
    app/bootstrap/AppBootstrap.cpp
)

target_include_directories(QtFrontend
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app
)

target_link_libraries(QtFrontend
    PRIVATE
        ServoBackend
        FrontendWidgets
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
)

set_target_properties(QtFrontend PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

function(flashpoint_copy_runtime_files target)
    foreach(_flashpoint_runtime_file IN LISTS ARGN)
        if(EXISTS "${_flashpoint_runtime_file}")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_flashpoint_runtime_file}"
                    $<TARGET_FILE_DIR:${target}>
            )
        endif()
    endforeach()
endfunction()

if(TARGET SDL2::SDL2)
    flashpoint_copy_runtime_files(QtFrontend $<TARGET_FILE:SDL2::SDL2>)
endif()

foreach(_flashpoint_runtime_target
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    FFmpeg::swresample
    FFmpeg::swscale
    FFmpeg::avdevice
    FFmpeg::avfilter
    FFmpeg::postproc
)
    if(TARGET ${_flashpoint_runtime_target})
        flashpoint_copy_runtime_files(QtFrontend $<TARGET_FILE:${_flashpoint_runtime_target}>)
    endif()
endforeach()

if(DEFINED FLASHPOINT_FFMPEG_ROOT AND EXISTS "${FLASHPOINT_FFMPEG_ROOT}/bin")
    file(GLOB _flashpoint_ffmpeg_runtime_files
        "${FLASHPOINT_FFMPEG_ROOT}/bin/*.dll"
    )
    flashpoint_copy_runtime_files(QtFrontend ${_flashpoint_ffmpeg_runtime_files})
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    get_filename_component(_flashpoint_compiler_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    flashpoint_copy_runtime_files(QtFrontend
        "${_flashpoint_compiler_bin_dir}/libgcc_s_seh-1.dll"
        "${_flashpoint_compiler_bin_dir}/libstdc++-6.dll"
        "${_flashpoint_compiler_bin_dir}/libwinpthread-1.dll"
    )
endif()

find_program(FLASHPOINT_WINDEPLOYQT_EXECUTABLE
    NAMES windeployqt windeployqt6
    HINTS "${FLASHPOINT_QT_ROOT}/bin"
)

if (FLASHPOINT_WINDEPLOYQT_EXECUTABLE)
    add_custom_command(TARGET QtFrontend POST_BUILD
        COMMAND "${FLASHPOINT_WINDEPLOYQT_EXECUTABLE}"
        --no-system-dxc-compiler
        $<TARGET_FILE:QtFrontend>
    )
endif()
