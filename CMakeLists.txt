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
  src/app/WindowFind.cpp
  src/app/WindowPalette.cpp
  src/app/WindowSession.cpp
  src/app/WindowAi.cpp
  src/app/SettingsDialog.cpp
  src/app/Tab.cpp
  src/core/TerminalSession.cpp
  src/core/Config.cpp
  src/core/ShellProfiles.cpp
  src/core/KeyBinding.cpp
  src/core/SshProfiles.cpp
  src/core/Update.cpp
  src/core/Ai.cpp
  src/core/CommandHistory.cpp
  src/render/D2DRenderer.cpp
  src/pty/ConPty.cpp
  src/vt/Terminal.cpp
  src/vt/OscParser.cpp
  src/workspace/Workspace.cpp
  src/workspace/GitStatusParser.cpp
  src/util/Process.cpp
  src/util/Json.cpp
  src/util/InputBox.cpp
  src/util/Dialogs.cpp
  src/util/Base64.cpp
  src/util/Http.cpp
  src/util/Diagnostics.cpp
  src/util/Authenticode.cpp
  src/util/AccessibilityProvider.cpp
)

# The GUI binary is Liney.exe. It also serves as the companion CLI: invoked as
# `liney notify ...` it emits OSC sequences a Liney pane turns into
# notifications (see src/main.cpp). One binary avoids a separate liney.exe,
# which would collide with Liney.exe on Windows' case-insensitive filesystem.
set_target_properties(liney_win PROPERTIES OUTPUT_NAME "Liney")

target_include_directories(liney_win PRIVATE src)

target_compile_definitions(liney_win PRIVATE
  UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
  WINVER=0x0A00 _WIN32_WINNT=0x0A00)

target_link_libraries(liney_win PRIVATE
  d2d1 dwrite d3d11 dxgi imm32 winhttp bcrypt windowscodecs ole32 oleaut32
  comdlg32 shell32 dbghelp wintrust crypt32 uiautomationcore)

# resource.rc already embeds our compatibility/DPI/long-path manifest. MSVC's
# default linker manifest would create a second RT_MANIFEST/1 resource and make
# cvtres fail with CVT1100.
if(MSVC)
  target_link_options(liney_win PRIVATE /MANIFEST:NO)
endif()

# Ship the MSVC runtime app-locally. A clean Windows 10/11 installation is not
# guaranteed to have the Visual C++ Redistributable, and both Liney and the
# Ghostty DLL depend on these files. Keeping them next to Liney.exe makes the
# installer and portable zip genuinely self-contained.
include(InstallRequiredSystemLibraries)
foreach(runtime_dll IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
  add_custom_command(TARGET liney_win POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${runtime_dll}" "$<TARGET_FILE_DIR:liney_win>"
    COMMENT "Copying MSVC runtime: ${runtime_dll}")
endforeach()

# Fetch Ghostty and build its libghostty-vt (via Zig) as the terminal core.
include(FetchContent)
set(LINEY_GHOSTTY_REVISION
    "a887df42c56f6de86c0fe6da9c4eeca37931e083"
    CACHE STRING "Verified Ghostty commit used to build libghostty-vt")
FetchContent_Declare(ghostty
  GIT_REPOSITORY https://github.com/ghostty-org/ghostty.git
  GIT_TAG ${LINEY_GHOSTTY_REVISION}
  GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(ghostty)

# `ghostty-vt` is the target produced by Ghostty's CMake (delegates to
# `zig build lib-vt`); it also supplies the include directory.
target_link_libraries(liney_win PRIVATE ghostty-vt)

# ghostty-vt is a DLL; copy it next to Liney.exe so the app runs in-place.
add_custom_command(TARGET liney_win POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${ghostty_SOURCE_DIR}/zig-out/bin/ghostty-vt.dll"
    "$<TARGET_FILE_DIR:liney_win>"
  COMMENT "Copying ghostty-vt.dll next to Liney.exe")
