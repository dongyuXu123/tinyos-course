# Lesson 156: cgroup 设备策略 — 精讲文档

> **课号**：Lesson 156 ｜ **主题**：cgroup 设备策略（cgroup device policy）
> **课程主线位置**：bounded networking / namespace / cgroup / security 收敛检查点阶段（Lesson 143–156），本课为 **cgroup 系列四连**（层级 153 → CPU 统计 154 → 内存限制 155 → **设备策略 156**）的收官课
> **前置课程**：[`../lesson-155-stable/README.md`](../lesson-155-stable/README.md)（cgroup 内存限制）
> **后续课程**：[`../lesson-157-stable/README.md`](../lesson-157-stable/README.md)（资源限制与回收）——本课为 Lesson 143–156 阶段收尾，下一课把资源台账与回收机制串成「资源限制」主题
> **一句话目标**：讲清 cgroup 设备策略如何回答「这个容器能用哪些设备」——`devices.allow`/`devices.deny` 白名单、cgroup v1 device 控制器与 v2 的 BPF device 程序，对照 Linux `security/device_cgroup.c`、`kernel/bpf/cgroup.c` 与 `kernel/cgroup/devices/`，并把教学内核中继承的**设备访问设施**（I/O 端口 `inb64`/`outb64`、VGA 文本 `0xb8000`、framebuffer MMIO、键盘 IRQ1、CPL3 用户态对设备不可直接访问）按 cgroup 设备策略主题系统化复述，运行 `l156test` 检查点验证。

> **Course status: stable snapshot.**（旧 README 声明保留）本课为检查点（checkpoint）课，主题机制（cgroup 设备控制器）**未在本课源码中实现**，实际增量只是「主题宣告 + 检查点模型」：`kernel64.c` 相对上一课把 `l155test` 恢复为历史命名 `l148test`（挂 `lesson_148_state`），新增 `lesson_149_model`/`lesson_149_state` 与 `l156test`，并更新 `about`/开机横幅为本课主题。继承的进程、GUI、子系统回归保持有效：bounded networking、namespaces、cgroups、security metadata 以确定性方式验证，同时保持 freestanding、固定容量与既有安全不变量。

> **命令说明**：本课检查点命令为 `l156test`（旧 README 所写 `l149test` 按源码勘误）；另保留历史检查点 `l100test`–`l148test`，以及 `guiinfo`/`drawtest`/`fonttest`/`canvastest`/`inputtest`/`windowtest`/`kbdinfo`/`syscallinfo`/`cpl3test` 等设备访问回归命令。

---

## 1. 课程定位（Mission）

**学完本课你能**：用「白名单」解释 cgroup 设备策略（`devices.allow` 写什么设备才让容器用，`devices.deny` 显式拒绝）；说出 Linux cgroup v1 device 控制器（`security/device_cgroup.c` 的 `dev_cgroup`、`devices.allow`/`devices.deny`）与 v2 BPF device 程序（`kernel/bpf/cgroup.c` 的 `BPF_CGROUP_DEVICE`）的差别；在教学内核中沿 `inb64`/`outb64`（I/O 端口）→ `VGA 0xb8000`/`framebuffer`（MMIO）→ `kbdinfo`（IRQ1 键盘）→ `cpl3test`/`syscallinfo`（用户态对设备不可直接访问）观察「设备访问」的边界；运行 `l156test` 验证。

**在课程主线中的位置**：Lesson 153–155 讲完 cgroup 的层级、CPU、内存三个控制器，本课以**设备策略**收官——从「能用多少资源」转到「能不能碰设备」。同时，本课也是 Lesson 143–156 这一「bounded networking/namespace/cgroup/security」收敛阶段的最后一课，把设备访问、用户态边界、权限判定收束在一起。作为检查点课，源码 diff 极小，任务是把继承机制中与「设备访问」相关的设施（I/O 指令、VGA/framebuffer、键盘 IRQ、CPL3 隔离）按 cgroup 设备策略主题系统化复述。

**前置知识清单**（学本课前必须掌握）：
1. I/O 端口：`inb64`/`outb64`/`io_wait64` 与 PIC/PIT/键盘端口地址（Lesson 22s/50s）。
2. 显示设备：`#define VGA ((volatile u16 *)0xb8000ULL)` 文本模式、framebuffer MMIO（`FRAMEBUFFER_VA`）与 `framebuffer_init` 校验（Lesson 25s/70s）。
3. 键盘 IRQ1：`KBD_QUEUE_SIZE 64`、`kbd_queue`/`kbd_head`/`kbd_tail`、`kbdinfo`（Lesson 22s）。
4. CPL3 隔离：`USER_CS 0x33`/`USER_DS 0x2b`、`enter_user`、`cpl3test`/`userpitest`（Lesson 30s/152）。
5. 权限判定：`uaccess_validate` 四重检查与「指针永不 dereference」（Lesson 42s/152）。
6. 检查点模型范式：四 `u32` 计数 + 四 `u8` 布尔位、`b==a+1U` 连续性断言（Lesson 100–155）。

**本课交付（可见结果）**：
- 开机横幅与 `about` 更新为 `Lesson 156: cgroup 设备策略`；
- 新命令 `l156test` 输出 `l156test: bounded networking, namespaces, cgroups, and security checkpoint passed`（或 fallback）；
- `guiinfo`/`drawtest`/`kbdinfo`/`syscallinfo` 继续展示设备访问元数据与边界。

---

## 2. 核心概念精讲

### 2.1 cgroup 设备策略：设备访问的白名单

**直觉**：容器里 `/dev/null`、`/dev/sda` 都能 open，但内核怎么知道「这个容器可以用磁盘但碰不了声卡」？答案是一张**白名单**：设备 cgroup 维护「容器内允许访问的设备号范围（major:minor）与读写权限」，每次 open/读/写都查这张表。

```
容器 /dev/sda → open("/dev/sda", O_RDWR)
   └─ device cgroup 查表：major 8 minor 0 是否在 devices.allow 的 "b 8:0 rw" 中？
       ├─ 命中 → 放行
       └─ 未命中 → 被 devices.deny 拒绝 → open 返回 EPERM
```

**准确定义**：设备 cgroup 是「设备访问控制」的容器原语：它按**设备类型**（b 块 / c 字符）+ **major:minor** + **权限**（r 读/w 写/m 创建节点）构成规则，`devices.allow` 加白、`devices.deny` 加黑，白名单优先于黑名单。容器内的设备节点是否真的存在由 mknod 权限决定，但「能不能用」由设备 cgroup 把关。

### 2.2 为什么需要 cgroup 设备策略（动机）

1. **缩小攻击面**：磁盘、GPU、声卡都是设备；默认全拒、按需放行能把逃逸面压到最小。
2. **容器差异化**：同宿主上「A 容器能用 GPU、B 容器只能用 CPU」——设备是最后一种可细分的资源。
3. **补足 mknod 语义**：只靠 `CAP_MKNOD` 只能管「创建节点」，管不了「打开已有节点」；设备 cgroup 在 open 路径上拦截。

### 2.3 Linux 中 cgroup 设备策略的工作机制

- **v1（传统 device 控制器）**：`security/device_cgroup.c` 的 `struct dev_cgroup` 维护 `dev_exceptions`（allow/deny 规则链表）；`devcgroup_check_permission` 在 `__dev_open`/`__mknod` 时被调用，白名单优先、未命中则查黑名单、再查 `default` 策略（`DEVCG_DEFAULT_ALLOW`/`DEVCG_DEFAULT_DENY`）。文件：`devices.allow`、`devices.deny`、`devices.list`。
- **v2（BPF device 程序）**：`kernel/bpf/cgroup.c` 支持 `BPF_PROG_TYPE_CGROUP_DEVICE`；容器运行时 attach 一段 BPF 程序到 cgroup，程序接收 `struct bpf_cgroup_dev_ctx`（major/minor/access），返回 0/1 决定放行——策略完全由 BPF 表达，`/dev` 节点用 eBPF 的 `bpf_trace_printk` 等方式可审计。
- **v2 移除**：Linux 5.x 后 v2 用 BPF 取代 v1 device 控制器，`/sys/fs/cgroup/devices` 不再默认出现。
- **教学简化**：教学内核没有 `dev_cgroup` 或 `bpf_cgroup_dev_ctx`，但「设备访问边界」由更底层的设施承担：I/O 端口只能在内核态（`inb64`/`outb64` 仅内核代码调用）、CPL3 用户态无法直接触碰端口与 MMIO、`uaccess` 保证用户指针不被 dereference——这是「设备访问被内核垄断」的教学对应。

### 2.4 教学内核中与「设备访问」有关的既有设施

本课主题机制（cgroup 设备控制器）**未在源码中实现**，但「设备访问边界」素材完整存在：

| 素材 | 源码 | 主题含义 |
|---|---|---|
| I/O 端口 | `inb64(p)`/`outb64(p,v)`/`io_wait64()`，PIC 0x20/0xa0、PIT 0x43/0x40 | 端口映射 I/O——设备访问原语（对照 device 规则的 major/minor） |
| VGA 文本 | `#define VGA ((volatile u16 *)0xb8000ULL)`、`putc64` | 内存映射显示设备（MMIO） |
| framebuffer | `struct framebuffer_model{address,bytes,pixels,rects;pitch,width,height;bpp,type,ready,mapped}`、`framebuffer_init` 校验 | 32 位帧缓冲设备（对照 `devices.allow` 的显示设备放行） |
| 键盘 IRQ1 | `KBD_QUEUE_SIZE 64`、`kbd_queue`/`kbd_head`/`kbd_tail`、`kbdinfo` | 输入设备中断路径 |
| 用户态边界 | `USER_CS 0x33`/`USER_DS 0x2b`、`enter_user` | CPL3 用户态无法直接执行 in/out、无法直接访问 MMIO |
| syscall 约束 | `syscallinfo`：`WRITE_CONSOLE uses a fixed kernel-owned message and no user pointer` | 设备访问只经内核 syscall，用户指针不进入设备路径 |
| 权限判定 | `uaccess_validate` 四重检查 | 「指针被拒绝」= 设备访问被拒绝的教学对应 |

### 2.5 检查点模型：lesson_149_model 与 l156test

与前课同构的四 `u32` 计数器 + 四 `u8` 布尔位，计数串 `149→152` 标记 Origin 为 Lesson 149（`a=149,b=150,c=151,d=152`），`valid/active/ready/accounted` 四布尔位与 `b==a+1U` 断言「策略连续性」。本课同时把上一课新增的 `l155test` 恢复为历史命名 `l148test`（挂 `lesson_148_state`，计数 `148→151`）——继续执行「`lXXtest` 命令名向其 Origin 课号收敛」的命名整理。

### 2.6 机制继承 + 检查点增量

本课主题机制（cgroup 设备策略）**不是本课新写的代码**：I/O 端口来自中断/PIC 阶段，framebuffer 来自 GUI 阶段，CPL3 隔离来自用户态阶段。本课实际增量只有三处：`l155test`→`l148test` 更名、`lesson_149_model`+`l156test`、横幅与 `about`。精讲以「机制继承 + 检查点增量」视角，把继承代码按「设备访问边界」主题重新组织，并如实说明：**cgroup 设备控制器（`dev_cgroup`/`devices.allow` 式对象）在源码中不存在，本课只宣告主题**。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|---|---|---|
| `kernel64.c` | 64 位内核主体（PMM、VM、进程/线程、VFS、GUI、检查点） | `l155test`→`l148test` 恢复命名；新增 `lesson_149_model`/`lesson_149_state`/`l156test`；`about` 与开机横幅更新。cgroup 设备策略主题由累积代码承载，本课按主题精讲 |
| `kernel.c` | 32 位引导：Multiboot2 解析、分页表、long mode 交接 | 未变化 |
| `boot.S` | `_start`、进入 long mode、GDT、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位 raw 镜像链接脚本（栈守护段 + ASSERT） | 未变化 |
| `linker.ld` | 32 位外镜像链接脚本 | 未变化 |
| `Makefile` | 构建 + `check` | 仅 `check` 课程串更新（`cgroup 设备策略`/`l156test`/`Lesson 156`） |
| `grub.cfg` | GRUB menuentry | 未变化 |

### 3.2 kernel64.c（设备访问边界 + 本课增量）

#### 3.2.1 本课新增检查点函数

```c
struct lesson_149_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_149_model lesson_149_state;
static TEXT64 void l156test(u16*c){lesson_149_state=(struct lesson_149_model){149U,150U,151U,152U,1,1,1,1};int ok=lesson_149_state.valid&&lesson_149_state.active&&lesson_149_state.ready&&lesson_149_state.accounted&&lesson_149_state.b==lesson_149_state.a+1U;text64(c,"l156test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 149 fallback reported");putc64(c,'\n');}
```

1. **结构与序列**：计数 `149→152`（Origin Lesson 149），四布尔位全置 1，`b==a+1U` 校验计数连续。
2. **逻辑分析（≥3 行）**：结构体字面量一次性写入 `lesson_149_state`，`ok` 由四布尔位 + `b==a+1U` 合取；字面量全 1 使断言恒真，成功串必输出；`Lesson 149 fallback reported` 是防御性兜底，仅在模型计数被破坏时命中。
3. **输出串（逐字抄录）**：成功 `l156test: bounded networking, namespaces, cgroups, and security checkpoint passed`；fallback `Lesson 149 fallback reported`。
4. **恢复的 `l148test`**：本课把上一课 `l155test` 更名回 `l148test`（同为 `lesson_148_state`，计数 `148→151`）；`l100test`–`l147test` 历史检查点全部保留。

#### 3.2.2 设备访问原语：I/O 端口（inb64 / outb64）

```c
static TEXT64 u8 inb64(u16 p){u8 v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static TEXT64 void outb64(u16 p,u8 v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static TEXT64 void io_wait64(void){outb64(0x80,0);}
```

1. **端口 I/O 即设备访问**：`inb`/`outb` 是 x86 的端口映射 I/O 指令——PIC（0x20/0xa0）、PIT（0x43/0x40）都是端口设备；设备 cgroup 的「major/minor」在教学内核里就是「端口号」。
2. **只在内核态可用**：`inb64`/`outb64` 只被内核代码调用（`irq0_schedule` 发 EOI、`pit_init`/`pic_init` 配置）；CPL3 用户态没有这些指令的执行权——「设备访问被内核垄断」即设备策略的最朴素形态。
3. **io_wait64**：对 0x80 端口写 0 做 I/O 延迟——ISA 总线同步的经典做法，也是设备时序的一部分。

#### 3.2.3 显示设备：VGA 文本与 framebuffer

```c
#define VGA ((volatile u16 *)0xb8000ULL)
```

1. **MMIO 文本设备**：`0xb8000` 是 VGA 文本模式帧缓冲，`putc64` 以 `VGA[(*c)++]=0x0f00U|(u8)x` 写入——内存映射 I/O（MMIO），与端口 I/O 并列的第二种设备访问。
2. **volatile**：写 MMIO 必须 `volatile`，防止编译器把「写设备」优化掉——设备寄存器与普通内存不同。
3. **内核独占**：只有 `putc64`/`text64` 等内核函数写 VGA；用户态进程（`enter_user_c` 的 stub）只做 syscall，无法直接写屏。

```c
static TEXT64 void framebuffer_init(struct long_mode_handoff*h){u64 bytes;if(!h->framebuffer_address||h->framebuffer_bpp!=32||h->framebuffer_type!=1||h->framebuffer_width>1024||h->framebuffer_height>768){framebuffer=(struct framebuffer_model){0};return;}
```

1. **设备校验**：`framebuffer_address` 非空、`bpp==32`、`type==1`、尺寸上限 1024x768 全通过才初始化——不合格则置空模型（`ready=0`），这是「设备可用性检查」，对照 device cgroup 的放行判定。
2. **设备元数据**：`struct framebuffer_model{address,bytes,pixels,rects;pitch,width,height;bpp,type,ready,mapped}`——一个完整的设备对象（地址、尺寸、像素数、绘制次数），`guiinfo` 全部打印。
3. **绘制验证**：`drawtest` 用 `framebuffer_rect` 清屏 + 画矩形，成功串 `drawtest: bounded framebuffer clear/rectangles passed`（无帧缓冲时 fallback `framebuffer unavailable; safe fallback reported`）。

#### 3.2.4 输入设备：键盘 IRQ1

```c
#define KBD_QUEUE_SIZE 64
static volatile u8 kbd_queue[KBD_QUEUE_SIZE];
static volatile u8 kbd_head, kbd_tail;
```

1. **中断驱动设备**：键盘经 IRQ1 中断把扫描码压入 `kbd_queue` 环形队列（`kbd_head`/`kbd_tail` 消费者/生产者指针）——输入设备的完整数据路径。
2. **容量边界**：`KBD_QUEUE_SIZE 64` 定长，溢出记 `kbd_overflow_count`——设备缓冲的容量不变量（对照设备策略的访问上限）。
3. **观察**：`kbdinfo` 打印 `kbdinfo: ...` 队列与溢出统计；主循环 `kbd_dequeue` 取键进入命令处理。

#### 3.2.5 用户态对设备的边界：CPL3 与 syscall 约束

```c
text64(c,"syscalls: 0=GETTICKS 1=GETPID 2=WRITE_CONSOLE 3=EXIT; unknown=-ENOSYS\nWRITE_CONSOLE uses a fixed kernel-owned message and no user pointer\nEXIT reports and intentionally halts; no user IRQ callback or cross-address-space scheduler\n");
```

1. **设备访问只经 syscall**：用户态写屏只能走 `WRITE_CONSOLE`（2 号），且 `uses a fixed kernel-owned message and no user pointer`——用户不能把任意指针递给设备路径（对照设备 cgroup 的 open 路径拦截）。
2. **无用户 IRQ 回调**：`no user IRQ callback`——用户态无法注册中断处理，键盘/PIT 设备只归内核。
3. **CPL3 边界**：`USER_CS 0x33`/`USER_DS 0x2b`（RPL=3）保证用户态没有 I/O 特权——设备访问权的最终硬件兜底（对照 Intel SDM 的 IOPL/IOPB）。

#### 3.2.6 exec64 增量与开机横幅

- `about` 输出 `Lesson 156: cgroup 设备策略\n`；检查点分支：
```c
else if(eq64(word,"l148test")){if(!noargs64(arg))usage64(c,"l148test");else l148test(c);}else if(eq64(word,"l156test")){if(!noargs64(arg))usage64(c,"l156test");else l156test(c);}
```
- 开机横幅：
```c
text64(&c,"Lesson 156: cgroup 设备策略\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

### 3.3 构建管线（Makefile / linker）

与之前课程完全相同的构建管线（`-m64 -ffreestanding -fpie -mno-red-zone -mno-sse*` → `kernel64.ld` → `objcopy` → `boot.S` 嵌入 → `grub-mkrescue`）。`make check` 断言 README 含 `cgroup 设备策略`、`Lesson 156`，kernel64.c 含 `l156test`。无新增编译步骤。

### 3.4 主控制流

```
_start → kernel_main32 → enter_long_mode → kernel_main64_binary
 ├─ framebuffer_init（显示设备校验）/ pit_init / pic_init / install_idt（含 IRQ1 键盘）
 ├─ 横幅 "Lesson 156: cgroup 设备策略"
 └─ 主循环：命令 → exec64
     ├─ l156test / l148test → 阶段检查点（lesson_149_state / lesson_148_state）
     ├─ guiinfo / drawtest / fonttest / canvastest → 显示设备元数据与绘制
     ├─ kbdinfo / inputtest → 键盘 IRQ1 队列与输入
     ├─ syscallinfo → 设备访问只经 syscall（WRITE_CONSOLE 无用户指针）
     └─ cpl3test / userpitest → CPL3 用户态边界（不可直接触碰设备）
```

---

## 4. 数据流与运行逻辑

输入命令 → exec64 分支 → 调用函数 → 输出串 → 屏幕显示：

1. **开机**：`framebuffer_init` 校验并映射帧缓冲，`install_idt` 挂 IRQ1 键盘，打印横幅 `Lesson 156: cgroup 设备策略`。
2. **`l156test`** → `l156test(c)` → 初始化 `lesson_149_state` → 五条件断言 → `l156test: bounded networking, namespaces, cgroups, and security checkpoint passed`。
3. **`guiinfo`** → `guiinfo: framebuffer addr/pitch/size/bpp/type: <addr>/<pitch>/<w>x<h>/<bpp>/<type> ready/mapped: ...`——显示设备元数据一览。
4. **`drawtest`** → `framebuffer_rect` 清屏 + 矩形 → `drawtest: bounded framebuffer clear/rectangles passed`（或无帧缓冲 fallback）。
5. **键盘输入** → IRQ1 → `kbd_queue` → 主循环 `kbd_dequeue` → `exec64`；`kbdinfo` 打印队列统计。
6. **`about`** → `Lesson 156: cgroup 设备策略`。

---

## 5. 构建、运行与验证

**依赖**：同前几课（`build-essential gcc-multilib binutils grub-pc-bin grub-common xorriso mtools qemu-system-x86`）。

**构建**（与 Makefile 目标一致）：
```bash
make clean && make -j"$(nproc)"
make check
```
`make check` 预期最终输出：
```
Multiboot2 and Lesson 156 checks passed.
```

**运行**：
```bash
make run
```
成功画面在 QEMU 图形窗口（VGA），**不要加 `-display none`**。

**验证步骤**（预期输出串从源码逐字抄录）：

| 输入 | 预期 VGA 输出 |
|---|---|
| 开机 | `Lesson 156: cgroup 设备策略` 横幅 |
| `l156test` | `l156test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `l148test` | `l148test: bounded networking, namespaces, cgroups, and security checkpoint passed` |
| `drawtest` | `drawtest: bounded framebuffer clear/rectangles passed` |
| `guiinfo` | `guiinfo: framebuffer addr/pitch/size/bpp/type: ...` 行 |
| `syscallinfo` | `syscalls: 0=GETTICKS 1=GETPID 2=WRITE_CONSOLE 3=EXIT; unknown=-ENOSYS` 及说明行 |
| `about` | `Lesson 156: cgroup 设备策略` |

判定成功：`l156test`/`drawtest` 输出 passed、无 fallback/`BROKEN`，GUI 窗口正常绘制，无 triple fault。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `l156test` 输出 `Lesson 149 fallback reported` | `lesson_149_state` 初始化/断言失败（stale 镜像） | `grep -n "l156test" kernel64.c`；确认初始化串 `{149U,150U,151U,152U,1,1,1,1}` 与 `b==a+1U` |
| `drawtest` 输出 `framebuffer unavailable; safe fallback reported` | GRUB 未提供 32bpp 帧缓冲或 `framebuffer_init` 校验失败 | 对照 `framebuffer_init` 的 bpp==32/type==1/尺寸上限；QEMU 命令行确保图形显示 |
| `guiinfo` 的 ready/mapped 为 0 | `framebuffer_init` 被无效 tag 短路 | 检查 Multiboot2 framebuffer tag（boot.S info request）；`mmap64` 看内存映射 |
| 键盘命令不响应 | IRQ1 未安装或 `kbd_queue` 溢出 | `kbdinfo` 看 `kbd_overflow_count`；确认 `install_idt` 挂了 IRQ1 且 PIC 未屏蔽 |
| `cpl3test` 后屏写异常 | 用户态经 WRITE_CONSOLE 之外途径写屏 | 对照 `syscallinfo` 约束：`WRITE_CONSOLE uses a fixed kernel-owned message and no user pointer` |
| 开机横幅仍是旧课 | 未重新构建 | `make clean && make -j"$(nproc)"`；`grep -o 'Lesson 156' kernel64.c` |
| `make check` 报 grep 失败 | README 缺少课程标记 | 确认 README 含 `cgroup 设备策略` 与 `Lesson 156` |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现（文件路径） | 教学模型简化了什么 |
|---|---|---|
| `inb64`/`outb64` 端口 I/O | `arch/x86/include/asm/io.h` 的 `inb`/`outb`；设备 cgroup 的 `dev_exceptions` 按 major/minor 匹配 | 模型无设备号/规则表，端口访问全凭代码位置 |
| `VGA 0xb8000` 与 `framebuffer` | `drivers/video/fbdev/` 的 framebuffer 驱动；`devices.allow` 放行 `c 29:0`（fb） | 模型无 `struct fb_info` 与 DRM 栈，只是内存写 |
| `framebuffer_init` 设备校验 | `drivers/video/fbdev/core/fbmem.c` 的 `register_framebuffer` | 模型只校验 bpp/type/尺寸，无驱动注册 |
| 键盘 IRQ1 环形队列 | `drivers/input/serio/i8042.c`、`drivers/tty/vt/keyboard.c` | 模型无 scancode 译码表与 tty 层，直接出 ASCII |
| CPL3 用户态无法 I/O | `security/device_cgroup.c` 的 `devcgroup_check_permission`（open 路径）；Intel SDM IOPL/IOPB | 模型靠段特权级兜底，无规则匹配 |
| `WRITE_CONSOLE uses ... no user pointer` | `fs/char_dev.c`/`drivers/tty/vt/vt.c` 的写路径；device cgroup 拒绝返回 `EPERM` | 模型只有固定消息，无 copy_from_user 到设备缓冲 |
| `BPF_CGROUP_DEVICE` | `kernel/bpf/cgroup.c`（v2 BPF device 程序，`struct bpf_cgroup_dev_ctx`） | 模型无 eBPF，策略是「内核代码即策略」 |

**权威来源**：Linux `security/device_cgroup.c`（v1 device 控制器）、`kernel/bpf/cgroup.c`（v2 BPF device 程序）、`arch/x86/include/asm/io.h`、`drivers/video/fbdev/` 为对照；Intel SDM 的端口 I/O 与段特权级（IOPL/IOPB）以及 VGA 规范仍为硬件权威来源。

**如实说明**：本课**没有** `dev_cgroup`、`devices.allow`/`devices.deny` 或 `bpf_cgroup_dev_ctx` 的等价实现——cgroup 设备策略是「主题宣告」，教学内核的「设备访问控制」由 CPL3 隔离 + 内核独占端口/MMIO + syscall 约束共同承担。

---

## 8. 思考题与练习

1. **概念理解**：设备 cgroup 的 `devices.allow` 白名单与 `devices.deny` 黑名单为什么是「白名单优先」？`b 8:0 rw` 中的 b/8/0/rw 各指什么？
2. **源码定位**：在 `kernel64.c` 中找出全部调用 `outb64` 的位置（提示：`pit_init`/`pic_init`/`irq0_schedule`/`io_wait64`），列出每个调用的端口号与设备。
3. **动手实验**：给 `framebuffer_init` 增加一条「`framebuffer_height>768` 也允许」的改动，观察 `guiinfo` 输出与 `drawtest` 行为，说明设备校验的作用。
4. **动手实验**：仿照 `devices.allow` 语义，实现一个「端口白名单」函数 `port_allowed(u16 p)` 并在 `inb64`/`outb64` 调用前检查，拒绝时返回 0/空操作，用 `kbdinfo`/`tickinfo` 观察受影响设备。
5. **Linux 对照**：阅读 `security/device_cgroup.c` 的 `devcgroup_check_permission`，说明 open/mknod 路径如何被拦截；对比教学模型「设备访问全靠内核代码位置」的简化。

---

## 9. 本课小结与下一课预告

1. cgroup 设备策略是「设备访问白名单」：按 b/c + major:minor + r/w/m 匹配，`devices.allow` 放行、`devices.deny` 拒绝。
2. Linux v1 用 `security/device_cgroup.c` 的 `dev_cgroup` 规则表，v2 用 `kernel/bpf/cgroup.c` 的 `BPF_CGROUP_DEVICE` 程序——v2 把策略交给 eBPF 表达。
3. 教学内核没有设备 cgroup，但「设备访问边界」由三层承载：端口 I/O（`inb64`/`outb64`）与 MMIO（`VGA`/`framebuffer`）只归内核、CPL3 用户态无 I/O 特权、syscall 约束（`WRITE_CONSOLE ... no user pointer`）。
4. `framebuffer_init` 的设备校验、`KBD_QUEUE_SIZE` 的输入容量不变量、`guiinfo`/`drawtest`/`kbdinfo` 的观察命令共同构成设备层元数据。
5. 检查点增量：`l155test`→`l148test` 更名、新增 `lesson_149_model`+`l156test`、横幅与 `about` 更新为 `Lesson 156: cgroup 设备策略`。
6. 本课收束 Lesson 143–156 的 bounded networking/namespace/cgroup/security 阶段；下一课（[Lesson 157](../lesson-157-stable/README.md)）主题为 **资源限制与回收**——把资源台账（`resource_ledger`）、有序释放（`teardown`）与页回收（`reclaim_one`）串成「资源限制」主题，继续沿用「主题宣告 + checkpoint 增量」模式。
