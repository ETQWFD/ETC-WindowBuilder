# 06 · Java 开发

安装包内置 **Eclipse Temurin OpenJDK 17**，可直接编译运行 Java 程序（GUI 设计器本身生成 C/Python；
Java 支持面向文件模式的学习与练习）。

## 编写并运行一个 Java 程序

1. 在「文件」页进入你的工作目录（或用外部编辑器）。
2. 新建 `Hello.java`（文件名必须与 public 类名一致）：

```java
import javax.swing.JOptionPane;
public class Hello {
    public static void main(String[] args) {
        JOptionPane.showMessageDialog(null, "你好, ETC WindowBuilder!");
    }
}
```

3. 在文件列表选中 `Hello.java`，点 **▶ 编译并运行文件 (C/Py/Java)**。

构建器依次执行：

```bat
runtime\jdk\bin\javac.exe -encoding UTF-8 Hello.java
runtime\jdk\bin\java.exe  -Dfile.encoding=UTF-8 -cp <目录> Hello
```

编译错误会弹出并写入实时日志；成功则运行主类。

## Swing / AWT 图形界面

JDK 自带 Swing，可直接写桌面 GUI，例如：

```java
import javax.swing.*;
public class Win {
    public static void main(String[] a){
        JFrame f = new JFrame("ET Java 窗口");
        f.setSize(400, 300);
        f.add(new JButton("按钮"));
        f.setDefaultCloseOperation(JFrame.EXIT_ON_CLOSE);
        f.setVisible(true);
    }
}
```

## 工具位置

| 工具 | 路径 |
|---|---|
| 编译器 | `runtime\jdk\bin\javac.exe` |
| 运行器 | `runtime\jdk\bin\java.exe` |
| 打包 jar | `runtime\jdk\bin\jar.exe` |

许可：GPL v2 + Classpath Exception（见 `runtime\jdk\legal`）。
