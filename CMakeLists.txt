cmake_minimum_required(VERSION 3.16)
project(OllamaPlusGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(OllamaPlusGUI WIN32
    main.cpp
    resources.rc
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
)

target_include_directories(OllamaPlusGUI PRIVATE
    third_party/imgui
    third_party/imgui/backends
)

target_compile_definitions(OllamaPlusGUI PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(OllamaPlusGUI PRIVATE
    d3d11
    dxgi
    d3dcompiler
    dwmapi
    uuid
    ole32
    comdlg32
    winhttp
    shell32
    shlwapi
    ws2_32
    imm32`r`n    windowscodecs`r`n    ole32
)

if(MSVC)
    target_compile_options(OllamaPlusGUI PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(OllamaPlusGUI PRIVATE -Wall -Wextra -Wpedantic -O2 -municode)
    target_link_options(OllamaPlusGUI PRIVATE
        -municode
        -static
        -static-libgcc
        -static-libstdc++
        "-specs=${CMAKE_CURRENT_SOURCE_DIR}/no-default-manifest.specs"
    )
endif()

