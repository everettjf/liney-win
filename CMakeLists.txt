cmake_minimum_required(VERSION 3.20)
project(liney_win LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
  message(FATAL_ERROR
    "liney-win targets Windows only (Win32 + Direct2D/Direct3D 11 + ConPTY).")
endif()

add_executable(liney_win WIN32
  src/main.cpp
  src/app/Window.cpp
  src/render/D2DRenderer.cpp
  src/pty/ConPty.cpp
)

target_include_directories(liney_win PRIVATE src)

target_compile_definitions(liney_win PRIVATE
  UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

target_link_libraries(liney_win PRIVATE
  d2d1 dwrite d3d11 dxgi)
