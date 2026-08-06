# Lesson 19: PIT 定时休眠、阻塞与唤醒 — 精讲文档

> **课号**：Lesson 19（可执行课）
> **主题**：在 Lesson 18 的 PIT IRQ0 抢占调度器上加入「线程睡眠」原语：`wake_tick` 截止时间、
> `THREAD_SLEEPING` 状态、IRQ0 每 tick 的有界唤醒扫描，以及内核线程用 `sti; hlt` 阻塞等待唤醒。
> **课程主线位置**：调度与同步阶段（Lesson 17 协作切换 → Lesson 18 定时抢占 →
> 本课定时休眠 → Lesson 20 键盘阻塞队列）。上一课解决了「谁先跑」，本课解决「谁不该跑」。
> **前置课程**：[`lesson-18-stable/README.md`](../lesson-18-stable/README.md)（PIT 抢占调度器）
> **后续课程**：[`lesson-20-stable/README.md`](../lesson-20-stable/README.md)（键盘阻塞等待队列 `wake_one`）
> **一句话目标**：学完本课你能说出并演示：一个内核线程如何「睡 120 个 tick」——记录
> 截止时间、把自己置为 `sleeping`、用 `sti; hlt` 阻塞，然后在某个 IRQ0 tick 被
> `wake_sleepers` 扫描唤醒、从被保存的 IRQ0 帧处继续执行。

---

## 1. 课程定位（Mission）

**一句话目标**：在 PIT 抢占调度的边界上实现「定时休眠」：线程通过 `thread_sleep_ticks(delta)`
自愿阻塞 `delta` 个 tick，时钟中断负责到期唤醒，且全程不引入动态队列、不分配内存。

- **在课程主线中的位置**：调度与同步阶段（Lesson 17 协作切换 → Lesson 18 定时抢占 →
  **本课 定时休眠** → Lesson 20 键盘阻塞队列 → Lesson 21 通用等待队列 → Lesson 22 信号量）。
  为什么在抢占之后立刻做睡眠？因为「抢占」只解决「谁先跑」，而「阻塞」解决「谁不该跑」——
  真实线程需要等待 I/O 与定时，这正是等待队列的雏形。
- **前置知识清单**：
  1. Lesson 18 的 `struct irq0_frame` 帧布局与「IRQ0 保存→调度→恢复→唯一 `iretq`」流程；
  2. PIT 100 Hz 周期中断如何进入 `irq0_schedule`、`quantum_left` 时间片机制；
  3. `THREAD_*` 状态机、固定三 TCB、`next_runnable` 轮转扫描；
  4. `sti`/`cli`/`hlt` 与 IF 标志的关系（`irq_save64`/`irq_restore64`）。
- **本课交付**：两个定时工作线程（A 每步睡 120 tick、B 每步睡 270 tick，各 4 步）；
  `ps` 新增 `wake-tick` 列；`threadinfo` 新增 `mode`/`sleep wakeups`/`shell-only ticks`；
  新增 `sleeptest` 命令；睡眠期间 shell 依然可交互。

---

## 2. 核心概念精讲

### 2.1 单调 tick 与 wake_tick 截止时间

`ticks` 是全局单调计数，每个 IRQ0 中断执行 `ticks++`，即 100 Hz 的「教学时间」。休眠者
不保存「还差多少 tick」，而保存**绝对截止时间** `wake_tick = ticks + delta`。判断到期的函数：

```c
static TEXT64 int tick_due(u64 now,u64 deadline){
    return (u64)(now-deadline)<(1ULL<<63);
}
```

`(now - deadline)` 用无符号回绕减法，只要 `now` 从「没到」进入「到了」（且差值不超过
2^63 的半个计数器空间），表达式即为真。这避免了「now 必须大于 deadline」的绝对比较，
与 Linux 的 `time_after_eq` 同一思想（详见第 7 节对照）。

### 2.2 线程睡眠的完整机制（核心）

睡眠分三步：

```text
1. 记录:  wake_tick = ticks + delta;  state = THREAD_SLEEPING   （关中断临界区）
2. 阻塞:  while(state == SLEEPING) sti; hlt                      （开中断等 IRQ0）
3. 唤醒:  IRQ0 中 wake_sleepers 到期扫描 → state = RUNNABLE
          → 下次调度选中 → iretq 恢复到 hlt 之后的指令 → while 条件为假 → 返回调用者
```

关键洞察：睡眠者不是「占着 CPU 等时间」，而是**用 `hlt` 让出 CPU**。`hlt` 停住 CPU 直到
下一个中断；IRQ0 每 10 ms 来一次，唤醒 CPU 执行 `irq0_schedule`。调度器发现当前线程在睡，
就切换回 shell（线程 0）继续跑 shell 命令；等 `wake_tick` 到期，`wake_sleepers` 把它标成
runnable，轮转扫描在某个量子边界把它换回来——`iretq` 精确恢复到它 `hlt` 时被保存的那份
IRQ0 帧，`while` 重查状态为假，`thread_sleep_ticks` 正常返回。

### 2.3 有界唤醒扫描：wake_sleepers

```c
static TEXT64 void wake_sleepers(void){
    u32 i;
    for(i=1;i<THREAD_COUNT;i++)
        if(threads[i].state==THREAD_SLEEPING&&tick_due(ticks,threads[i].wake_tick)){
            threads[i].state=THREAD_RUNNABLE; sleep_wakeups++;
        }
}
```

每 tick 扫描固定槽位 1–2，不分配内存、不打印、不操作动态队列——这是**有界扫描**
（bounded scan），是「通用等待队列」之前的教学简化。它回答了「谁来叫醒我」这个问题：
在 TinyOS 里，**时钟中断本身就是唤醒者**。

### 2.4 线程状态机：从四态到五态

| 状态 | 含义 | 谁能进入 | 如何离开 |
|---|---|---|---|
| `THREAD_EMPTY` | 槽位未创建 | 初始化 | `start_threads` 创建 |
| `THREAD_RUNNING` | 正在 CPU 上 | 调度器选中 | 被抢占 / 自愿睡眠 |
| `THREAD_RUNNABLE` | 就绪等调度 | `wake_sleepers` / 被抢占 | 被选中 |
| `THREAD_SLEEPING` | 阻塞中 | `thread_sleep_ticks` | `wake_sleepers` 到期 |
| `THREAD_FINISHED` | 已退出 | `thread_exit` | `reap_finished_threads` 回收 |

注意 `next_runnable` 只挑 `RUNNABLE`/`RUNNING`，`SLEEPING` 不会被选中——阻塞的语义
由「调度器看不见它」实现。

### 2.5 睡眠者仍是「活跃栈所有者」

睡眠者在 `ps` 里显示 `sleeping`，但其 `stack_phys` 仍指向 PMM 帧，`pfree` 会拒绝
（`cannot free: thread stack`）。因为睡眠者虽然没在跑，它的帧与将来要恢复执行的栈
还活着——**在它被 `reap_finished_threads` 处理前，栈页不能归还**。这是「活跃栈
ownership」概念的延续。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-18-stable） |
|---|---|---|
| `boot.S` | Multiboot2 32 位入口、建页表、进 long mode、`.incbin` 64 位 blob | 未变化 |
| `kernel.c` | 32 位引导阶段：解析 MBI、建九页页表、构造 `long_mode_handoff` | 未变化 |
| `kernel64.c` | 64 位内核本体：PMM、IDT、PIT/IRQ0 调度、键盘、shell | **全部本课增量** |
| `kernel64.ld` | 64 位裸 blob 布局（`.text64`/`.rodata`/`.data`） | 未变化 |
| `linker.ld` | 外层 ELF32 段布局（Multiboot2 段、读写段分离） | 未变化 |
| `Makefile` | 双阶段构建、ISO、check/run | 未变化 |
| `grub.cfg` | GRUB 菜单 | 仅 `menuentry` 标题改为 "TinyOS lesson 19: PIT timer sleep/wakeup" |

### 3.2 kernel64.c 增量精讲

（说明：源码习惯把函数压成单行。下列代码块为按语句重排以便逐行注释，**每条语句与源码
逐字一致**；省略了与 Lesson 18 完全相同的 PMM/VM/IDT/键盘部分。）

#### 3.2.1 新增常量与 TCB 字段

```c
#define SLEEP_A_TICKS 120ULL        // sleeptest 中工作线程 A 每步睡 120 个 tick
#define SLEEP_B_TICKS 270ULL        // 工作线程 B 每步睡 270 个 tick
enum thread_state { THREAD_EMPTY, THREAD_RUNNING, THREAD_RUNNABLE,
                    THREAD_SLEEPING, THREAD_FINISHED };   // 新增 THREAD_SLEEPING
struct thread { u64 frame,stack_phys,switches,progress,wake_tick; u8 state,id; };
                                   // wake_tick：绝对唤醒截止 tick
static u8 current_thread,round_robin,threads_started,sleep_test;
static u64 preempt_switches,quantum_left,sleep_wakeups,idle_worker_ticks;
```

- `sleep_test`：1 表示工作线程按定时方式运行（`sleeptest`），0 表示旧的忙等
  （`preempttest`）回归路径。
- `sleep_wakeups`：累计唤醒次数，`threadinfo` 展示。
- `idle_worker_ticks`：调度器停留在 shell 上的「纯 shell tick」计数（见 3.2.3）。
- `SLEEP_A_TICKS`/`SLEEP_B_TICKS` 各 4 步，A 全程睡 4×120=480 tick，B 睡 4×270=1080 tick。

#### 3.2.2 thread_sleep_ticks：睡眠原语（本课核心函数）

```c
static TEXT64 void thread_sleep_ticks(u64 delta){
    u64 flags; u8 id=current_thread;
    if(!delta) delta=1;                      // delta=0 时强制睡 1 个 tick，防止忙转
    flags=irq_save64();                      // 关中断保存旧 IF
    if(id&&threads[id].state==THREAD_RUNNING){  // 仅「正在运行的 worker」能入睡
        threads[id].wake_tick=ticks+delta;      // 记录绝对截止时间
        threads[id].state=THREAD_SLEEPING;      // 状态切换与记录在同一临界区内
    }
    irq_restore64(flags);                    // 恢复 IF（通常重新开中断）
    while(threads[id].state==THREAD_SLEEPING) // 阻塞循环：被唤醒后条件变假退出
        __asm__ volatile("sti; hlt");         // 开中断并让 CPU 停机等 IRQ0
}
```

逐层分析：

1. **为什么临界区包住「记时间 + 改状态」？** 若先 `sti` 再改状态，IRQ0 可能插在中间：
   IRQ0 里的 `wake_sleepers` 会拿着旧 `wake_tick`（可能为 0）判断，立刻把它唤醒，
   造成「睡了个寂寞」；反过来先把状态改成 sleeping 却还没记时间，`ps` 可能看到
   `wake_tick=0` 的睡眠者。所以两步必须原子。
2. **为什么用 `hlt` 而不是忙等？** 忙等（Lesson 18 的 `busy_delay`）把 CPU 烧满，
   抢占切走后线程仍在「跑步」。`hlt` 让 CPU 停止直到 IRQ0 到来，是省电且语义正确的
   「阻塞」。`sti; hlt` 连写是标准技巧：`sti` 确保 IF=1，紧接着 `hlt` 等待中断；
   中断返回后 IF 由 `iretq` 按保存值恢复为 1。
3. **为什么恢复后要重查状态？** `hlt` 的「返回点」就是 IRQ0 的 `iretq` 恢复点。中途
   的 IRQ0 可能只是「路过」（还没到期，唤醒的是别的线程）；所以必须用
   `while(state==SLEEPING)` 循环重判，只有 `wake_sleepers` 真正把自己标成 RUNNABLE
   才返回。
4. **边界与错误处理**：`if(id&&...)` 限定只有 worker（id≠0）且当前处于 RUNNING 才
   真的入睡；shell（id=0）永不睡。若调用者不是 RUNNING（正常不会发生），跳过睡眠后
   循环条件为假立即返回，不会死等。

#### 3.2.3 irq0_schedule 的增量

```c
TEXT64 struct irq0_frame *irq0_schedule(struct irq0_frame *f){
    u8 old,next;
    ticks++;
    outb64(PIC1_COMMAND,PIC_EOI);
    threads[current_thread].frame=(u64)(unsigned long)f;
    wake_sleepers();                       // 新增：每 tick 扫描到期睡眠者
    reap_finished_threads();
    if(!threads_started) return f;
    if(quantum_left) quantum_left--;
    if(quantum_left) return f;
    old=current_thread;
    next=next_runnable();
    quantum_left=TIME_SLICE_TICKS;
    if(next==old){ if(old==0) idle_worker_ticks++; return f; }  // 新增：停留 shell 计数
    if(threads[old].state==THREAD_RUNNING) threads[old].state=THREAD_RUNNABLE;
    threads[next].state=THREAD_RUNNING;
    current_thread=next;
    threads[next].switches++;
    preempt_switches++;
    return (struct irq0_frame *)(unsigned long)threads[next].frame;
}
```

增量分析：

- `wake_sleepers()` 放在 `ticks++` 之后立即执行，保证「到期 tick 的同一瞬间」就能唤醒，
  误差最多一个 tick 粒度。它运行在 IRQ0 上下文（关中断中），与 `thread_sleep_ticks`
  的临界区不会互相打断，因此无竞态。
- `if(next==old){ if(old==0) idle_worker_ticks++; return f; }`：量子耗尽但轮转扫到的
  仍是当前线程，且当前是 shell 时，说明两个 worker 都在睡，计一次 `idle_worker_ticks`。
  这就是「shell-only ticks」——它度量的是**没有 worker 可跑的时间**（每 2 tick 加一）。

#### 3.2.4 worker_run 与 thread_exit

```c
static TEXT64 void worker_run(u8 id){
    while(threads[id].progress<THREAD_STEPS){
        threads[id].progress++;
        if(sleep_test)
            thread_sleep_ticks(id==1?SLEEP_A_TICKS:SLEEP_B_TICKS); // 定时路径
        else
            busy_delay();                  // 回归路径：Lesson 18 忙等抢占
    }
    thread_exit();
}
static TEXT64 void thread_exit(void){
    u64 flags=irq_save64();
    threads[current_thread].state=THREAD_FINISHED;  // 临界区内改状态
    irq_restore64(flags);
    for(;;)__asm__ volatile("sti; hlt");    // 挂起自己，等 IRQ0 里 reap
}
```

- `worker_run` 每步先 `progress++` 再睡，所以 `ps` 里 progress 是「已完成步数」；
  A/B 各自睡 `SLEEP_A_TICKS`/`SLEEP_B_TICKS`，走完 4 步共睡 4 次。
- `thread_exit` 用 `irq_save64/irq_restore64` 保护状态切换（Lesson 18 直接改状态，
  这里改为临界区，避免 IRQ0 在切换中间读到中间状态），随后 `sti; hlt` 永久挂起；
  下一次 IRQ0 的 `reap_finished_threads` 把它回收——条件是 `i != current_thread` 且
  `stack_phys` 非 0，绝不会在自己栈上回收自己（这正是旧 README 记录的「只在另一个
  当前栈下回收」）。

#### 3.2.5 start_threads 与时序启动

```c
static TEXT64 int start_threads(int timed){
    u32 i; u64 flags;
    if(threads_started) return 0;
    if(!pmm_ready) return -1;
    flags=irq_save64();
    sleep_test=(u8)timed;
    sleep_wakeups=idle_worker_ticks=0;
    for(i=1;i<THREAD_COUNT;i++){
        u64 p=pmm_alloc();
        ...
        threads[i].switches=threads[i].progress=threads[i].wake_tick=0;
        ...
    }
    for(i=1;i<THREAD_COUNT;i++) threads[i].state=THREAD_RUNNABLE;
    threads_started=1; quantum_left=TIME_SLICE_TICKS;
    irq_restore64(flags);
    return 1;
}
```

- 签名从无参变为 `int timed`：`sleeptest` 传 1，`preempttest`/`threadstart` 传 0。
- 在关中断的临界区内设置 `sleep_test` 并清零 `sleep_wakeups`/`idle_worker_ticks`，
  保证时序统计干净地从 0 开始。
- 每次启动为 worker 分配独立 PMM 栈帧、构造首帧（`rip=thread_trampoline`、
  `cs=0x08`、`rflags=0x202`、`r12=栈顶`），然后整体置 RUNNABLE——启动在原子区内完成，
  IRQ0 最早在下一 tick 才能看到全部 runnable 状态，避免「半初始化」线程被调度。

#### 3.2.6 ps / threadinfo 输出增量

- `ps64` 表头新增 `wake-tick` 列，行内依次打印
  `id/state/frame/stack_phys/stack 高位别名/switches/progress/wake_tick`：
  ```c
  text64(c,"threads: id state frame stack-pa stack-high switches progress wake-tick\n");
  ```
- `threadinfo` 标题变为 `scheduler: PIT preemptive timer wakeups`，并新增三行：
  ```c
  text64(c,"\nmode: ");  text64(c,sleep_test?"sleeptest":"preempttest");
  text64(c,"\nsleep wakeups: "); hex64(c,sleep_wakeups);
  text64(c,"\nshell-only ticks: "); hex64(c,idle_worker_ticks);
  ```

#### 3.2.7 exec64 与 sleeptest 命令

```c
}else if(eq64(word,"sleeptest")){
    if(!noargs64(arg)) usage64(c,"sleeptest");
    else{ int r=start_threads(1);
        if(r>0) text64(c,"sleeptest: two timed workers started\n");
        else if(!r) text64(c,"sleeptest: already started\n");
        else text64(c,"sleeptest: PMM allocation failed\n");
    }
}
```

- `sleeptest` 是唯一新命令；`preempttest`/`threadstart` 改为调用 `start_threads(0)`
  走忙等回归路径。`about` 输出变为 `TinyOS lesson 19: PIT timer sleep/wakeup on IRQ0 return frames`。
- `help` 列表新增 `sleeptest`（位于 `preempttest` 与 `threadstart` 之间），完整串：
  `commands: help about clear lminfo hhinfo hhtest preempttest sleeptest threadstart yield ps threadinfo meminfo palloc pfree <hex> pageinfo <hex> vmap <hex> vunmap vminfo vmtest vmfaulttest mmap idtinfo tickinfo uptime kbdinfo bptest udtest pftest`。

### 3.3 构建管线

Makefile 与 Lesson 18 完全一致，无新增标志：

| 目标 | 含义 |
|---|---|
| `build/kernel64.o` | `gcc -m64 -ffreestanding -fpie -fno-stack-protector -mno-red-zone` 编译 64 位内核 |
| `build/kernel64.bin` | `ld -m elf_x86_64 -T kernel64.ld` 链接成 ELF 后 `objcopy -O binary` 成裸 blob |
| `build/kernel.elf` | 32 位阶段：`boot.S`（`.incbin` 装入 blob）与 `kernel.c` 用 `-m32` 链接 |
| `build/kernel.iso` | `grub-mkrescue -o $@ $(ISO_ROOT)` 打包 ISO |
| `make check` | `grub-file --is-x86-multiboot2 build/kernel.elf` 校验 Multiboot2 header |
| `make run` | `qemu-system-x86_64 -accel tcg -boot order=d -cdrom build/kernel.iso -serial stdio -no-reboot -no-shutdown` |

### 3.4 主控制流

```text
boot.S _start
  → kernel_main32 (kernel.c) 建页表/交接结构
  → enter_long_mode → kernel_main64 (boot.S) → kernel_main64_binary
  → pmm_init → install_idt → pit_init → pic_init → 打印横幅 → 进入 shell 循环
  → 输入 sleeptest → exec64 → start_threads(1)
  → 某 tick 后 IRQ0 抢占，thread_trampoline 首次运行 worker
  → worker_run → thread_sleep_ticks(120/270) → THREAD_SLEEPING + sti;hlt
  → 后续每个 IRQ0: irq0_schedule → wake_sleepers → 到期则置 RUNNABLE
  → 轮转选中 → iretq 恢复 worker 帧 → while 退出 → progress++ 继续下一步
  → 4 步后 thread_exit → FINISHED → 某 IRQ0 中 reap_finished_threads 归还栈页
```

---

## 4. 数据流与运行逻辑

1. 键入 `sleeptest` 回车 → `exec64` 的 `sleeptest` 分支 → `start_threads(1)`，
   屏幕输出 `sleeptest: two timed workers started`，shell 回到 `tinyos> ` 提示符。
2. 下一 IRQ0 起调度生效；worker A 首跑即入眠（`wake_tick=ticks+120`），B 同理
   （`ticks+270`）。`ps` 显示两 worker 状态为 `sleeping`、`wake-tick` 为不同值。
3. 睡眠窗口内键入 `threadinfo`：`mode: sleeptest`、`PIT ticks` 持续增长、
   `sleep wakeups` 从 0 起步、`shell-only ticks` 随 shell 持续运行增长——
   证明「两个 worker 都在睡，CPU 仍在为 shell 服务」。
4. 约 1.2 秒（120 tick）后 A 先醒：`wake_sleepers` 将其置 RUNNABLE 并 `sleep_wakeups++`；
   约 2.7 秒后 B 醒。两者交替进入下一轮睡眠，直到 `progress` 到 4 各自 `finished`。
5. `meminfo`：两 worker 栈页被 `reap_finished_threads` 归还后，`used` 回到基线，
   `tracked = free + used: yes` 不变量保持。
6. 回归：`preempttest` 走 `busy_delay` 忙等路径，`threadinfo` 显示 `mode: preempttest`。

---

## 5. 构建、运行与验证

### 5.1 依赖

与 Lesson 18 相同：`gcc`、`binutils`、`grub-common`（`grub-file`、`grub-mkrescue`）、
`xorriso`、`mtools`、`qemu-system-x86_64`。本课无新增工具。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check                     # 期望输出: Multiboot2 header check passed.
readelf -rW build/kernel64.elf # 期望: 无内部重定位
nm -u build/kernel64.elf       # 期望: 无未定义符号
readelf -SW build/kernel64.elf # 期望: .data 为 PROGBITS（objcopy 生成实数据）
objdump -d -Mintel build/kernel64.elf  # 期望: irq0_entry 只有一条 iretq
readelf -lW build/kernel.elf   # 期望: 外层无 RWX LOAD 段
```

`make check` 的输出由 Makefile 的 `@printf '%s\n' 'Multiboot2 header check passed.'`
产生，与源码逐字一致。`objdump` 中应保留 `invlpg`、IRQ1、异常路径与唯一 `iretq`。

### 5.3 运行与 VGA 验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`**。启动横幅逐字如下（来自
`kernel_main64_binary`）：

```text
TinyOS lesson 19: PIT timer sleep/wakeup
IRQ0 return-frame switching; high stacks, PMM, VM slot and keyboard enabled
tinyos> 
```

验证步骤（保留并扩充旧 README 的 QEMU VGA 流程）：

1. `meminfo` 记录基线；`sleeptest` → `ps`：worker 1/2 有不同 PMM 栈页，状态进入
   `sleeping`，`wake-tick` 为相差 150 量级的不同值。
2. 两 worker 都睡时跑 `threadinfo`、`tickinfo`、`kbdinfo`、`ps`：`PIT ticks` 前进、
   shell 仍响应键盘、`mode: sleeptest`。
3. 每个截止点后验证 worker 变 `runnable/running`、`progress` 前进、再睡、最终
   `finished`；`sleep wakeups` 累计到 8（4 步 × 2 worker）。
4. 完成后 `meminfo`：worker 栈仅在另一个当前栈下被回收，`used` 回基线，
   `tracked = free + used: yes`。
5. 回归 `preempttest`、`hhinfo`、`hhtest`、`vmtest`、`tickinfo`、`kbdinfo`、可恢复的
   `bptest`（输出 `#BP returned to shell`）。
6. 另起独立启动分别跑 `vmfaulttest`、`pftest`、`udtest`：预期致命结果保持
   CR2 `00000000003ff000`、CR2 `0000000000400000`、`#UD`（`exception_report` 显示
   `exception: #PF`/`#UD` 后 `CPU halted intentionally.`）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| worker 永远停在 `sleeping`，`sleep wakeups` 不增长 | `wake_tick` 与 `ticks` 不同步：记录/改状态没在同一临界区，或 `wake_sleepers` 没被调用 | 确认 `irq0_schedule` 调用了 `wake_sleepers`；核对 `wake_tick=ticks+delta` |
| 一睡就醒、`sleep wakeups` 暴涨 | `wake_tick` 记为 0/旧值，`tick_due(ticks,0)` 恒真 | 检查 `wake_tick` 赋值是否在改状态前完成；`ps` 看 `wake-tick` 列 |
| worker 一启动就 `finished`，progress 不前进 | `worker_run` 走了忙等路径（`sleep_test` 未置 1） | `threadinfo` 看 `mode:` 是否为 `sleeptest` |
| 两个 worker 睡时 shell 也卡死 | 睡眠者把 CPU 拿走（`hlt` 未执行 / 中断被关） | 查 `thread_sleep_ticks` 的 `sti; hlt`；IRQ0 是否反复返回睡者帧 |
| `threadinfo` 的 `shell-only ticks` 不增长 | 停留 shell 的计数只在量子耗尽且 `next==old==0` 时加一 | 确认 `if(next==old){if(old==0)idle_worker_ticks++;...}` 位于 `quantum_left` 重置之后 |
| `pfree` 报 `cannot free: thread stack` | 该页仍是活跃 worker（含 sleeping、finished 待 reap）的栈 | `ps` 看 `stack-pa`；只释放已 reap 的页 |
| `sleeptest` 报 `already started` | `threads_started` 已为 1，`start_threads` 提前返回 | 重新 `make run` 再验证 |
| `ps` 里没有 `wake-tick` 列 | 运行的是旧二进制 | `make clean && make` 后重试 |
| 内核 hang 在启动横幅前 | `install_idt`/`pit_init`/`pic_init` 顺序问题，PIT 没开则无 tick | 确认 `install_idt(h); pit_init(); pic_init();` 顺序与横幅打印先后 |

---

## 7. 与 Linux 源码对照

- **`schedule_timeout` / `msleep`**：Linux 的 `kernel/time/timer.c::schedule_timeout`
  用高精度定时器挂起当前任务并注册唤醒；TinyOS 的 `thread_sleep_ticks` 是它的极简版：
  用 `wake_tick` 当「软件定时器」、IRQ0 当定时器滴答、`hlt` 当挂起。
- **单调时间与回绕比较**：Linux `include/linux/jiffies.h` 的 `time_after_eq(a,b)` 用
  `(long)(a - b) >= 0` 处理 jiffies 回绕；TinyOS `tick_due` 用
  `(u64)(now-deadline)<(1ULL<<63)`，是同一「符号位比较」技巧。
- **阻塞与等待队列**：Linux `kernel/sched/wait.c` 用链表等待队列
  （`prepare_to_wait_exclusive` + `wake_up`）；TinyOS 本课没有队列，只有固定两槽扫描。
  下一课（Lesson 20）才引入有界环形等待队列。
- **`hlt` 与 CPU 空闲**：Linux `arch/x86/kernel/process.c::default_idle` 也执行 `hlt`
  让 CPU 空闲；TinyOS 在 worker 睡眠时靠 shell 的 `sti; hlt` 等键盘，思想一致。
- **PIT（8254）**：Intel 8254 可编程间隔定时器。Linux 在 `arch/x86/kernel/i8253.c` 与
  `drivers/clocksource/timer_pit.c` 使用；TinyOS 直接编程 `PIT_COMMAND 0x43` /
  `PIT_CHANNEL0 0x40`，分频 11932 → 约 100 Hz。
- **教学模型简化了什么**：单核、固定 TCB 数组、无优先级、无定时器红黑树、无 per-thread
  内核栈切换（仍靠 IRQ0 帧替换）、无「伪唤醒/竞态唤醒」处理、无睡眠线程的动态队列。

---

## 8. 思考题与练习

1. （概念理解）`thread_sleep_ticks` 为什么必须把「记 `wake_tick`」和「改 `state`」放进
   同一个关中断临界区？如果分开会出什么问题？
2. （源码定位）`irq0_schedule` 中 `wake_sleepers()` 为什么放在 `ticks++` 之后、
   `reap_finished_threads` 之前？它每 tick 都会跑吗？`!threads_started` 时它是否被跳过？
3. （动手实验）把 `SLEEP_A_TICKS` 改成 1、`SLEEP_B_TICKS` 改成 10，重新构建运行，
   观察 `sleep wakeups`、`shell-only ticks` 与之前有什么不同，解释原因。
4. （动手实验）删除 `thread_sleep_ticks` 循环里的 `sti` 只留 `hlt`，重新运行，
   观察 shell 是否还能响应命令，解释 IF 标志的作用。
5. （Linux 对照）Linux 的 `schedule_timeout` 与 TinyOS `thread_sleep_ticks` 在
   「到期判断」上各用什么机制？TinyOS 的固定两槽扫描在 worker 数量增大后会有什么问题？

---

## 9. 本课小结与下一课预告

**小结**：本课把 Lesson 18 的「抢占」升级为「抢占 + 阻塞」：TCB 新增 `wake_tick` 与
`THREAD_SLEEPING` 状态；`thread_sleep_ticks` 以「关中断临界区记录截止时间 → `sti; hlt`
阻塞 → IRQ0 到期唤醒 → 帧恢复续跑」四步实现定时休眠；IRQ0 每 tick 的 `wake_sleepers`
做有界两槽扫描完成唤醒，`tick_due` 用回绕减法做到期判定；`sleeptest` 命令与
`ps`/`threadinfo` 输出让「睡眠—唤醒」全程可见，`sleep_wakeups` 与 `shell-only ticks`
量化了调度行为。睡眠者仍是活跃栈所有者，`reap_finished_threads` 只在别的栈当前时回收。

**下一课预告**：[`lesson-20-stable/README.md`](../lesson-20-stable/README.md) 会把
「阻塞—唤醒」从「等时间」推广到「等键盘」：为键盘 IRQ1 建立有界等待队列，让 shell 在
队列为空时阻塞、键盘中断负责 `wake_one`——届时你会看到 `hlt` 的同一模式用在事件等待上。
