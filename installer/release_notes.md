# ETC WindowBuilder 专业版 v11.0

一款用 **纯 C 语言 / Win32 / GDI+** 从零实现的可视化 GUI 构建器与积木式编程 IDE，面向所有技术人员学习与开发。版权所有 **ET / ET Studio**，(c) 2024-2026。

## 下载

| 文件 | 说明 | 大小 |
|---|---|---|
| **ETCWindowBuilder-Setup-v11.0.exe** | 完整离线安装包，**装完即用**（内置三套工具链） | 约 284 MB（安装后约 1.1 GB） |
| **ETC-WindowBuilder.exe** | 绿色版主程序（不含内置工具链，需自备编译器） | 约 1 MB |

> 支持 Windows 7 / 10 / 11（x64）。安装包会请求管理员权限，默认安装到 `C:\Program Files\ET Studio\ETCWindowBuilder`，并创建开始菜单与桌面快捷方式。

## 内置工具链（无需另装、无需配置 PATH）

- **MinGW-w64 GCC 14.2**（C/C++ 编译器、windres、ld、g++）— `runtime\mingw64`
- **CPython 3.11.16**（含 tkinter / Tcl-Tk）— `runtime\python`
- **Eclipse Temurin OpenJDK 17.0.20**（javac / java / jar）— `runtime\jdk`

## 核心功能

- 可视化 UI 窗口设计：拖放按钮、标签、输入框、复选框、单选钮、下拉列表、进度条、图片，以及星形 / 菱形 / 圆形特殊形状，支持缩放、对齐、层级、颜色与字体设置。
- 积木式编程：为「窗口启动 / 按钮点击 / 窗口关闭」等事件组装动作，无需记语法。
- 设计画布、C 代码、Python 代码三向实时同步。
- 一键编译运行 C（内置 GCC，生成静态独立 EXE）、运行 Python（tkinter）、编译运行 Java；文件模式可直接处理磁盘上的 `.c` / `.py` / `.java`。
- ET 引擎加密工程文件 `.nep`（PBKDF2-HMAC-SHA256 + ChaCha20，可设密码）。
- 一键把项目打包为带 ET 版本资源与图标的 EXE，并生成 NSIS 安装程序。

## 从源码构建

见仓库 README「从源码构建」一节：使用 MinGW-w64 交叉编译 9 个 C 源文件 + 资源，再用 NSIS 制作离线安装包。

## 学习文档

仓库 `docs/tutorials/` 内含 8 篇中文教程：快速入门、可视化设计、积木编程、C/Python/Java 开发、加密项目与打包、内置工具链说明。

## 许可

本软件版权归 **ET / ET Studio** 所有，可自由用于学习与个人开发，禁止未经授权的商业再分发。
内置 MinGW-w64/GCC、CPython、Eclipse Temurin/OpenJDK 遵循各自开源许可。
