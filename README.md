# liney-win

Windows 版终端工作区(对标 macOS 的 [liney](https://github.com/everettjf/liney))。

## 当前状态

**MVP:可交互的本地终端(仅需 MSVC,已在 Windows 上编译并运行验证)**。

完整链路:**键盘 → ConPTY → 终端核心(VT 解析/屏幕缓冲)→ Grid → Direct2D/DirectWrite 渲染**。

- `Window`(Win32):窗口 + 消息循环;`WM_CHAR`/`WM_KEYDOWN` 把按键编码成 UTF-8 / xterm 转义序列回写 PTY
- `ConPty`:Windows 伪控制台封装(起 shell `cmd.exe`、读输出、回写输入、resize)
- `Terminal`:终端核心封装,两种后端二选一:
  - **默认:内置 `VTEmulator`**——自带的 xterm 子集解析器 + 屏幕缓冲(光标、UTF-8、CSI 光标移动、擦除、SGR 颜色/粗体/下划线/反显、滚动区、插入/删除行列)。**仅需 MSVC,无外部依赖。**
  - 可选:`-DLINEY_WITH_LIBGHOSTTY=ON` 接入 [libghostty-vt](https://github.com/ghostty-org/ghostty)(需 **Zig** 工具链),复用其完整缓冲/scrollback/reflow
- `D2DRenderer`:Direct2D 画背景填充 + DirectWrite 画字形,DXGI/D3D11 swap chain 呈现;画光标、反显、粗体、下划线

已验证:在 VS 2022 的 MSVC/Ninja 下构建通过,运行后窗口内 `cmd.exe` 可正常输入命令并显示输出(`echo` / `ver` / `dir` 等)。

## 技术选型(见调研文档)

- 底座:**自建**。MVP 用内置 `VTEmulator` 作终端核心以摆脱构建期外部依赖;libghostty-vt 作为可选升级(其 Windows `lib-vt` 仍待充分验证)
- 渲染:**Win32 + Direct2D/DirectWrite 直绘 → 后期 glyph atlas + D3D11**(见 [`RENDERING.md`](./RENDERING.md))
- 范围:本地终端 MVP 起步

完整决策依据:[`RESEARCH.md`](./RESEARCH.md)、[`ALT_PLAN_SELFBUILT.md`](./ALT_PLAN_SELFBUILT.md)、[`TERMINAL_LANDSCAPE.md`](./TERMINAL_LANDSCAPE.md)。Fork Windows Terminal 的备选方案见 [`TECH_PLAN.md`](./TECH_PLAN.md)。

## 构建与运行(Windows)

需要:Windows 10 (1809+)/11、Visual Studio 2022(Desktop C++)、CMake ≥ 3.20、Windows 10/11 SDK。VS 2022 已自带 CMake 与 Ninja。

```powershell
# 默认:内置终端核心,仅需 MSVC(在 “x64 Native Tools Command Prompt for VS 2022” 中)
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\liney_win.exe

# 或用 VS 生成器
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\liney_win.exe

# 可选:接入 libghostty-vt(需 PATH 上有 zig)
cmake -B build -G Ninja -DLINEY_WITH_LIBGHOSTTY=ON
cmake --build build
```

## 目录结构

```
src/
  main.cpp            入口(wWinMain)
  app/Window.*        Win32 窗口、消息循环、键盘输入编码
  render/
    Cell.h            cell / grid(含光标)数据结构
    IRenderer.h       渲染器接口
    D2DRenderer.*     Direct2D/DirectWrite 直绘实现(阶段一)
  pty/ConPty.*        ConPTY 封装(起 shell + 读输出 + 回写输入)
  vt/
    Terminal.*        终端核心封装(内置 VTEmulator 或 libghostty-vt → Grid)
    VTEmulator.*      内置 xterm 子集解析器 + 屏幕缓冲(MVP 默认核心)
```

## 里程碑(自建本地终端 MVP)

- **S0** 骨架 + 渲染管线打通 ✓
- **S1** ConPTY 输出 → 网格 → 渲染(只读)✓
- **S2** 键盘输入回写 ConPTY ✓
- **S3** 内置 VT 核心:光标、SGR 颜色/属性、擦除、滚动区、插入/删除行列 ✓(MVP)
  - 待办:scrollback 历史回滚、选择/复制粘贴、resize reflow、备用屏幕(vim/less 全屏应用)、鼠标、IME
- **S4** 配置/字体/配色,单窗口可用
- **S5** UI 外壳(tab/分屏)+ 仓库/worktree 侧边栏(liney 价值闭环)
