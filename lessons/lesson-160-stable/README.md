# Lesson 160: 审计事件缓冲区 — 精讲文档

> **课号**：Lesson 160（可执行课，checkpoint 快照）
> **主题**：审计事件缓冲区——把 Linux 内核审计（audit）的「审计事件 → 有界缓冲 →
> 溢出丢失计数」模型作为概念模型精讲，并回顾 TinyOS 内核里既有的「有界环形事件
> 队列 + 丢弃计数」机制（键盘环、输入事件环、管道 FIFO、工作队列环），追加确定性
> 校验的 checkpoint 模型 `lesson_153_model`。
> **课程主线位置**：资源/安全主题的「检查点课」序列（Lesson 157–162），位于
> Lesson 159（syscall 安全边界）之后、Lesson 161（安全策略决策）之前。
> **前置课程**：[`lesson-159-stable/README.md`](../lesson-159-stable/README.md)
> **后续课程**：[`lesson-161-stable/README.md`](../lesson-161-stable/README.md)
> **一句话目标**：学完本课你能说清「审计事件缓冲区」在 Linux 里的样子（audit 记录
> 产生 → 编码 → 有界 backlog → 丢失计数），以及 TinyOS 用哪些既有环形缓冲机制近似
> 它、`l160test` 校验了什么。

---

## 1. 课程定位（Mission）

**一句话目标**：读懂「主题宣告 + checkpoint 增量」模式下本课新增的确定性模型
`lesson_153_model` 及其 `l160test` 断言，会用 `l152test`、`l160test`、`kbdinfo`、
`inputtest`、`pipeinfo`、`pipetest` 等命令复现与「有界事件队列」相关的既有机制，
并理解 audit 事件缓冲概念与它们的对应关系。

- **在课程主线中的位置**：与 Lesson 157–159、161–162 同属「资源/安全主题的检查点
  课」，相邻课 `kernel64.c` 的 diff 通常只有几行（本课相对 Lesson 159 仅 4 处改动：
  `l159test`→`l152test` 改名、新增 `struct lesson_153_model` 与 `l160test`、
  exec64/about/banner 文案）。**注意：源码中没有 `audit_*` 结构体或审计 API**——本课
  主题是「宣告 + 概念模型」，机制载体是继承自早期课程的各类有界环形缓冲。
- **前置知识清单**：
  1. 环形缓冲（ring buffer）基本形态：`head`/`tail`/`used` 三变量 + 取模入队出队，
     Lesson 21（键盘环 `kbd_queue`）与 Lesson 33（管道 `pipe_model`）打下的地基；
  2. 溢出/丢失记账：`kbd_overflow_count`、`input_dropped`、`softirq_model.drops`——
     「满了就丢并计数」的模式；
  3. 键盘/输入/GUI 子系统命令：`kbdinfo`、`inputtest`、`pipeinfo`、`pipetest`、
     `softirqinfo`；
  4. checkpoint 课固定模式（`struct lesson_K_model` + `lXXtest`，Lesson 133–159）。
- **本课交付**：audit 事件缓冲的概念模型（记录产生 → 编码 → 有界 backlog → 丢失
  计数）；既有有界环形缓冲机制的逐函数精讲；命令 `l152test`（改名）与 `l160test`
  （新增）；`about`/banner 文案。

---

## 2. 核心概念精讲

### 2.1 概念一：Linux 审计（audit）与审计事件

**直觉**：安全审计 = 「谁、在什么时候、做了什么敏感操作」的黑匣子记录。Linux 内核
在敏感操作（syscall、文件访问、配置变更）入口处产生审计记录，交给用户态 `auditd`
守护进程落盘。

**准确定义**：一条审计事件（audit record）通常是一个文本行，含类型
（`AUDIT_SYSCALL`/`AUDIT_EXECVE` 等，见 `include/uapi/linux/audit.h`）、序列号、
时间戳与上下文（uid、pid、comm、syscall 号、参数）。内核用 `audit_log_start` 向
`struct audit_buffer` 里 `audit_log_format` 追加字段，完成后再提交。

**为什么需要**：入侵检测、合规审计都要以「可信的内核侧记录」为依据；用户态程序
可以撒谎，内核审计记录不能。

### 2.2 概念二：审计缓冲区与 backlog 丢失

**直觉**：审计事件不能阻塞业务，也不能无限堆积。Linux 内核维护一个有上限的审计
backlog：事件先写进缓冲，`auditd` 异步取走；若事件产生速度超过 `auditd` 消费速度，
backlog 超过 `audit_backlog_limit`（默认 64）就**丢事件并计数**（`audit_lost`），
内核还会尝试向控制台发 `audit: backlog limit exceeded` 警告。

**准确定义**：审计缓冲区 = 生产端（内核各处审计点）与消费端（auditd）之间的
**有界异步队列**。缓冲满 = 溢出 = 丢弃 + 计数，这是审计系统「宁可丢、不可堵」的
设计取舍。

**工作机制**（Linux 侧）：`kernel/audit.c` 用 `audit_buffer` 对象链表 + 全局
`audit_backlog_wait_time`/`audit_backlog_limit` 控制水位；`auditsc.c` 在 syscall
出入口调用 `__audit_syscall_entry`/`__audit_syscall_exit` 填充记录。

### 2.3 概念三：TinyOS 的「有界环形事件队列」——审计缓冲的本地近似

**直觉**：TinyOS 没有 audit，但它到处是同一模式的「有界环形队列 + 满则丢 + 计数」，
这正是审计事件缓冲的教学投影：

| TinyOS 缓冲 | 容量宏 | 满时行为 | 观察命令 |
|------------|--------|---------|---------|
| `kbd_queue` 键盘环 | `KBD_QUEUE_SIZE=64` | `kbd_overflow_count++` | `kbdinfo` |
| `input_queue` 输入事件环 | `INPUT_QUEUE_CAP=16` | `input_dropped++` | `inputtest` |
| `pipe_model.data` 管道 FIFO | `PIPE_CAP=4` | `blocked_writers++`（生产者阻塞） | `pipeinfo` |
| `workqueue` 延迟工作环 | `WORK_CAP=4` | `softirq_model.drops++` 拒绝入队 | `softirqinfo` |
| `pc_buffer` 生产者消费者环 | `PC_BUFFER_CAP=2` | 信号量 `pc_spaces` 阻塞生产者 | `pcinfo` |

**为什么这样设计**：内核不可能让生产者无限等待或让事件无限堆积，必须给每个队列
一个固定容量和一个「满了怎么办」的策略（丢 / 阻塞 / 拒绝）。`input_dropped` 之于
`input_queue` 恰如 `audit_lost` 之于 `audit_backlog`——**满则丢并计数**。

### 2.4 概念四：检查点模型（checkpoint model）

**直觉**：与 Lesson 157–159 完全相同的模式——本课新增：

```c
struct lesson_153_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
```

**工作机制**：`l160test` 把 `lesson_153_state` 整体赋为 `{153U,154U,155U,156U,1,1,1,1}`
（a=153, b=154, c=155, d=156，四个状态位全 1），断言 `valid && active && ready &&
accounted && b==a+1`。字面量赋值使断言恒真，输出恒为 `bounded networking,
namespaces, cgroups, and security checkpoint passed`。模型名 `lesson_153_model`
的 153 = 160−7，延续「回锚」链（150/151/152/153 连续四课）。**教学模型：不执行任何
审计代码，只校验元数据自洽。**

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 159） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验并加载用户镜像、构造 `long_mode_handoff` | 未变化 |
| `kernel64.c` | 64 位内核本体（1039 行）：PMM/异常/中断/调度/进程/VFS/GUI/环形缓冲/checkpoint 模型 | `l159test`→`l152test` 改名；新增 `struct lesson_153_model`、`l160test`；exec64 增加 `l152test`/`l160test` 分支；about/banner 文案 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局（`.multiboot`/`.text64`/`.data`/`.bss`） | 未变化 |
| `Makefile` | `make all/check/run/clean`；`check` 目标 grep `审计事件缓冲区`、`l160test`、`Lesson 160` | 仅 grep 文案（Lesson 159→160） |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

### 3.2 kernel64.c：有界环形缓冲机制精讲（继承代码）

> 说明：本课**没有**新增审计缓冲代码；以下函数都是早期课程继承的既有机制，但
> 它们正是本课主题「审计事件缓冲区」的载体，逐函数精讲其「有界 + 丢弃计数」语义。

#### 3.2.1 键盘事件环（kbd_queue）：审计式「满则丢」的典范

```c
static volatile u8 kbd_queue[KBD_QUEUE_SIZE];
static volatile u8 kbd_head, kbd_tail;
```

- 结构：64 字节环形数组 + `head`（生产者写位置）与 `tail`（消费者读位置），生产端是
  IRQ1 键盘中断，消费端是 shell 的 `kbd_dequeue`。

```c
TEXT64 void irq1_record(void){u8 raw=inb64(0x60),ch,next,id;irq1_last_scancode=raw;irq1_raw_count++;if(!(raw&0x80)){irq1_count++;ch=(u8)scan64(raw);if(ch){if(waitq_wake_one(&kbd_waitq,THREAD_BLOCKED_KBD,&id)){threads[id].mailbox=ch;threads[id].mailbox_ready=1;kbd_direct_deliveries++;}else{next=(u8)((kbd_head+1)&(KBD_QUEUE_SIZE-1));if(next==kbd_tail)kbd_overflow_count++;else{kbd_queue[kbd_head]=ch;kbd_head=next;}}}}outb64(PIC1_COMMAND,PIC_EOI);}
```

逐行注释：
1. `raw=inb64(0x60)` 读键盘端口，`irq1_raw_count++` 计原始字节；`!(raw&0x80)` 过滤
   break 码（只处理 make 码）。
2. 优先尝试 `waitq_wake_one` 直接唤醒阻塞等待的 worker（`kbd_direct_deliveries++`），
   这是「直投」快路径；没有 waiter 才走环形缓冲慢路径。
3. 环形入队：`next=(head+1)&(KBD_QUEUE_SIZE-1)`——容量是 64 的幂，`&63` 即取模；
   `next==tail` 说明缓冲区已满 → `kbd_overflow_count++` 并**丢弃本次按键**；否则
   `kbd_queue[head]=ch; head=next` 入队。
4. 边界语义：`kbd_overflow_count` 就是这个「审计缓冲」的 `audit_lost` 计数——满了
   就丢、丢了就数，`kbdinfo` 把 `overflows: ` 一行打在屏幕上。
- 为什么这样设计：中断上下文绝不能阻塞（不能等消费者腾位置），所以「满则丢 +
   计数」是唯一正确策略——与 audit backlog 满时的处理同一逻辑。

#### 3.2.2 输入事件环（input_queue）：GUI 事件缓冲

```c
#define INPUT_QUEUE_CAP 16
struct input_event { u8 type,code,flags; int x,y; };
static struct input_event input_queue[INPUT_QUEUE_CAP];
static u32 input_head,input_tail,input_dropped,input_keyboard,input_mouse,input_timer;
static TEXT64 int input_push(u8 type,u8 code,u8 flags,int x,int y){u32 next=(input_head+1)%INPUT_QUEUE_CAP;if(next==input_tail){input_dropped++;return 0;}input_queue[input_head]=(struct input_event){type,code,flags,x,y};input_head=next;return 1;}
static TEXT64 int input_pop(struct input_event*e){if(input_tail==input_head)return 0;*e=input_queue[input_tail];input_tail=(input_tail+1)%INPUT_QUEUE_CAP;return 1;}
```

- 签名与职责：`input_push` 向 16 槽事件环投递一条键盘/鼠标/定时器事件，满则
  `input_dropped++` 返回 0；`input_pop` 从队尾取出一条。
- 算法：`next=(head+1)%16`，若 `next==tail` 说明环满（16 槽最多存 15 条），
  `input_dropped++` 丢弃；否则写入并推进 `head`。
- 与审计缓冲的对应：`input_dropped` 就是事件流的丢失计数器，`inputtest` 用
  `!input_dropped` 断言「4 键盘 + 1 鼠标 + 1 定时器全部入队成功」，输出
  `inputtest: bounded keyboard/mouse/timer event queue passed`。

#### 3.2.3 管道 FIFO 与工作队列环（pipe_model / workqueue）

```c
struct pipe_model { u8 data[PIPE_CAP],head,tail,used; u64 reads,writes,blocked_readers,blocked_writers,wake_readers,wake_writers,poll_registrations; };
```

- `pipe_model`：4 字节环形管道，`head/tail/used` 三变量。`pipe_try_write` 在
  `used>=PIPE_CAP` 时 `blocked_writers++` 并返回 0（生产者阻塞策略——与键盘环的
  「丢」策略不同，但同为「满了怎么办」的边界决策）；`pipe_try_read` 空时
  `blocked_readers++`。
- `pipeinfo` 打印 `pipe used/capacity: ` 与 `blocked r/w:`——把缓冲水位暴露成可观察
  状态，等价于审计系统的 backlog 水位观测。

```c
static TEXT64 int workqueue_submit(u8 kind,u8 data){if(work_used>=WORK_CAP){softirq_model.drops++;return 0;}workqueue[work_head]=(struct work_model){kind,data,1,0};work_head=(u8)((work_head+1)%WORK_CAP);work_used++;softirq_raise(1);return 1;}
```

- 工作队列环：`work_used>=WORK_CAP(4)` 时 `softirq_model.drops++` 并拒绝入队——又是
  「满则拒绝 + 计数」策略；`softirq_run_budget` 用 `SOFTIRQ_BUDGET(2)` 限流消费，
  与 audit backlog 的消费限流同思路。

#### 3.2.4 本课新增 checkpoint：lesson_153_model 与 l160test

```c
struct lesson_153_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_153_model lesson_153_state;
static TEXT64 void l160test(u16*c){lesson_153_state=(struct lesson_153_model){153U,154U,155U,156U,1,1,1,1};int ok=lesson_153_state.valid&&lesson_153_state.active&&lesson_153_state.ready&&lesson_153_state.accounted&&lesson_153_state.b==lesson_153_state.a+1U;text64(c,"l160test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 153 fallback reported");putc64(c,'\n');}
```

- `struct lesson_153_model`：4 个 u32 + 4 个状态位，`a` 以 `153U` 起头，153 = 160−7，
  延续「回锚」链（150/151/152/153 连续四课）。
- `l160test` 算法：①字面量赋值；②五连断言（valid/active/ready/accounted/b==a+1）；
  ③成功串 `bounded networking, namespaces, cgroups, and security checkpoint passed`
  或失败串 `Lesson 153 fallback reported`。
- 为什么：回归探针，不执行任何审计代码；消息里的 "networking, namespaces, cgroups,
  and security" 描述继承机制的覆盖面。

#### 3.2.5 exec64 命令接线（本课增量）

```c
}else if(eq64(word,"l152test")){if(!noargs64(arg))usage64(c,"l152test");else l152test(c);}else if(eq64(word,"l160test")){if(!noargs64(arg))usage64(c,"l160test");else l160test(c);}
```

- 本课把上一课的 `l159test` 分支改名 `l152test`（其模型 `lesson_152_state` 不动，
  仍是 `{152,153,154,155}`），并新增 `l160test` 分支。
- **勘误**：旧 README 写的 `Commands: l153test` 与源码不符——源码中**不存在**
  `l153test` 命令（`grep -c l153test` 为 0），可用的 checkpoint 命令是 `l152test`
  与 `l160test`。
- about 文案 `else text64(c,"Lesson 160: 审计事件缓冲区\n");` 与开机横幅
  `text64(&c,"Lesson 160: 审计事件缓冲区\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT;
  unknown=-ENOSYS; bounded reclaim metadata\n");` 一起构成主题标识。

### 3.3 构建管线（Makefile / linker）

- `kernel64.o`：`gcc $(CFLAGS64) -c`。`CFLAGS64` 含 `-m64 -ffreestanding -fpie
  -fno-stack-protector -mno-red-zone -mno-sse -mno-sse2 -mno-mmx
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror`——`-fpie` 允许 RIP 相对
  寻址（`leaq` 取 stub 地址依赖它），`-mno-red-zone` 防止中断路径踩红区。
- `kernel64.bin`：`ld -m elf_x86_64 -T kernel64.ld -nostdlib` 再 `objcopy -O binary`；
  `kernel64.ld` 从 0 开始布局，`.data` 内用 `. = ALIGN(0x1000)` 依次划出
  idle/rsp0/ist1 三块 guard+stack，末尾三条 `ASSERT(...==0x1000)` 锁死每块栈尺寸。
- `boot.o`：`gcc $(CFLAGS)`（32 位），依赖 `build/kernel64.bin`——外层 `.text64`
  段 `kernel_main64` 以 `.incbin` 嵌入二进制。
- `kernel.iso`：`ld -m elf_i386 -T linker.ld` 链接外层 ELF32，`grub-mkrescue` 出 ISO；
  `linker.ld` 保证 `.multiboot` 在 1 MiB 起、8 字节对齐、`.text64` 紧随其后。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`审计事件缓冲区`、`l160test`、`Lesson 160`）——README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**。Makefile 仅 `check` 目标的 grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) 解析 MBI/建页表
  → enter_long_mode (boot.S) CR4.PAE → EFER.LME → CR0.PG → far jump
  → kernel_main64_binary (kernel64.c)
       module_init_model() → init_model_start() → wait_model_start()
       → adoption_start() → resource_start()
       → pmm_init() → vma_init() → reclaim_init() → vfs_init()
       → 进程/线程元数据装配 → framebuffer_init
       → stack_guards_init / runtime_gdt_tss_init / idle_init / install_idt
       → pit_init()+pic_init() → 横幅 "Lesson 160: 审计事件缓冲区\n..."
       → 键盘 shell 循环：kbd_dequeue → exec64(word)
  IRQ1 → irq1_record：按键→直投 worker 或入 kbd_queue 环（满→overflow 计数）
  exec64 分支 → kbdinfo:ring head/tail/overflows 观测
             → inputtest:input_push×6→环满与否→input_dropped 断言
             → pipeinfo/pipetest:管道环水位与阻塞计数
             → softirqinfo:workqueue 环与 drops 计数
             → l152test / l160test:checkpoint 断言
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径，以本课主题相关的三条命令为例：

1. **`about`** → exec64 命中 `about` 分支 → `text64(c,"Lesson 160: 审计事件缓冲区\n")` → 屏幕打印 `Lesson 160: 审计事件缓冲区`。
2. **`inputtest`** → `inputtest(c)` 复位六个计数器 → 4 次 `input_push(1,…)` 键盘事件
   + 1 次鼠标 + 1 次定时器 → 全部 `input_pop` 消费 → 断言 `input_keyboard==4 &&
   input_mouse==1 && input_timer==1 && !input_dropped` → 输出 `inputtest: bounded
   keyboard/mouse/timer event queue passed`。
3. **`l160test`** → `l160test(c)` 对 `lesson_153_state` 赋值并五连断言 → 输出
   `l160test: bounded networking, namespaces, cgroups, and security checkpoint passed`。

VGA 输出管线全由 `putc64`/`text64`/`hex64` 驱动（0xB8000 文本模式，属性 0x0f 白字
黑底，80×25），`clear64` 负责清屏。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-160-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `审计事件缓冲区`、`l160test`、`Lesson 160` 与 kernel64.c 中的 `l160test`，
  全部命中输出 `Multiboot2 and Lesson 160 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。
  启动横幅第一行为 `Lesson 160: 审计事件缓冲区`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 160: 审计事件缓冲区`
  2. `l160test` → `l160test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  3. `l152test` → `l152test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  4. `inputtest` → `inputtest: bounded keyboard/mouse/timer event queue passed`
  5. `pipeinfo` → `pipe used/capacity: 0/4` 之后 ` reads/writes: ...` 与
     ` blocked r/w: ...`
  6. `pipetest` → `pipetest: bounded FIFO empty/full blocking transitions passed`
  7. `kbdinfo` → 首行 `keyboard: IRQ1 ring producer plus direct worker wake-one`，
     其中 `overflows: 0000000000000000`（未触发溢出时）
- **如何判断成功**：上述命令逐一打印预期串即成功；`make check` 三条 grep 全命中即
  通过。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l160test` 输出 `Lesson 153 fallback reported` | checkpoint 模型字段被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l160test` 的赋值 `{153U,154U,155U,156U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| 输入 `l153test` 报 `unknown command` | 旧 README 命令名是笔误，源码无此命令 | 源码中可用命令是 `l152test` 与 `l160test` |
| `kbdinfo` 的 `overflows` 持续增长 | 键盘事件生产快于消费，`kbd_queue` 64 槽写满 | 检查 `irq1_record` 的 `next==kbd_tail` 判定；确认 shell 主循环持续 `kbd_dequeue` |
| `inputtest` 输出 `input queue fallback reported` | `input_dropped` 非零（环满丢弃）或计数器不符 | 检查 `input_push` 的 `next==input_tail` 判定与 `INPUT_QUEUE_CAP=16`；确认 6 次 push 未超 15 槽 |
| `pipetest` 输出 `BROKEN` | `pipe_model` 三变量被改坏或 `PIPE_CAP` 改动 | 检查 `pipe_try_write/pipe_try_read` 的取模入队与 `used` 增减；`pipeinfo` 看 used/capacity |
| `softirqinfo` 的 `drops` 非零 | `workqueue_submit` 在环满时被调用 | 检查 `WORK_CAP=4` 与 `work_used` 的维护；`softirqtest` 会打印 budget 行为 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 160: 审计事件缓冲区`；`make check` 的 grep 覆盖此串 |

---

## 7. 与 Linux 源码对照

1. **审计记录产生**：Linux 各审计点在 `kernel/auditsc.c` 的 `__audit_syscall_entry`/
   `__audit_syscall_exit` 与 `kernel/audit.c` 的 `audit_log_start`/`audit_log_format`
   中把事件编码进 `struct audit_buffer`；TinyOS 没有审计点，但它的「事件产生 → 有界
   队列」管线（IRQ1 → `kbd_queue`、GUI → `input_queue`）扮演了同类角色。
2. **backlog 与丢失**：Linux `kernel/audit.c` 用 `audit_backlog_limit`（默认 64）
   控制待处理记录上限，超出后 `audit_log_lost` 递增丢失计数并向控制台发
   `audit: backlog limit exceeded`；TinyOS `kbd_queue` 满时 `kbd_overflow_count++`、
   `input_queue` 满时 `input_dropped++`——「满则丢并计数」完全同构。
3. **缓冲结构**：Linux 审计缓冲是 `struct audit_buffer` 链表 + `sk_buff` 队列
   （`audit_skb_queue`），消费端是 `auditd`；TinyOS 用固定数组环形队列（head/tail/
   used 三变量 + 取模）模拟，容量在编译期定死（`KBD_QUEUE_SIZE`/`INPUT_QUEUE_CAP`
   /`PIPE_CAP`/`WORK_CAP`），没有动态分配。
4. **生产者/消费者解耦**：Linux auditd 异步消费 backlog；TinyOS 键盘环由 IRQ1 生产、
   shell 主循环消费，`pipe_model` 由 worker 线程生产/消费并通过 wait queue 唤醒——
   两者都解耦了生产与消费节奏。
5. **水位观测**：Linux 有 `audit_backlog_wait_time` 与 `/proc/net/audit` 统计；
   TinyOS `kbdinfo` 打印 `ring head/tail` 与 `overflows`、`pipeinfo` 打印
   `used/capacity` 与 `blocked r/w`——把缓冲水位暴露成可观察状态。

**权威来源**：Linux `kernel/audit.c`、`kernel/auditsc.c`、`include/uapi/linux/audit.h`、
`include/linux/audit.h`、`Documentation/admin-guide/audit`（audit_backlog_limit 参数）。
**教学模型简化了什么**：真实审计记录有类型枚举、序列号、时间戳、格式化的 syscall
上下文与 skb 队列，支持 auditd 用户态落盘与 `auditctl` 规则过滤；TinyOS 只保留
「有界环形事件队列 + 满则丢 + 计数」的骨架，没有审计 API、没有用户态消费者。

---

## 8. 思考题与练习

1. **概念理解**：Linux audit 的 `audit_backlog_limit` 与 TinyOS `kbd_queue` 的
   `KBD_QUEUE_SIZE` 各自决定什么？为什么两个系统都不允许「无限缓冲」？
2. **源码定位**：在 `kernel64.c` 中找出所有「满则丢并计数」的代码点（至少三个：
   键盘环、输入环、工作队列环），说明各自的丢弃计数器变量名。
3. **动手实验**：修改 `l160test` 的赋值，把 `b` 从 `154U` 改成 `153U`，重新构建运行，
   观察输出是否翻转为 `Lesson 153 fallback reported`；再改回。
4. **动手实验**：把 `INPUT_QUEUE_CAP` 从 16 改成 8，重新构建后运行 `inputtest`，
   观察 6 次 push 是否仍全部成功（提示：环最多存 CAP−1 条，6 条仍安全）；再改成 4
   观察 `input_dropped` 的变化。
5. **Linux 对照**：阅读 `kernel/audit.c` 的 `audit_log_start` 与 `audit_log_lost`，
   对比它们与 `input_push`/`kbd_overflow_count` 的「满则丢」语义，指出 TinyOS
   砍掉了哪些阶段（如 backlog 定时冲洗、auditd 通知、记录落盘）。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是资源/安全主题的检查点课，`kernel64.c` 相对上一课只有 4 处小增量，主题由
   banner/about 文案标识，**源码中无审计 API**——本课是「主题宣告 + 概念模型」型
   检查点课。
2. Linux 审计 = 审计点产生记录 → `audit_log_format` 编码 → 有界 backlog → auditd
   消费；backlog 满则 `audit_lost` 递增并丢弃。
3. TinyOS 的本地近似：`kbd_queue`（满→`kbd_overflow_count++`）、`input_queue`
   （满→`input_dropped++`）、`pipe_model`（满→生产者阻塞）、`workqueue`
   （满→`softirq_model.drops++`）——每个有界环形队列都有「满了怎么办」的显式策略
   与计数。
4. 环形缓冲通用形态：`head/tail/used` 三变量 + 容量取模入队出队，容量为 2 的幂时
   用 `&(CAP-1)` 取代 `%`。
5. 新 checkpoint `l160test` 用字面量赋值 + 五连断言固化回归探针；模型名
   `lesson_153_model` 的 153 = 160−7 延续「回锚」链。
6. 旧 README 的 `Commands: l153test` 已勘误为源码实际的 `l152test` 与 `l160test`。

**下一课**：[`lesson-161-stable/README.md`](../lesson-161-stable/README.md) 主题为
「安全策略决策」，将站在本课「审计事件记录」之上，讲解安全策略（policy）如何被
教学模型固化为新的 checkpoint 模型（命令 `l161test`）。两课的衔接点是「安全监控
链」：本课讲「事件的缓冲记录」，下节课讲「基于记录的策略裁决」。
