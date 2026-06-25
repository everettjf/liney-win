# liney-win 路线图:对标 macOS 版 liney 的差距与计划

> 与 [everettjf/liney](https://github.com/everettjf/liney)(macOS,Swift + Ghostty)对比,梳理已完成项、差距、以及分阶段计划。
> 现状基线见 [`README.md`](./README.md);选型背景见 [`RESEARCH.md`](./RESEARCH.md) / [`ALT_PLAN_SELFBUILT.md`](./ALT_PLAN_SELFBUILT.md)。

---

## 1. 功能对照表

图例:✅ 已具备 · 🟡 部分 · ❌ 未做

| 领域 | macOS liney | liney-win 现状 |
|---|---|---|
| **终端内核** | Ghostty(完整 VT、scrollback、reflow、Unicode、连字、GPU) | ✅ Ghostty 的 libghostty-vt(经 Zig 构建,即上游同一引擎)—— 完整 VT、scrollback、reflow、Unicode、grapheme;经 C API 拿渲染快照 + 标题/cwd。已删除早期内置 VTEmulator。待补:桌面通知(OSC 9/777)与 bracketed-paste 需在 libghostty 路径重新接线 |
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
- ✅ **resize reflow(scrollback 重排)**:记录每行软换行标记,改变列宽时把历史里的软换行行重新拼接成逻辑行再按新宽度重排(确定性验证:窄化后行尾 `_ENDMARK` 仍在、内容不丢;旧的截断行为会丢失)。活动屏由 shell 收到 resize 后重绘
- ✅ **IME(中日韩输入)**:已提交字符走 `WM_CHAR`(含代理对);组词/候选窗口跟随光标定位(`WM_IME_STARTCOMPOSITION/COMPOSITION` + `ImmSetCompositionWindow/CandidateWindow`)
- ⬜ **鼠标上报**:受 ConPTY 影响——经验证,子程序输出里的 `?1000h` 会被 ConPTY 吸收(host 输出流里收不到),需走 ConPTY 的鼠标透传机制(类似 Windows Terminal 的处理),非简单解析即可,留到后续专门处理

### P2 — 配置与会话基础 ✅ 已完成(配色主题除外)
- ✅ **配置文件** `%USERPROFILE%\.liney\config.json`(极简 JSON 库,容忍 BOM;缺失则写默认)
- ✅ **可选 shell**(确定性验证:`shell=powershell.exe` 时子进程为 powershell)
- ✅ **字号缩放** `Ctrl+ +/-/0`(运行时重建字体 + 重算 cell 尺寸 + 重排)
- ✅ **工作区根可配置**(`workspaceRoot`,留空回退父目录)
- ✅ **配色主题**:config `theme`(background / foreground / 16 色 palette,hex)→ 终端前景/背景/调色板;默认与内置一致(确定性验证:设 `#102840` 后终端像素恰为 16,40,64)

### P3 — 工作区深化(liney 差异化)🟡 进行中
- ✅ **布局持久化**:标签 + 分屏树 + 每个 pane 的 cwd → `%USERPROFILE%\.liney\layout.json`,重开恢复(liney 核心卖点;确定性验证:已知布局恢复出 3 个 shell,优雅关闭后回写结构正确)
- ✅ **拖拽分隔条**调 pane 比例(命中分隔线即拖动改 ratio)
- ✅ **worktree 操作**:右键仓库新建 worktree(InputBox 输入分支名,`git worktree add`)、右键 worktree 删除(确认,`git worktree remove`)——git 命令形态与 porcelain 解析已验证
- ✅ **文件树**:侧边栏 FILES 面板跟随聚焦 pane 的 cwd,目录在前、文件在后;点目录导航、点文件把文件名插入到 pane(PrintWindow 截图确认:点击插入 LICENSE/RENDERING.md/TECH_PLAN.md)
- ✅ **标签拖拽重排**:在标签栏拖动标签到新位置(reorder tabs_,活动标签跟随)

### P4 — 集成与通知 🟡 进行中
- ✅ **`liney notify` CLI**(独立 `liney.exe`:`notify` / `title`)+ **OSC 0/2/7/9/777 解析** → 托盘气泡通知 + 实时标题(确定性验证:CLI 输出 OSC 字节正确;窗口标题随 OSC 实时变化;`sessionStart` hook 写出标记文件)
- ✅ **生命周期 hooks**:`hooks.sessionStart`(新 shell 执行)、`hooks.sessionExit`(pane 关闭时)、`hooks.appExit`(退出时,确定性验证:优雅关闭后标记文件已写)
- ✅ **Git history / diff 视图**:`Ctrl+Shift+L` 在新标签开 `git log`(图形化历史,走 pager + alt 屏)、`Ctrl+Shift+G` 开 `git diff`

### P5 — 远程与高级会话(体量大,独立推进)🟡 进行中
- ✅ **SSH 会话**:config `sshHosts` → 侧边栏 SSH 区,点击在新标签起 `ssh <host>`(ConPTY,Windows 自带 OpenSSH);会话 shell 命令随布局持久化(SSH 标签可恢复)。确定性验证:点击后 liney 子进程出现 `ssh test@192.0.2.1`
- ✅ **agent 会话**:config `agents: [{name, command, cwd}]` → 侧边栏 AGENTS 区,点击在新标签起该命令(对标 liney 的 agent 会话)。确定性验证:点击后 liney 子进程出现配置的命令
- 🟡 **tmux 集成**:可通过把 `shell` 设为 `wsl tmux` 或在 `agents` 加一条命令实现(ConPTY 起任意程序);原生 tmux control-mode 集成留待后续
- ⬜ 远程文件树(SFTP)
- ⬜ **glyph atlas + D3D11** 渲染升级(性能/连字)

### P6 — 分发 🟡 进行中
- ✅ **应用图标**:`res/liney.ico`(多尺寸,`tools/gen-icon.ps1` 生成)经 `res/resource.rc` 编入 exe(已验证可从 exe 提取)
- ✅ **NSIS 安装包**:`packaging/liney-win.nsi` + `tools/make-installer.ps1`(每用户安装、开始菜单快捷方式、Add/Remove、卸载;已验证静默安装/卸载完整闭环)
- ✅ **便携 zip 打包**:`tools/make-portable.ps1`(已验证产出 `dist\liney-win-portable.zip`,含两个 exe + 文档)
- ✅ **MSIX 脚手架**:`packaging/AppxManifest.xml`(身份 `everettjf.liney-win`)+ `tools/gen-assets.ps1`(已验证生成图标)+ `tools/make-msix.ps1`(makeappx 打包 + 可选自签)
- ✅ **WinGet 清单模板**:`packaging/winget/*.yaml`(installer / locale / version)
- ✅ **自动更新(对标 Sparkle,通过 GitHub release)**:`Ctrl+Shift+U` 后台查询 GitHub releases,比较版本;有新版则在 release 资产里找安装包(`*Setup.exe`),弹框确认后下载(WinHTTP,自动跟随 github→CDN 跨主机跳转,确定性验证:成功下载真实 GitHub 资产 1950 字节)到临时目录并运行 NSIS 安装包,随后退出以便就地替换(安装包 `.onInit` 会先 taskkill 旧实例)

---

## 4. 建议执行顺序

P1 → P2 → P3 是把「自建终端 + 工作区」做扎实的主线,完成后 liney-win 就是一个**日常可用、带工作区与布局持久化的本地终端**。P4 加上 liney 标志性的通知/hooks。P5/P6 是远程能力与分发,按需推进。

> 从 **P1(终端完整度)** 开始。
