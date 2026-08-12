# 注释：`kernel/sched.c`

- 源码：`linux011-course/source/kernel/sched.c`
- 版本：`f8d044e078f5e5ee20a3ad2f72c243f041526983`
- 角色：任务状态、时间片、调度选择和上下文切换。

```bash
grep -nE 'schedule|switch_to|sleep_on|wake_up|sched_init' linux011-course/source/kernel/sched.c
```

重点记录 task table、current、等待队列和 TSS 的关系；不要将现代 CFS 术语直接套入 Linux 0.11。
