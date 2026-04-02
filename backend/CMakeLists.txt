add_library(ServoBackend STATIC
    domain/user/User.hpp
    domain/session/Session.hpp
    repository/user/UserRepository.hpp
    repository/session/SessionRepository.hpp
    infrastructure/database/InMemoryUserRepository.hpp
    infrastructure/database/InMemoryUserRepository.cpp
    infrastructure/database/SQLiteDatabase.hpp
    infrastructure/database/SQLiteDatabase.cpp
    infrastructure/database/SQLiteUserRepository.hpp
    infrastructure/database/SQLiteUserRepository.cpp
    infrastructure/database/SQLiteSessionRepository.hpp
    infrastructure/database/SQLiteSessionRepository.cpp
    service/auth/AuthResult.hpp
    service/auth/AuthService.hpp
    service/auth/AuthService.cpp
    controller/auth/AuthController.hpp
    controller/auth/AuthController.cpp
    liveplayer/decode/FlvVideoDecoder.hpp
    liveplayer/decode/FlvVideoDecoder.cpp
    liveplayer/logging/LiveWatchLogger.hpp
    liveplayer/logging/LiveWatchLogger.cpp
    liveplayer/protocol/FlvTypes.hpp
    liveplayer/protocol/FlvDemuxer.hpp
    liveplayer/protocol/FlvDemuxer.cpp
    liveplayer/protocol/HttpFlvStreamReader.hpp
    liveplayer/protocol/HttpFlvStreamReader.cpp
    liveplayer/session/LiveVideoDecodeWorker.hpp
    liveplayer/session/LiveVideoDecodeWorker.cpp
    liveplayer/session/VideoFrameQueue.hpp
    liveplayer/session/LivePlayerSession.hpp
    liveplayer/session/LivePlayerSession.cpp
    liveplayer/service/LivePlayerController.hpp
    liveplayer/service/LivePlayerController.cpp
    playercontroller/service/PlayerController.hpp
    playercontroller/service/PlayerController.cpp
    playerEngine/ff_ffplay_def.cpp
    playerEngine/ff_ffplay_def.hpp
    playerEngine/FFMessage.hpp
    playerEngine/FFMessageQueue.cpp
    playerEngine/FFMessageQueue.hpp
    playerEngine/FFplayer.cpp
    playerEngine/FFplayer.hpp
    playerEngine/IjkMediaPlayer.hpp
    playerEngine/IjkMediaPlayer.cpp
)

function(flashpoint_import_shared_library target_name import_lib runtime_dll)
    if(NOT TARGET ${target_name})
        add_library(${target_name} SHARED IMPORTED GLOBAL)
        set_target_properties(${target_name} PROPERTIES
            IMPORTED_IMPLIB "${import_lib}"
            IMPORTED_LOCATION "${runtime_dll}"
        )
    endif()
endfunction()

set(_flashpoint_default_ffmpeg_root "")
set(_flashpoint_local_ffmpeg6_root "${CMAKE_CURRENT_SOURCE_DIR}/include/ffmpeg6.0")
set(_flashpoint_external_ffmpeg6_root "D:/itffmpeg/av_media/online/08-03-muxing_flv/ffmpeg6.0")
set(_flashpoint_legacy_ffmpeg_root "${CMAKE_CURRENT_SOURCE_DIR}/include/ffmpeg-4.2.1-win32-dev")
set(DLL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/dll")

if(EXISTS "${_flashpoint_local_ffmpeg6_root}/lib/avcodec.lib")
    set(_flashpoint_default_ffmpeg_root "${_flashpoint_local_ffmpeg6_root}")
elseif(EXISTS "${_flashpoint_external_ffmpeg6_root}/lib/avcodec.lib")
    set(_flashpoint_default_ffmpeg_root "${_flashpoint_external_ffmpeg6_root}")
elseif(EXISTS "${_flashpoint_legacy_ffmpeg_root}/lib/avcodec.lib")
    set(_flashpoint_default_ffmpeg_root "${_flashpoint_legacy_ffmpeg_root}")
endif()

set(FLASHPOINT_FFMPEG_ROOT "${_flashpoint_default_ffmpeg_root}" CACHE PATH
    "Path to the FFmpeg development package used by playerEngine.")

if(FLASHPOINT_FFMPEG_ROOT STREQUAL "${_flashpoint_external_ffmpeg6_root}"
   AND EXISTS "${_flashpoint_local_ffmpeg6_root}/lib/avcodec.lib")
    set(FLASHPOINT_FFMPEG_ROOT "${_flashpoint_local_ffmpeg6_root}" CACHE PATH
        "Path to the FFmpeg development package used by playerEngine." FORCE)
endif()

set(_flashpoint_default_sdl2_root "${CMAKE_CURRENT_SOURCE_DIR}/include/SDL2")
if(EXISTS "${FLASHPOINT_FFMPEG_ROOT}/lib/SDL2.lib")
    set(_flashpoint_default_sdl2_root "${FLASHPOINT_FFMPEG_ROOT}")
endif()

set(FLASHPOINT_SDL2_ROOT "${_flashpoint_default_sdl2_root}" CACHE PATH
    "Path to the SDL2 development package used by playerEngine.")

if(FLASHPOINT_SDL2_ROOT STREQUAL "${_flashpoint_external_ffmpeg6_root}"
   AND EXISTS "${_flashpoint_local_ffmpeg6_root}/lib/SDL2.lib")
    set(FLASHPOINT_SDL2_ROOT "${_flashpoint_local_ffmpeg6_root}" CACHE PATH
        "Path to the SDL2 development package used by playerEngine." FORCE)
endif()

if(EXISTS "${FLASHPOINT_FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    set(FLASHPOINT_FFMPEG_INCLUDE_DIR "${FLASHPOINT_FFMPEG_ROOT}/include")
else()
    set(FLASHPOINT_FFMPEG_INCLUDE_DIR "")
endif()

if(EXISTS "${FLASHPOINT_SDL2_ROOT}/include/SDL2/SDL.h")
    set(FLASHPOINT_SDL2_INCLUDE_DIR "${FLASHPOINT_SDL2_ROOT}/include/SDL2")
elseif(EXISTS "${FLASHPOINT_SDL2_ROOT}/include/SDL.h")
    set(FLASHPOINT_SDL2_INCLUDE_DIR "${FLASHPOINT_SDL2_ROOT}/include")
else()
    set(FLASHPOINT_SDL2_INCLUDE_DIR "")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_flashpoint_sdl2_arch_dir "x64")
else()
    set(_flashpoint_sdl2_arch_dir "x86")
endif()

set(FLASHPOINT_SDL2_LIBRARY "")
set(FLASHPOINT_SDL2_RUNTIME "")
if(EXISTS "${FLASHPOINT_SDL2_ROOT}/lib/SDL2.lib")
    set(FLASHPOINT_SDL2_LIBRARY "${FLASHPOINT_SDL2_ROOT}/lib/SDL2.lib")
    set(FLASHPOINT_SDL2_RUNTIME "${FLASHPOINT_SDL2_ROOT}/bin/SDL2.dll")
elseif(EXISTS "${FLASHPOINT_SDL2_ROOT}/lib/${_flashpoint_sdl2_arch_dir}/SDL2.lib")
    set(FLASHPOINT_SDL2_LIBRARY "${FLASHPOINT_SDL2_ROOT}/lib/${_flashpoint_sdl2_arch_dir}/SDL2.lib")
    set(FLASHPOINT_SDL2_RUNTIME "${FLASHPOINT_SDL2_ROOT}/lib/${_flashpoint_sdl2_arch_dir}/SDL2.dll")
endif()

set(_flashpoint_ffmpeg_lib_dir "${FLASHPOINT_FFMPEG_ROOT}/lib")
set(_flashpoint_ffmpeg_bin_dir "${FLASHPOINT_FFMPEG_ROOT}/bin")

set(_flashpoint_required_ffmpeg_import_libs
    "${_flashpoint_ffmpeg_lib_dir}/avcodec.lib"
    "${_flashpoint_ffmpeg_lib_dir}/avformat.lib"
    "${_flashpoint_ffmpeg_lib_dir}/avutil.lib"
    "${_flashpoint_ffmpeg_lib_dir}/swresample.lib"
    "${_flashpoint_ffmpeg_lib_dir}/swscale.lib"
)

set(_flashpoint_missing_playerengine_deps "")

if(FLASHPOINT_FFMPEG_INCLUDE_DIR STREQUAL "")
    list(APPEND _flashpoint_missing_playerengine_deps
        "FFmpeg headers not found under ${FLASHPOINT_FFMPEG_ROOT}")
endif()

if(FLASHPOINT_SDL2_INCLUDE_DIR STREQUAL "")
    list(APPEND _flashpoint_missing_playerengine_deps
        "SDL2 headers not found under ${FLASHPOINT_SDL2_ROOT}")
endif()

if(FLASHPOINT_SDL2_LIBRARY STREQUAL "")
    list(APPEND _flashpoint_missing_playerengine_deps
        "SDL2 import library not found under ${FLASHPOINT_SDL2_ROOT}")
endif()

foreach(_flashpoint_required_lib IN LISTS _flashpoint_required_ffmpeg_import_libs)
    if(NOT EXISTS "${_flashpoint_required_lib}")
        list(APPEND _flashpoint_missing_playerengine_deps
            "Missing FFmpeg import library: ${_flashpoint_required_lib}")
    endif()
endforeach()

if(_flashpoint_missing_playerengine_deps)
    string(JOIN "\n  - " _flashpoint_dependency_summary ${_flashpoint_missing_playerengine_deps})
    message(FATAL_ERROR
        "playerEngine native dependencies are incomplete:\n  - ${_flashpoint_dependency_summary}")
endif()

flashpoint_import_shared_library(
    SDL2::SDL2
    "${FLASHPOINT_SDL2_LIBRARY}"
    "${FLASHPOINT_SDL2_RUNTIME}"
)

flashpoint_import_shared_library(
    FFmpeg::avcodec
    "${_flashpoint_ffmpeg_lib_dir}/avcodec.lib"
    "${_flashpoint_ffmpeg_bin_dir}/avcodec-60.dll"
)
flashpoint_import_shared_library(
    FFmpeg::avformat
    "${_flashpoint_ffmpeg_lib_dir}/avformat.lib"
    "${_flashpoint_ffmpeg_bin_dir}/avformat-60.dll"
)
flashpoint_import_shared_library(
    FFmpeg::avutil
    "${_flashpoint_ffmpeg_lib_dir}/avutil.lib"
    "${_flashpoint_ffmpeg_bin_dir}/avutil-58.dll"
)
flashpoint_import_shared_library(
    FFmpeg::swresample
    "${_flashpoint_ffmpeg_lib_dir}/swresample.lib"
    "${_flashpoint_ffmpeg_bin_dir}/swresample-4.dll"
)
flashpoint_import_shared_library(
    FFmpeg::swscale
    "${_flashpoint_ffmpeg_lib_dir}/swscale.lib"
    "${_flashpoint_ffmpeg_bin_dir}/swscale-7.dll"
)
if(EXISTS "${_flashpoint_ffmpeg_lib_dir}/avdevice.lib" AND EXISTS "${_flashpoint_ffmpeg_bin_dir}/avdevice-60.dll")
    flashpoint_import_shared_library(
        FFmpeg::avdevice
        "${_flashpoint_ffmpeg_lib_dir}/avdevice.lib"
        "${_flashpoint_ffmpeg_bin_dir}/avdevice-60.dll"
    )
endif()
if(EXISTS "${_flashpoint_ffmpeg_lib_dir}/avfilter.lib" AND EXISTS "${_flashpoint_ffmpeg_bin_dir}/avfilter-9.dll")
    flashpoint_import_shared_library(
        FFmpeg::avfilter
        "${_flashpoint_ffmpeg_lib_dir}/avfilter.lib"
        "${_flashpoint_ffmpeg_bin_dir}/avfilter-9.dll"
    )
endif()
if(EXISTS "${_flashpoint_ffmpeg_lib_dir}/postproc.lib" AND EXISTS "${_flashpoint_ffmpeg_bin_dir}/postproc-57.dll")
    flashpoint_import_shared_library(
        FFmpeg::postproc
        "${_flashpoint_ffmpeg_lib_dir}/postproc.lib"
        "${_flashpoint_ffmpeg_bin_dir}/postproc-57.dll"
    )
endif()

target_include_directories(ServoBackend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${FLASHPOINT_FFMPEG_INCLUDE_DIR}
        ${FLASHPOINT_SDL2_INCLUDE_DIR}
)

target_link_libraries(ServoBackend
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Sql
        Qt6::Widgets
        SDL2::SDL2
        FFmpeg::avcodec
        FFmpeg::avformat
        FFmpeg::avutil
        FFmpeg::swresample
        FFmpeg::swscale
)

if(TARGET FFmpeg::avdevice)
    target_link_libraries(ServoBackend PUBLIC FFmpeg::avdevice)
endif()

if(TARGET FFmpeg::avfilter)
    target_link_libraries(ServoBackend PUBLIC FFmpeg::avfilter)
endif()

if(TARGET FFmpeg::postproc)
    target_link_libraries(ServoBackend PUBLIC FFmpeg::postproc)
endif()

message(STATUS "playerEngine FFmpeg root: ${FLASHPOINT_FFMPEG_ROOT}")
message(STATUS "playerEngine SDL2 root: ${FLASHPOINT_SDL2_ROOT}")
