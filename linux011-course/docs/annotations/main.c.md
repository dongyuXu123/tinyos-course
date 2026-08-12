# 注释：`init/main.c`

- 源码：`linux011-course/source/init/main.c`
- 版本：`f8d044e078f5e5ee20a3ad2f72c243f041526983`
- 角色：把 early assembly 状态编排成完整内核，并进入第一个用户进程。

```bash
grep -nE 'main|init\(|trap_init|sched_init|buffer_init|hd_init' linux011-course/source/init/main.c
```

以源码实际调用顺序建立表格：

| 顺序 | 调用 | 作用 |
|---:|---|---|
| 1 | `mem_init` | 建立物理内存可分配范围 |
| 2 | `trap_init` | 建立异常/陷阱入口 |
| 3 | `blk_dev_init` | 初始化块设备请求路径 |
| 4 | `chr_dev_init` | 初始化字符设备 |
| 5 | `tty_init` | 建立 TTY 状态 |
| 6 | `time_init` | 初始化时间/PIT 相关状态 |
| 7 | `sched_init` | 初始化任务、TSS/LDT、timer gate 和 `int 0x80` gate |
| 8 | `buffer_init` | 建立 buffer cache |
| 9 | `hd_init`、`floppy_init` | 初始化磁盘设备 |
| 10 | `sti` | 开放硬件中断 |
| 11 | `move_to_user_mode` | 从 task 0 进入用户模式 |
| 12 | `fork` | 创建 task 1，随后进入 `init()` |

根文件系统不是 `main()` 的独立调用：task 1 的 `init()` 通过 `setup()`、`sys_setup()` 最终调用 `mount_root()`；之后打开 `/dev/tty0`、复制描述符、fork `/etc/rc` shell 并等待，再创建 login shell。每一行都要记录写入状态、后续依赖和中断是否已经开放。
