# Lesson 64: 桌面对象模型与事件分发 — 精讲文档

> **课号**：Lesson 64（可执行课）
> **主题**：桌面对象模型与事件分发；命令 `windowtest`
> **课程主线位置**：第 4 阶段「图形桌面主线」（61–67）第四课。61 提供可靠
> framebuffer、62 提供字体与画布、63 提供输入事件队列，本课在纯数据层面定义
> 「窗口（window）、控件（widget）」两类对象并实现 hit-test、focus、事件分发
> 的模型。GUI 主线在 Lesson 67 结课。
> **前置课程**：[`lesson-63-stable/README.md`](../lesson-63-stable/README.md)
> **后续课程**：[`lesson-65-stable/README.md`](../lesson-65-stable/README.md)
> **一句话目标**：学完本课你能说清——`struct window_model`/`struct widget_model`
> 各字段的含义、`widget_hit` 如何在固定数组上做有界命中测试、`windowtest` 的
> 夹具数据如何摆布两个窗口和两个控件、事件分发分支为什么要求「命中 + 焦点窗口
> 的 focused 标志」同时成立，以及**当前夹具下该分支不触发的事实**（源码逐字
> 推导），以及为什么 iconinfo/icontest 不在本课而属于 Lesson 67。

---

## 1. 课程定位（Mission）

**一句话目标**：让内核拥有「窗口/控件」的**对象模型**与**事件分发判定**：
在固定容量的 `windows[4]`/`widgets[8]` 数组上，回答「坐标 (x,y) 命中哪个窗口
的哪个控件」「焦点窗口是谁」「命中且焦点合法时事件是否被分发」。全程无动态
分配、无任意回调。

- **在课程主线中的位置**：第 4 阶段第四课，是桌面 UI 的**数据层**。65 课要把
  这些窗口画出来（compositor），66 课要让 Terminal 窗口接收输入；本课的
  `window_count`/`widget_count`/`focused_window`/`dispatched_events` 就是那些
  课程的模型前置。
- **责任边界**（旧 README 责任边界原文）：本课只验证**对象模型和状态转换**，
  **不证明**真实显存提交、光标恢复或物理鼠标；确定性 `windowtest` 是模型覆盖，
  不是 GUI 验收。旧 README 提到的 `iconinfo`/`icontest`（图标与有界双击）在
  **本课源码中不存在**——`grep` 证实它们属于 Lesson 67。
- **前置知识清单**：① Lesson 63 的 `input_event` 与事件队列（本课
  `dispatched_events` 消费的就是「事件」概念）；② 结构体与复合字面量初始化
  `(struct X){...}` 的逐字段对应；③ 矩形包含判定 `x>=x0 && y>=y0 &&
  x-x0<w && y-y0<h`（注意用减法避免 `x0+w` 的 u32 溢出）；④ z-order 概念
  （数值大者在顶层）。
- **本课交付**：新命令 `windowtest`；新结构 `struct window_model`/
  `struct widget_model`；新函数 `widget_hit`；`WINDOW_CAP=4`/`WIDGET_CAP=8`
  两个上限常量与四个全局状态量。

---

## 2. 核心概念精讲

### 2.1 概念一：window 与 widget —— 桌面对象的两层模型

定义（源码逐字）：

```c
#define WINDOW_CAP 4
#define WIDGET_CAP 8
struct window_model { u32 x,y,w,h,z; u8 visible,focused,dirty; };
struct widget_model { u8 window,type,visible,focused; u32 x,y,w,h; };
```

- `window_model`：窗口的屏幕矩形（x/y/w/h）、z-order（z，大者在上）、可见性、
  焦点、脏标志。`dirty` 是「这个窗口需要重绘」的标记——正是 Lesson 62
  `dirty_regions` 思想的对象化；
- `widget_model`：窗口内的控件。`window` 指向所属窗口下标；`type` 是控件类型
  （windowtest 里 1 与 2）；`visible`/`focused` 是控件级可见/焦点；
  x/y/w/h 是**窗口内相对坐标**（windowtest 里 widgets[1] 的 x=176 相对
  window1 原点，注意窗口 1 在屏幕 (160,40)）；
- 为什么两层：窗口管理「整体布局/层级」，控件管理「窗口内的可点击区域」。
  命中测试先按窗口过滤，再按控件过滤——与 Linux/X11 的 window tree 分层思想
  一致，只是教学模型固定为两层数组。

### 2.2 概念二：hit-test —— 有界线性查找

定义（源码逐字）：

```c
static TEXT64 int widget_hit(u32 wi,u32 x,u32 y){
    u32 i;
    for(i=0;i<widget_count;i++)
        if(widgets[i].window==wi&&widgets[i].visible&&
           x>=widgets[i].x&&y>=widgets[i].y&&
           x-widgets[i].x<widgets[i].w&&y-widgets[i].y<widgets[i].h)
            return 1;
    return 0; }
```

- 线性扫描 `widget_count` 个控件（上限 8），条件链：所属窗口匹配 `wi`、
  控件可见、点落在 `[x0,x0+w) × [y0,y0+h)` 半开区间内；
- 用 `x-x0<w` 而非 `x<x0+w`：避免 `x0+w` 的 u32 加法溢出（与 Lesson 61 的
  `endx=(u64)x+w` 同一纪律）；
- 命中即返回 1——**返回「第一个」命中控件，不区分 z 序**；这正是模型的简化
  （Linux/X11 命中要沿窗口树从顶层往下）。
- 边界：`i<widget_count` 而非 `WIDGET_CAP`——只扫已登记控件；`widget_count`
  未越界由调用方保证。

### 2.3 概念三：focus 与事件分发 —— 命中后还要过「焦点关」

定义：分发条件是 `hit && windows[focused_window].focused`——命中**且**全局焦点
窗口 `focused_window` 的 `focused` 标志为 1，事件才被分发（`dispatched_events++`）
并把该窗口标脏（`dirty=1`）。

为什么需要：图形系统里点击一个窗口不代表能输入——只有**聚焦窗口**接收键盘/
焦点类事件。教学模型把「焦点窗口（全局指针）」与「窗口自身的 focused 标志」
分开建模，是 Linux 的 active window / focus window 概念的简化。

### 2.4 概念四：z-order 与焦点的一致性 —— 本课夹具的一个真实瑕疵

诚实说明（源码逐字推导，重要）：`windowtest` 的夹具把 `focused_window` 设为 1
（z=1 的顶层窗口），但 `windows[1]` 的 `focused` 字段是 **0**（`{160,40,140,90,
1,1,0,1}` 的第 6 个字段）；真正 `focused=1` 的是 `windows[0]`（z=0 的底层窗口）。
于是 `hit && windows[focused_window].focused` = `hit && windows[1].focused` =
`1 && 0` = **假**，`dispatched_events` 保持 0，成功判据 `dispatched_events==1`
不成立——`windowtest` 在**当前夹具下输出的是 fallback 串**
`window model fallback reported`。

这是 stable 快照里的一个**夹具不一致**（要么 `focused_window=0`、要么
`windows[1].focused=1` 才能触发成功路径）。精讲文档按源码事实如实呈现：成功串
与 fallback 串都是源码逐字，但当前夹具停在 fallback 一侧；它同时是理解
「焦点关」的最好教材——`focused_window` 与窗口 `focused` 标志必须指向一致。

### 2.5 概念五：为什么 iconinfo/icontest 不在本课

旧 README 声称本课命令含 `iconinfo`/`icontest`、且做「图标选中和有界双击」。
按源码 `grep` 事实：这两个命令、图标模型与双击状态机出现在 **Lesson 67**
（`icontest`/`iconinfo`/`desktopinfo` 在 lesson-67 的 exec64 里）。本课的实际
增量只有 `windowtest`。文档以源码为准，把图标模型列为 Lesson 67 的内容，并在
验证章节提示：不要在本课环境里期待 `iconinfo`/`icontest` 命令。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-63） |
|---|---|---|
| `boot.S` | 引导 | 未变化 |
| `kernel.c` | 32 位阶段 | **未变化**（`diff` 为空） |
| `kernel64.c` | 64 位内核主体 | **核心**：`WINDOW_CAP`/`WIDGET_CAP` + `struct window_model`/`struct widget_model` + 两个对象数组 + 四状态量 + `widget_hit` + `windowtest` 命令 + `about`/横幅更新 |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | `check` grep 改为 `桌面对象模型与事件分发`/`Lesson 64`（`gui` 保留） |
| `grub.cfg` | GRUB 菜单 | menuentry 标题更新为 lesson-64 主题 |

### 3.2 kernel64.c —— 窗口/控件模型与 hit-test（本课全部增量）

结构与状态（源码逐字）：

```c
#define WINDOW_CAP 4
#define WIDGET_CAP 8
struct window_model { u32 x,y,w,h,z; u8 visible,focused,dirty; };
struct widget_model { u8 window,type,visible,focused; u32 x,y,w,h; };
static struct window_model windows[WINDOW_CAP];
static struct widget_model widgets[WIDGET_CAP];
static u32 window_count,widget_count,focused_window,dispatched_events;
```

- `WINDOW_CAP=4`/`WIDGET_CAP=8`：playbook §10 记载的「窗口 4 个、widget 8 个」
  上限，与输入队列 16、Terminal 96/10 一起构成课程的固定容量体系；
- `focused_window` 是全局焦点窗口下标；`dispatched_events` 统计被分发的事件数；
- 对象数组是**静态固定**的，不引入动态分配——这是本课对「不引入任意应用回调」
  的落实。

命中测试 `widget_hit`（源码逐字）：

```c
static TEXT64 int widget_hit(u32 wi,u32 x,u32 y){
    u32 i;
    for(i=0;i<widget_count;i++)
        if(widgets[i].window==wi&&widgets[i].visible&&
           x>=widgets[i].x&&y>=widgets[i].y&&
           x-widgets[i].x<widgets[i].w&&y-widgets[i].y<widgets[i].h)
            return 1;
    return 0; }
```

- 循环上界 `widget_count`（≤8），每步五个条件 AND：窗口归属、可见、x 下界、
  y 下界、x/y 上界（半开区间）；
- 用 `x-widgets[i].x<widgets[i].w` 的形式：避免 `x0+w` 溢出，且自然处理负向
  差值（若 `x<x0`，`x-x0` 无符号下会是一个大数，必然不小于 w，从而排除——
  妙处在于无符号减法自动完成方向判定）；
- 命中即返回 1，不继续找更优命中（教学简化）；未命中返回 0。

命令 `windowtest`（源码逐字）：

```c
static TEXT64 void windowtest(u16*c){
    int hit;
    window_count=2;widget_count=2;focused_window=1;dispatched_events=0;
    windows[0]=(struct window_model){8,32,120,80,0,1,1,1};
    windows[1]=(struct window_model){160,40,140,90,1,1,0,1};
    widgets[0]=(struct widget_model){0,1,1,0,16,44,80,20};
    widgets[1]=(struct widget_model){1,2,1,1,176,52,100,24};
    hit=widget_hit(1,180,60);
    if(hit&&windows[focused_window].focused){
        dispatched_events++; windows[focused_window].dirty=1; }
    text64(c,"windowtest: ");
    text64(c,window_count==2&&widget_count==2&&hit&&dispatched_events==1?
        "bounded windows, widgets, focus, hit testing, and event dispatch passed":
        "window model fallback reported");
    putc64(c,'\n'); }
```

夹具逐字段拆解：
- `windows[0]`：屏幕 (8,32) 起 120×80，z=0，visible=1，**focused=1**，dirty=1；
- `windows[1]`：屏幕 (160,40) 起 140×90，z=1（顶层），visible=1，**focused=0**，
  dirty=1；
- `widgets[0]`：属于窗口 0，type=1，visible=1，窗口内 (16,44) 起 80×20；
- `widgets[1]`：属于窗口 1，type=2，visible=1，窗口内 (176,52) 起 100×24
  （即屏幕绝对坐标 (336,92) 起——因为窗口 1 在 (160,40)）。

执行推导（每步 ≥3 行）：
1. `hit=widget_hit(1,180,60)`：只查 window=1 的控件——`widgets[1]` 满足
   `180>=176 && 60>=52 && 4<100 && 8<24` → hit=1；
2. 分发分支 `if(hit && windows[focused_window].focused)`：`focused_window=1`，
   但 `windows[1].focused==0`（夹具第 6 字段）→ 条件为假，`dispatched_events`
   保持 0；
3. 成功判据 `window_count==2 && widget_count==2 && hit && dispatched_events==1`：
   前三项真、最后一项假 → **输出 fallback 串** `window model fallback reported`；
4. 意图 vs 事实：成功串 `bounded windows, widgets, focus, hit testing, and
   event dispatch passed` 设计上需要 `focused_window` 指向 `focused=1` 的窗口
   （例如把 `focused_window` 设为 0，或用 `windows[1].focused=1` 的夹具）——
   当前 stable 快照的夹具没有做到，这是需要如实记录的 fixture 不一致。

`exec64` 新分支（源码逐字）：

```c
else if(eq64(word,"windowtest")){if(!noargs64(arg))usage64(c,"windowtest");else windowtest(c);}
```

- 插在 `inputtest` 之后、`resourceinfo` 之前；`guiinfo`~`inputtest` 全部保留
  （回归）；
- `help` 输出串未追加 windowtest（延续既有小瑕疵）。

横幅与 `about`（源码逐字）：

```c
text64(&c,"Lesson 64: 窗口、widget 与事件分发\nGETTICKS, GETPID,
WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
/* about: text64(c,"Lesson 64: 窗口、widget 与事件分发\n"); */
```

- 主循环与 63 相同，仅横幅更新；`windowtest` 不画任何像素——纯模型命令
  （VGA 文本层可见其输出串）。

### 3.3 构建管线（Makefile / kernel64.ld / grub.cfg）

- 编译/链接流程与 61–63 相同（`kernel64.o` → `kernel64.bin` → `boot.S incbin`
  → 外层 i386 ELF → `grub-mkrescue`）；
- `check`（本课）：`grub-file --is-x86-multiboot2` + 三条 grep：README 含
  `桌面对象模型与事件分发`、`gui`、`Lesson 64`；通过打印
  `Multiboot2 and Lesson 64 checks passed.`；
- `run`：`qemu-system-x86_64 -accel tcg -vga std ...`（与 63 相同）；
- `grub.cfg`：`gfxmode/gfxpayload=800x600x32` 不变，menuentry 更名；
- `kernel64.ld`/`linker.ld` 未变。

### 3.4 主控制流

```text
GRUB → _start → kernel_main32（未变）→ 长模式 → kernel64.bin
  ├─ framebuffer_init(h)：ready/mapped（Lesson 61）
  ├─ 横幅 "Lesson 64: 窗口、widget 与事件分发\n..."
  └─ 键盘循环 → exec64:
        windowtest → 置 2 窗口 + 2 控件 → widget_hit(1,180,60)=1
                    → if(hit && windows[1].focused)  ← focused==0，不触发
                    → dispatched_events==0
                    → "windowtest: window model fallback reported"（当前夹具）
```

---

## 4. 数据流与运行逻辑

```text
输入 "windowtest"
  → window_count=2, widget_count=2, focused_window=1, dispatched_events=0
  → windows[0]=(8,32,120,80,z=0,visible,focused=1)
  → windows[1]=(160,40,140,90,z=1,visible,focused=0)
  → widgets[0]=(window=0,type=1,x=16,y=44,w=80,h=20)
  → widgets[1]=(window=1,type=2,x=176,y=52,w=100,h=24)   [窗口内相对坐标]
  → hit = widget_hit(1,180,60)
         widgets[0]: window=0 ≠ 1 → 跳过
         widgets[1]: 180≥176 && 60≥52 && 4<100 && 8<24 → 命中 → hit=1
  → if(1 && windows[1].focused=0) → 假 → dispatched_events 仍为 0
  → 判据 window_count==2 && widget_count==2 && hit==1 && dispatched_events==1
     → 最后一项为假
  → "windowtest: window model fallback reported"
```

若把 `focused_window` 改为 0（其 `focused` 标志为 1），则分发分支触发：
`dispatched_events=1`、`windows[0].dirty=1`，判据全真，输出成功串
`bounded windows, widgets, focus, hit testing, and event dispatch passed`。

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

`make check` 输出：`Multiboot2 and Lesson 64 checks passed.`（README 必须含
`桌面对象模型与事件分发`、`gui`、`Lesson 64`，缺一即失败）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：
`Lesson 64: 窗口、widget 与事件分发\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n`

验证步骤（输出串从源码逐字）：

```bash
guiinfo
fonttest
canvastest
inputtest
```

预期：`ready/mapped: ...1/1` 与三条 `passed` 串（61–63 回归）。

```bash
windowtest
```

**如实说明**：源码判据要求 `dispatched_events==1`，而当前夹具的
`windows[1].focused==0` 使分发分支不触发，因此按源码逐字推导，本命令当前输出：

```text
windowtest: window model fallback reported
```

成功串（设计意图，源码逐字）为
`bounded windows, widgets, focus, hit testing, and event dispatch passed`——
需要先修正夹具（如 `focused_window=0` 或 `windows[1].focused=1`）才能触发
（见 §6 调试地图，勿提交修改）。

注意：旧 README 声称的 `iconinfo`/`icontest` **本课不存在**（源码事实），
图标与双击模型属于 Lesson 67。

### 5.4 GUI 专项验收（QEMU VGA 自动化）

按教程要求 GUI 课程走专项流程（`docs/gui-debugging-playbook.md` §8 与
`learning-guide.md` §10.2）：单课验收
`scripts/qemu-vga-check.sh lessons/lesson-64-stable windowtest`；
第 4 阶段结课验收统一
`scripts/qemu-vga-check.sh lessons/lesson-67-stable guiinfo drawtest fonttest canvastest inputtest windowtest desktest shellgui`。

脚本对 `windowtest` 只要求 VGA 文本 dump 含 `windowtest:` 标记（成功/fallback
串都满足），且无 `fallback`/异常针对 GUI 图形命令的硬校验主要落在
drawtest/fonttest 等绘制命令上；本课是纯模型命令，物理鼠标/光标等验收从
Lesson 65/67 开始。

### 5.5 课程实测记录（稳定快照）

旧 README 的学习路径（`make -C lessons/lesson-64-learning` +
`make -C lessons/lesson-64-learning check`）已验证；stable 快照复验：
`make check` 输出 `Multiboot2 and Lesson 64 checks passed.`。关于 `windowtest`
的实测：按源码静态推导当前夹具输出 fallback 串（详见 §5.3 与 §6），这一行为
在 stable 快照中如实保留，构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `windowtest` 输出 `window model fallback reported` | `windows[1].focused==0` 使 `if(hit && windows[focused_window].focused)` 不触发，`dispatched_events` 保持 0 | 静态推导：`focused_window=1` 指向的窗口 focused 字段为 0；把 `focused_window` 改为 0（focused=1）或把 windows[1].focused 置 1 即可触发成功路径（勿提交） |
| 想验证分发成功串 | 夹具不一致（见上） | 先改夹具再跑 `windowtest`，观察 `dispatched_events` 与 `windows[0].dirty` 的变化 |
| 命中结果与预期不符 | `widget_hit` 的坐标是**窗口内相对坐标**，不是屏幕绝对坐标 | 对照 `widgets[1]`（窗口内 176,52）与窗口 1 屏幕原点 (160,40)：绝对坐标 = 原点+相对 |
| 忘记 `widgets[i].visible` 条件 | 不可见控件仍被命中 | 逐条核对 `widget_hit` 五条件链 |
| 把 `iconinfo`/`icontest` 当本课命令 | 它们不在本课源码（`grep` 为 0） | 图标/双击模型属 Lesson 67；本课用 `windowtest` 验证对象模型 |
| 误以为窗口会画到屏幕上 | `windowtest` 是纯模型命令，不调用 `framebuffer_*` 绘制 | 观察 VGA 文本层输出；真实显存提交从 Lesson 65 的 compositor 开始 |
| `make check` 报错 | README 缺 `桌面对象模型与事件分发`/`gui`/`Lesson 64` 之一 | 对照 Makefile `check` 三条 grep |
| 分不清「模型通过」与「GUI 通过」 | 模型测试只是状态转换 | **VGA 文本仍是权威诊断通道**：先看 `windowtest:` 文本标记，再做视觉/物理验收（playbook §5/§9/§10） |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `struct window_model`（x/y/w/h/z/visible/focused/dirty） | X11/DRM 的 window 对象：`drivers/gpu/drm/drm_atomic_state.c` 的 plane/crtc 状态与 focus window 概念 | 教学模型把窗口压成一张静态表；Linux 有完整状态树与提交原子化 |
| `struct widget_model`（window/type/visible/focused + 相对矩形） | GTK/Qt 的 widget 树 / X11 的 `XQueryPointer` 命中的 window 子树 | 教学模型固定两层数组线性查找，无父子嵌套树 |
| `widget_hit` 线性命中 | `drivers/input/` 之上用户态的 hit-test 与 X11 的 `XTranslateCoordinates` | 教学模型 O(n) 全扫；真实系统按 z 序树剪枝 |
| `focused_window` + 窗口 `focused` 标志 | `drivers/gpu/drm` 的 active window / 输入焦点的 `focus` 归属 | 教学模型把「全局焦点指针」与「窗口自述焦点」分开，正是当前夹具暴露一致性问题之处 |
| `dispatched_events`/`dirty` 标记 | DRM 的 `dirty_fb` 与 input 的 `input_pass_values` | 教学模型用计数器与标志模拟事件到达与重绘请求 |
| 固定容量表（4 窗口/8 控件） | 内核侧无此硬编码（对象动态创建） | 教学模型用 `WINDOW_CAP`/`WIDGET_CAP` 实现「有界」，对应 playbook §10 的容量清单 |

**权威来源**：Linux `drivers/input/` 与 `drivers/gpu/drm/` 作为工程对照；
本课是纯数据模型，无新硬件操作，不涉及 Intel SDM 新内容。

**教学模型简化了什么**：无窗口嵌套树（只有两层）；命中不区分 z 序（第一个
命中即返回）；无拖拽/缩放/关闭；无真实绘制与输入接线（纯模型命令）；图标与
双击状态机未在本课实现（属 Lesson 67）；`focused_window` 与 `focused` 标志的
一致性靠夹具维护，当前 stable 夹具并不一致。

---

## 8. 思考题与练习

1. **概念理解**：`widget_hit` 为什么用 `x-x0<w` 而不是 `x<x0+w`？如果把
   `widgets[1].x` 改成 `0xffffff00`（接近 u32 上界），两种写法分别会怎样？
2. **源码定位**：在 `windowtest` 中逐个指出 `focused_window`、`hit`、
   `windows[1].focused`、`dispatched_events` 四个变量的取值，并解释为什么当前
   夹具下分发分支不触发。
3. **动手实验**：把 `focused_window=1` 改成 `focused_window=0`（`windows[0].focused`
   是 1），重建运行 `windowtest`，观察输出变为成功串，然后改回（勿提交）。
4. **Linux 对照**：为什么真实窗口系统命中时要按 z-order 从顶层往下找，而本课
   `widget_hit` 只返回第一个命中？列出至少两种需要 z 序的命中场景。
5. **设计思考**：`dispatched_events` 目前只是计数器；如果 Lesson 65 要让
   compositor 根据窗口 `dirty` 标志增量重绘，`dirty` 还需要升级成什么信息
   （区域？场景快照？）？

---

## 9. 本课小结与下一课预告

**小结**：本课在纯数据层建立了桌面对象模型：`window_model`（矩形+z+可见/
焦点/脏）与 `widget_model`（所属窗口+类型+相对矩形）各以固定数组承载；
`widget_hit` 用五条件链做有界命中测试；`windowtest` 用 2 窗口 + 2 控件的夹具
演示「命中→焦点关→分发→标脏」的状态机。**关键事实**：当前 stable 夹具中
`focused_window=1` 指向的窗口 `focused` 标志是 0，导致分发分支不触发、
`dispatched_events==0`，`windowtest` 实际输出 fallback 串——成功串需要先修正
夹具；本课也没有 `iconinfo`/`icontest`（图标与有界双击在 Lesson 67）。本课
不画任何像素，是 65 课 compositor 的模型前置。

**下一课预告**：[Lesson 65](../lesson-65-stable/README.md) 把场景合成出来：
scene/compositor、backbuffer、panel/taskbar 与光标提交，命令 `desktest`——
本课的窗口/控件模型将被画成可见的桌面。跨课程排错统一参考
[`docs/gui-debugging-playbook.md`](../../docs/gui-debugging-playbook.md)。
