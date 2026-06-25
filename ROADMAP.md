# liney-win 路线图:对标 macOS 版 liney 的差距与计划

> 与 [everettjf/liney](https://github.com/everettjf/liney)(macOS,Swift + Ghostty)对比,梳理已完成项、差距、以及分阶段计划。
> 现状基线见 [`README.md`](./README.md);选型背景见 [`RESEARCH.md`](./RESEARCH.md) / [`ALT_PLAN_SELFBUILT.md`](./ALT_PLAN_SELFBUILT.md)。

---

## 1. 功能对照表

图例:✅ 已具备 · 🟡 部分 · ❌ 未做

| 领域 | macOS liney | liney-win 现状 |
|---|---|---|
| **终端内核** | Ghostty(完整 VT、scrollback、reflow、Unicode、连字、GPU) | 🟡 内置 VTEmulator:颜色/SGR、光标、擦除、滚动区、插入删除行列、UTF-8、宽字符 |
| ├ 历史回滚 scrollback | ✅ | ❌ 仅当前屏,无历史、无滚轮滚动 |
| ├ 选择 / 复制粘贴 | ✅ | ❌ 无选区、无剪贴板 |
| ├ 备用屏 alt-screen | ✅(vim/less/htop 正常) | ❌ 全屏 TUI 应用会错乱 |
| ├ resize reflow | ✅ | ❌ 改窗口大小不重排长行 |
| ├ 鼠标上报 | ✅ | ❌ 应用收不到鼠标 |
| ├ IME / 输入法 | ✅ | ❌ 中日韩输入未处理 |
| ├ 连字 / 字形 atlas | ✅ | ❌ 逐 cell DrawText(阶段一) |
| **工作区 / 侧边栏** | 多仓库 + worktree | 🟡 扫描父目录的 git 仓库,惰性列 worktree |
| ├ 点 worktree 开终端 | ✅ | ✅ 在该目录开新标签 |
| ├ worktree 增删/切分支 | ✅ | ❌ 只读列举 |
| ├ 工作区根可配置 | ✅ | ❌ 硬编码为启动目录的父目录 |
| ├ 布局持久化(按仓库恢复) | ✅ **核心卖点** | ❌ 关闭即丢 |
| **标签 / 分屏** | ✅ | ✅ 多标签、二叉分屏、方向聚焦、关闭收拢 |
| ├ 拖拽分隔条调比例 | ✅ | ❌ 固定 50% |
| ├ 标签拖拽重排 | ✅ | ❌ |
| **文件浏览** | 跟随聚焦 pane 的文件树(本地 + SSH) | ❌ 无 |
| **会话类型** | 本地 shell / SSH / agent / tmux | 🟡 仅本地 `cmd.exe`(且 shell 硬编码) |
| ├ 可选 shell(pwsh/wsl) | ✅ | ❌ |
| ├ SSH + 远程文件树 | ✅ | ❌ |
| ├ agent 会话 | ✅ | ❌ |
| ├ tmux 集成 | ✅ | ❌ |
| **Git 集成** | worktree、分支、diff、history | 🟡 worktree 列举 + 分支名 |
| ├ diff 视图 | ✅ | ❌ |
| ├ history 视图 | ✅ | ❌ |
| **生命周期 hooks** | app/session 启停执行命令 | ❌ |
| **通知** | OSC 9/777 → 灵动岛 + `liney notify` CLI | ❌ |
| **配置 / 设置** | 设置面板 + `~/.liney/` 持久化 | ❌ 无配置文件 |
| **打包 / 更新** | DMG + Homebrew + Sparkle | ❌ 直接跑 exe,无安装包/更新 |
| **CLI 工具** | `liney notify`、`skills/liney-cli` | ❌ |

---

## 2. 判断:先补「终端是否好用」,再扩「工作区广度」

liney 的前提是**底层是一个好用的终端**(它直接白嫖 Ghostty)。我们自建内核,所以**终端完整度是当前最大短板**——没有 scrollback / 复制粘贴 / alt-screen,日常根本不顺手。因此优先级高于 SSH/tmux/agent 这些广度功能。

SSH / agent / tmux / 打包更新 体量大、相对独立,排在后面;diff/history/文件树 依赖一个稳定的内核与 UI 框架,居中。

---

## 3. 分阶段计划

### P1 — 终端完整度(让单 pane 真正好用)✅ 已完成(reflow 除外)
- ✅ **备用屏 alt-screen**(`?1049/?47/?1047`):vim/less/git log/htop 不再错乱
- ✅ **scrollback 历史 + 滚轮/Shift+PgUp·PgDn·Home·End 滚动**(确定性验证:滚动后可见早期行)
- ✅ **选择 + 复制粘贴**:鼠标拖选、`Ctrl+Shift+C/V`、bracketed paste(`?2004`)、`WM_COPY/WM_PASTE`
- ⬜ **resize reflow**:窗口变化时长行重排(当前为截断/补齐,未重排)— 留到后续

### P2 — 配置与会话基础 ✅ 已完成(配色主题除外)
- ✅ **配置文件** `%USERPROFILE%\.liney\config.json`(极简 JSON 库,容忍 BOM;缺失则写默认)
- ✅ **可选 shell**(确定性验证:`shell=powershell.exe` 时子进程为 powershell)
- ✅ **字号缩放** `Ctrl+ +/-/0`(运行时重建字体 + 重算 cell 尺寸 + 重排)
- ✅ **工作区根可配置**(`workspaceRoot`,留空回退父目录)
- ⬜ **配色主题**:留到后续(当前为内置配色)

### P3 — 工作区深化(liney 差异化)🟡 进行中
- ✅ **布局持久化**:标签 + 分屏树 + 每个 pane 的 cwd → `%USERPROFILE%\.liney\layout.json`,重开恢复(liney 核心卖点;确定性验证:已知布局恢复出 3 个 shell,优雅关闭后回写结构正确)
- ⬜ **拖拽分隔条**调 pane 比例;**标签拖拽**重排
- ⬜ **worktree 操作**:新建 / 删除 worktree、切分支(shell out `git`)
- ⬜ **文件树**:跟随聚焦 pane cwd 的本地文件树面板

### P4 — 集成与通知
- **`liney notify` CLI**(独立 `liney.exe`)+ **OSC 9/777 解析** → **Windows 토스트通知**
- **生命周期 hooks**:app/session start/exit 执行命令(读 config)
- **Git diff / history** 简易视图
- 验收:`liney notify "done"` 弹 Windows 通知;长任务完成提醒

### P5 — 远程与高级会话(体量大,独立推进)
- **SSH 会话**(起 `ssh` ConPTY 即可基本可用)+ 远程文件树(SFTP)
- **agent 会话**、**tmux 集成**
- **glyph atlas + D3D11** 渲染升级(性能/连字)

### P6 — 分发
- **MSIX 打包** + 应用标识/图标;**WinGet** 清单;自动更新(Squirrel/MSIX)

---

## 4. 建议执行顺序

P1 → P2 → P3 是把「自建终端 + 工作区」做扎实的主线,完成后 liney-win 就是一个**日常可用、带工作区与布局持久化的本地终端**。P4 加上 liney 标志性的通知/hooks。P5/P6 是远程能力与分发,按需推进。

> 从 **P1(终端完整度)** 开始。
