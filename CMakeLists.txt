cmake_minimum_required(VERSION 3.16)

project(FlashpointShortVideos)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_PREFIX_PATH "D:/itffmpeg/Qt/6.10.2/llvm-mingw_64")

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Sql)

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

if (EXISTS "D:/itffmpeg/Qt/6.10.2/llvm-mingw_64/bin/windeployqt.exe")
    add_custom_command(TARGET QtFrontend POST_BUILD
        COMMAND D:/itffmpeg/Qt/6.10.2/llvm-mingw_64/bin/windeployqt.exe
        $<TARGET_FILE:QtFrontend>
    )
endif()
