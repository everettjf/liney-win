# liney-win

Windows 版终端工作区(对标 macOS 的 [liney](https://github.com/everettjf/liney))。

## 当前状态

**骨架 + libghostty-vt 集成(可选开启)**:

- Win32 窗口 + 消息循环
- `IRenderer` 渲染器接口(为后续 glyph atlas 升级预留)
- `D2DRenderer`:Direct2D/DirectWrite 直绘一张等宽 cell 网格(阶段一)
- `ConPty`:Windows 伪控制台封装(起 shell + 读输出)
- `Terminal`:libghostty-vt 封装。打开 `LINEY_WITH_LIBGHOSTTY` 后接通完整链路
  **ConPTY 输出 → libghostty-vt(解析/缓冲/reflow)→ Grid → 渲染**

运行形态:
- 默认(`LINEY_WITH_LIBGHOSTTY=OFF`):仅需 MSVC,渲染一段占位文本网格,验证渲染管线
- 开启(`=ON`):跑一个真实本地 shell(`cmd.exe`),需要 **Zig 工具链**(Ghostty 用 `zig build lib-vt` 产出 `ghostty-vt`)

> ⚠️ 开发环境为 Linux,本代码**未在 Windows 工具链上编译验证**,首次构建可能需小幅修正(SDK 头文件、libghostty-vt 的 `GhosttyColorRgb` 字段布局等已在注释标出)。

## 技术选型(见调研文档)

- 底座:**自建**,终端核心(解析/缓冲/scrollback/reflow/Unicode)复用 **libghostty-vt**
- 渲染:**Win32 + Direct2D/DirectWrite 直绘 → 后期 glyph atlas + D3D11**(见 [`RENDERING.md`](./RENDERING.md))
- 范围:本地终端 MVP 起步

完整决策依据:[`RESEARCH.md`](./RESEARCH.md)、[`ALT_PLAN_SELFBUILT.md`](./ALT_PLAN_SELFBUILT.md)、[`TERMINAL_LANDSCAPE.md`](./TERMINAL_LANDSCAPE.md)。Fork Windows Terminal 的备选方案见 [`TECH_PLAN.md`](./TECH_PLAN.md)。

## 构建(Windows)

需要:Windows 10 (1809+)/11、Visual Studio 2022(Desktop C++)、CMake ≥ 3.20、Windows 10/11 SDK。

```powershell
# 骨架(仅渲染管线,无需 Zig)
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\liney_win.exe

# 接入 libghostty-vt 跑真实 shell(需 PATH 上有 zig)
cmake -B build -G "Visual Studio 17 2022" -A x64 -DLINEY_WITH_LIBGHOSTTY=ON
cmake --build build --config Debug
```

## 目录结构

```
src/
  main.cpp            入口(wWinMain)
  app/Window.*        Win32 窗口与消息循环
  render/
    Cell.h            cell / grid 数据结构
    IRenderer.h       渲染器接口
    D2DRenderer.*     Direct2D/DirectWrite 直绘实现(阶段一)
  pty/ConPty.*        ConPTY 封装(起 shell + 读输出)
  vt/Terminal.*       libghostty-vt 封装(VT 解析/缓冲 → Grid)
```

## 里程碑(自建本地终端 MVP)

- **S0** 骨架 + 渲染管线打通 ✓
- **S1** 接入 libghostty-vt,ConPTY 输出 → 网格 → 渲染(只读)← *当前(待 Windows 编译验证)*
- **S2** 键盘/IME 输入回写 ConPTY
- **S3** 滚动/scrollback、选择/复制粘贴、resize+reflow
- **S4** 配置/字体/配色,单窗口可用
- **S5** UI 外壳(tab/分屏)+ 仓库/worktree 侧边栏
