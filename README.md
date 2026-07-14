# Sigil 修改版

基于 Sigil 的 EPUB 编辑器修改版

本项目是草莓佬维护的 [ichigo250/sigil-modified](https://gitee.com/ichigo250/sigil-modified)
的继续改版，以其中的 `Sigil-1.9.30.X4A` 为代码底本，并选择性合并了后续修改版中的修复与功能。

## 项目简介

Sigil 对 EPUB 规范支持完善、操作界面友好，但编辑包含数百至数千个文件的大型 EPUB 时，部分操作可能耗时较长。
草莓佬的修改版围绕大型 EPUB 的编辑效率、代码编辑体验、格式转换和实用工具进行了长期优化；本仓库在此基础上继续维护稳定性和常用功能。

X5 仍保持在 Sigil 1.9.30 ，没有升级到 Sigil 2。

## 下载

可在 [GitHub Releases](https://github.com/RRRRUDDDD/re-sigil-modified/releases) 下载自动构建的 Windows x86/x64 安装程序和 macOS x64/ARM64 DMG。

macOS 自动构建目前未签名、未公证，首次打开时可能被 Gatekeeper 拦截。

## 主要功能

### 大型 EPUB 与资源管理

- 优化批量删除、批量重命名等操作在大量文件下的执行效率。
- 优化大型 EPUB 设置封面页的耗时。
- 添加字体文件时可覆盖同名文件。
- 支持从文件浏览器把图片、SVG、音视频或字体引用插入当前 HTML/CSS 光标处。
- HTML/CSS 代码编辑器可直接粘贴图片数据或本地图片文件，并自动导入 EPUB。

### 代码编辑

- 支持多行缩进与反缩进，Tab 输出两个空格。
- HTML 换行自动缩进，输入 `</` 自动补全闭合标签。
- CSS 智能缩进及代码块结束符对齐。
- 支持自定义 XHTML 格式化规则。
- 支持粘贴带格式文本。
- Emmet 支持识别大写字母。
- 支持批量套用 `p`、`div` 标签，以及使用 `Ctrl+Return` 分割段落或插入 `br` 空行。

### EPUB 检查与转换

- 支持 EPUB 2 与 EPUB 3 相互转换。
- F7【EPUB 格式良好性检查】使用严格 XML 检查，并整合 OPF 规范化。
- OPF 检查可处理重复 ID、无效 href、href 大小写不一致、无效 ID 引用及未登记文件。
- OPF 自动修正会保留注释、EPUB 3 `collection` 和未知扩展节点。
- 修复 XHTML 自定义格式化、CSS 首字符 `/` 等场景下的崩溃问题。

### 其他改进

- 保存插件快捷键映射后立即刷新快捷启动图标。
- 调整 TXT 导入逻辑，使分行文本按段落处理。
- 修复搜索循环、快捷键、添加副本等修改版既有问题。
- 禁用启动时自动检查 Sigil 官方更新，避免跨代码线升级提示。

## 支持平台

- Windows 10、Windows 11，提供 x86 与 x64；不支持 Windows 7，Windows 8 未测试。
- macOS x64 支持 11 及更高版本，ARM64 支持 12 及更高版本；x64 使用 Qt 5，ARM64 使用 Qt 6。

## 功能演示

### 多文件重命名和删除

![多文件重命名和删除](docs/modified_version_gif/demo-1.gif?raw=true)

### 多行缩进与自动缩进

![多行缩进与自动缩进](docs/modified_version_gif/demo-2.gif?raw=true)

### 闭合标签自动补全

![闭合标签自动补全](docs/modified_version_gif/demo-3.gif?raw=true)

### 批量套用 P、DIV 标签

![批量套用标签](docs/modified_version_gif/demo-4.gif?raw=true)

### 分割段落与插入 BR 空行

![分割段落与插入空行](docs/modified_version_gif/demo-5.gif?raw=true)

## 项目来源与致谢

- 修改版主要代码与既有功能：[草莓佬 / ichigo250/sigil-modified](https://gitee.com/ichigo250/sigil-modified)
- Sigil 官方项目：[Sigil-Ebook/Sigil](https://github.com/Sigil-Ebook/Sigil)

感谢草莓佬对 Sigil 修改版的开发和维护，以及 Sigil 官方项目的所有贡献者。

## 许可证

本项目遵循 GNU General Public License v3.0，详见 [COPYING.txt](COPYING.txt)。
