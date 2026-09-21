cmake_minimum_required(VERSION 3.5.0)
project(thextech)

set(NATIVERENDER_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR})

include_directories(
    ${NATIVERENDER_ROOT_PATH}
    ${NATIVERENDER_ROOT_PATH}/sdl2_include/SDL2
)

add_library(entry SHARED
    ${NATIVERENDER_ROOT_PATH}/thextech_main.cpp
)

target_link_libraries(entry PUBLIC

    ${NATIVERENDER_ROOT_PATH}/libSDL2.a
    ${NATIVERENDER_ROOT_PATH}/libthextech.so
    libace_napi.z.so
    libhilog_ndk.z.so
    libEGL.so
    libGLESv3.so
    libnative_window.so
    libohaudio.so
    libz.so
    libm.so
)
