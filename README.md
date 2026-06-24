# liney-win

Windows 版终端工作区(对标 macOS 的 [liney](https://github.com/everettjf/liney))。

## 当前状态

**初始骨架(scaffold)** —— 落地了渲染架构的最小可构建版本:

- Win32 窗口 + 消息循环
- `IRenderer` 渲染器接口(为后续 glyph atlas 升级预留)
- `D2DRenderer`:Direct2D/DirectWrite 直绘一张等宽 cell 网格(阶段一)
- `ConPty`:Windows 伪控制台封装(已实现,**尚未接入渲染**)

> ⚠️ 本骨架面向 **Windows + VS2022/CMake** 构建,尚未接入 libghostty-vt,ConPTY 也未与网格联动。当前可运行效果:开窗 + 渲染一段占位文本网格,验证渲染管线。
> 注:开发环境为 Linux,以下代码**未在 Windows 工具链上编译验证**,可能需小幅修正。

## 技术选型(见调研文档)

- 底座:**自建**,终端核心(解析/缓冲/scrollback/reflow/Unicode)复用 **libghostty-vt**
- 渲染:**Win32 + Direct2D/DirectWrite 直绘 → 后期 glyph atlas + D3D11**(见 [`RENDERING.md`](./RENDERING.md))
- 范围:本地终端 MVP 起步

完整决策依据:[`RESEARCH.md`](./RESEARCH.md)、[`ALT_PLAN_SELFBUILT.md`](./ALT_PLAN_SELFBUILT.md)、[`TERMINAL_LANDSCAPE.md`](./TERMINAL_LANDSCAPE.md)。Fork Windows Terminal 的备选方案见 [`TECH_PLAN.md`](./TECH_PLAN.md)。

## 构建(Windows)

需要:Windows 10 (1809+)/11、Visual Studio 2022(Desktop C++)、CMake ≥ 3.20、Windows 10/11 SDK。

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
.\build\Debug\liney_win.exe
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
  pty/ConPty.*        ConPTY 封装(待接入)
```

## 里程碑(自建本地终端 MVP)

- **S0** 骨架 + 渲染管线打通 ← *当前*
- **S1** 接入 libghostty-vt,ConPTY 输出 → 网格 → 渲染(只读)
- **S2** 键盘/IME 输入回写 ConPTY
- **S3** 滚动/scrollback、选择/复制粘贴、resize+reflow
- **S4** 配置/字体/配色,单窗口可用
- **S5** UI 外壳(tab/分屏)+ 仓库/worktree 侧边栏
