cmake_minimum_required(VERSION 3.20)
project(mumpf_windows LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(mumpf WIN32
    src/main.cpp
    src/sprite_sheet.cpp
    src/mumpf_state.cpp
    src/event_server.cpp
    src/llm_client.cpp
    src/uia_observer.cpp
    src/overlay_window.cpp
)

target_compile_definitions(mumpf PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(mumpf PRIVATE /W4 /utf-8 /permissive-)
endif()

target_link_libraries(mumpf PRIVATE
    user32
    gdi32
    shell32
    ole32
    oleaut32
    dwmapi
    d2d1
    dwrite
    windowscodecs
    winhttp
    uiautomationcore
)
