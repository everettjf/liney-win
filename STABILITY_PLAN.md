# 稳定可用计划:距离一个「日常顺手」的终端还差什么

> [`ROADMAP.md`](./ROADMAP.md) 盘点的是**功能广度**(对标 macOS liney:SSH / agent / tmux / 打包更新 等),其中很多项已打勾。
> 本文档关注的是另一条正交的轴——**正确性、健壮性、性能与日常使用的毛刺**,也就是「稳定可用」。
> 每条都带**代码证据**(`file:line`),按影响 × 成本排优先级。结论基于对当前源码的通读。

---

## TL;DR — 当前最该补的五件事

1. **括号粘贴(bracketed paste)未接线** —— 多行粘贴会被 shell 直接逐行执行,是安全/误操作隐患。
2. **空闲时仍 60fps 满帧重绘** —— 没事干也跑满一个核,费电发热。
3. **完全没有 DPI 感知** —— 在高分屏(多数笔记本 >100% 缩放)上被系统位图拉伸,字发虚。
4. **鼠标上报缺失** —— vim / htop / tmux / lazygit / fzf 收不到鼠标。
5. **宽字符(CJK)/斜体/光标样式**等渲染正确性的零散缺口。

前三件是**低成本、高收益**,应最先做。

---

## P0 — 正确性 / 体验「踩雷」位(优先,且大多改动很小)

### 0.1 括号粘贴(bracketed paste)在 libghostty 路径接线
- **现状**:`Terminal::bracketedPaste()` 永远 `return false`(`src/vt/Terminal.cpp:176`)。
  粘贴逻辑其实已支持(`src/app/WindowMouse.cpp` `paste()` 里有 `\x1b[200~ … \x1b[201~` 分支),
  但因查询恒为 false 而走不到。`ROADMAP.md` 也已标注「bracketed-paste 需在 libghostty 路径重新接线」。
- **后果**:把一段多行命令粘进 shell / vim,会被当成「逐行回车」**直接执行**——既是误操作,也是安全隐患。
- **方案**:通过 libghostty 的模式查询 API 读 DEC private mode `?2004`,据此返回真实状态。
- **成本**:小(几十行,取决于 C API)。

### 0.2 空闲不重绘:把 60fps 满帧改成「按需绘制」
- **现状**:`Window::runMessageLoop()`(`src/app/Window.cpp:124`)在 `PeekMessage` 空转后**每轮都调 `renderFrame()`**,
  `endFrame()` 用 `Present(1,0)`(vsync)。即**没有输入、没有输出时照样 60fps 重绘**,且每个 cell 每帧重新 `DrawText`(`src/render/D2DRenderer.cpp:356+`)。代码里**无** `WaitMessage` / `WM_TIMER`(已确认)。
- **后果**:空闲也吃满一个 CPU 核,笔记本掉电、风扇起飞。
- **方案**:引入「脏标记 + 节流」——
  - 有 PTY 新输出 / 输入 / 光标闪烁 / 选区变化时才标记 dirty;
  - 用 `MsgWaitForMultipleObjects`(等消息或等 PTY 可读事件)替代忙等,空闲时真正休眠;
  - 或保底用一个 ~16ms 的 `WM_TIMER` + dirty 标记。
- **成本**:中(改主循环 + 加一个 dirty 信号)。**收益最大的一项。**

### 0.3 DPI 感知(Per-Monitor v2)+ `WM_DPICHANGED`
- **现状**:全仓**无任何 DPI 处理**——`src/main.cpp` 无 `SetProcessDpiAwarenessContext`,
  `res/resource.rc` 未嵌 DPI manifest,窗口过程不处理 `WM_DPICHANGED`(均已确认)。
- **后果**:进程为 DPI-unaware,系统把整窗**位图拉伸**;在 125%/150%/200% 缩放下(现代笔记本默认)**字发虚**;
  多显示器跨屏不重算 cell 尺寸。
- **方案**:
  - 在 manifest 或 `main.cpp` 声明 `PerMonitorV2`;
  - 处理 `WM_DPICHANGED`:按新 DPI 重建字体、重算 `metrics_.cellW/cellH`、`SetWindowPos` 到建议矩形;
  - 字号、`strokeRect` 线宽等按 DPI 缩放。
- **成本**:中小。**对观感影响极大。**

---

## P1 — 终端正确性(让 TUI 程序真的能用)

### 1.1 鼠标上报透传
- **现状**:`ROADMAP.md` P1 已标 ⬜,并指出 ConPTY 会吸收子程序的 `?1000h`,host 输出流里收不到。
- **后果**:vim 可视块、htop 点列、tmux 选窗、lazygit / fzf 鼠标全部失效。
- **方案**:走 ConPTY 的鼠标透传(参考 Windows Terminal 处理);把 `WM_MOUSE*` 在鼠标模式开启时编码成 SGR/X10 上报序列回写 PTY。
- **成本**:中大(需要 ConPTY 模式探测)。

### 1.2 宽字符(CJK)与 spacer cell 渲染正确性
- **现状**:`D2DRenderer::drawGrid`(`src/render/D2DRenderer.cpp:356`)对每个 cell 用固定 `cellW` 画;
  快照 `Terminal::snapshotInto`(`src/vt/Terminal.cpp:62`)未见对「宽字符占 2 格、第二格为 spacer」的显式处理。
- **风险**:CJK / emoji 字形可能溢出单格或在尾格重复绘制、选区/查找按列计数与视觉错位。
- **方案**:快照阶段读取 libghostty 的 cell 宽度/spacer 标记;渲染时宽字形按 2×cellW 出图,spacer 跳过。需在真机用中文/emoji 核对。
- **成本**:中。

### 1.3 斜体 / 暗淡 / 删除线渲染
- **现状**:`kFlagItalic` 在 `src/render/Cell.h:30` 已定义,但 `drawGrid` 只用了 bold + underline,**斜体从不渲染**。
- **方案**:补斜体 text format(类似已有的 `textFormatBold_`);若 libghostty 暴露 dim/strikethrough 一并补。
- **成本**:小。

### 1.4 光标样式 / 闪烁 / 失焦空心
- **现状**:`drawGrid` 只画一个半透明方块(`src/render/D2DRenderer.cpp:415`),
  不支持 `DECSCUSR`(条形/下划线/方块)、不闪烁、失焦不变空心。
- **方案**:从 libghostty 读光标形状;非聚焦 pane 画空心框;加可选闪烁(配合 0.2 的 dirty 节流)。
- **成本**:小中。

### 1.5 scrollback 容量可配置且更大
- **现状**:`max_scrollback = 1000` 硬编码(`src/vt/Terminal.cpp:31`)。
- **后果**:跑构建 / 长日志时历史不够。
- **方案**:提到 config(如 `scrollback`,默认 10000),`Terminal::create` 读取。
- **成本**:小。

---

## P2 — 日常毛刺 / 打磨

- **2.1 选区跟随内容**:当前选区是**视口坐标**(`Window` 的 `selAX_/selAY_…`,见 `src/app/WindowMouse.cpp`),
  新输出/滚动后高亮停在原屏幕格而非原文本。锚定到 scrollback 绝对行更稳。**(中)**
- **2.2 矩形/块选**(Alt+拖):复制表格/对齐文本常用。**(小中)**
- **2.3 可点击 URL**:OSC 8 + 启发式识别,`Ctrl+Click` 打开;鼠标悬停下划线。**(中)**
- **2.4 查找扩到整段 scrollback**:本次 PR 的查找是**视口内 + 翻页**;有了缓冲区查询 API 后做成跨历史一次性定位。**(中)**
- **2.5 设置界面 / 配置热重载**:现在改 `config.json` 要重启;可做文件监视热重载或一个最简设置面板。**(中)**
- **2.6 窗口状态记忆**:记住大小/位置/最大化;支持「新建窗口」。**(小)**
- **2.7 标签打磨**:标签上的关闭按钮、双击标签栏空白处新建、`Ctrl+1..9` 跳标签、OSC 标题截断更聪明。**(小)**
- **2.8 关闭确认**:pane 里有前台进程在跑时关闭/退出给提示。**(小)**

---

## P3 — 性能 / 引擎(仅在重载场景才成为瓶颈)

- **3.1 glyph atlas + D3D11**:见 [`RENDERING.md`](./RENDERING.md) 阶段二。4K + 满屏快速滚动时,
  当前逐 cell `DrawText` 的 per-call 开销会掉帧。**注意:P0.2 的「空闲不重绘」比这个更影响日常体感,应先做。**
- **3.2 脏区/差量渲染**:接 libghostty 的 dirty diff,只重画变化的 cell(与 0.2 协同)。

---

## P4 — 健壮性与「能发布」

- **4.1 崩溃日志 / 诊断**:目前失败大多**静默**(如 `session->start` 失败直接 return)。加一个日志文件 + 顶层 `__try`/SetUnhandledExceptionFilter 落最小转储,便于收 bug。**(中)**
- **4.2 内核缺失/不兼容的清晰报错**:`ghostty-vt.dll` 缺失或 ABI 不匹配时 `Terminal::create` 返回 false → 可能静默退出;应弹出可读错误。**(小)**
- **4.3 自动测试 + CI**:仓库目前**无自动化测试**。建议加:
  - VT 快照黄金测试(喂固定字节序列,断言 Grid);
  - `selectionText()` 提取、config / layout JSON 往返、版本比较的单测;
  - GitHub Actions 在 Windows 上构建(Zig + MSVC)防回归。**(中,长期回报高)**
- **4.4 自动更新的供应链安全**:`checkForUpdates`/`startDownloadAndInstall`(`src/app/WindowSession.cpp:102+`)
  会下载并运行 release 里任意 `*setup.exe`/`*.exe`。应校验**签名或发布哈希**,降低 release 被篡改时的风险。**(中)**

---

## 建议执行顺序

1. **先做 P0 三件**(括号粘贴、空闲不重绘、DPI)——成本低、每天都能感知,直接把「发虚 + 费电 + 粘贴踩雷」三个最扎眼的问题解决。
2. **再做 P1**(鼠标上报、宽字符、斜体、光标、scrollback)——补齐「TUI 程序能用 + 渲染正确」。
3. P2 打磨毛刺,P4.3 的测试/CI 尽早起一个最小版以防回归。
4. P3(atlas)与 P4 其余按需推进。

> 一句话:ROADMAP 把**广度**铺开了;要「稳定可用」,核心是补**正确性(粘贴/鼠标/宽字符/DPI)**与**资源占用(空闲不重绘)**这两块。
