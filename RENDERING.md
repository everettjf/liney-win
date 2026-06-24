# 渲染设计:Direct2D/DirectWrite 直绘 → glyph atlas + D3D11

> liney-win 自建终端的渲染演进设计。配合 [`ALT_PLAN_SELFBUILT.md`](./ALT_PLAN_SELFBUILT.md)。

---

## 终端渲染在画什么

终端本质是一张**等宽网格**(行 × 列),每个 cell 携带:一个 grapheme、前景色、背景色、属性(粗/斜/下划线/反显)。libghostty-vt 维护这张网格并提供「哪些 cell 变了」的 diff,渲染器负责**把网格变成像素**。

**等宽**是后续所有优化的基础——cell 是固定大小的点阵。

两个阶段共用底层地基:**DXGI swap chain + D3D11 设备**把画面 present 到 Win32 HWND。差异只在「怎么把 cell 画出来」。

---

## 阶段一:Direct2D / DirectWrite 直绘(MVP)

分工:
- **DirectWrite**:字形整形、连字、字体回退、CJK、emoji、组合字符
- **Direct2D**:2D 绘图(跑在 D3D11 上,GPU 加速)

每帧:
1. 逐 cell `FillRectangle` 填背景
2. 逐 cell(或逐 run)`DrawText`/`DrawGlyphRun` 画前景字形
3. 画光标、选区覆盖层

**优点**:简单、上手快;最难的排版/字体回退/emoji 由 DirectWrite 包办;D2D 本身走 GPU,日常交互够用。

**瓶颈**:每帧重新排版 + 重新栅格化,且每 cell/run 一次 API 调用。同一字形在屏幕上重复几千次却被反复栅格化——纯浪费。平时无感,但 **4K 屏 + 满屏快速滚动**时 per-call 开销堆成掉帧。

> 这正是 Windows Terminal 的弯路:老 DxRenderer 偏直绘扛不住,后来重写为 AtlasEngine。

对应实现:[`src/render/D2DRenderer.cpp`](./src/render/D2DRenderer.cpp)。

---

## 阶段二:glyph atlas + D3D11(后期优化)

**核心洞察**:终端用到的不同字形很少(字体在某字号下的字符集)。每个字形**只栅格化一次**存进纹理,之后复用。

管线:
1. **Glyph atlas**:一张 GPU 纹理。首次遇到某字形(如「粗体 A」),用 DirectWrite/Direct2D 栅格化进一个空槽,记下其纹理坐标。缓存 key ≈ `(grapheme, 字体, 粗体, 斜体, 字号)`。
2. **网格打包成 GPU 数据**:每个可见 cell 产出 `atlas 槽 + 前景色 + 背景色 + 格子坐标`,整屏是一个矩阵。
3. **一次 draw call**:per-cell 数据上传 instance/structured buffer,一个 HLSL shader 逐 cell 从 atlas 采样、套色、贴格子。几千 cell 一次 GPU 调用。
4. 只更新变化的 cell(libghostty-vt 的 diff);已缓存字形「免费」。

**为什么快**:每个字形只栅格化一次;全屏基本一次 draw call;DirectWrite 仅在出现新字形时调用(预热后极少);重活交给 GPU 并行。即 AtlasEngine 的做法。

**代价**:atlas 管理(LRU 淘汰、多页、resize)、宽字形(CJK 占 2 格)、彩色 emoji、连字(跨格)、组合字符、亚像素抗锯齿、HLSL/D3D11 资源管理——代码量明显更大。

---

## 关键设计:渲染器接口隔离两阶段

从第一天起把渲染抽象成接口 [`IRenderer`](./src/render/IRenderer.h):

```
IRenderer::render(grid)   // 后续扩展 dirty set / cursor / selection
```

- 阶段一:`D2DRenderer` 实现
- 阶段二:新增 `AtlasRenderer` 实现,**切换 = 替换一个实现,不返工**
- 两者共用同一套 DXGI/D3D11 present 地基,差异关在接口后
- D2D 版还可作为老 GPU / WARP 软件渲染的兜底

**一句话:阶段一借 DirectWrite 把「对」做出来,阶段二借 glyph atlas 把「快」做出来,中间用接口隔开。**
