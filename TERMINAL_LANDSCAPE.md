# 主流终端技术栈调研(GitHub)

> 目的:为 liney-win 自建路线的渲染/UI 选型提供横向参考。重点关注**原生 GPU 渲染、非 WebView** 的终端。
> 数据为 2026 年中近似值,星标会变动。

---

## A 类:原生 GPU 渲染(无 WebView)—— 参考对象

| 终端 | Star(约) | 语言 | 渲染技术 | Windows 原生 | 备注 |
|---|---|---|---|---|---|
| **Windows Terminal** | ~95k | C++/C++WinRT | **DirectWrite + Direct2D + D3D11**(AtlasEngine)+ XAML chrome | ✅ | **与本项目暂定栈一致;MIT 参考实现** |
| **Alacritty** | ~63k | Rust | OpenGL | ✅ | 极简,无 tab/分屏,内存最低(~22MB) |
| **Ghostty** | ~45k | Zig | Metal(mac)/ OpenGL | 社区 fork | 原 liney 终端内核 |
| **Kitty** | ~23k | Python + C | OpenGL | ❌ | 图形协议强,无 Windows 原生 |
| **WezTerm** | ~23k | Rust | OpenGL / WebGPU | ✅ | 自带多路复用;维护放缓 |
| **Contour** | 数千 | C++ | OpenGL | ✅ | C++ 阵营参考 |
| **Rio** | 数千 | Rust | WebGPU(wgpu) | ✅ | 新锐现代 GPU 栈 |
| **ConEmu** | 数千 | C++ | Win32/GDI(非 GPU) | ✅ | 老牌,技术偏旧 |

## B 类:WebView / Electron —— 明确避开

| 终端 | Star(约) | 技术 |
|---|---|---|
| **Hyper** | ~44k | Electron + xterm.js |
| **Tabby** | 高 | Electron + Angular + xterm.js |
| **FluentTerminal** | 中 | UWP(C#)+ 内嵌 xterm.js(WebView) |

体积大、内存高、非原生观感。无严肃高性能终端用 WebView 渲染终端格子。

---

## 对 liney-win 的结论

1. **严肃终端分两阵营**:Rust + GPU(Alacritty/WezTerm/Rio)与 C++ + DirectWrite/D3D(Windows Terminal)。WebView 系是另一条「省事但臃肿」的路,刻意避开是对的。

2. **Win32 + Direct2D 选型得到验证**:在「Windows 原生 + C++ + GPU」交集里,微软 AtlasEngine 就是 **DirectWrite + Direct2D + D3D11**,与本项目暂定栈完全重合,且有 MIT 生产级实现可抄。跨平台终端用 OpenGL/WebGPU 是为跨平台;本项目只做 Windows,Direct2D/DirectWrite 最原生。

3. **演进路径**:现代 GPU 终端统一套路——DirectWrite 栅格化进 glyph atlas → D3D11 + shader 贴格子。MVP 用 Direct2D/DirectWrite 直绘起步,性能不够再升级到 atlas。

4. **chrome 自绘有先例**:Alacritty 极简无 chrome,WezTerm/Ghostty 自绘或用平台原生 UI,说明「Win32 + Direct2D 自绘 chrome」是成立的工程路线。
