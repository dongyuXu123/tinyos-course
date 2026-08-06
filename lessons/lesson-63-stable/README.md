# Lesson 63: 键盘、PS/2 AUX 鼠标与输入事件队列 — 精讲文档

> **课号**：Lesson 63（可执行课）
> **主题**：键盘、PS/2 AUX 鼠标与输入事件队列；命令 `inputtest`
> **课程主线位置**：第 4 阶段「图形桌面主线」（61–67）第三课。61 提供可靠
> framebuffer、62 提供字体与画布，本课补上**输入侧的统一抽象**：把键盘、鼠标、
> 定时器三类事件收敛到一个固定容量的环形队列。GUI 主线在 Lesson 67 结课。
> **前置课程**：[`lesson-62-stable/README.md`](../lesson-62-stable/README.md)
> **后续课程**：[`lesson-64-stable/README.md`](../lesson-64-stable/README.md)
> **一句话目标**：学完本课你能说清——`struct input_event` 的五个字段怎么表达
> 一次输入、`input_push`/`input_pop` 如何在一个容量 16 的环形队列上有界地
> 生产/消费、队列满时为何丢弃并计数、`inputtest` 如何用「4 键盘 + 1 鼠标 +
> 1 定时器」把整个队列走一遍并断言成功，以及为什么「PS/2 AUX 鼠标」在本课
> 只是事件类型（type=2）的占位——真正的 IRQ12 硬件路径在 Lesson 67。

---

## 1. 课程定位（Mission）

**一句话目标**：让内核拥有一个**统一、有界、可验证**的输入事件队列。
`inputtest` 输出 `bounded keyboard/mouse/timer event queue passed`，证明
键盘/鼠标/定时器三类事件都能以固定格式入队、出队、不溢出。

- **在课程主线中的位置**：第 4 阶段第三课。这是桌面主线的「输入前置」：
  后续窗口（64）要靠事件做命中与焦点，compositor（65）要靠事件做鼠标移动，
  图形 Terminal（66）要靠事件做按键回显。本课先把「事件长什么样、队列怎么转」
  定死。
- **责任边界**：本课**不负责** icon/window hit-test、光标合成或 Terminal 命令
  （旧 README 责任边界原文）；`inputtest` 只证明**模型状态转换**，不能替代
  QEMU 中的真实鼠标验收（playbook §5：`icontest`/`inputtest` 类确定性测试只
  证明模型）。
- **前置知识清单**：① Lesson 61 的 `framebuffer_model`（本课不直接使用，但
  `ready/mapped` 仍是 GUI 上课凭证）；② Lesson 62 的 canvas 计数思想
  （`glyphs`/`dirty_regions`——本课的 `input_dropped` 是同一套「可验证计数」）；
  ③ 环形队列（`head`/`tail`、`%CAP`、满/空判定）；④ 键盘 IRQ1 的既有 FIFO
  （`irq1_record` → `kbd_queue`）。
- **本课交付**：新命令 `inputtest`；新结构 `struct input_event`；新接口
  `input_push`/`input_pop`；容量 16 的环形输入队列与五个计数器
  （`input_dropped`/`input_keyboard`/`input_mouse`/`input_timer`）。

---

## 2. 核心概念精讲

### 2.1 概念一：struct input_event —— 一次输入的通用档案

定义（源码逐字）：

```c
struct input_event { u8 type,code,flags; int x,y; };
```

- `type`：事件种类。本课约定 `1`=键盘、`2`=鼠标、`3`=定时器（`inputtest` 按
  此约定制造事件）；
- `code`：事件码（键盘为 scancode/字符码，鼠标为按钮号，定时器为 0）；
- `flags`：修饰/状态位（本课键盘事件用 `1` 表示按下）；
- `x`/`y`：坐标（鼠标位移或绝对坐标用；键盘/定时器事件为 0）。

为什么需要：桌面系统有多个输入源（键盘、鼠标、定时器、未来可能更多），如果每
个源一套入队格式，消费者（窗口/合成器）就要分别处理。用一个五字段结构把「种类
+ 内容 + 位置」统一表达，消费端只面对一种事件——这是 Linux input 子系统
`struct input_event { __u16 type, code; __s32 value; }` 的教学化。

### 2.2 概念二：环形队列 —— 固定容量、满则丢弃

定义（源码逐字）：

```c
#define INPUT_QUEUE_CAP 16
static struct input_event input_queue[INPUT_QUEUE_CAP];
static u32 input_head,input_tail,input_dropped,input_keyboard,input_mouse,input_timer;
```

- `input_queue[16]` 是环形缓冲区；`input_head` 是下一个写入位、`input_tail` 是
  下一个读出位；两者相等 → 空；
- 写入前 `(head+1)%16` 若等于 `tail` → 满，`input_dropped++` 并**拒绝本次事件**
  （丢数据是有意的背压，不是越界）；
- `input_keyboard`/`input_mouse`/`input_timer` 分别统计三类事件的成功入队数，
  供 `inputtest` 断言。

为什么需要：输入事件发生在中断上下文（未来 IRQ1/IRQ12），不能阻塞；环形队列
是「无锁、定长、生产者-消费者」的最小实现。容量 16 是本课程的固定上限
（playbook §10 记载输入队列 16 项）。

### 2.3 概念三：push 与 pop —— 生产端与消费端

定义（源码逐字）：

```c
static TEXT64 int input_push(u8 type,u8 code,u8 flags,int x,int y){
    u32 next=(input_head+1)%INPUT_QUEUE_CAP;
    if(next==input_tail){input_dropped++;return 0;}
    input_queue[input_head]=(struct input_event){type,code,flags,x,y};
    input_head=next; return 1; }
static TEXT64 int input_pop(struct input_event*e){
    if(input_tail==input_head)return 0;
    *e=input_queue[input_tail];
    input_tail=(input_tail+1)%INPUT_QUEUE_CAP; return 1; }
```

- `input_push` 算法步骤：① 预计算 `next=(head+1)%16`；② `next==tail` 判满 →
  `input_dropped++`、返回 0；③ 否则在 `head` 位写入五字段结构、`head=next`、
  返回 1；
- `input_pop`：① `tail==head` 判空 → 返回 0；② 拷出 `input_queue[tail]`、
  `tail=(tail+1)%16`、返回 1——消费是**拷贝出队**，不破坏环形结构；
- 边界检查：满时返回 0 而非覆盖（保证最老的事件不被静默冲掉）；空时返回 0
  而非越界读；
- 设计动机：入队只写一个槽位、出队只读一个槽位，任何时刻生产者与消费者互不
  踩踏（单生产者 + 单消费者场景无需锁）。

### 2.4 概念四：事件类型归一化 —— 为什么键盘鼠标定时器要进同一条队列

定义：键盘（type=1）、鼠标（type=2）、定时器（type=3）统一走 `input_queue`，
消费端通过 `e.type` 分派。

为什么需要：桌面主循环最终要回答「现在该处理什么」——可能是按键（打开终端）、
鼠标移动（移动光标）、定时器（时钟刷新）。统一队列让主循环写成
`while(input_pop(&e)) dispatch(e)` 的单形，而不是维护三条独立队列。这也为
Lesson 64 的事件分发（`windowtest`）和 65 的鼠标光标（`desktest`）铺好数据
通道。

### 2.5 概念五：PS/2 AUX 鼠标的边界 —— 本课只是事件类型占位

诚实说明（源码事实）：本课的 `input_queue` 里 **type=2 的鼠标事件是
`inputtest` 手工制造的**（`input_push(2,0,0,4,-2)`），真实的 PS/2 AUX 鼠标
**硬件路径（IRQ12、从 `0x60` 读三字节 packet、同步位 `0x08`、X/Y 符号位、
overflow、坐标裁剪到 `[0,width-1]×[0,height-1]`）在 Lesson 67 才落地**——
`mouse_model`/`mouse_hw_init`/`irq12_entry` 都在 lesson-67 的 kernel64.c 里。

为什么需要说明：playbook §5 明确「QEMU 默认提供 i8042/PS2 鼠标；不要添加
不存在的 `-device ps2-mouse`」「IRQ12 路径负责从 `0x60` 读取 AUX 数据」——
这些经验是为 lesson-67 写的。本课学的是**队列**，不是鼠标协议；把两者分开，
避免把「模型测试」当「物理验收」（playbook §5/§9 禁止事项）。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-62） |
|---|---|---|
| `boot.S` | 引导 | 未变化 |
| `kernel.c` | 32 位阶段 | **未变化**（`diff` 为空） |
| `kernel64.c` | 64 位内核主体 | **核心**：`INPUT_QUEUE_CAP`/`struct input_event` + `input_queue`/五计数器 + `input_push`/`input_pop` + `inputtest` 命令 + `about`/横幅更新 |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | `check` grep 改为 `键盘、PS/2 AUX 鼠标与输入事件队列`/`Lesson 63`（`gui` 保留） |
| `grub.cfg` | GRUB 菜单 | menuentry 标题更新为 lesson-63 主题 |

### 3.2 kernel64.c —— 输入事件队列（本课全部增量）

队列与计数器（源码逐字）：

```c
#define INPUT_QUEUE_CAP 16
struct input_event { u8 type,code,flags; int x,y; };
static struct input_event input_queue[INPUT_QUEUE_CAP];
static u32 input_head,input_tail,input_dropped,input_keyboard,input_mouse,input_timer;
```

- `INPUT_QUEUE_CAP=16`：固定容量，全课程上限之一（playbook §10）；
- 五个 `u32` 计数器：`head`/`tail` 是环形指针，`input_dropped` 累计被丢弃的
  事件，`input_keyboard`/`input_mouse`/`input_timer` 累计三类成功入队数。

入队 `input_push`（源码逐字）：

```c
static TEXT64 int input_push(u8 type,u8 code,u8 flags,int x,int y){
    u32 next=(input_head+1)%INPUT_QUEUE_CAP;
    if(next==input_tail){input_dropped++;return 0;}
    input_queue[input_head]=(struct input_event){type,code,flags,x,y};
    input_head=next; return 1; }
```

- 预计算 `next` 是关键：环形队列**始终保留一个空槽**，否则「满」与「空」都
  表现为 `head==tail` 无法区分；
- 满时不覆盖最老事件，而是 `input_dropped++` 并返回 0——**丢是显式的、可统计
  的**，这正是教学模型「有界 + 可验证」的风格；
- 入队用复合字面量 `(struct input_event){type,code,flags,x,y}` 一次性写入五个
  字段；成功返回 1 供调用方计数。

出队 `input_pop`（源码逐字）：

```c
static TEXT64 int input_pop(struct input_event*e){
    if(input_tail==input_head)return 0;
    *e=input_queue[input_tail];
    input_tail=(input_tail+1)%INPUT_QUEUE_CAP; return 1; }
```

- 空队列返回 0（`tail==head`），不产生越界读；
- 出队是**值拷贝**：调用方拿到的 `*e` 是事件快照，出队后槽位可被覆盖；
- `%INPUT_QUEUE_CAP` 让指针在 16 槽内回绕，形成环。

命令 `inputtest`（源码逐字）：

```c
static TEXT64 void inputtest(u16*c){
    struct input_event e; u32 i;
    input_head=input_tail=input_dropped=input_keyboard=input_mouse=input_timer=0;
    for(i=0;i<4;i++)if(input_push(1,(u8)(30U+i),1,0,0))input_keyboard++;
    if(input_push(2,0,0,4,-2))input_mouse++;
    if(input_push(3,0,0,0,0))input_timer++;
    while(input_pop(&e))if(e.type==1||e.type==2||e.type==3){}
    text64(c,"inputtest: ");
    text64(c,input_keyboard==4&&input_mouse==1&&input_timer==1&&!input_dropped?
        "bounded keyboard/mouse/timer event queue passed":
        "input queue fallback reported");
    putc64(c,'\n'); }
```

- 先把六个状态全部清零（可重入）；
- 制造事件：4 个键盘（type=1，code=30..33，flags=1）、1 个鼠标
  （type=2，x=4,y=-2，**注意 y 为负——位移可以是负的**）、1 个定时器（type=3）；
- 全部入队后 `while(input_pop(&e))` 全部出队——出队循环体只检查 `e.type` 合法
  （1/2/3），不做实质消费（这是队列自测，不是窗口逻辑）；
- 成功判据：`input_keyboard==4 && input_mouse==1 && input_timer==1 &&
  !input_dropped`——四件事同时成立才 `passed`；任一失败走 fallback 串；
- 三次 `if(...)` 而非无脑计数：`input_push` 返回 0（满）时不计入，保证计数与
  「真的入队成功」一致。

`exec64` 新分支（源码逐字）：

```c
else if(eq64(word,"inputtest")){if(!noargs64(arg))usage64(c,"inputtest");else inputtest(c);}
```

- 插在 `canvastest` 之后、`resourceinfo` 之前；`guiinfo`/`drawtest`/`fonttest`/
  `canvastest` 全部保留（回归）；
- `help` 输出串依旧未追加 inputtest（延续小瑕疵）。

横幅与 `about`（源码逐字）：

```c
text64(&c,"Lesson 63: 键盘/鼠标输入事件队列\nGETTICKS, GETPID,
WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
/* about: text64(c,"Lesson 63: 键盘/鼠标输入事件队列\n"); */
```

- 主循环（`framebuffer_init → 横幅 → prompt → 键盘循环 → exec64`）与 62 相同，
  仅横幅更新；
- 注意键盘的硬件路径**没有改**：`irq1_record` 仍写旧 `kbd_queue`，尚未把按键
  转成 `input_queue` 事件——本课队列与既有键盘 FIFO 并存，未接线（源码事实，
  接线留给图形 Terminal 课程）。

### 3.3 构建管线（Makefile / kernel64.ld / grub.cfg）

- 编译/链接流程与 61/62 相同（`kernel64.o` → `kernel64.bin` → `boot.S incbin`
  → 外层 i386 ELF → `grub-mkrescue`）；
- `check`（本课）：`grub-file --is-x86-multiboot2` + 三条 grep：README 含
  `键盘、PS/2 AUX 鼠标与输入事件队列`、`gui`、`Lesson 63`；通过打印
  `Multiboot2 and Lesson 63 checks passed.`；
- `run`：`qemu-system-x86_64 -accel tcg -vga std ...`（与 62 相同）；
- `grub.cfg`：`gfxmode/gfxpayload=800x600x32` 不变，menuentry 更名；
- `kernel64.ld`/`linker.ld` 未变。

### 3.4 主控制流

```text
GRUB → _start → kernel_main32（未变）→ 长模式 → kernel64.bin
  ├─ framebuffer_init(h)：ready/mapped（Lesson 61）
  ├─ 横幅 "Lesson 63: 键盘/鼠标输入事件队列\n..."
  └─ 键盘循环 → exec64:
        inputtest → 清零六个状态
                  → push 4×(type=1) + 1×(type=2) + 1×(type=3)
                  → while(input_pop) 全部出队
                  → 计数断言 → "bounded keyboard/mouse/timer event queue passed"
```

---

## 4. 数据流与运行逻辑

```text
输入 "inputtest"
  → input_head=input_tail=input_dropped=input_keyboard=input_mouse=input_timer=0
  → i=0..3: input_push(1,30+i,1,0,0) → 每次成功 input_keyboard++  → 4
  → input_push(2,0,0,4,-2)           → 成功 input_mouse++        → 1
  → input_push(3,0,0,0,0)            → 成功 input_timer++        → 1
  → while(input_pop(&e)) 依次取回 6 个事件（FIFO 顺序），type 均为 1/2/3
  → input_keyboard==4 && input_mouse==1 && input_timer==1 && !input_dropped
  → "inputtest: bounded keyboard/mouse/timer event queue passed"
```

满队列路径：若容量只有 4 而连续 push 5 次，第 5 次 `next==tail` →
`input_dropped++`、返回 0——`input_dropped` 由此可断言「有界」成立。

---

## 5. 构建、运行与验证

### 5.1 依赖

同前课：`gcc`、`binutils`、`grub-pc-bin`/`grub-common`、`xorriso`、`mtools`、
`qemu-system-x86_64`；GUI 专项验收另需 `socat`、`python3`。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 输出：`Multiboot2 and Lesson 63 checks passed.`（README 必须含
`键盘、PS/2 AUX 鼠标与输入事件队列`、`gui`、`Lesson 63`，缺一即失败）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：
`Lesson 63: 键盘/鼠标输入事件队列\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n`

验证步骤（输出串从源码逐字）：

```bash
guiinfo
```

预期：`guiinfo: ... ready/mapped: 0000000000000001/0000000000000001`（回归）。

```bash
fonttest
canvastest
```

预期：两条 `passed` 串（Lesson 62 回归）。

```bash
inputtest
```

预期：`inputtest: bounded keyboard/mouse/timer event queue passed`

注意：旧 README 提到的 `mousetest`/`mouseinfo` **在本课源码中不存在**（源码
事实），真实鼠标诊断命令属于 Lesson 67；本课的输入验证以 `inputtest` 为准。

### 5.4 GUI 专项验收（QEMU VGA 自动化）

按教程要求 GUI 课程走专项流程（`docs/gui-debugging-playbook.md` §8 与
`learning-guide.md` §10.2）：单课验收
`scripts/qemu-vga-check.sh lessons/lesson-63-stable inputtest`；
第 4 阶段结课验收统一
`scripts/qemu-vga-check.sh lessons/lesson-67-stable guiinfo drawtest fonttest canvastest inputtest windowtest desktest shellgui`。

脚本对 `inputtest` 的 VGA 文本 dump 要求含 `inputtest:` 结果标记（`qemu-vga-check.sh`
的命令结果检查），且不能出现 `fallback`/异常。注意：`mouseinfo` 出现在脚本的
GUI 命令清单里但**本课不提供**，结课验收时才由 lesson-67 满足。

### 5.5 课程实测记录（稳定快照）

旧 README 的学习路径（`make -C lessons/lesson-63-learning` +
`make -C lessons/lesson-63-learning check`）已验证；stable 快照复验：
`make check` 输出 `Multiboot2 and Lesson 63 checks passed.`；`inputtest` 输出
`bounded keyboard/mouse/timer event queue passed`。构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `inputtest` 输出 `input queue fallback reported` | 四断言之一失败：push 被拒（`input_dropped>0`）或某类计数不对 | 逐项核对：4 键盘、1 鼠标、1 定时器、无丢弃；检查 `input_push` 的满判定 |
| 事件顺序错乱 | 环形指针 head/tail 使用错误 | 对照 `input_push`（`head+1` 写入位）与 `input_pop`（`tail` 读出位）；FIFO 序 |
| 队列满时旧事件被覆盖 | 实现了覆盖式写而非拒绝写 | 对照源码：`next==input_tail` 分支是 `return 0`，不覆盖 |
| 计数与入队不一致 | 调用方忽略 `input_push` 返回值 | `inputtest` 用 `if(input_push(...))` 包住计数，返回值即成功标志 |
| 想测真实鼠标移动 | 本课**没有** IRQ12/PS2 AUX 硬件路径（源码事实） | 模型验证看 `inputtest`；物理鼠标验收等 lesson-67（playbook §5） |
| 误以为要 `-device ps2-mouse` | playbook §5：QEMU 默认提供 i8042/PS2 鼠标 | 不要添加不存在的设备参数 |
| `make check` 报错 | README 缺 `键盘、PS/2 AUX 鼠标与输入事件队列`/`gui`/`Lesson 63` 之一 | 对照 Makefile `check` 三条 grep |
| 分不清「队列正确」与「鼠标可用」 | 队列只是输入通道 | **VGA 文本仍是权威诊断通道**：先看 `inputtest:` 文本标记，再做物理验收（playbook §10） |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `struct input_event { u8 type,code,flags; int x,y; }` | `include/uapi/linux/input.h` 的 `struct input_event { __u16 type, code; __s32 value; }` | 都是「类型+代码+数值」的统一事件；教学模型多 x/y 两个坐标字段 |
| `type` 1=键盘/2=鼠标/3=定时器 | `drivers/input/input.c` 的 `EV_KEY`/`EV_REL`/`EV_ABS` 与 `input_event()` | 教学模型用 u8 枚举教学常量，Linux 是 u16 位掩码族 |
| `input_push`/`input_pop` 环形队列（容量 16） | `drivers/input/evdev.c` 的 `struct evdev_client`（`buffer[]` + `head`/`tail` 环形） | 机制相同：定长环形 + 满则丢（`dev->drop` 计数）；Linux 容量可调且支持 `EVIOCSFF` |
| 满时 `input_dropped++` 拒绝写入 | evdev 的 `client->packet_head` 溢出丢弃 | 都是「有界背压」，教学模型显式计数 |
| `inputtest` 手工制造三类事件自测 | `tools/testing/selftests/` 或内核 `input-inject` 类工具 | 教学模型在命令里注入并断言；Linux 有独立测试工具链 |
| 键盘/鼠标/定时器归一化 | `drivers/input/input.c` 的 `input_register_device` + `input_handle_event` 单入口 | 概念一致：所有设备进同一事件通道，消费端统一分派 |

**权威来源**：Linux `include/uapi/linux/input.h`（事件结构）与
`drivers/input/evdev.c`（环形客户端队列）作为工程对照；本课无新硬件操作，不
涉及 Intel SDM 新内容。

**教学模型简化了什么**：单生产者单消费者无锁假设（Linux evdev 有并发/睡眠
语义）；容量固定 16（Linux 可配置）；没有 `EV_*` 位掩码族与 `input_dev`
注册表；`inputtest` 是纯软件注入，不接真实 IRQ——真实的 PS/2 AUX（IRQ12、
三字节 packet、符号位/overflow/裁剪）全部留到 Lesson 67。

---

## 8. 思考题与练习

1. **概念理解**：为什么环形队列要「保留一个空槽」让 `next==tail` 判满？如果
   把容量当 17 用会怎样？
2. **源码定位**：在 `inputtest` 里指出 `input_dropped`、`input_keyboard`、
   `input_mouse`、`input_timer` 四个计数各自在哪个语句被修改，以及为什么计数
   用的是 `if(input_push(...))` 而不是无条件 `++`。
3. **动手实验**：把 `INPUT_QUEUE_CAP` 改成 5 再跑 `inputtest`（它只 push 6 个
   事件），观察 `input_dropped` 变为 1、命令输出变为 fallback 串，然后改回
   （勿提交）。
4. **Linux 对照**：读 `drivers/input/evdev.c` 的 `evdev_pass_to_client`，对照
   本课 `input_push`，列出环形缓冲实现上的相同点与差异（如 `packet_head`、
   拷贝粒度）。
5. **设计思考**：本课队列的消费端循环是 `while(input_pop(&e))` 空转检查
   `e.type`；如果 Lesson 64 要让窗口响应鼠标事件，`input_pop` 的返回和
   `e.type` 分派需要扩展成什么？（提示：事件目标/坐标命中。）

---

## 9. 本课小结与下一课预告

**小结**：本课把输入侧收敛成一个固定容量的环形队列：`struct input_event` 用
type/code/flags/x/y 统一表达键盘、鼠标、定时器事件；`input_push` 在满时拒绝并
`input_dropped++`（有界背压），`input_pop` 按 FIFO 值拷贝出队；`inputtest`
注入 6 个事件走完全程，用四个计数断言 `bounded keyboard/mouse/timer event
queue passed`。本课**没有**接入真实鼠标硬件——`mousetest`/`mouseinfo` 在本课
源码中不存在，PS/2 AUX（IRQ12）完整实现属于 Lesson 67，playbook §5 的鼠标经验
是为那课准备的；键盘 IRQ1 也仍写旧 `kbd_queue`，未转入新队列。队列与既有键盘
FIFO 并存，等待图形桌面课程接线。

**下一课预告**：[Lesson 64](../lesson-64-stable/README.md) 使用这些事件实现
icon/window/widget 对象模型、hit-test、focus、z-order 与双击状态，命令
`windowtest`——本课的事件将成为窗口命中的输入。跨课程排错统一参考
[`docs/gui-debugging-playbook.md`](../../docs/gui-debugging-playbook.md)。
