# Liney 产品战略与路线图（2026）

> 结论先行：Liney 不应把目标定成“另一个 Windows Terminal”，而应成为 **Windows 上以仓库、worktree 和编码 Agent 为中心的可靠终端工作区**。Windows Terminal 负责通用 shell 宿主；Liney 负责把项目上下文、并行任务、命令结果和 Agent 生命周期组织起来。任何 AI 功能都必须建立在稳定、可解释、可授权、可恢复的终端底座上。

## 1. 当前判断

Liney 已经具备一个清晰但尚未完全兑现的差异化雏形：Win32/Direct2D 原生界面、Ghostty VT 内核、仓库与 worktree 侧边栏、文件树、分屏布局、Agent/SSH 入口、通知和生命周期 hooks。它不是从“标签页终端”起步，而是从“项目工作区”起步。

当前最大的产品风险不是少几个炫酷功能，而是：

1. 终端基础能力和 Windows 可访问性还没有达到用户可以无条件替换日常终端的程度。
2. 依赖、发布、升级、崩溃诊断和真实 Windows 环境测试还不够可复现。
3. Agent 目前更像快捷启动项，尚未成为有状态、可观察、可恢复的任务对象。
4. 现有路线图文档与实际功能状态已经不同步，会影响优先级判断和社区信任。

因此产品优先级应固定为：**可靠性 > 终端完整性 > 项目工作区效率 > Agent 差异化 > 扩展生态**。

## 2. 市场位置与竞品

GitHub 数据是 2026-07-21 的快照，只代表社区规模，不代表产品质量：

| 产品 | GitHub stars（约） | 核心优势 | Liney 应学习什么 |
|---|---:|---|---|
| [Windows Terminal](https://github.com/microsoft/terminal) | 104.3k | Windows 默认心智、PowerShell/WSL/profile、GPU 渲染、命令面板、可访问性 | 基础能力、系统整合和质量基线 |
| [Tabby](https://github.com/Eugeny/tabby) | 73.4k | SSH/Telnet/Serial、插件、SFTP、跨平台配置 | 远程连接与插件生态，但避免 Electron 级复杂度 |
| [Alacritty](https://github.com/alacritty/alacritty) | 65.0k | 性能、简洁、跨平台 | 保持渲染与输入路径克制、可测量 |
| [WezTerm](https://github.com/wezterm/wezterm) | 27.8k | 本地/远程 multiplexing、SSH、丰富协议、Lua 配置 | 会话持久化、远程能力和高级用户自动化 |
| [Fluent Terminal](https://github.com/felixse/FluentTerminal) | 9.6k | Windows 风格 UI、多 shell | 原生 Windows 体验的一致性 |
| [Rio](https://github.com/raphamorim/rio) | 7.0k | 新一代 GPU 终端、现代架构 | 性能预算和现代渲染能力 |
| [Contour](https://github.com/contour-terminal/contour) | 3.0k | VT 能力、跨平台、配置 | 协议兼容性和测试覆盖 |

[Windows Terminal 官方能力说明](https://learn.microsoft.com/en-us/windows/terminal/)显示，它已经覆盖标签、窗格、Unicode/UTF-8、GPU 渲染、主题、快捷键和命令行启动；[OSC 133 shell integration](https://learn.microsoft.com/en-ca/windows/terminal/tutorials/shell-integration)还提供 prompt、command、output、exit code 等语义标记。因此“有标签、能分屏、能换主题”不是可持续差异。

[WezTerm](https://wezterm.org/features.html)在 SSH、multiplexing、字体回退、图像协议和高级配置上很成熟；[Tabby](https://github.com/Eugeny/tabby)已经把 SSH 管理、SFTP、插件和会话恢复做成完整产品。Liney 不应正面复制它们的功能总量。

AI 终端方面，[Warp Blocks](https://docs.warp.dev/terminal/blocks)把命令和输出组织成可操作单元，[Blocks as context](https://docs.warp.dev/agent-platform/local-agents/agent-context/blocks-as-context)允许将终端结果加入 Agent 上下文，[Full terminal use](https://docs.warp.dev/agent-platform/capabilities/full-terminal-use)则让 Agent 在用户可接管和审批的前提下操作活动 PTY。这说明 AI 终端的真正基础不是聊天窗口，而是**结构化终端状态、上下文选择、权限控制和人在回路中**。

## 3. Liney 与 Windows Terminal 的差异

| 维度 | Windows Terminal | Liney 应选择的位置 |
|---|---|---|
| 核心对象 | shell/profile/tab | repository/worktree/task/session |
| 目标用户 | 所有 Windows 命令行用户 | 同时管理多个项目和编码 Agent 的开发者 |
| 项目上下文 | 主要依赖 cwd 和 profile | 仓库、分支、worktree、文件、Git 状态是一等对象 |
| AI | 不宜假设系统终端绑定某一家 Agent | provider-neutral，优先接入 Codex、Claude、Gemini 和自定义 CLI |
| 自动化 | 命令行参数、actions、settings | 项目模板、任务生命周期、本地控制面、hooks |
| 产品承诺 | 通用、兼容、系统集成 | 项目切换快、并行任务清楚、失败可诊断、会话可恢复 |

一句产品定位可以是：

> **Liney is the reliable Windows terminal workspace for repositories, worktrees, and coding agents.**

中文可表述为：**为仓库、worktree 与编码 Agent 打造的可靠 Windows 终端工作区。**

## 4. 必须先补齐的能力

### P0：可信赖的日常终端（0—6 周）

这是所有新功能的发布门槛。

- **可复现构建**：Ghostty 当前使用 `GIT_TAG main`，必须固定到经过验证的 commit/tag，记录 ABI 和升级清单；依赖升级单独走兼容性回归。
- **可信发布链**：对主程序和安装包做 Authenticode 签名；发布 SHA-256、SBOM 和构建来源；自动更新同时校验摘要与签名者。
- **崩溃与卡死诊断**：生成 minidump、轮转日志和一键导出诊断包；默认不上传，导出前展示并脱敏 cwd、命令和环境变量。
- **真实系统矩阵**：CI 除编译外，增加干净 Windows 10 22H2、Windows 11 的安装、首次启动、portable、升级、卸载和 runtime smoke test。Windows 10 1809 可保留兼容编译目标，定期而非每次完整测试。
- **终端回归与 fuzz**：为 VT 序列、UTF-8/CJK/grapheme、IME、reflow、scrollback、alt-screen、鼠标、选择和粘贴建立 golden tests；fuzz VT 输入、JSON/config 和 OSC。
- **生命周期压力测试**：高吞吐输出、频繁 resize/split、shell 崩溃、强制关窗、睡眠/唤醒、显示器/DPI 切换、磁盘空间不足、损坏配置、连续运行 24 小时。
- **配置安全**：配置 schema 版本、严格校验、原子写入、自动备份、迁移和失败回滚；坏配置不能阻止应用启动。
- **可访问性基线**：UI Automation、Narrator、键盘全路径、焦点可见、高对比度和缩放测试。微软的[可访问性指南](https://learn.microsoft.com/en-us/windows/apps/design/accessibility/accessibility-overview)应作为 Windows 产品基线。
- **性能预算**：建立启动、输入延迟、resize、高吞吐、idle CPU/RAM、多 pane 的基准，性能退化阻止合并。

建议发布门槛（目标值，不是当前实测）：

| 指标 | P0 门槛 |
|---|---:|
| 无崩溃会话率 | ≥ 99.9% |
| 安装/升级成功率 | ≥ 99.5% |
| 24 小时 soak | 0 crash、0 hang、0 output corruption |
| 冷启动 p95 | ≤ 300 ms（基准机） |
| 空闲 CPU | ≤ 0.2%（单 pane、基准机） |
| 配置损坏恢复 | 100% 可启动并可恢复备份 |
| 键盘核心路径 | 100% 不依赖鼠标 |

### P1：Windows 终端完整性（6—12 周）

- OSC 133 shell integration：识别 prompt/command/output/exit code/cwd，成为命令块和 Agent 上下文的共同底座。
- OSC 8 可点击超链接；OSC 52 剪贴板必须带 allow/deny policy 和来源提示。
- PowerShell、pwsh、cmd、WSL、Git Bash 自动发现与 profile picker；允许项目覆盖默认 profile。
- 统一 action registry、命令面板和可配置快捷键；所有菜单项和快捷键都从同一 action 定义生成。
- 命令块基础交互：跳到上一条/下一条命令、复制命令、复制输出、重跑、显示退出码和耗时、收藏。
- 字体 fallback、emoji、ligature 的正确性和回归测试。
- 多窗口和提升权限流程先完成可靠设计；不要允许同一进程内含糊混用普通与管理员 pane。
- SSH 从“启动 ssh 命令”提升到可靠 profile、known_hosts、Windows Credential Manager 和连接诊断；SFTP 放到后续，不阻塞 P1。

## 5. 真正的 AI/Agent 差异化

### P2：项目工作区成为产品核心（3—5 个月）

- 命名工作区、布局快照、项目模板和一键恢复。
- 将 task、branch、worktree、pane、Agent session 建立明确关联。
- 一键为任务创建隔离 worktree，启动指定 Agent/命令，展示 Git 状态、测试状态和变更摘要。
- Agent 完成后进入 review 流程：查看 diff、运行测试、打开编辑器、保留或安全清理 worktree。
- 工作区侧边栏显示有效状态，而不是只显示目录：dirty、ahead/behind、running、waiting、failed、done。
- 恢复 UI 布局与“恢复进程”要明确区分；不能向用户暗示已经恢复了实际 shell 状态。

### P3：Agent-native Terminal（5—8 个月）

- **Agent session model**：Codex、Claude、Gemini、自定义 CLI 都映射到统一状态：starting/running/waiting approval/needs input/done/failed。
- **语义上下文**：用户可选择某个命令块、文件、Git diff 或测试失败作为上下文；默认不把整个 scrollback 发给模型。
- **本地控制面**：优先使用受权限约束的 named pipe/local API；能力包括列举工作区、打开或聚焦 pane、读取限定范围的语义输出、发送输入。随后可提供 MCP adapter。
- **审批与接管**：写入 PTY、执行命令、访问新目录、读取敏感文件都应有项目级 trust policy、明确审批和随时人工接管。
- **安全与审计**：密钥/令牌模式脱敏、命令审计日志、能力最小化、每项目授权、可撤销 token；控制面默认仅本用户可访问。
- **任务总览**：在一个轻量视图里看到所有项目中 Agent 的运行、等待、失败和完成状态，并一键跳转；这比常驻聊天侧栏更有价值。
- **provider-neutral**：Liney 不托管模型也能完整使用；BYOK、云端模型和本地模型均作为可选适配器。

AI 功能的红线：

- 不默认执行模型生成的命令。
- 不默认上传终端历史、环境变量或仓库内容。
- 不把“Agent 输出了完成”当成任务成功，必须结合退出码、测试或用户定义检查。
- 不先做一个占空间的通用聊天面板；先让现有 CLI Agent 在 Liney 中更安全、更清楚、更容易并行。

### P4：生态与规模（8—12 个月）

- 稳定的 action/plugin API，先支持菜单、命令面板、工作区模板和 Agent adapter，避免允许插件直接侵入渲染线程。
- 远程会话持久化或 multiplexing；必须先定义断线、重连、凭据和状态一致性模型。
- SFTP/远程文件树、串口等能力由真实需求排序。
- Kitty/iTerm2/Sixel 图像协议、quake mode、终端录制/回放与可分享命令块按用户反馈进入版本。
- 企业策略：禁止外部 AI、限定可执行程序、集中配置、审计导出。

## 6. 建议的首个 90 天执行顺序

| 周期 | 交付物 | 验收标准 |
|---|---|---|
| 第 1—2 周 | 固定 Ghostty、配置原子写/恢复、日志和 crash dump | 干净机器可复现构建；坏配置可启动；崩溃可定位 |
| 第 3—4 周 | Win10/11 VM smoke、installer/update/uninstall 测试、签名方案 | 每个 release candidate 自动跑完整安装链路 |
| 第 5—6 周 | VT golden tests、fuzz、24h soak、性能基线 | P0 指标形成机器可执行 gate |
| 第 7—8 周 | OSC 133 + PowerShell/pwsh/cmd shell integration | cwd、command、output、exit code 可被可靠标记 |
| 第 9—10 周 | OSC 8/52、profile discovery、action registry | 常用 shell 零配置可用；敏感协议有安全策略 |
| 第 11—12 周 | 命令面板与第一版 command blocks | 可跳转、复制、重跑、查看退出码/耗时；键盘全可达 |

90 天后再启动 Agent control plane。否则 AI 层会建立在不稳定且无语义的字节流上，后续返工成本很高。

## 7. 明确不做或暂缓

- 暂不追求“所有终端功能最多”或逐项复刻 Tabby/WezTerm。
- 暂不开发自有大模型、自有云同步或账号体系。
- 暂不把插件系统、SFTP、图像协议放在可靠性和 OSC 133 前面。
- 暂不以 star 数作为目标；先追踪 crash-free、升级成功率、每周重复使用工作区、worktree/Agent 任务完成率。
- 不自动扫描大型父目录。项目加入应显式、可预测；可提供“最近 Git 仓库”建议，但必须由用户确认。

## 8. 产品决策原则

每个新功能进入开发前按以下顺序评估：

1. 是否降低崩溃、数据丢失、误操作或无法恢复的风险？
2. 是否让 Windows 开发者更快地从项目进入可工作状态？
3. 是否强化 repository/worktree/task/Agent 这条主线？
4. 是否能通过自动化测试和明确指标验收？
5. 如果 Windows Terminal、WezTerm 或 Tabby 已经做得很好，Liney 是否真的需要重复实现？

Liney 获得一席之地的关键不会是功能数量，而是形成一种明确体验：**打开 Liney，所有项目和并行任务都在正确的位置；Agent 在做什么一目了然；任何操作都可授权、可诊断、可恢复。**
