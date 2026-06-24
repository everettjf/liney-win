# liney-win 调研记录

> 本文记录 Windows 版立项前的技术调研:原项目分析、技术路线对比、以及最终选型的决策依据。规划与落地方案见 [`TECH_PLAN.md`](./TECH_PLAN.md)。

---

## 1. 原项目分析:liney(macOS)

[everettjf/liney](https://github.com/everettjf/liney) 是一个 **macOS 原生的「终端工作区」应用**,面向同时管理多个仓库、worktree、分支、分屏的开发者。

**技术栈**
- 语言/框架:Swift(~95%)+ AppKit + SwiftUI
- 终端内核:**Ghostty**,通过 vendored `GhosttyKit.xcframework`(通用二进制 arm64 + x86_64)
- 更新:Sparkle(签名更新)
- 持久化:`~/.liney/`
- 平台要求:macOS 14.6+

**核心功能**
- 侧边栏统一管理多仓库 / worktree
- 分屏布局,随 worktree 切换持久化恢复
- 混合会话:本地 shell / SSH / agent,文件浏览跟随活动 pane
- 生命周期 hooks(app/session 启停时执行命令)
- agent 通知(`liney notify` CLI + OSC 序列 → 灵动岛)

**源码结构**
- `Liney/App` `Domain` `Persistence` `Services` `Support` `UI` `Vendor/GhosttyKit.xcframework`
- `Services/`:`AgentNotify` `Git` `Hooks` `Process` `SFTP` `SSH` `Terminal` `Tmux` `Updates`
- `UI/`:`Sidebar` `Workspace` `Canvas` `Diff` `History` `Island` `Orchestration` `Overview` `Sheets` `Components`

**对 Windows 移植的含义**:UI 层(AppKit/SwiftUI)、终端内核(GhosttyKit)、更新(Sparkle)在 Windows 上**均无法复用**,需另选底座。Git/Process/SSH 等是跨平台概念但需重新实现。

---

## 2. 终端内核路线对比

移植的核心难点是「终端内核 + 渲染」。评估了以下路线:

### 2.1 libghostty / libghostty-vt(与原项目同源)
- **libghostty-vt**:仅 VT 解析器(状态机、转义序列、key encoder)。C ABI,库级别支持 Windows,**但参考实现 Ghostling 尚未在 Windows 验证,C API 明确不稳定、有破坏性变更**。
- **完整 libghostty**(带渲染/窗口):跨平台不稳定,**只有 macOS 的 GhosttyKit 成熟**。
- 结论:能拿到的跨平台部分**只是个解析器**,不提供渲染/窗口/ConPTY。用它仍要自写渲染栈。
- 旁支:[`InsipidPoint/ghostty-windows`](https://github.com/InsipidPoint/ghostty-windows) 是 Zig 写的完整 Windows fork(Win32+OpenGL+ConPTY),但**是独立 App 不是可嵌入库**,且语言是 Zig。

### 2.2 渲染层有没有现成的
- **无**「输入 libghostty-vt 输出 → 渲染终端网格」的现成 C++ 库。
- 最现成但替掉解析器:**WebView2 + xterm.js**(解析+渲染+输入全包),代价是放弃同源、放弃原生。
- 保留解析器前提下最现成:**DirectWrite/Direct2D 直绘**(字形栅格化白嫖,自写几百行 cell 循环)。
- 生产级参考:**Windows Terminal 的 AtlasEngine**(MIT)——DirectWrite 栅格化进 glyph atlas + D3D11 + HLSL shader 贴格子。但与 WT 的 `til`/`TextBuffer` 深度耦合,**不是独立可 link 的库**。

### 2.3 关键认知
> AtlasEngine 只是「渲染层」,不是完整终端。一个好用的终端 = VT 解析 + TextBuffer + 渲染 + ConPTY/UI 四层。单独「走 AtlasEngine」要么 fork 整个 WT 拿到全部四层,要么自建其余三层。

---

## 3. 决策记录

| 决策点 | 选择 | 依据 |
|---|---|---|
| 是否绑定 Ghostty 同源 | **否** | libghostty-vt 只是解析器、Windows 未验证、API 不稳定;核心目标是「好用的终端」而非同源 |
| 主力语言 | **C++** | 直接吃 C API、Windows 原生图形/ConPTY 同栈、贴合 Windows 生态 |
| 终端底座 | **Fork Windows Terminal** | Windows 上最好的终端,MIT/C++,VT 解析 + TextBuffer + AtlasEngine + ConPTY + tab/分屏全现成;精力集中在工作区差异化 |
| 第一阶段范围 | **本地终端 MVP** | 仅本地 shell,先打通「工作区 → 终端」闭环,SSH/agent/tmux/更新留待后续 |

**被否决的路线**
- 纯 C++ + libghostty-vt 自研:渲染/缓冲/ConPTY 全自写,周期最长、扛不稳定 API
- WebView2 + xterm.js 作为终态:放弃原生体验(仅作脚手架验证可选)
- 基于 ghostty-windows fork 二开:主力语言 Zig,与「用 C++」诉求不符

---

## 4. 结论

**Fork Windows Terminal,用 C++ 在其上增量开发 liney 风格的工作区层,先交付本地终端 MVP。** 终端最难的解析、缓冲、GPU 渲染三层直接复用,差异化精力投入到侧边栏 / 多仓库 / worktree / 布局持久化。

详细工程方案(代码地图、构建环境、Fork 同步策略、里程碑、风险)见 [`TECH_PLAN.md`](./TECH_PLAN.md)。
