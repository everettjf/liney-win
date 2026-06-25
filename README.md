# liney-win

Windows 版终端工作区(对标 macOS 的 [liney](https://github.com/everettjf/liney))。

## 当前状态

**MVP:可交互的本地终端工作区——多标签 / 分屏 / 仓库·worktree 侧边栏(仅需 MSVC,已在 Windows 上编译并运行验证)**。

界面布局(全自绘 Win32 + Direct2D,无 XAML):

```
┌──────────────┬───────────────────────────────┐
│  WORKSPACE   │  tab1 │ tab2 │ +               │  ← 标签栏
│  > repoA     ├───────────────┬───────────────┤
│  v repoB     │   pane (cmd)  │   pane (cmd)   │  ← 分屏的 pane 树
│    - main    │               ├───────────────┤
│  FILES repoB │               │   pane (cmd)   │
│   ..         │               │                │
│  > src/      │               │                │
│   README.md  │               │                │
└──────────────┴───────────────┴───────────────┘
  侧边栏(仓库/worktree + 跟随 pane 的文件树)  每个 pane 一个 shell 会话
```

单 pane 链路:**键盘 → ConPTY → 终端核心(VT 解析/屏幕缓冲)→ Grid → Direct2D/DirectWrite 渲染**。

- `Window`:窗口 + 消息循环 + 工作区编排;合成侧边栏 / 标签栏 / pane 树;键盘路由到聚焦 pane,应用快捷键管理标签·分屏·焦点·侧边栏,鼠标切标签 / 聚焦 pane / 打开 worktree
- `Tab` + `Pane`:每个标签是一棵二叉分屏树,叶子托管一个 `TerminalSession`
- `TerminalSession`:`Terminal` + `ConPty` + `Grid` + cwd/title,一个 pane 一个
- `Workspace`:侧边栏数据模型——扫描根目录下一层的 git 仓库,展开时惰性 `git worktree list`
- 侧边栏 **FILES** 面板:跟随聚焦 pane 的 cwd 列目录;点目录导航、点文件把文件名插入到 pane
- `ConPty`:Windows 伪控制台封装(在指定 cwd 起 shell、读输出、回写输入、resize、退出检测)
- `Terminal`:终端核心封装,两种后端二选一:
  - **默认:内置 `VTEmulator`**——自带的 xterm 子集解析器 + 屏幕缓冲(光标、UTF-8、CSI 光标移动、擦除、SGR 颜色/粗体/下划线/反显、滚动区、插入/删除行列)。**仅需 MSVC,无外部依赖。**
  - 可选:`-DLINEY_WITH_LIBGHOSTTY=ON` 接入 [libghostty-vt](https://github.com/ghostty-org/ghostty)(需 **Zig** 工具链),复用其完整缓冲/scrollback/reflow
- `D2DRenderer`:帧生命周期 + chrome 图元(填充/描边/文本)+ 在像素原点绘制 pane 网格;画光标、反显、粗体、下划线

已验证:VS 2022 的 MSVC/Ninja 下构建通过;运行后窗口内 `cmd.exe` 可输入命令并显示输出;分屏与新标签各自拉起独立 shell(以子进程计数确定性验证:1→2→3 分屏、新标签 +1);侧边栏列出 `D:\GitHub` 下的多个 git 仓库。

## 快捷键

| 键 | 作用 |
|---|---|
| `Ctrl+Shift+T` | 新建标签(继承当前 pane 的 cwd) |
| `Ctrl+Shift+W` | 关闭当前 pane(最后一个 pane 则关标签;最后一个标签则退出) |
| `Ctrl+Shift+E` | 左右分屏(竖直分隔) |
| `Ctrl+Shift+O` | 上下分屏(水平分隔) |
| `Ctrl+Shift+B` | 折叠/展开侧边栏 |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | 下一个 / 上一个标签 |
| `Ctrl+Shift+L` / `Ctrl+Shift+G` | 当前 pane 仓库的 `git log` / `git diff`(新标签,pager 视图) |
| `Ctrl+Shift+U` | 检查更新(查询 GitHub releases,有新版弹托盘通知) |
| `Alt+方向键` | 在分屏 pane 间移动焦点 |
| `Ctrl+Shift+C` / `Ctrl+Shift+V` | 复制选区 / 粘贴(支持 bracketed paste) |
| `Ctrl++` / `Ctrl+-` / `Ctrl+0` | 放大 / 缩小 / 重置字号 |
| 滚轮 / `Shift+PgUp·PgDn·Home·End` | 在 scrollback 历史中滚动(输入时自动回到底部) |
| 鼠标拖选 | 在 pane 内选择文本;拖动分屏分隔线调整 pane 比例;拖动标签重排 |
| 鼠标左键 | 点标签切换、点 `+` 新标签、点 pane 聚焦、点仓库展开、点 worktree/SSH/AGENT 开新标签 |
| 鼠标右键 | 右键仓库 → 新建 worktree(输入分支名);右键 worktree → 删除(确认) |

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

## 配置

首次运行会在 `%USERPROFILE%\.liney\config.json` 写入默认配置(对应 macOS liney 的 `~/.liney/`):

```json
{
  "shell": "cmd.exe",
  "fontFamily": "Cascadia Mono",
  "fontSize": 16,
  "workspaceRoot": ""
}
```

- `shell`:新标签使用的 shell(如 `powershell.exe` / `pwsh.exe` / `wsl.exe` / `cmd.exe`)
- `fontFamily` / `fontSize`:等宽字体与字号(也可运行时 `Ctrl++/-/0` 调整)
- `workspaceRoot`:侧边栏扫描的根目录;留空则用启动目录的父目录
- `hooks.sessionStart` / `hooks.sessionExit` / `hooks.appExit`:新 shell 启动后 / pane 关闭时 / 程序退出时执行的命令
- `sshHosts`:`["user@host", ...]` —— 出现在侧边栏 SSH 区,点击在新标签起 `ssh <host>`(用 Windows 自带 OpenSSH;会话随布局持久化)
- `agents`:`[{ "name", "command", "cwd" }]` —— 出现在侧边栏 AGENTS 区,点击在新标签起该命令(对标 liney 的 agent 会话)
- `theme`:`{ "background": "#102840", "foreground": "#e8e8d0", "palette": ["#..." ×16] }` —— 终端前景/背景与 16 色 ANSI 调色板(缺省即内置配色)

完整示例见仓库根目录 [`config.example.json`](./config.example.json)。

> **tmux / WSL**:把 `shell` 设为 `wsl tmux`(或在 `agents` 里加一条 `wsl tmux`)即可在 pane 内跑 tmux/WSL 会话——ConPTY 起任意程序,终端核心负责渲染。

布局(标签 + 分屏树 + 各 pane 的 cwd)在关闭时写入 `%USERPROFILE%\.liney\layout.json`,下次启动自动恢复。

## 通知 / `liney` CLI

随主程序一起构建一个伴随 CLI `liney.exe`,在 pane 内运行即可通过 OSC 序列驱动终端(对标 macOS liney 的 `liney notify`):

```
liney notify <body>             # 弹出 Windows 托盘通知
liney notify <title> <body>
liney title  <text>            # 设置标签/窗口标题
```

终端还会解析常见 OSC:`0/2`(标题)、`7`(cwd,用于新标签继承)、`9` 与 `777;notify`(通知)。把 `build\liney.exe` 加入 PATH 后,长任务结束时 `liney notify "done"` 即可提醒。

## 分发 / 打包

对标 macOS liney 的 DMG / Homebrew 分发,提供 Windows 侧打包脚手架(脚本在 `tools/`,清单在 `packaging/`):

```powershell
# 便携版:构建 Release 并打成 dist\liney-win-portable.zip(解压即用,免安装)
powershell -ExecutionPolicy Bypass -File tools\make-portable.ps1

# MSIX 安装包(需 Windows SDK 的 makeappx;本地安装需签名)
powershell -ExecutionPolicy Bypass -File tools\gen-assets.ps1   # 生成占位图标
powershell -ExecutionPolicy Bypass -File tools\make-msix.ps1 -SelfSign
```

- `tools/make-portable.ps1` —— 便携 zip(已验证可产出)
- `tools/make-msix.ps1` + `packaging/AppxManifest.xml` + `tools/gen-assets.ps1` —— MSIX 包(身份 `everettjf.liney-win`)
- `packaging/winget/*.yaml` —— WinGet 清单模板(填好 release URL + SHA256 后提交 winget-pkgs)
- 自动更新(对标 Sparkle)留待后续(MSIX/Store 自带更新,或 Squirrel)

## 目录结构

```
src/
  main.cpp                入口(wWinMain)
  cli/main.cpp            liney.exe 伴随 CLI(notify / title → OSC)
  app/
    Window.*              Win32 窗口、工作区编排、输入路由、绘制
    Layout.h              Rect + 由字号派生的 UI 度量
    Pane.h                分屏树节点(叶=会话,内部=二叉分屏)
    Tab.*                 标签 = 分屏树 + 聚焦叶;分屏/关闭/布局/命中测试/方向聚焦
  core/
    TerminalSession.*    一个 pane 的终端会话(Terminal + ConPty + Grid + cwd)
    Config.*             读取 %USERPROFILE%\.liney\config.json
  workspace/Workspace.*   侧边栏模型:git 仓库扫描 + worktree 惰性加载
  util/
    Process.*            无窗口子进程捕获(给 git worktree list 用)
    Json.*               极简 JSON parse/serialize(配置 + 后续布局持久化)
  render/
    Cell.h                cell / grid(含光标)数据结构
    IRenderer.h           渲染器接口(帧生命周期 + chrome 图元 + 网格)
    D2DRenderer.*         Direct2D/DirectWrite 直绘实现(阶段一)
  pty/ConPty.*            ConPTY 封装(在 cwd 起 shell + 读输出 + 回写 + 退出检测)
  vt/
    Terminal.*            终端核心封装(内置 VTEmulator 或 libghostty-vt → Grid)
    VTEmulator.*          内置 xterm 子集解析器 + 屏幕缓冲 + scrollback + alt 屏 + OSC
    Notification.h        OSC 9/777 通知数据
```

## 里程碑(自建本地终端 MVP)

- **S0** 骨架 + 渲染管线打通 ✓
- **S1** ConPTY 输出 → 网格 → 渲染(只读)✓
- **S2** 键盘输入回写 ConPTY ✓
- **S3** 内置 VT 核心:光标、SGR 颜色/属性、擦除、滚动区、插入/删除行列 ✓
- **P1** 终端完整度:备用屏 alt-screen(vim/less)、scrollback 历史 + 滚动 + 重排(reflow)、选择 + 复制粘贴、IME 候选定位 ✓
  - 待办:鼠标上报(受 ConPTY 限制)、glyph atlas 渲染
- **S4** 配置/字体/配色,单窗口可用(部分:字号派生度量已就位)
- **S5** UI 外壳(多标签 / 分屏)+ 仓库/worktree 侧边栏(liney 价值闭环)✓
  - 待办:拖拽分隔条调整 pane 比例、标签拖动重排、worktree 增删操作、布局持久化
