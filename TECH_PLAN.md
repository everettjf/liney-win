# liney-win 技术方案(Windows 版)

> 目标:在 Windows 上做出一个**好用的终端**,并在其上叠加 [liney](https://github.com/everettjf/liney) 的「终端工作区」理念(多仓库 / worktree 侧边栏、分屏、布局持久化)。
>
> 选型结论:**Fork [Windows Terminal](https://github.com/microsoft/terminal)(MIT,C++)** 作为终端底座,在其上增量开发工作区层。先交付**本地终端 MVP**。

---

## 1. 为什么是 Fork Windows Terminal

原版 liney 是 macOS 原生应用(Swift + AppKit/SwiftUI),终端内核用 Ghostty(`GhosttyKit.xcframework`)。这套在 Windows 上无法复用。

候选路线对比后(详见对话记录):

| 路线 | 结论 |
|---|---|
| libghostty-vt + 纯 C++ 自研 | libghostty-vt 只是 VT 解析器,Windows 仍未在参考实现中验证、C API 不稳定;渲染/缓冲/ConPTY/UI 全要自写,周期最长 |
| WebView2 + xterm.js | 渲染最省事,但放弃原生体验,只适合做脚手架验证 |
| **Fork Windows Terminal** ✅ | Windows 上最好的终端,MIT/C++,VT 解析 + TextBuffer + AtlasEngine 渲染 + ConPTY + tab/分屏全现成;精力集中在差异化 |

**核心理由:终端最难的两层(VT 解析 + 文本缓冲)和生产级 GPU 渲染(AtlasEngine)直接复用,我们只做工作区差异化。**

---

## 2. Windows Terminal 代码地图(集成点)

关键目录(`src/cascadia/` 为主):

| 模块 | 作用 | 我们要不要动 |
|---|---|---|
| `TerminalApp` — **TerminalPage** | XAML 应用主编排:tab、pane、命令面板等 | **要**。侧边栏在这里加,tab/pane 区右移 |
| `TerminalControl` — TermControl / ControlCore | 用户交互的 XAML 控件 + 无 UI 依赖的逻辑层 | 基本不动(直接复用一个终端实例) |
| `src/renderer/atlas` — **AtlasEngine** | glyph atlas + D3D11 的 GPU 渲染 | 不动(白嫖) |
| `TerminalCore` | buffer、颜色表、VT 解析、输入 | 不动 |
| `TerminalConnection` — ConptyConnection | 终端后端连接(本地用 ConPTY) | MVP 后扩展:SSH / agent 连接在这里加新实现 |
| `WindowsTerminal` — IslandWindow / NonClientIslandWindow / AppHost | Win32 宿主窗口、自定义标题栏 | 少量动(品牌、窗口) |
| `CascadiaPackage` | MSIX 打包工程 | 改为 liney-win 包标识 |

**liney 工作区概念 → WT 落点映射:**

- 多仓库 / worktree 侧边栏 → `TerminalApp` 里新增一个左侧 XAML 面板(可折叠),作为工作区容器
- 「点仓库 → 右侧开终端且 cwd 切过去」→ 调用 TerminalPage 现有的「新建 tab/pane」逻辑,传入起始目录
- 布局持久化 → 扩展 WT 已有的窗口/布局状态持久化
- 混合会话(本地/SSH/agent)→ 新增 `TerminalConnection` 实现(MVP 之后)
- Git / worktree 操作 → 新增服务模块,shell out 调 `git`

---

## 3. 构建环境

- Windows 10 (1809+) / 11,**x64**(WT 是 C++,不能 Any CPU)
- **Visual Studio 2022**,工作负载:Desktop development with C++ + Universal Windows Platform development
- Windows 10/11 SDK(跟随 WT 仓库 `.vsconfig` 指定的版本)
- 构建:`OpenConsole.slnx`,VS 内 F5,或命令行 `Import-Module .\tools\OpenConsole.psm1; Set-MsBuildDevEnvironment; Invoke-OpenConsoleBuild`
- 本地调试:右键 `CascadiaPackage` → 属性 → Debug 把 Application/Background process 设为 Native Only,F5
- ⚠️ 本地运行需要 MSIX 包部署(开发者模式),不是双击 exe

---

## 4. Fork 与上游同步策略

1. `liney-win` 仓库添加 `upstream = microsoft/terminal` remote,首次 squash/导入其源码到本仓库
2. **改动尽量隔离**:新功能放新文件/新目录(如 `src/cascadia/TerminalApp/Workspace*`),少改 WT 原文件,降低 merge 冲突
3. 定期 `git fetch upstream` 并 rebase/merge,跟上安全与渲染改进
4. **品牌剥离(MIT 允许 fork,但「Windows Terminal」名称与图标是微软商标)**:改产品名、应用图标、包标识(Package Identity)、关于页、移除微软商标资源

---

## 5. 本地终端 MVP — 里程碑拆解

> 范围:**仅本地 shell**(ConPTY),不含 SSH/agent/tmux/自动更新(后续阶段)。

| 里程碑 | 内容 | 验收 |
|---|---|---|
| **M0 构建打通** | Fork 源码,本地构建出原版 WT 并运行 | F5 能跑起原终端 |
| **M1 品牌化** | 改名 liney-win、图标、包标识,去微软商标 | 跑起来是 liney-win,功能同原版 |
| **M2 侧边栏骨架** | TerminalApp 加左侧可折叠面板(空壳),tab/pane 区右移 | 能看到/折叠侧边栏,终端正常 |
| **M3 工作区闭环** | 侧边栏列出「仓库/worktree」,点击在右侧新开 tab 且 cwd 切到该路径 | 点目录 → 新终端在该目录 |
| **M4 布局持久化** | 关闭重开后恢复 pane 布局 / 工作区 | 回到仓库恢复上次布局 |

M0–M1 是「把巨人搬进门」,M2–M4 才开始体现 liney 价值。完成 M4 即得到一个**能用的、带工作区雏形的本地终端**。

---

## 6. 风险与规避

| 风险 | 规避 |
|---|---|
| WT 构建链重、对 SDK/nuget 版本敏感 | 严格按仓库 `.vsconfig`,M0 先把构建跑绿再动代码 |
| C++/WinRT + XAML 学习曲线 | 集成面主要是 TerminalPage 的 XAML/编排,先吃透这一处 |
| 改 WT 原文件导致上游同步冲突 | 新功能隔离到新文件;原文件最小侵入 |
| 商标 | 完整剥离微软品牌后再分发 |
| 后续 SSH/agent | 走 TerminalConnection 扩展点,架构已预留 |

---

## 7. 后续阶段(MVP 之后)

- SSH / agent 会话(新 TerminalConnection 实现)+ 远程文件浏览
- Git/worktree 深度集成、diff、history 可视化
- 生命周期 hooks、`liney notify` 风格通知
- 自动更新(Windows 用 MSIX / Squirrel / WinGet,替代 macOS 的 Sparkle)

---

## 参考

- Windows Terminal 源码:https://github.com/microsoft/terminal
- 构建文档:https://github.com/microsoft/terminal/blob/main/doc/building.md
- 代码组织:https://github.com/microsoft/terminal/blob/main/doc/ORGANIZATION.md
- AtlasEngine:`src/renderer/atlas/`
- 原版 liney:https://github.com/everettjf/liney
