# liney-win

**Windows 上的「终端工作区」** —— 把多个仓库 / worktree、分屏、标签放进一个窗口,
对标 macOS 的 [liney](https://github.com/everettjf/liney)。自建终端核心 + 全自绘
Win32 / Direct2D,**仅需 MSVC,无外部运行时依赖**。

![liney-win 截图](docs/screenshot.png)

---

## ✨ 功能

**终端**(自建 xterm 子集核心 `VTEmulator`)
- VT 解析:CSI 光标移动 / 擦除 / 滚动区 / 插入删除行列、SGR 16/256/truecolor + 粗体/斜体/下划线/反显、UTF-8、宽字符
- **scrollback 历史** + 滚轮 / `Shift+PgUp` 滚动,改窗口大小时长行**重排(reflow)**
- **备用屏 alt-screen**:vim / less / `git log` 等全屏程序正常
- **选择 + 复制粘贴**(`Ctrl+Shift+C/V`,bracketed paste)
- **IME**:中日韩输入,候选窗口跟随光标
- 字号缩放、可配置**配色主题**

**工作区**(liney 的差异化)
- 多标签 + 二叉**分屏**,拖动分隔条调比例、拖动标签重排,`Alt+方向键` 切焦点
- 侧边栏列出**多仓库**,展开看 **worktree**;右键新建 / 删除 worktree
- 点 worktree / SSH 主机 / agent 在其目录开新标签
- **文件树**面板跟随聚焦 pane 的目录
- **布局持久化**:标签 + 分屏树 + 各 pane 的 cwd 关闭后下次自动恢复

**会话与集成**
- 本地 shell(cmd / PowerShell / pwsh / wsl…)、**SSH**、**agent** 会话(tmux 经 `wsl tmux`)
- **通知**:`liney notify` CLI + OSC `9` / `777` → Windows 托盘通知
- **Git**:`Ctrl+Shift+L/G` 在新标签看 `git log` / `git diff`
- **生命周期 hooks**:session 启停 / app 退出执行命令
- **自动更新**:`Ctrl+Shift+U` 查 GitHub release,有新版下载安装包并就地升级

---

## 📦 安装

**下载**(推荐):到 [Releases](https://github.com/everettjf/liney-win/releases) 下载
- `liney-win-Setup.exe` —— 安装包(每用户安装,免管理员,带开始菜单 + 卸载)
- `liney-win-portable.zip` —— 便携版,解压双击 `liney_win.exe` 即用

**从源码构建**(Windows 10 1809+/11,Visual Studio 2022 Desktop C++,CMake ≥ 3.20;VS 2022 自带 CMake/Ninja):

```powershell
# 在 “x64 Native Tools Command Prompt for VS 2022” 中
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
.\build\liney_win.exe
```

> 可选:`-DLINEY_WITH_LIBGHOSTTY=ON` 接入 [libghostty-vt](https://github.com/ghostty-org/ghostty)
> 作为终端核心(需 Zig 工具链);默认用内置 `VTEmulator`,无需任何额外依赖。

---

## ⌨️ 快捷键

| 键 | 作用 |
|---|---|
| `Ctrl+Shift+T` / `Ctrl+Shift+W` | 新建标签 / 关闭当前 pane |
| `Ctrl+Shift+E` / `Ctrl+Shift+O` | 左右分屏 / 上下分屏 |
| `Ctrl+Tab` / `Ctrl+Shift+Tab` | 下一个 / 上一个标签 |
| `Alt+方向键` | 在分屏 pane 间移动焦点 |
| `Ctrl+Shift+B` | 折叠 / 展开侧边栏 |
| `Ctrl+Shift+C` / `Ctrl+Shift+V` | 复制选区 / 粘贴 |
| `Ctrl++` / `Ctrl+-` / `Ctrl+0` | 放大 / 缩小 / 重置字号 |
| `Ctrl+Shift+L` / `Ctrl+Shift+G` | 当前仓库的 `git log` / `git diff`(新标签) |
| `Ctrl+Shift+U` | 检查并安装更新 |
| 滚轮 · `Shift+PgUp/PgDn/Home/End` | 在 scrollback 历史中滚动 |
| 鼠标拖动 | pane 内选文本;拖分隔条调比例;拖标签重排 |
| 鼠标左键 / 右键 | 切标签 · 聚焦 pane · 展开仓库 · 开 worktree/SSH/agent / 右键管理 worktree |

---

## ⚙️ 配置

首次运行在 `%USERPROFILE%\.liney\config.json` 写入默认配置(对应 macOS liney 的 `~/.liney/`;完整示例见 [`config.example.json`](./config.example.json)):

```json
{
  "shell": "cmd.exe",
  "fontFamily": "Cascadia Mono",
  "fontSize": 16,
  "workspaceRoot": "",
  "hooks": { "sessionStart": "", "sessionExit": "", "appExit": "" },
  "sshHosts": ["user@host"],
  "agents": [{ "name": "agent", "command": "claude", "cwd": "" }],
  "theme": { "background": "#102840", "foreground": "#e8e8d0", "palette": ["#000000", "..."] }
}
```

- `shell` —— 新标签的 shell(`powershell.exe` / `pwsh.exe` / `wsl.exe` …;`wsl tmux` 即可跑 tmux)
- `workspaceRoot` —— 侧边栏扫描根目录;留空则用启动目录的父目录
- `sshHosts` / `agents` —— 侧边栏 SSH / AGENTS 区的入口(随布局持久化)
- `theme` —— 终端前景/背景 + 16 色 ANSI 调色板
- `hooks` —— session 启停 / app 退出执行的命令

布局写入 `%USERPROFILE%\.liney\layout.json`,下次启动自动恢复。

---

## 🔔 `liney` CLI 与通知

随主程序构建伴随 CLI `liney.exe`,在 pane 内运行即可通过 OSC 序列驱动终端(对标 macOS liney 的 `liney notify`):

```
liney notify <body>            # 弹 Windows 托盘通知
liney notify <title> <body>
liney title  <text>           # 设置标签/窗口标题
```

把 `liney.exe` 加入 PATH 后,长任务结束 `liney notify "done"` 即可提醒。终端也解析常见 OSC:`0/2`(标题)、`7`(cwd)、`9` 与 `777;notify`(通知)。

---

## 📦 打包 / 分发

脚本在 `tools/`,清单在 `packaging/`:

```powershell
powershell -ExecutionPolicy Bypass -File tools\make-installer.ps1   # NSIS 安装包(需 winget install NSIS.NSIS)
powershell -ExecutionPolicy Bypass -File tools\make-portable.ps1    # 便携 zip
powershell -ExecutionPolicy Bypass -File tools\make-msix.ps1 -SelfSign  # MSIX(需 Windows SDK)
```

- 应用图标 `res/liney.ico`(`tools/gen-icon.ps1` 生成)经 `res/resource.rc` 编入 exe
- WinGet 清单模板见 `packaging/winget/`

---

## 🏗️ 架构

```
键盘/鼠标 → Window(工作区编排) → 路由到聚焦 pane
            ↑ 合成 sidebar / 标签栏 / 分屏树
TerminalSession = Terminal + ConPty + Grid
   ConPty   —— Windows 伪控制台(起 shell、读/写、resize)
   Terminal —— 内置 VTEmulator(或 libghostty-vt)解析 PTY 字节 → Grid
   D2DRenderer —— Direct2D/DirectWrite 把 Grid 画到窗口
```

源码导览见 [`src/`](./src);技术选型与调研见 [`RESEARCH.md`](./RESEARCH.md) /
[`ALT_PLAN_SELFBUILT.md`](./ALT_PLAN_SELFBUILT.md) / [`TERMINAL_LANDSCAPE.md`](./TERMINAL_LANDSCAPE.md);
渲染计划见 [`RENDERING.md`](./RENDERING.md)。

## 🗺️ 路线图

已完成与待办(含与 macOS liney 的对照)见 [`ROADMAP.md`](./ROADMAP.md)。
当前仍待后续的:鼠标上报(受 ConPTY 限制)、SFTP 远程文件树、glyph atlas 渲染、原生 tmux control-mode。

## 许可

MIT —— 见 [`LICENSE`](./LICENSE)。
