# 模块：异常、中断与系统调用

## 职责

建立 IDT 入口，处理异常/硬件中断，并通过汇编入口分派系统调用。

## 主要源码

- `source/kernel/traps.c`
- `source/kernel/system_call.s`
- `source/kernel/asm.s`
- `source/include/asm/system.h`

## 控制流

```text
CPU exception/IRQ → IDT stub → C handler
user int 0x80 → system_call.s → sys_call_table → kernel service → iret
```

`system_call.s` 在调用 C 函数前检查 `eax` 是否小于 72，并把 `ebx`、`ecx`、`edx` 作为最多三个参数压入栈中；`sys_call_table[eax]` 再间接调用对应实现。入口保存的返回帧依次包含 `eax`、`ebx`、`ecx`、`edx`、段寄存器以及 `eip/cs/eflags/oldesp/oldss`。返回前还会检查当前任务状态、时间片和信号，必要时进入调度。

典型路径：

```text
sys_open   → open_namei → namei/get_dir → iget → bread
sys_execve → do_execve → copy_strings/change_ldt/create_tables
sys_fork   → copy_process → copy_page_tables → scheduler
```

完整 ABI 讲解和只读练习见 [`../syscall-abi.md`](../syscall-abi.md)。

## 安全边界

系统调用参数和用户/内核边界必须按本版本的 segment 规则解释；不能用现代 x86-64 syscall ABI 代替。

## 只读练习

```bash
grep -RIn 'system_call\|sys_call_table\|trap_init' source/kernel source/include
```
