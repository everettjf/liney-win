# liney-win 备选方案:自建终端(libghostty-vt + DirectWrite)

> 与 [`TECH_PLAN.md`](./TECH_PLAN.md)(Fork Windows Terminal)并列的**备选路线**。
> 动机:Fork WT 会引入几十万行历史代码(含 conhost、OpenConsole、14 语言本地化、庞大测试)与微软商标包袱;若更看重「小而干净、完全自主」的代码库,可走自建。

---

## 1. 关键事实更正:libghostty-vt 包揽了终端核心

经查 [vt.h](https://github.com/ghostty-org/ghostty/blob/main/include/ghostty/vt.h) 与 [libghostty 文档](https://libghostty.tip.ghostty.org/),libghostty-vt **不只是解析器**,它维护完整终端状态:

- 屏幕网格(cells + 属性:颜色/粗体/斜体/反显,24bit/256 色)
- **scrollback 回滚历史**
- **line wrapping + resize reflow**(终端最难的部分之一)
- Unicode / grapheme 簇、宽字符
- **render-state diff API**:只告诉你「哪些 cell 变了、怎么画」,无需全帧重绘

⚠️ 注意:libghostty-vt 的 C API 仍处 pre-stable,会有破坏性变更;Windows 在库级别支持,但参考实现 Ghostling 尚未在 Windows 充分验证。需在 S0 spike 中先验证。

---

## 2. 分层与工作量

| 层 | 负责方 | 工作量 |
|---|---|---|
| PTY 起 shell | ConPTY(`CreatePseudoConsole`)自写薄封装 | 小 |
| **VT 解析 + 屏幕状态 + scrollback + reflow + Unicode** | **libghostty-vt(复用)** | 链接即用 |
| 渲染(画网格) | DirectWrite 直绘(MVP)→ glyph atlas + D3D11(后期) | 中,render-state diff API 减负 |
| 窗口 / 键鼠 / IME / 剪贴板 | Win32 自写 | 中 |
| UI 外壳(tab / 分屏 / 侧边栏 / 设置) | 自写,**需选 UI 框架(见下)** | 中—大 |

预估 MVP 量级 **1.5–3 万行**,远小于 fork WT。

---

## 3. 待决:UI 外壳框架

终端格子自绘,但 tab/侧边栏/设置等 chrome 需要框架:

| 选项 | 优 | 劣 |
|---|---|---|
| **Win32 + Direct2D 全自绘**(Ghostty/Alacritty 风) | 最小、最一致、零额外依赖 | chrome 全自画,IME/无障碍细节多 |
| **WinUI 3 (XAML)** | 原生现代 chrome,画的活少 | 依赖/复杂度回升(仍远小于 WT) |
| Qt | widget 成熟、跨平台 | 大依赖、LGPL/商业授权 |
| Dear ImGui | 出活最快 | 非原生观感,产品级体验打折 |

**倾向**:要极简自主选 Win32+Direct2D 自绘;要省 chrome 工作量、原生观感选 WinUI 3。**此决策未定。**

---

## 4. 里程碑(自建本地终端 MVP)

| 里程碑 | 内容 | 验收 |
|---|---|---|
| **S0 Spike** | ConPTY 起 shell + 链接 libghostty-vt + 跑通 render-state API;验证 Windows 可用性与 API 暴露程度 | 最小窗口/控制台能拿到网格数据并刷新 |
| **S1 只读渲染** | DirectWrite 把一屏网格画出来 | 能看到 shell 输出 |
| **S2 输入** | 键盘 + IME + 鼠标,送回 ConPTY | 能交互执行命令 |
| **S3 终端完善** | 滚动/scrollback、选择/复制粘贴、resize+reflow(靠 libghostty-vt)、光标/选区绘制 | 单 pane 体验接近成熟终端 |
| **S4 单窗口可用** | 配置、字体、配色 | 可日常使用的单终端 |
| **S5 工作区雏形** | UI 外壳(tab/分屏)+ 左侧仓库/worktree 侧边栏,点目录开终端 | liney 价值闭环 |

相比 Fork WT 路线,里程碑更多(终端外观/输入这些 WT 白送的要自建),但每步都小、代码完全自主。

---

## 5. 与 Fork WT 的取舍

| 维度 | Fork Windows Terminal | 自建(libghostty-vt + DirectWrite) |
|---|---|---|
| 代码量 | 几十万行(含历史包袱) | 1.5–3 万行 |
| 终端核心(解析/缓冲/reflow) | WT 现成 | libghostty-vt 现成 |
| 渲染 | AtlasEngine 现成 | 自写 DirectWrite(render-state API 减负) |
| 输入/IME/tab/分屏 | 现成 | 自写 |
| 上手速度 | 快(但代码海量难导航) | 渲染/输入要自建,但代码可控 |
| 自主可控 / 体积 / 启动内存 | 弱 | 强 |
| 主要风险 | 代码庞大、商标剥离、上游同步 | libghostty-vt API 不稳定、Windows 未充分验证 |

**结论**:两条路都成立。看重「快速拿到完整终端」选 Fork WT;看重「小而干净、完全自主」选自建——后者因 libghostty-vt 包揽核心而比初版评估轻得多。
