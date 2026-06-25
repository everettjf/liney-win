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

# libghostty-vt is the terminal core (parsing/buffer/scrollback/reflow/Unicode).
# It is fetched from Ghostty and built via Zig, so a Zig 0.15.2 toolchain must be
# on PATH to build liney-win. See README for the build steps.
find_program(ZIG_EXECUTABLE zig)
if(NOT ZIG_EXECUTABLE)
  message(FATAL_ERROR
    "Zig not found. liney-win's terminal core (libghostty-vt) is built via Zig; "
    "install Zig 0.15.2 and put it on PATH. https://ziglang.org/download/")
endif()

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

# Fetch Ghostty and build its libghostty-vt (via Zig) as the terminal core.
include(FetchContent)
FetchContent_Declare(ghostty
  GIT_REPOSITORY https://github.com/ghostty-org/ghostty.git
  GIT_TAG main)
FetchContent_MakeAvailable(ghostty)

# `ghostty-vt` is the target produced by Ghostty's CMake (delegates to
# `zig build lib-vt`); it also supplies the include directory.
target_link_libraries(liney_win PRIVATE ghostty-vt)

# ghostty-vt is a DLL; copy it next to liney_win.exe so the app runs in-place.
add_custom_command(TARGET liney_win POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${ghostty_SOURCE_DIR}/zig-out/bin/ghostty-vt.dll"
    "$<TARGET_FILE_DIR:liney_win>"
  COMMENT "Copying ghostty-vt.dll next to liney_win.exe")
