cmake_minimum_required(VERSION 3.16)
project(OllamaPlusGUI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(OllamaPlusGUI WIN32
    main.cpp
    resources.rc
)

target_compile_definitions(OllamaPlusGUI PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

target_link_libraries(OllamaPlusGUI PRIVATE
    comctl32
    comdlg32
    winhttp
    shell32
    shlwapi
    ole32
    ws2_32
    dxgi
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
