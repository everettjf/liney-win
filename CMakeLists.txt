cmake_minimum_required(VERSION 3.20)
project(liney_win LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files are UTF-8; tell MSVC so wide literals (em-dash, ellipsis, …) are
# decoded correctly instead of as the system codepage (which caused mojibake in
# the window/tab titles).
if(MSVC)
  add_compile_options(/utf-8)
endif()

if(NOT WIN32)
  message(FATAL_ERROR
    "liney-win targets Windows only (Win32 + Direct2D/Direct3D 11 + ConPTY).")
endif()

# libghostty-vt provides the terminal core (parsing/buffer/scrollback/reflow).
# OFF by default so the scaffold builds with just MSVC; ON fetches Ghostty and
# builds ghostty-vt via Zig (the Zig toolchain must be on PATH).
option(LINEY_WITH_LIBGHOSTTY "Integrate libghostty-vt (requires Zig)" OFF)

add_executable(liney_win WIN32
  res/resource.rc
  src/main.cpp
  src/app/Window.cpp
  src/app/WindowRender.cpp
  src/app/WindowInput.cpp
  src/app/WindowMouse.cpp
  src/app/WindowSession.cpp
  src/app/Tab.cpp
  src/core/TerminalSession.cpp
  src/core/Config.cpp
  src/render/D2DRenderer.cpp
  src/pty/ConPty.cpp
  src/vt/Terminal.cpp
  src/vt/VTEmulator.cpp
  src/vt/VTParser.cpp
  src/workspace/Workspace.cpp
  src/util/Process.cpp
  src/util/Json.cpp
  src/util/InputBox.cpp
  src/util/Dialogs.cpp
  src/util/Http.cpp
)

target_include_directories(liney_win PRIVATE src)

target_compile_definitions(liney_win PRIVATE
  UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

target_link_libraries(liney_win PRIVATE
  d2d1 dwrite d3d11 dxgi imm32 winhttp windowscodecs ole32 comdlg32 shell32)

# Companion CLI: liney.exe (console). `liney notify ...` emits OSC sequences a
# liney-win pane turns into notifications.
add_executable(liney_cli src/cli/main.cpp)
set_target_properties(liney_cli PROPERTIES OUTPUT_NAME liney)
target_compile_definitions(liney_cli PRIVATE UNICODE _UNICODE)

if(LINEY_WITH_LIBGHOSTTY)
  include(FetchContent)
  FetchContent_Declare(ghostty
    GIT_REPOSITORY https://github.com/ghostty-org/ghostty.git
    GIT_TAG main)
  FetchContent_MakeAvailable(ghostty)

  target_compile_definitions(liney_win PRIVATE LINEY_WITH_LIBGHOSTTY)
  # `ghostty-vt` is the target produced by Ghostty's CMake (delegates to
  # `zig build lib-vt`); it also supplies the include directory.
  target_link_libraries(liney_win PRIVATE ghostty-vt)
endif()
