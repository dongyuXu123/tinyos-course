# 模块：进程与调度

## 职责

管理固定任务表、任务状态、上下文切换、fork/exit 和信号状态。

## 主要源码

- `source/kernel/sched.c`: `schedule`、`switch_to`
- `source/kernel/fork.c`: `copy_process`
- `source/kernel/exit.c`: `do_exit`、等待/回收
- `source/kernel/signal.c`: signal 状态
- `source/include/linux/sched.h`: task 数据结构

## 数据流

```text
task[] → schedule() → switch_to() → user/kernel execution
fork() → copy_process() → child task
exit() → do_exit() → parent wait/reap
```

## 调度算法

` schedule()` 先在 `TASK_RUNNING` 任务中选择最大 `counter`；如果所有计数都为零，则对任务执行 `counter = (counter >> 1) + priority`，再重新选择。时钟中断路径负责递减当前任务的时间片，并在需要时进入调度。

重点源码：`sched.c:schedule`、`sched.c:do_timer`、`sched.c:sched_init`、`fork.c:copy_process`、`exit.c:do_exit`。`sched_init` 还安装 TSS/LDT、timer gate 和 `int 0x80` system gate。

## 不变量

任务状态、PID、内核栈和 TSS 关联必须一致；调度器只能选择可运行任务。具体字段以固定源码为准。

## 只读练习

```bash
grep -RIn 'schedule\|switch_to\|copy_process\|do_exit' source/kernel source/include
```
