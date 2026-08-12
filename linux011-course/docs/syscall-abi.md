# Linux 0.11 系统调用 ABI

固定源码版本见 `source-revision.txt`。本课只讲历史 32 位 ABI，不适用于 TinyOS x86-64 或现代 Linux。

## 入口

```text
user code
  → int 0x80
  → kernel/system_call.s:system_call
  → eax range check
  → sys_call_table[eax]
  → C implementation
  → iret
```

## 参数约定

在用户态 wrapper 中，系统调用号放入 `eax`，参数通过历史约定传递；进入 `system_call.s` 后，汇编代码建立内核栈帧、检查调用号范围、设置数据段并间接调用 `sys_call_table`。具体寄存器保存和参数读取必须以 `source/kernel/system_call.s`、`source/include/unistd.h`、`source/include/linux/sys.h` 为准，不要套用 x86-64 `syscall` 的 `rdi/rsi/rdx` 规则。

## 调用表

```bash
grep -nE 'nr_system_calls|sys_call_table|system_call|sys_execve|sys_fork' \
  source/kernel/system_call.s source/include/linux/sys.h source/include/unistd.h
```

本版本声明 `nr_system_calls = 72`，并在 `sys_call_table` 中按 0–71 顺序放置处理函数；索引是 ABI 的核心，新增或改变表项会改变用户程序与内核之间的契约。

## 三条工作路径

```text
open
  → sys_open → open_namei → namei/iget/bread

execve
  → sys_execve → do_execve → copy_strings/change_ldt/create_tables

fork
  → sys_fork → copy_process → copy_page_tables → child task
```

## 中断返回

系统调用完成后，入口代码还处理重新调度、信号检查和 `iret` 返回。系统调用、时钟中断和调度不能拆成互不相关的章节阅读。

## 安全的验证

只读取符号和文本：

```bash
grep -nE 'system_call|sys_call_table|sys_execve|timer_interrupt' \
  source/kernel/system_call.s source/include/linux/sys.h
```
