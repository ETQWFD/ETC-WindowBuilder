# 05 · Python 开发

构建器同时为同一份设计生成 **Python 3 + tkinter** 代码，适合快速验证界面逻辑。

## 运行

- 切到「Py代码」页，点顶部 **运行**。
- 构建器调用内置 `runtime\python\python.exe`，以隔离模式执行：
  `python.exe -I -X utf8 main.py`
- 无需系统安装 Python，tkinter 已随内置环境提供。

## 生成代码结构

```python
# -*- coding: utf-8 -*-
import tkinter as tk
from tkinter import ttk, messagebox
import os, time, webbrowser, sys

class Program:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("..."); self.root.geometry("800x600")
        self.vars = {}; self.components = {}; self._pos = {}
        self.create_widgets()
        self.on_init()
    def create_widgets(self):
        # tk.Button / tk.Label / tk.Entry / Checkbutton / Radiobutton
        # ttk.Combobox / ttk.Progressbar
        # 按钮 config(command=lambda i=N: self.on_btn(i))
    def on_btn(self, idx):
        if idx == 0:
            ...        # 按钮积木逻辑
    def on_init(self): ...
    def run(self): self.root.mainloop()

if __name__ == "__main__":
    Program().run()
```

## 积木到 Python 的映射

| 积木 | Python |
|---|---|
| 显示消息框 | `messagebox.showinfo("提示", "...")` |
| 设置文本 | `self.components["cN"].config(text="...")` |
| 打开文件 | `os.startfile(r"路径")` |
| 打开网址 | `webbrowser.open("URL")` |
| 退出程序 | `self.root.destroy()` |
| 隐藏/显示 | `.place_forget()` / `.place(...)` |
| 等待 N 毫秒 | `time.sleep(N/1000)` |
| 变量赋值 | `self.vars["名"] = 值` |

## 文件模式运行 .py

「文件」页选中任意 `.py`，点 **▶ 编译并运行文件**，用内置 Python 就地运行，
标准输出 / 报错会显示在实时日志里。

## 在自己电脑上用系统 Python

若你另装了 Python，也可直接 `python main.py`；tkinter 是标准库，Windows 官方安装包默认自带。
