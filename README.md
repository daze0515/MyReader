# MyReader 小说阅读器

[English README](README.en.md)

一个 Windows 桌面端小说阅读器：C++ / Win32 + WebView2 宿主，本地 HTML/JS 阅读界面。支持在线站点阅读与本地 TXT，内置水印清洗、简繁转换、离线下载与 TXT 导出。

## 功能

- **在线阅读**：粘贴书籍目录页网址即可开始读；站点适配规则外置为 JSON（`data/sites/*.json`），新增/修复站点不用重新编译
- **Cloudflare 人机验证**：首次访问在真实浏览器窗口里手动点一下即可，之后由隐藏浏览器会话抓目录与正文，指纹一致不触发拦截
- **水印清洗**：字符白名单 + 广告行识别，清掉正文中随机注入的伪装字符与推广行
- **简繁转换**：内置 OpenCC 映射，默认简体，可切「保持原文」
- **本地 TXT**：拖入窗口或书架导入；章节自动切分（可自定义正则）、编码自动识别（UTF-8 / GBK / UTF-16）、文件移动后可重新定位
- **离线下载**：整本 / 接下来 N 章 / 自定义区间，断点续传，节奏可调，遇验证自动暂停
- **导出 TXT**：范围 / 编码 / 含标题 / 转简体可选
- **阅读体验**：主题、字体、字号、行距、段距、字距、页宽可调；键盘驱动滚动；章末自动续章；阅读进度记忆；目录虚拟滚动（上千章不卡）；封面本地缓存

## 按键

| 按键 | 行为 |
|---|---|
| ↓ / ↑ | 向下 / 向上滚动一个步长（设置里可调） |
| 空格 / Shift+空格、PgDn / PgUp | 向下 / 向上滚动一屏 |
| ← / → | 上一章 / 下一章 |
| Home / End | 本章开头 / 结尾 |
| Ctrl+, | 设置 |
| F11 | 全屏 |
| Esc | 退出沉浸模式 / 关闭面板 |

## 下载

直接到 [Releases](https://github.com/daze0515/MyReader/releases) 下载打包好的 zip，解压后运行 `Reader.exe`（需要系统已装 [WebView2 Runtime](https://developer.microsoft.com/microsoft-edge/webview2/)，Win11 与多数 Win10 自带）。阅读页文件在 exe 同级的 `web/` 目录里，别删。

## 从源码构建

需要 Windows + Visual Studio（含 C++ 桌面开发工作负载）。

```bat
:: 1. 拉取 WebView2 SDK 并解压到 .deps\wv2
curl -sSL -o .deps\wv2.nupkg https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.2903.40
:: 把 wv2.nupkg 当 zip 解压到 .deps\wv2（如用 tar: tar -xf .deps\wv2.nupkg -C .deps\wv2）

:: 2. 构建
build.bat
```

产物在 `dist\Reader.exe`。

## 测试

```bat
test.bat
```

包含 C++ 单元测试（提取 / 适配器 / TXT / 拖放）与前端冒烟脚本（`tests/*.mjs`，需 `READER_DEBUG_PORT=9336` 启动后跑）。抓取样本（`testdata/`）因版权原因未包含在仓库里，缺失时依赖样本的测试会自动跳过。

## 目录结构

```
src/      C++ 宿主：窗口 / WebView2 / 抓取调度 / 节流 / 代理 / 存储
web/      阅读页 UI（index.html / reader.css / reader.js）
res/      应用图标
tools/    make_icon.py 图标生成器（Pillow）
tests/    单元测试与 CDP 调试脚本
PLAN.md   完整设计与实现笔记
```

运行时数据（书架、设置、章节缓存、封面）写在 exe 同级的 `data/` 目录。

## 合规说明

本项目只对用户自己本可访问的内容做重新排版，不破解付费内容、不绕过登录、不内置书源库；内置节流、缓存与失败退避以降低对站点的压力。站点内容版权归原站点与作者所有，请仅作个人学习交流使用。

## License

[MIT](LICENSE)
