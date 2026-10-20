# Sigil 修改版

基于 Sigil 的 EPUB 编辑器修改版

本项目是草莓佬维护的 [ichigo250/sigil-modified](https://gitee.com/ichigo250/sigil-modified)
的继续改版，以其中的 `Sigil-1.9.30.X4A` 为代码底本，并选择性合并了后续修改版中的修复与功能。

## 项目简介

Sigil 对 EPUB 规范支持完善、操作界面友好，但编辑包含数百至数千个文件的大型 EPUB 时，部分操作可能耗时较长。
草莓佬的修改版围绕大型 EPUB 的编辑效率、代码编辑体验、格式转换和实用工具进行了长期优化；本仓库在此基础上继续维护稳定性和常用功能。

## 下载

可在 [GitHub Releases](https://github.com/RRRRUDDDD/re-sigil-modified/releases) 下载自动构建的 Windows x86/x64 安装程序和 macOS x64/ARM64 DMG。

macOS 自动构建目前未签名、未公证，首次打开时可能被 Gatekeeper 拦截。

## 主要功能

### 大型 EPUB 与资源管理

- 优化批量删除、批量重命名等操作在大量文件下的执行效率。
- 优化大型 EPUB 设置封面页的耗时。
- 添加字体文件时可覆盖同名文件。
- 修复插件导入同名素材时可能闪退、无法覆盖或资源失联的问题。
- 支持从文件浏览器把图片、SVG、音视频或字体引用插入当前 HTML/CSS 光标处。
- HTML/CSS 代码编辑器可直接粘贴图片数据或本地图片文件，并自动导入 EPUB。
- 书籍浏览器中悬停图片文件可预览缩略图及尺寸信息。
- 图书浏览器支持将单选或多选 HTML 文件整体移动到指定 HTML 文件之前或之后，快速调整阅读顺序。

### 代码编辑

- 支持多行缩进与反缩进，Tab 输出两个空格。
- HTML 换行自动缩进，输入 `</` 自动补全闭合标签。
- CSS 智能缩进及代码块结束符对齐。
- 新增独立的 CSS 编辑器停靠面板（`视图 → CSS 编辑器`，`Ctrl+Alt+E`），与 CSS 标签页和资源文档实时同步；停止输入后自动写回工作副本并刷新预览，无需手动保存或重载。
- 支持自定义 XHTML 格式化规则。
- 修复自定义 XHTML 格式化遇到未闭合节点、以及在 CSS 文档首字符输入 `/` 时的闪退问题。
- 支持粘贴带格式文本。
- Emmet 支持识别大写字母。
- 支持批量套用 `p`、`div` 标签，以及使用 `Ctrl+Return` 分割段落或插入 `br` 空行。

### EPUB 检查与转换

- 支持 EPUB 2 与 EPUB 3 相互转换。
- F7【EPUB 格式良好性检查】使用严格 XML 检查，并整合 OPF 规范化。
- OPF 检查可处理重复 ID、无效 href、href 大小写不一致、无效 ID 引用及未登记文件。
- OPF 在导入、保存、自动修正与元数据编辑过程中均会保留注释；自动修正同时保留 EPUB 3 `collection` 和未知扩展节点。

### 查找与替换

- 保存搜索批量替换采用内存预演机制，大幅提升批量操作性能；修复“当前文件”范围替换无反应及 Ctrl 限定“替换所有”失效的问题。
- 支持 `^`、`$`、`\b` 等零宽正则表达式匹配。
- 单文件替换保留原生撤销历史；Replace All、选择替换、Mend HTML 与 Mend and Prettify All 的跨文件修改可作为一个批次撤销或重做。
- 批量写回前会校验资源快照，失败时不会留下部分修改；不再自动创建恢复检查点，手动检查点功能保持不变。

### 编辑工具

- 编辑目录（TOC）支持多选条目进行批量升降级与删除。
- 编辑目录时会同步更新 EPUB 3 导航文档；书内存在 NCX 时一并更新。由导航生成的 NCX 统一 `navPoint-<n>` 命名并补齐 `playOrder`。
- 插件快速启动工具栏扩展至 4 组共 20 个插件槽位。

### 其他改进

- 保存插件快捷键映射后立即刷新快捷启动图标。
- 调整 TXT 导入逻辑，使分行文本按段落处理。
- 预览复制按 XHTML 结构规范化为纯文本，保留空行与 `<pre>` 空白；ruby 注音以全角括号输出。
- 补齐简体/繁体中文标准按钮与插件槽位翻译；代码编辑器、查找框和预览的右键菜单中，剪切、复制、粘贴、全选等标准动作已汉化。
- 统一菜单、工具栏、快捷键和编辑器右键菜单的撤销/重做入口。
- 修复页面底部按 `Ctrl+X` 时偶发跳回文档开头的问题；降低快速切换文件、查找、编辑目录等操作下的闪退风险。
- 修复 Windows 下关闭 Sigil 时卡顿数秒、退出后剪贴板内容丢失的问题。
- 加固 EPUB 导入导出与资源失败处理：限制 ZIP 解压资源用量、校验导入路径与解压写入、检查导出收尾；资源加载或重命名失败时保持原状态。
- 修复搜索循环、快捷键、添加副本等修改版既有问题。
- 禁用启动时自动检查 Sigil 官方更新，避免跨代码线升级提示。

## 支持平台

- Windows 10、Windows 11，提供 x86 与 x64；不支持 Windows 7，Windows 8 未测试。
- macOS x64 支持 11 及更高版本，ARM64 支持 12 及更高版本；x64 使用 Qt 5，ARM64 使用 Qt 6。

## 部分功能演示

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
- Sigil-Enhanced：[3tic-project/Sigil-Enhanced](https://github.com/3tic-project/Sigil-Enhanced)

感谢草莓佬对 Sigil 修改版的开发和维护，感谢 Sigil-Enhanced 项目的贡献者，以及 Sigil 官方项目的所有贡献者。

## 许可证

本项目遵循 GNU General Public License v3.0，详见 [COPYING.txt](COPYING.txt)。
