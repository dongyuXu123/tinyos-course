# Lesson 103: poll 就绪队列 — 精讲文档

> **课号**：Lesson 103（主线源课编号 Lesson 96 线）
> **本课主题**：poll 就绪队列（poll readiness）——`pipe_poll` 如何根据管道"有数据可读 / 有空间可写"返回 `POLL_IN`/`POLL_OUT` 就绪位，`polltest` 如何验证空→写→满→读的状态迁移
> **课程主线位置**：VFS / 设备 / epoll / 服务管理教学模型阶段（Lesson 81 起的「checkpoint 课」系列）。Lesson 102 讲设备生命周期与卸载，本课把"阻塞 I/O 的另一种出口——非阻塞就绪查询"做成显式模型：不睡在 wait 队列里等别人唤醒，而是直接回答"现在能不能读、能不能写"。
> **前置课程**：[`../lesson-102-stable/README.md`](../lesson-102-stable/README.md)（设备生命周期与卸载：`resource_ledger`、`teardowntest`；`l102test` 校验 `lesson_95_state`）
> **后续课程**：[`../lesson-104-stable/README.md`](../lesson-104-stable/README.md)（epoll 实例与固定 watch 表：从"单管道轮询就绪"升级到"一个实例管多个 fd 的 watch 表"）
> **本课一句话目标**：理解 poll 的核心理念是**就绪性（readiness）**——内核不承诺"数据马上到"，只承诺"此刻可读/可写与否"，并掌握 `pipe_poll(mask)` 的位运算判定与 `polltest` 的确定性迁移验证。
> **保留的原始快照信息**：This checkpoint models bounded VFS, devices, epoll, and service management metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants. Commands: `l95test` + `l103test`（**勘误**：旧 README 标注的 `l96test` 在源码命令表中并不存在——源码 `exec64` 分派的是 `l95test` 与 `l103test`），plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能用 `pipe_poll(mask)` 精确回答"这根管道现在可读吗、可写吗"，能读懂 `polltest` 里"空管道可写不可读 → 写入后可读 → 写满不可写 → 读出一个后恢复可写"的四段迁移断言，并能在 `pipeinfo` 里看到每次 poll 的注册计数。
- **在课程主线中的位置**：checkpoint 课（Lesson 81 起）系列中的"poll 就绪"检查点。这个系列每课源码增量只有几行（上一课测试改名 + 新测试 + about/banner 文案），主题由课程标签承载；本课主题标签是「poll 就绪队列」，要求你把**继承自更早课程**的 `pipe_poll`/`polltest`/`pipeinfo`/wait 队列机制拿出来精读，为下一课（104）的"epoll 实例 + 固定 watch 表"打好就绪性概念地基。
- **前置知识清单**（学本课之前必须掌握）：
  1. 环形缓冲区管道模型：`struct pipe_model`、`pipe_try_write`/`pipe_try_read` 的阻塞语义（Lesson 84 起）；
  2. 就绪位常量的位运算：`POLL_IN=1`、`POLL_OUT=2` 是位标志，`&` 取交集、`|=` 置位（Lesson 84 起）；
  3. wait 队列与事件机制：`struct wait_queue`、`waitq_enqueue`/`waitq_wake_one`/`waitq_wake_all`、`event_set`/`event_wait`（Lesson 78 起的调度模型）；
  4. `exec64` 命令分派与 VGA 输出（Lesson 34 起）。
- **本课交付**：新增固定容量记录 `struct lesson_96_model` + `lesson_96_state` + `l103test`；把 `l102test` 改名为 `l95test`；`about` 与 banner 更新为「Lesson 103: poll 就绪队列」。可见结果：`polltest` 打印 `POLLIN/POLLOUT readiness transitions passed`。

---

## 2. 核心概念精讲

### 2.1 就绪性（readiness）：poll 不睡觉，只做"体检"

**直觉**：阻塞 I/O（`pipe_try_read` 空时把调用者挂进 `pipe_read_wait`）是"我不知道数据什么时候来，干脆睡过去"。poll 是另一个思路：**我不睡，我先问一句"现在能用吗"**，能用就立刻干活，不能用就返回"未就绪"，让调用者决定要不要等、等多久。

**定义**：就绪（ready）= 此刻执行这次 I/O 一定不会阻塞。对管道：
- **可读（`POLL_IN`）**：缓冲区非空（`used > 0`），读一次必有数据；
- **可写（`POLL_OUT`）**：缓冲区未满（`used < PIPE_CAP`），写一次必有空间。

**为什么需要**：一个程序往往同时盯着好几个 fd（键盘、管道、网络）。逐个阻塞等会互相卡死；轮询式（poll）一次把所有 fd 的"体检报告"拿回来，就绪的干活、未就绪的跳过，这就是"事件循环"的雏形。Linux 的 `poll()`/`select()`/`epoll_wait()` 全部建立在"fd 是否就绪"这一概念上。

```
empty pipe:  used=0        可读? NO (used>0 不成立)   可写? YES (used<4)
one byte:    used=1        可读? YES                 可写? YES
full pipe:   used=4        可读? YES                 可写? NO  (used<4 不成立)
```

### 2.2 就绪位与位运算（`POLL_IN`/`POLL_OUT`）

Linux 的 `POLLIN`/`POLLOUT` 是 `poll.h` 里定义的位掩码。本课沿用同一个约定，但容量更小：

```c
#define POLL_IN 1U
#define POLL_OUT 2U
```

`mask` 是"我想查什么"的位集合，返回值是"现在哪些就绪"的位集合。两者都是 `u8`，`&` 判断"这个位我想查吗/就绪了吗"，`|=` 把就绪位累加进结果。**这是本课的全部核心**：`pipe_poll(mask)` 只有 3 个语句，分别回答"可读吗""可写吗""把两个答案按位拼起来"。

### 2.3 就绪查询与 wait 队列的关系：非阻塞的"问"，阻塞的"等"

管道模型里两组机制并存：
- **阻塞路径**：`pipe_try_read` 空 → `blocked_readers++` 返回 0；`pipe_try_write` 满 → `blocked_writers++` 返回 0。真实内核里这会挂 wait 队列；本课的教学模型只计数。
- **就绪路径**：`pipe_poll` 不做任何阻塞、不动 wait 队列，只读 `used` 计数并返回就绪位。

Linux 的真身 `pipe_poll()` 其实两者都做：先 `poll_wait()` 登记（把当前任务挂进管道 wait 队列，供唤醒），再立刻查状态返回就绪位——**查询时不睡**，但如果调用者之后真的 `wait`，内核保证就绪位变化时会唤醒它。本课的 `pipe_poll` 省略了 `poll_wait` 登记这一步，是教学模型的第一处简化。

### 2.4 「固定元数据 + 确定性验证」checkpoint 模型

`struct lesson_96_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };` 继续承担系列 checkpoint：`l103test` 断言 `b==a+1`（96+1=97）与四标志。它说明"本课的机制讲解归机制讲解，课程的自动验证始终锚定在一个确定性小断言上"，与 `polltest` 的确定性迁移断言互相独立又互相补充。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-102） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：管道 poll 就绪模型、wait 队列/事件、`exec64` 分派 | **微增量（仅 3 行）**：新增 `struct lesson_96_model` + `lesson_96_state` + `l103test()`；把 `l102test` 改名为 `l95test`；`about`/banner/`exec64` 文案更新（`pipe_poll`/`polltest` 继承自更早课程，本课重点精讲） |
| `kernel.c` | 32 位引导 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode | 未变化 |
| `kernel64.ld` | 64 位续传段布局（`.text64` 与三块 guard/stack 页） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局（`KEEP(.multiboot)`、可写段分页） | 未变化 |
| `Makefile` | 构建 + `check` + `run` | 微变化：`check` 目标三条 `grep` 换成「poll 就绪队列 / l103test / Lesson 103」关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 本课新增的 checkpoint 元数据：`l103test`

```c
static TEXT64 void l103test(u16*c){lesson_96_state=(struct lesson_96_model){96U,97U,98U,99U,1,1,1,1};int ok=lesson_96_state.valid&&lesson_96_state.active&&lesson_96_state.ready&&lesson_96_state.accounted&&lesson_96_state.b==lesson_96_state.a+1U;text64(c,"l103test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 96 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `lesson_96_state=(struct lesson_96_model){96U,97U,98U,99U,1,1,1,1};`：`a=96,b=97,c=98,d=99`，四标志 `valid/active/ready/accounted` 全 1。
- `int ok=...`：四个标志与 `b==a+1U` 五个条件的与——`97==96+1` 必须成立。
- 成功串 `bounded VFS, devices, epoll, and service management checkpoint passed`、失败串 `Lesson 96 fallback reported` 逐字来自源码。
- **设计说明**：`fallback` 串里的编号是**被校验状态**的编号（96），不是本课号；这就是旧 README 把命令名误写成 `l96test` 的来源——`l96test` 在 lesson-103 的 `exec64` 分派中并不存在，本课真实命令是 `l95test`（校验 `lesson_95_state`）与 `l103test`（校验 `lesson_96_state`）。

#### (b) 上一课测试改名为 `l95test`

```c
static TEXT64 void l95test(u16*c){lesson_95_state=(struct lesson_95_model){95U,96U,97U,98U,1,1,1,1};int ok=lesson_95_state.valid&&lesson_95_state.active&&lesson_95_state.ready&&lesson_95_state.accounted&&lesson_95_state.b==lesson_95_state.a+1U;text64(c,"l95test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 95 fallback reported");putc64(c,'\n');}
```

- lesson-102 里它叫 `l102test`（校验同一份 `lesson_95_state`）；本课改名为 `l95test`，让"测试名 = 被校验状态号"。
- `exec64` 分派因此把 `l102test` 分支换成 `l95test` 分支，并新增 `l103test` 分支：
```c
else if(eq64(word,"l95test")){if(!noargs64(arg))usage64(c,"l95test");else l95test(c);}
else if(eq64(word,"l103test")){if(!noargs64(arg))usage64(c,"l103test");else l103test(c);}
```
- 两条分支结构一致：无参数才执行，带参数打印 `usage: <命令名>`。`help` 字符串与命令表随之更新（`l95test`、`l103test` 入表，`l102test` 出表）。

#### (c) 就绪队列核心：`pipe_poll`

```c
static TEXT64 u8 pipe_poll(u8 mask){u8 ready=0;pipe_model.poll_registrations++;if((mask&POLL_IN)&&pipe_model.used)ready|=POLL_IN;if((mask&POLL_OUT)&&pipe_model.used<PIPE_CAP)ready|=POLL_OUT;return ready;}
```

逐行注释：
- `u8 ready=0;`：结果位集合，初始"什么都不就绪"。
- `pipe_model.poll_registrations++;`：**每次 poll 调用都记账**——这是"就绪查询次数"的统计，`pipeinfo` 不打印它但它属于 `struct pipe_model` 的一部分，体现"查询也是一种事件"。
- `if((mask&POLL_IN)&&pipe_model.used)ready|=POLL_IN;`：只有**调用者想查可读**（`mask` 含 `POLL_IN`）**且管道非空**（`used>0`）时，可读位才置位。
- `if((mask&POLL_OUT)&&pipe_model.used<PIPE_CAP)ready|=POLL_OUT;`：同理，想查可写**且未满**（`used<4`）时，可写位置位。
- **边界与设计**：判定只依赖 `used` 一个计数，无锁、无 wait 队列交互，因此对同一状态序列输出完全确定——这是确定性验证的基础。对照 Linux：`fs/pipe.c` 的 `pipe_poll()` 同样只算"可读/可写"两个条件，但多了一步 `poll_wait()` 注册。
- **为什么这样设计**：`mask` 参数让一个函数服务多种查询（只查读、只查写、都查），与 Linux `poll()` 的 `struct pollfd` 语义一一对应；返回值仍是一个位集合，调用者 `&` 自己的掩码即可判定。

#### (d) 就绪迁移的确定性验证：`polltest`

```c
static TEXT64 void polltest(u16*c){u8 v=0;pipe_init();int a=pipe_poll(POLL_IN)==0,b=pipe_poll(POLL_OUT)==POLL_OUT,w=pipe_try_write(7),d=pipe_poll(POLL_IN)==POLL_IN,e=pipe_model.used==1;while(pipe_model.used<PIPE_CAP)pipe_try_write(8);int f=pipe_poll(POLL_OUT)==0,g=pipe_try_read(&v),h=pipe_poll(POLL_OUT)==POLL_OUT;text64(c,"polltest: ");text64(c,a&&b&&w&&d&&e&&f&&g&&h?"POLLIN/POLLOUT readiness transitions passed":"BROKEN");putc64(c,'\n');}
```

算法步骤（编号与源码变量一一对应）：
1. `pipe_init()` 复位管道与两个 wait 队列；
2. `a=pipe_poll(POLL_IN)==0`：**空管道不可读**——就绪队列的初始状态；
3. `b=pipe_poll(POLL_OUT)==POLL_OUT`：**空管道可写**；
4. `w=pipe_try_write(7)`：写入 1 字节（7）；
5. `d=pipe_poll(POLL_IN)==POLL_IN`：**有数据后可读**；
6. `e=pipe_model.used==1`：used 计数精确为 1；
7. `while(pipe_model.used<PIPE_CAP)pipe_try_write(8);`：连写 8 直到**写满**（第 4 字节；第 5 次写被 `used>=PIPE_CAP` 拒绝、`blocked_writers++`）；
8. `f=pipe_poll(POLL_OUT)==0`：**满管道不可写**；
9. `g=pipe_try_read(&v)`：读出一个字节腾出空间；
10. `h=pipe_poll(POLL_OUT)==POLL_OUT`：**有空位后可写**——回到第 3 步的状态。

成功串 `polltest: POLLIN/POLLOUT readiness transitions passed` 逐字来自源码。这 8 个布尔条件覆盖"空/有数据/满/再空"四个阶段的全部就绪迁移，是 `pipe_poll` 的完整行为矩阵。

#### (e) 就绪查询的统计窗口：`pipeinfo`

```c
static TEXT64 void pipeinfo(u16*c){text64(c,"pipe used/capacity: ");hex64(c,pipe_model.used);text64(c,"/");hex64(c,PIPE_CAP);text64(c," reads/writes: ");hex64(c,pipe_model.reads);text64(c,"/");hex64(c,pipe_model.writes);text64(c," blocked r/w: ");hex64(c,pipe_model.blocked_readers);text64(c,"/");hex64(c,pipe_model.blocked_writers);text64(c," wake r/w: ");hex64(c,pipe_model.wake_readers);text64(c,"/");hex64(c,pipe_model.wake_writers);putc64(c,'\n');}
```

- 一次打印 4 组：`used/capacity`（当前占用）、`reads/writes`（读写次数）、`blocked r/w`（阻塞次数，即"试图读空/写满被拒"的次数）、`wake r/w`（唤醒对端 wait 队列的次数）。
- 与 `polltest` 配合观察：`polltest` 跑完后 `reads=1`（第 9 步）、`writes=4`（第 4 步 1 次 + while 循环 3 次成功 + 1 次被拒不算）、`blocked_writers=1`（写满后第 5 次写被拒）、`blocked_readers` 视此前 `pipetest` 而定。
- **为什么这样设计**：poll 是"查询"，查询本身不该改变管道内容，但查询次数是诊断信息——这就是 `poll_registrations` 计数器存在的意义；`pipeinfo` 把"内容、事件、阻塞、唤醒"四类计数集中呈现，方便对照就绪状态。

#### (f) `exec64` 增量、`about` 与 banner

`about` 分支逐字来自源码：
```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 103: poll 就绪队列\n");}
```
banner 逐字来自 `kernel_main64_binary`：
```c
text64(&c,"Lesson 103: poll 就绪队列\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'poll 就绪队列' README.md
	@grep -q 'l103test' kernel64.c
	@grep -q 'Lesson 103' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 103 checks passed.'
```

- 与 lesson-102 的唯一差异是三条 `grep` 关键字与 printf 文案。
- 构建链不变：`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse -mno-sse2 -mno-mmx ... -Werror` 编 `kernel64.c` → `kernel64.ld` 链接出 `.text64` 续传镜像 → `objcopy -O binary` 内嵌进 `boot.S` → 32 位 `boot.o` + `kernel.o` 用 `linker.ld`（`-m elf_i386`）链接成 ELF32 → `grub-mkrescue` 打包 ISO。
- `kernel64.ld` 中三块 guard/stack 页（`__idle_*`、`__rsp0_*`、`__ist1_*`）与三个 `ASSERT` 尺寸断言未变；`linker.ld` 里 `KEEP(*(.multiboot))` 保证 Multiboot2 header 保留在镜像前 32 KiB 内。

### 3.4 主控制流

```
kernel_main64_binary
    ├─ 初始化：module_init_model / init_model_start / wait_model_start / adoption_start / resource_start / pmm_init / vma_init / reclaim_init / vfs_init（内部 pipe_init）
    ├─ banner: "Lesson 103: poll 就绪队列\nGETTICKS, ...; bounded reclaim metadata\n"
    ├─ 装 IDT / PIT / PIC，开中断
    └─ for(;;) 键盘循环
        ├─ "polltest" ──► pipe_init ──► pipe_poll(IN)==0 / pipe_poll(OUT)==OUT ──► 写 7 ──► pipe_poll(IN)==IN
        │                 ──► 写满（while used<4）──► pipe_poll(OUT)==0 ──► 读一个 ──► pipe_poll(OUT)==OUT
        │                 ──► "polltest: POLLIN/POLLOUT readiness transitions passed"
        ├─ "pipeinfo" ──► 打印 used/capacity、reads/writes、blocked r/w、wake r/w
        ├─ "l103test" ──► lesson_96_state 校验 ──► VGA 打印 passed
        └─ "l95test"  ──► lesson_95_state 校验（上一课回归）──► VGA 打印 passed
```

---

## 4. 数据流与运行逻辑

1. **启动**：`vfs_init()` 内部调用 `pipe_init()`，把 `pipe_model` 清零并 `waitq_reset` 两个 wait 队列；banner 打出「Lesson 103: poll 就绪队列」。
2. **输入 `polltest`**：进入 `exec64` 的 `polltest` 分支 → `polltest(c)`。
   - 空管道：`pipe_poll(POLL_IN)==0`（不可读）、`pipe_poll(POLL_OUT)==POLL_OUT`（可写）；
   - `pipe_try_write(7)` 后：`used=1`，`pipe_poll(POLL_IN)==POLL_IN`（可读）；
   - `while(used<4) pipe_try_write(8)` 写满：`used=4`，第 5 次写被拒（`blocked_writers=1`），`pipe_poll(POLL_OUT)==0`（不可写）；
   - `pipe_try_read` 读出 1 字节：`used=3`，`pipe_poll(POLL_OUT)==POLL_OUT`（恢复可写）。
   - 8 个断言全真 → VGA 打印 `polltest: POLLIN/POLLOUT readiness transitions passed`。
3. **查询而不扰动**：整个 `polltest` 过程中 `pipe_poll` 只读 `used`、只做位运算、只增加 `poll_registrations` 计数——它**不修改** `data[]/head/tail/used`，所以"就绪查询"与"读写"在数据上是隔离的。
4. **读统计**：`pipeinfo` 打印 `pipe used/capacity: 3/4 reads/writes: 1/4 blocked r/w: 0/1 wake r/w: 0/0`（`polltest` 刚跑完后的典型值；阻塞读计数取决于此前是否执行过 `pipetest`）。
5. **checkpoint**：`l103test` 打印 `l103test: bounded VFS, devices, epoll, and service management checkpoint passed`；`l95test` 打印 `l95test: ... checkpoint passed`（回归）。

输出串与源码逐字一致：`polltest: POLLIN/POLLOUT readiness transitions passed`；`l103test: bounded VFS, devices, epoll, and service management checkpoint passed`。

---

## 5. 构建、运行与验证

**依赖**：与全仓库一致（`gcc-multilib`、`binutils`、`grub-pc-bin`、`xorriso`、`mtools`、`qemu-system-x86`，详见 [`docs/local-validation.md`](../../docs/local-validation.md)）。

**构建**：

```bash
make clean && make -j"$(nproc)"
make check
```

**运行**：

```bash
make run
```

> 成功画面在 QEMU 图形窗口，请勿加 `-display none`。

**验证步骤与预期输出**（输出串从源码逐字抄录）：

1. 开机第一屏应显示：
   ```
   Lesson 103: poll 就绪队列
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `polltest`，预期输出：
   ```
   polltest: POLLIN/POLLOUT readiness transitions passed
   tinyos>
   ```
   （任一步迁移失败会打印 `polltest: BROKEN`。）
3. 输入 `l103test`，预期输出：
   ```
   l103test: bounded VFS, devices, epoll, and service management checkpoint passed
   tinyos>
   ```
   （失败场景打印 `l103test: Lesson 96 fallback reported`。）
4. 输入 `l95test`（上一课回归），预期输出：
   ```
   l95test: bounded VFS, devices, epoll, and service management checkpoint passed
   tinyos>
   ```
5. 输入 `pipeinfo`，预期出现：
   ```
   pipe used/capacity: 3/4 reads/writes: 1/4 blocked r/w: 0/1 wake r/w: 0/0
   tinyos>
   ```
   （该串数值依赖此前 `polltest`/`pipetest` 的执行顺序，故用"出现 `pipe used/capacity:`"作判定标准。）
6. 输入 `about`，预期输出：
   ```
   Lesson 103: poll 就绪队列
   tinyos>
   ```

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 103 checks passed.`；QEMU 中 `polltest` 打印 `...transitions passed`、`l103test` 打印 `...checkpoint passed`，即代表 poll 就绪队列模型验证成立。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `polltest` 打印 `BROKEN` | 8 个断言中某一项失败，最常见是 `pipe_poll` 判空/判满条件写反 | 逐项核对：空管道 `pipe_poll(POLL_IN)==0` 且 `pipe_poll(POLL_OUT)==POLL_OUT`；写 1 字节后 `pipe_poll(POLL_IN)==POLL_IN`；写满后 `pipe_poll(POLL_OUT)==0` |
| `polltest` 中 `pipe_try_write` 返回 0 导致断言失败 | `pipe_init()` 没有把 `used` 复位，或 `while(used<PIPE_CAP)` 之前 `used` 已非 0 | 在 `polltest` 入口确认 `pipe_init()` 已执行；`pipeinfo` 看 `used` 是否归 0 |
| `pipeinfo` 的 `used` 与你预期不符 | 之前跑过 `pipetest`（它把 `used` 手动设成 `PIPE_CAP` 再归 0）污染了状态 | 先跑 `polltest` 再跑 `pipeinfo`；或重新 `pipe_init` 后观察 |
| `pipe_poll` 明明有数据却返回 0 | 查询掩码与结果位不匹配（用 `&` 取交集时写成了 `==`） | 检查 `pipe_poll` 里的 `(mask&POLL_IN)` 与 `(mask&POLL_OUT)` 两个条件；对照 `polltest` 的用法 |
| `l103test` 打印 fallback 串 | `lesson_96_state` 5 条件中某一项不满足 | 检查 `ok` 表达式：四标志与 `b==a+1`（`a=96,b=97`） |
| `make check` 失败于第一条 grep | README 主题字串不一致 | `grep 'poll 就绪队列' README.md` |
| `make check` 失败于第二条 grep | `kernel64.c` 丢失 `l103test` 符号 | `grep -q 'l103test' kernel64.c` |
| QEMU 无图形输出 | `-display` 设成 `none` 或显示环境缺失 | 用 `make run` 原样启动；参考 `scripts/qemu-vga-check.sh` 流程 |

---

## 7. 与 Linux 源码对照

**对照点 1：就绪位与 poll 的掩码约定**
- TinyOS 教学模型：`POLL_IN=1U`、`POLL_OUT=2U`，`pipe_poll(mask)` 用 `&` 匹配掩码、`|=` 累加就绪位。
- Linux 实现：`include/uapi/asm-generic/poll.h` 定义 `POLLIN=0x0001`、`POLLOUT=0x0004`（另有 `POLLERR/POLLHUP/POLLRDHUP` 等）；`do_poll()`（`fs/select.c`）对每个 `struct pollfd` 调用 fd 的 `.poll()` 方法收集 `revents`。
- 教学简化：只有 `IN`/`OUT` 两个位，没有错误/挂断位，也没有 fd 数组与超时逻辑。

**对照点 2：`pipe_poll` 与 `fs/pipe.c`**
- TinyOS：`pipe_poll` 只查 `used` 计数返回就绪位，不登记 wait 队列。
- Linux：`pipe_poll()`（`fs/pipe.c`）做两件事：先 `poll_wait(pipe->poll_wait)` 把当前任务登记进管道的 wait 队列头（保证状态变化时被唤醒），再检查 `pipe_empty()`/`pipe_full()` 与 `EPIPE` 等状态返回 `POLLIN`/`POLLOUT`/`POLLHUP`/`POLLERR`。
- 教学简化：省略了 `poll_wait` 登记——所以本课 `pipe_poll` 之后如果调用者去睡，不会有人唤醒它；真实内核的"查询 + 登记"是成对出现的。

**对照点 3：就绪性 vs 阻塞性**
- TinyOS：`pipe_try_read` 空时只 `blocked_readers++` 并返回 0（不挂队列），`pipe_poll` 则完全不碰状态；两条路径泾渭分明。
- Linux：`pipe_read` 空且无写入端时直接 `wake_up_interruptible_sync_poll(pipe->poll_wait, EPOLLIN)` 并把 `pipe_wait` 挂起；`pipe_poll` 与 `pipe_read` 通过 `pipe->poll_wait` 队列头互相协作——就绪位变化会唤醒 poll 等待者，poll 等待者醒来会重新检查就绪位。
- 权威来源：Linux v6.x `fs/pipe.c`（`pipe_poll`、`pipe_read`、`pipe_write`、`pipe_wait`）；`include/linux/poll.h`（`poll_wait`、`struct poll_table`）。

**对照点 4：为什么 poll 只给"体检报告"不给"保证"**
- TinyOS：`pipe_poll` 的结果是"此刻"的快照，`polltest` 每次都重新查询。
- Linux：`poll()`/`select()` 与 `epoll_wait()` 都只保证"返回时该 fd 曾就绪或条件满足"，从不保证"读到数据成功"——经典竞态是 poll 返回可读后另一线程抢先读走。epoll 的 LT 语义之所以要"重复上报"，正是为了弥合这个竞态（见 Lesson 106）。
- 教学简化：单线程模型没有竞态，但概念上必须理解"就绪是瞬时状态"。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `pipe_poll` 的返回值不能保证"随后读一定成功"？如果用 `pipe_poll(POLL_IN)` 返回了 `POLL_IN`，紧接着另一个执行者把管道读空，会发生什么？这与 Linux 中 poll 的经典竞态对应吗？
2. **源码定位**：说出 `struct pipe_model` 里 `poll_registrations` 字段在哪个函数里被 `++`，以及 `pipeinfo` 打印的四组计数分别对应哪些字段。
3. **动手实验**：修改 `polltest`，把第 8 步的 `pipe_try_read(&v)` 删掉再断言 `h=pipe_poll(POLL_OUT)==POLL_OUT`，重新 `make run`。预期 `h` 变成 0（满管道仍不可写）→ 打印 `BROKEN`。改完请**恢复原值**。
4. **动手实验**：把 `pipe_poll` 里可写条件 `pipe_model.used<PIPE_CAP` 改成 `pipe_model.used<=PIPE_CAP`，观察满管道时 `pipe_poll(POLL_OUT)` 错误地返回 `POLL_OUT`。改完请**恢复原值**。
5. **Linux 对照**：阅读 `fs/pipe.c` 的 `pipe_poll`，说出它相比 TinyOS 多做的"登记 wait 队列"那一步对应本课的哪个教学简化；在什么场景下没有这一步会导致唤醒丢失。

---

## 9. 本课小结与下一课预告

- 本课把"poll 就绪队列"讲清楚了：就绪 = 此刻执行不阻塞，可读 = `used>0`，可写 = `used<PIPE_CAP`。
- 你掌握了 `pipe_poll(mask)` 的三语句位运算：想查的位（`mask&`）与就绪的位（`|=`），以及 `poll_registrations` 的查询计数。
- 你读懂了 `polltest` 的四段迁移断言（空→写→满→读），知道它是 `pipe_poll` 的完整行为矩阵。
- 你对照了 `fs/pipe.c` 的 `pipe_poll`/`poll_wait`，理解了"查询 + 登记"成对出现的真实语义与教学简化。
- 你完成了 checkpoint 验证：`l103test` 与回归 `l95test` 打印 `bounded VFS, devices, epoll, and service management checkpoint passed`，并把旧 README 的 `l96test` 命令勘误为源码真实的 `l95test`/`l103test`。

**下一课预告**：Lesson 104「epoll 实例与固定 watch 表」。poll 只回答"单个 fd 此刻就绪吗"；下一课把它升级成"一个 epoll 实例同时登记多个 fd 的 watch 表"，用 `epoll_ctl` 的 ADD/MOD/DEL 维护固定容量的 watch 记录，再配合 ready 列表回答 `epoll_wait`。衔接点正是本课的"就绪位 = 掩码与状态的交集"：每个 watch 条目都存一份自己的兴趣掩码。见 [`../lesson-104-stable/README.md`](../lesson-104-stable/README.md)。
