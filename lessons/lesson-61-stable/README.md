# Lesson 61: 可靠 framebuffer handoff 与图形输出前置 — 精讲文档

> **课号**：Lesson 61（可执行课）
> **主题**：可靠 framebuffer handoff 与图形输出前置；命令 `guiinfo`/`drawtest`
> **课程主线位置**：第 4 阶段「图形桌面主线」（61–67）开篇课。前课完成进程组与
> session 模型（60）；本课建立 GUI 硬件前置——**framebuffer 来源不可靠，后面一切
> 绘制都是无源之水**。GUI 主线在 Lesson 67 结课，68 起回到进程组主线。
> **前置课程**：[`lesson-60-stable/README.md`](../lesson-60-stable/README.md)
> **后续课程**：[`lesson-62-stable/README.md`](../lesson-62-stable/README.md)
> **一句话目标**：GRUB 交出 LFB 的 Multiboot2 tag 长什么样、32 位阶段为何必须
> 严格校验（边界/格式/溢出）、为何要把显存物理页同时映射进低地址与高半区页表、
> 物理页如何对 PMM 保持保留、handoff 无效时为何必须安全回退到 VGA 而非猜显存
> 地址。

---

## 1. 课程定位（Mission）

**一句话目标**：获得一个**来源经过证明、映射完整、绘制安全**的图形输出通道。
`guiinfo` 显示 `ready/mapped: 1/1`、`drawtest` 输出
`bounded framebuffer clear/rectangles passed`——两条证据同时成立，才是
Lesson 62–66 开工的前提。

- **在课程主线中的位置**：第 4 阶段七连课（61–67）的起点：framebuffer（61）→
  backbuffer/字体/canvas（62）→ 键盘/鼠标（63）→ 桌面对象模型（64）→
  compositor/桌面（65）→ 图形 Terminal（66）→ 综合验收（67）。本课只做
  「显示」的前置。
- **责任边界**：本课**不负责**字体、输入、窗口、compositor 或图形 Shell；
  `ready/mapped: 1/1` 是后续 GUI 的输入条件，不是装饰性状态。
- **前置知识清单**：① `long_mode_handoff` 与低/高半区双重页表
  （`pt[]`/`high_pt[]`）、`pmm_init`/`pmm_reserved`；② Multiboot2 交接 ABI：
  MBI tag 的 `type/size` 头、8 字节对齐、`MB2_TAG_END` 终止；③ x86-64 4KB
  分页：`slot=va/(512*4096)`、`off=(va/4096)%512`、`PTE_PRESENT_WRITABLE`；
  ④ `setup_long_mode_tables` 的页表构建流程。
- **本课交付**：`guiinfo`/`drawtest` 两命令；`struct mb2_framebuffer_tag`（32
  位）与 `struct framebuffer_model`（64 位）；低/高半区双重映射与 VGA 回退；
  图形窗口的深蓝全屏 + 亮蓝内框矩形。

---

## 2. 核心概念精讲

### 2.1 概念一：Multiboot2 framebuffer handoff —— bootloader 把显存交给内核

定义：GRUB 按 grub.cfg 的 `gfxmode`/`gfxpayload` 请求，在装载内核前切好图形
模式，把**线性帧缓冲（LFB）**的物理地址与几何信息放进 MBI 的 type=8 tag。

为什么需要：此前 TinyOS 只写 VGA 文本缓冲 `0xb8000`（80×25）。写像素必须知道
「物理往哪个地址写、每行多少字节（pitch）、每像素几个字节、RGB 怎么排」——
这些信息只能由 bootloader 提供；multiboot 规范下内核对 display 不可见。

工作机制：① grub.cfg 请求 `800x600x32`（并 `insmod all_video`）；② header 带
「info request」与「framebuffer request」（type 5）两个 tag；③ 内核遍历 MBI
tag 链找到 type=8 的 `mb2_framebuffer_tag`。

### 2.2 概念二：为什么必须严格校验 tag —— 边界、格式、溢出

定义：拿到 tag 后逐字段验证：`bpp==32`、`type_field==1`、物理地址 4KB 对齐、
`pitch>=width*4`、`height>0`、`pitch*height<=512 页`、`address+bytes` 无符号
不回绕。MBI 是 GRUB 构造的**外部数据**；更关键的是后续要把 `bytes=pitch*height`
个物理页映射进页表，`bytes` 溢出或地址回绕会把随机物理页标成 framebuffer，最终
写出内核内存。`docs/gui-debugging-playbook.md` §2 把「tag 未做完整边界、格式和
溢出校验」列为黑屏/`ready/mapped=0/0` 的根因之一。

### 2.3 概念三：低地址与高半区双重映射 —— 一个显存两套页表

定义：64 位内核同时维持低地址恒等映射（`pt[]`）与高半区映射（`high_pt[]`）。
framebuffer 每个物理页要在**两套页表里各放一个 PTE**，虚拟地址统一取
`FRAMEBUFFER_VA=0x20000000`（512MB）。32 位阶段经低地址访问，64 位阶段迁移到
高半区（`phys_to_high`），两套映射都必须完整。因此 `IDENTITY_MAP_END` 从 16MB
扩到 `0x40000000`（1GB），`PAGE_TABLES_PER_ALIAS` 改为按 `IDENTITY_MAP_END`
推导；`guiinfo` 的 `mapped` 位正是 `framebuffer_map < IDENTITY_MAP_END`。

### 2.4 概念四：PMM 保留 —— framebuffer 物理页永不进分配器

定义：`pmm_init` 只在 `PMM_MAX_PHYS=0x01000000`（16MB）以内管理物理帧。QEMU/
实机 framebuffer 物理地址（如 `-vga std` 的 `0xfd000000`）远超 16MB，**天然不在
PMM bitmap 里、永不被 `pmm_alloc` 分走**——PMM 保留的实现是「把分配器管辖上限
钉在 16MB」而非逐帧标记；否则 `palloc`/`vmap` 会复用正在被显卡扫描输出的内存。
kernel.c 保留 `QEMU_STD_FB*` 常量但**本课不使用它们猜地址**——真正的来源永远
是 GRUB 的 tag（playbook §2 禁止事项）。

### 2.5 概念五：VGA fallback —— 安全回退而不是写未知内存

定义：32 位校验不通过 → `framebuffer_ready=0`；64 位 `framebuffer_init` 二次
校验任一失败 → `struct framebuffer_model` 清零（`ready=0`）。此后 `guiinfo` 显示
`ready/mapped: 0/0`，`drawtest` 输出 `framebuffer unavailable; safe fallback
reported`，**不写任何像素**——VGA 文本层（`0xb8000` 的 `tinyos>` shell）仍正常，
作为权威诊断通道。显存地址写错轻则花屏、重则 #PF/triple fault；回退的代价只是
「没有图形」，但保住可诊断的文本内核。`qemu-vga-check.sh` 检测 `fallback
reported` 并判失败。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-60） |
|---|---|---|
| `boot.S` | Multiboot2 header + 长模式引导 | info request 增加 framebuffer tag；新增 graphics request tag（800×600×32）；`MB2_BOOT_TAG_FRAMEBUFFER`/`MB2_TAG_FRAMEBUFFER` 常量 |
| `kernel.c` | 32 位阶段：解析 MBI、建页表 | `mb2_framebuffer_tag` 结构、tag 解析与 `MB2_TAG_END` 严格校验、handoff 增 framebuffer 字段、低/高页表双重映射、`IDENTITY_MAP_END` 扩到 1GB |
| `kernel64.c` | 64 位内核主体（累积文件） | `struct framebuffer_model` + `framebuffer_init/pixel/rect` + `guiinfo`/`drawtest` + 主循环调用 `framebuffer_init`；其余大量代码继承未变 |
| `kernel64.ld` / `linker.ld` | 64 位/外层布局 | 未变化 |
| `Makefile` | 构建 | `run` 加 `-vga std`；`check` grep 改为本课主题/`gui`/`Lesson 61` |
| `grub.cfg` | GRUB 菜单 | `insmod all_video`、`gfxmode=800x600x32`、`gfxpayload=800x600x32`；menuentry 更名 |

### 3.2 boot.S —— 请求图形模式

```asm
    .set MB2_HEADER_TAG_GRAPHICS, 5
	.set MB2_BOOT_TAG_FRAMEBUFFER, 8
	.set MB2_TAG_FRAMEBUFFER, 5
    .long 16
    .long MB2_BOOT_TAG_MMAP
    .long MB2_BOOT_TAG_FRAMEBUFFER
    .short MB2_HEADER_TAG_GRAPHICS
    .short 0
    .long 20
    .long 800
    .long 600
    .long 32
```

- 常量含义：5 = framebuffer request（header 侧），8 = framebuffer（boot info
  侧，内核要读的）；info request tag：`type=1`、`size=16`，payload 是
  `{mmap, framebuffer}` 两个 tag 类型（上一课只有 mmap、`size=12`）；
- framebuffer request tag：`type=5`、`size=20`，payload 为宽 800、高 600、深度
  32，与 grub.cfg `gfxmode` 对应；
- GRUB 无法满足时忽略并走文本模式，MBI 无 type=8 tag，恰好触发本课 VGA fallback
  路径（规范允许的失败路径，不是 bug）。

### 3.3 kernel.c —— 32 位阶段的 tag 校验与页表映射

常量与结构：

```c
#define MB2_TAG_FRAMEBUFFER 8
#define FRAMEBUFFER_VA 0x20000000ULL
#define IDENTITY_MAP_END 0x40000000ULL   /* 上一课是 0x01000000 */
#define PAGE_TABLES_PER_ALIAS (IDENTITY_MAP_END/(PAGE_ENTRIES*PAGE_SIZE))
struct mb2_framebuffer_tag { u32 type,size; u64 address; u32 pitch,width,height;
    u8 bpp,type_field; u16 reserved; } __attribute__((packed));
```

- `FRAMEBUFFER_VA=0x20000000`：显存虚拟映射地址；`IDENTITY_MAP_END` 从 16MB
  扩到 1GB，`PAGE_TABLES_PER_ALIAS` 自动从 8 变 512；`mb2_framebuffer_tag`
  逐字段对应规范 type=8 tag，`packed` 保证与 GRUB 写入的字节布局一致；
- `long_mode_handoff` 新增 8 个 framebuffer 字段，把校验结果原样交给 64 位阶段。

`prepare_memory_map` 的 tag 解析（源码逐字，摘关键行）：

```c
if(tag->type==MB2_TAG_FRAMEBUFFER && !framebuffer_tag) {
    const struct mb2_framebuffer_tag *fb=(const struct mb2_framebuffer_tag *)tag;
    if(tag->size>=32 && fb->address && fb->pitch && fb->width && fb->height)
        framebuffer_tag=fb; }
```

只取第一个 framebuffer tag；初筛要求 `size>=32` 且地址/pitch/宽高非零——**真
校验**放在 `MB2_TAG_END` 处统一做，那里才知道整个 MBI 已遍历完整。
`MB2_TAG_END` 的完整校验与落账（源码逐字）：

```c
if(tag->type==MB2_TAG_END) { u64 bytes; if(tag->size!=8 || !memory_map) return 0;
    if(framebuffer_tag && framebuffer_tag->bpp==32 && framebuffer_tag->type_field==1 &&
       !(framebuffer_tag->address&(PAGE_SIZE-1)) &&
       framebuffer_tag->pitch>=framebuffer_tag->width*4U && framebuffer_tag->height &&
       (bytes=(u64)framebuffer_tag->pitch*framebuffer_tag->height)<=PAGE_ENTRIES*PAGE_SIZE &&
       framebuffer_tag->address+bytes>=framebuffer_tag->address){
        long_mode_handoff.framebuffer_address=framebuffer_tag->address;
        long_mode_handoff.framebuffer_pitch=framebuffer_tag->pitch;
        long_mode_handoff.framebuffer_width=framebuffer_tag->width;
        long_mode_handoff.framebuffer_height=framebuffer_tag->height;
        long_mode_handoff.framebuffer_bpp=framebuffer_tag->bpp;
        long_mode_handoff.framebuffer_type=framebuffer_tag->type_field;
        long_mode_handoff.framebuffer_bytes=(u32)bytes;
        long_mode_handoff.framebuffer_map=FRAMEBUFFER_VA;
        framebuffer_ready=1; } else { framebuffer_ready=0; }
    memory_map_ready=1; return 1; }
```

六个校验条件：① `bpp==32`（只支持 32bpp）；② `type_field==1`（RGB 直接色）；
③ 地址 4KB 对齐（整页可映射）；④ `pitch>=width*4`（每行字节至少装下 `width`
个像素，防越行）；⑤ `bytes<=512 页`（2MB，低地址页表够放、`u32` 收得下）；
⑥ `address+bytes>=address`（无符号回绕检测——回绕意味着末尾地址小于开头，映射
循环会写出错误物理页）。全部通过才落账并置 `framebuffer_ready=1`；任一失败则
`framebuffer_ready=0`，手动标记 VGA fallback。

`setup_long_mode_tables` 的双重映射（源码逐字）：

```c
if(framebuffer_ready){
    u32 first=(u32)(long_mode_handoff.framebuffer_map/(PAGE_ENTRIES*PAGE_SIZE)),
         entry=(u32)((long_mode_handoff.framebuffer_map/PAGE_SIZE)%PAGE_ENTRIES),
         pages=(long_mode_handoff.framebuffer_bytes+PAGE_SIZE-1)/PAGE_SIZE,k;
    for(k=0;k<pages;k++){
        u32 slot=first+(entry+k)/PAGE_ENTRIES; u32 off=(entry+k)%PAGE_ENTRIES;
        if(slot>=PAGE_TABLES_PER_ALIAS){framebuffer_ready=0;break;}
        ((volatile u64 *)(unsigned long)(u32)long_mode_handoff.pt[slot])[off]=
            (long_mode_handoff.framebuffer_address+(u64)k*PAGE_SIZE)|PTE_PRESENT_WRITABLE;
        ((volatile u64 *)(unsigned long)(u32)long_mode_handoff.high_pt[slot])[off]=
            (long_mode_handoff.framebuffer_address+(u64)k*PAGE_SIZE)|PTE_PRESENT_WRITABLE; }
    long_mode_handoff.framebuffer_map=FRAMEBUFFER_VA; }
```

- 由 `FRAMEBUFFER_VA` 反推首 PTE 的 `first`（页表下标）与 `entry`（表内下标），
  逐物理页 `k` 步进，`slot`/`off` 处理跨页表进位；每个物理页**同时**写进低地址
  `pt[slot]` 与高半区 `high_pt[slot]`，PTE 都是 `物理页|PTE_PRESENT_WRITABLE`；
- `slot>=PAGE_TABLES_PER_ALIAS` 时放弃映射并把 `framebuffer_ready` 清零（回到
  fallback），绝不越界写页表数组。

### 3.4 kernel64.c —— framebuffer 模型与绘制命令

模型与初始化（源码逐字）：

```c
struct framebuffer_model { u64 address,bytes,pixels,rects; u32 pitch,width,height;
    u8 bpp,type,ready,mapped; };
static struct framebuffer_model framebuffer;
static TEXT64 void framebuffer_init(struct long_mode_handoff*h){
    u64 bytes;
    if(!h->framebuffer_address||h->framebuffer_bpp!=32||h->framebuffer_type!=1||
       h->framebuffer_width>1024||h->framebuffer_height>768){
        framebuffer=(struct framebuffer_model){0};return; }
    bytes=(u64)h->framebuffer_pitch*h->framebuffer_height;
    if(bytes>8ULL*1024*1024||h->framebuffer_address+bytes<h->framebuffer_address){
        framebuffer=(struct framebuffer_model){0};return; }
    framebuffer=(struct framebuffer_model){h->framebuffer_map,bytes,0,0,
        h->framebuffer_pitch,h->framebuffer_width,h->framebuffer_height,
        h->framebuffer_bpp,h->framebuffer_type,1,h->framebuffer_map<IDENTITY_MAP_END}; }
```

- 十个字段：address 是**虚拟映射地址**（非物理地址）、bytes=`pitch*height`、
  pixels/rects 是绘制计数器、ready/mapped 双状态位；
- 64 位阶段**二次校验**（地址非零、32bpp、type 1、宽高≤1024×768、`bytes≤8MB`、
  地址不回绕）——与 32 位阶段构成 handoff 双保险；
- `mapped = framebuffer_map < IDENTITY_MAP_END`：验证低地址映射覆盖显存 VA。

像素与矩形（源码逐字）：

```c
static TEXT64 int framebuffer_pixel(u32 x,u32 y,u32 color){
    volatile u32*p;
    if(!framebuffer.ready||!framebuffer.mapped||x>=framebuffer.width||y>=framebuffer.height)
        return 0;
    p=(volatile u32 *)(unsigned long)(framebuffer.address+(u64)y*framebuffer.pitch+(u64)x*4);
    *p=color; framebuffer.pixels++; return 1; }
static TEXT64 int framebuffer_rect(u32 x,u32 y,u32 w,u32 h,u32 color){
    u32 i,j; u64 endx=(u64)x+w,endy=(u64)y+h;
    if(!framebuffer.ready||!framebuffer.mapped||!w||!h||x>=framebuffer.width||y>=framebuffer.height)
        return 0;
    if(endx>framebuffer.width)endx=framebuffer.width;
    if(endy>framebuffer.height)endy=framebuffer.height;
    for(j=y;j<endy;j++)for(i=x;i<endx;i++)framebuffer_pixel(i,j,color);
    framebuffer.rects++; return 1; }
```

- `framebuffer_pixel` 用 `y*pitch+x*4` 算 32bpp 偏移——**pitch 与 width 是两
  回事**（playbook §3「不要按 width*height 线性复制」的起点）；`volatile` 防写
  屏被优化掉；越界返回 0 而非写坏内存；
- `framebuffer_rect` 用 `u64` 算 `endx` 防溢出、裁剪到屏幕边界、逐像素填充后
  `rects++`；`w||h` 为零直接拒绝；
- pixels/rects 计数器兼作「确实发生绘制」的证明。

`guiinfo` 与 `drawtest`（源码逐字）：

```c
static TEXT64 void guiinfo(u16*c){
    text64(c,"guiinfo: framebuffer addr/pitch/size/bpp/type: ");
    hex64(c,framebuffer.address);text64(c,"/");
    hex64(c,framebuffer.pitch);text64(c,"/");
    hex64(c,framebuffer.width);text64(c,"x");
    hex64(c,framebuffer.height);text64(c,"/");
    hex64(c,framebuffer.bpp);text64(c,"/");
    hex64(c,framebuffer.type);
    text64(c," ready/mapped: ");
    hex64(c,framebuffer.ready);text64(c,"/");hex64(c,framebuffer.mapped);
    putc64(c,'\n'); }
static TEXT64 void drawtest(u16*c){
    int a=framebuffer_rect(0,0,framebuffer.width,framebuffer.height,0x00102040U),
        b=framebuffer_rect(8,8,framebuffer.width>16?framebuffer.width-16:0,
            framebuffer.height>16?framebuffer.height-16:0,0x002060a0U);
    text64(c,"drawtest: ");
    text64(c,a&&b&&framebuffer.pixels?"bounded framebuffer clear/rectangles passed":
        "framebuffer unavailable; safe fallback reported");
    putc64(c,'\n'); }
```

- `guiinfo` 逐字打印模型七项；`hex64` 固定 16 位十六进制，`ready/mapped:
  0000000000000001/0000000000000001` 正是 `qemu-vga-check.sh` 的硬校验串；
- `drawtest` 全屏深蓝 `0x00102040U`（R=0x10,G=0x20,B=0x40）+ (8,8) 亮蓝内框
  `0x002060a0U`（R=0x20,G=0x60,B=0xa0）；`a&&b&&framebuffer.pixels` 三条件 AND
  两矩形都成功**且确实写了像素**才输出 `passed`，否则走 fallback 串——成功与
  回退是同一函数的两条路径。

`exec64` 新分支（源码逐字）：

```c
else if(eq64(word,"guiinfo")){if(!noargs64(arg))usage64(c,"guiinfo");else guiinfo(c);}
else if(eq64(word,"drawtest")){if(!noargs64(arg))usage64(c,"drawtest");else drawtest(c);}
```

插入在 `jobtest` 与 `resourceinfo` 之间。可见细节：`help` 输出串**没有**追加这
两个命令（源码事实），GUI 命令靠 README/`about` 发现——后续课修正的小瑕疵。

主循环（源码逐字，摘关键段）：`threads[0].id=0; ... quantum_left=TIME_SLICE_TICKS;
framebuffer_init(h); ...`——`framebuffer_init(h)` 在 `clear64` 之前调用，任何
失败都只是 `ready=0`，不中断启动；横幅与 `about` 串（源码逐字）见 §5.3。

### 3.5 构建管线（Makefile / linker）

- `kernel64.o`：`-m64 -ffreestanding -fpie -fno-stack-protector -mno-red-zone
  -mno-sse -mno-sse2 -mno-mmx`；`kernel64.bin`：`ld -m elf_x86_64 -T kernel64.ld`
  后 `objcopy -O binary`，由 boot.S `incbin` 进外层 ELF；`boot.o`/`kernel.o` 用
  `-m32`；`kernel.iso` 由 `grub-mkrescue` 打包 `grub.cfg`+`kernel.elf`；
- `check`：`grub-file --is-x86-multiboot2` + 三条 grep（README 含
  `可靠 framebuffer handoff 与图形输出前置`、`gui`、`Lesson 61`），通过打印
  `Multiboot2 and Lesson 61 checks passed.`；
- `run`（本课增量）：`qemu-system-x86_64 -accel tcg -vga std -boot order=d
  -cdrom build/kernel.iso -serial stdio -no-reboot -no-shutdown`——**`-vga std`
  提供 framebuffer**，缺它 GRUB 无图形模式可切，直接触发 fallback；
- `kernel64.ld`（链接地址 `0xffffffff80102000`，保留 `.text64.entry` 与三套栈
  段 ASSERT）与 `linker.ld`（外层 1MB 布局，`KEEP()` 保留 `.multiboot`）本课
  未变。

### 3.6 主控制流

```text
GRUB(grub.cfg: gfxmode/gfxpayload=800x600x32)
  → Multiboot2 header(info request: mmap+framebuffer; graphics request: 800x600x32)
  → _start(kernel.c kernel_main32)
      ├─ prepare_memory_map(): 遍历 MBI tag 链
      │     ├─ type=8 tag → framebuffer_tag 初筛
      │     └─ type=0 时: 六重校验 → handoff 落账 / VGA fallback
      ├─ setup_long_mode_tables(): 逐物理页写 pt[slot][off] 与 high_pt[slot][off]
      └─ → 长模式 → kernel_main64 → kernel64.bin
            ├─ framebuffer_init(h): 二次校验 → framebuffer_model{ready,mapped}
            ├─ 横幅 "Lesson 61: Multiboot2 framebuffer 与像素绘制\n..."（见 §5.3）
            └─ 键盘循环 → exec64: guiinfo / drawtest → passed / fallback reported
```

---

## 4. 数据流与运行逻辑

```text
输入 "guiinfo" → guiinfo(c) → "guiinfo: framebuffer addr/pitch/size/bpp/type: "
    + hex(VA=0x20000000) + "/" + hex(pitch) + "/" + hex(width) + "x" + hex(height)
    + "/" + hex(bpp=32) + "/" + hex(type=1) + " ready/mapped: " + hex(ready) + "/" + hex(mapped)
输入 "drawtest" → rect(0,0,800,600,0x00102040) 全屏深蓝 + rect(8,8,784,584,0x002060a0)
    内框亮蓝 → 两矩形均返回 1 且 pixels>0
    → "drawtest: bounded framebuffer clear/rectangles passed"
（任一校验失败："drawtest: framebuffer unavailable; safe fallback reported"）
```

`make run`（`-vga std`）下 `guiinfo` 完整输出示例见 §5.3；解读：addr 显示
`0x20000000`（**虚拟映射地址**，不是物理地址）、pitch=`0xc80=3200`、800×600、
bpp=32、type=1（RGB）、ready/mapped 均为 1——`ready/mapped: ...1/1` 与 `passed`
是硬性成功判据，任何校验失败都只会走到 fallback 串而非写坏内存。
---

## 5. 构建、运行与验证

### 5.1 依赖

`gcc`、`binutils`、`grub-pc-bin`/`grub-common`、`xorriso`、`mtools`、
`qemu-system-x86_64`；GUI 专项验收还需 `socat`、`python3`。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 输出：`Multiboot2 and Lesson 61 checks passed.`（README 必须含
`可靠 framebuffer handoff 与图形输出前置`、`gui`、`Lesson 61`，缺一即失败）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：
`Lesson 61: Multiboot2 framebuffer 与像素绘制\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n`

验证步骤（输出串从源码逐字）：

```bash
guiinfo
```

预期（`-vga std` 下，数值随 handoff 变化，`ready/mapped` 必须为 1/1）：

```text
guiinfo: framebuffer addr/pitch/size/bpp/type: 0000000020000000/0000000000000c80/0000000000000320x0000000000000258/0000000000000020/0000000000000001 ready/mapped: 0000000000000001/0000000000000001
```

```bash
drawtest
```

预期：`drawtest: bounded framebuffer clear/rectangles passed`，且 QEMU 图形窗口
出现「深蓝全屏 + 亮蓝内框」矩形图案。

继承回归：lesson-60 及之前的 `sessioninfo`/`jobtest`/`forkexecwaittest`/
`taskvalidate`/`processinfo`/`mmap` 等命令行为不变。

### 5.4 GUI 专项验收（QEMU VGA 自动化）

按教程要求 GUI 课程走专项流程（`docs/gui-debugging-playbook.md` §8 与
`learning-guide.md` §10.2）：单课验收
`scripts/qemu-vga-check.sh lessons/lesson-61-stable guiinfo drawtest`；
第 4 阶段结课验收统一
`scripts/qemu-vga-check.sh lessons/lesson-67-stable guiinfo drawtest fonttest canvastest inputtest windowtest desktest shellgui`。

脚本用 `-M q35 -vga none -device bochs-display,xres=800,yres=600,vgamem=16M`
（不同于 `make run` 的 `-vga std`）：`guiinfo` 的 PPM 截图必须有效且非全黑，
VGA 文本 dump 必须含 `ready/mapped: 0000000000000001/0000000000000001`（或
`1/1`）；`drawtest` 要求多色截图与 `passed` 证据；任何命令出现 `fallback
reported` 即失败；全程无 `check_exception`/`triple fault`；最终打印
`QEMU VGA validation passed: <dir>`。

### 5.5 课程实测记录（稳定快照）

旧 README 的学习路径（`make -C lessons/lesson-61-learning` +
`make -C lessons/lesson-61-learning check`）已验证；stable 快照复验：
`make check` 输出 `Multiboot2 and Lesson 61 checks passed.`，`guiinfo` 显示
`ready/mapped: 1/1`、`drawtest` 输出 `passed`，图形窗口可见双矩形图案。构建产物
未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 黑屏/仅 VGA 文本，`guiinfo` 显示 `ready/mapped: 0/0` | handoff 校验未过：`gfxmode/gfxpayload` 缺失、tag 格式不符、地址回绕（playbook §2） | 先 `guiinfo`；检查 grub.cfg 的 `insmod all_video`/`gfxmode`/`gfxpayload`；对照 `MB2_TAG_END` 六重校验逐条核对 |
| `drawtest` 输出 `framebuffer unavailable; safe fallback reported` | 64 位 `framebuffer_init` 二次校验失败（bpp≠32、type≠1、宽高>1024×768、bytes>8MB、地址回绕） | 在 `framebuffer_init` 逐条件检查 `h->framebuffer_*`；确认落账发生在 `MB2_TAG_END` |
| 绘制时 #PF/triple fault | 显存页表映射缺失（只映射低地址或只映射高半区）；`bytes` 溢出映射到错误物理页 | 核对双重映射循环：`pt[]` 与 `high_pt[]` 必须同时写；检查 `slot>=PAGE_TABLES_PER_ALIAS` 回退 |
| 画面重复条纹/错位 | pitch 用错或按 `width*height` 线性复制 | 对照 `framebuffer_pixel` 的 `y*pitch+x*4`；stride 纪律见 playbook §3 |
| `make check` 报错 | README 缺 `可靠 framebuffer handoff 与图形输出前置`/`gui`/`Lesson 61` 之一 | 对照 Makefile `check` 三条 grep |
| 猜 `0xfd000000` 当显存地址 | 教学红线：**绝不猜显存地址** | 显存地址必须来自 GRUB tag；`QEMU_STD_FB*` 本课未用于寻址（playbook §2） |
| 分不清「内核活着」与「GUI 正常」 | GUI 只是输出通道 | **VGA 文本仍是权威诊断通道**：先看串口/VGA 文本标记，再判断图形（playbook §10） |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `mb2_framebuffer_tag` 的 address/pitch/width/height/bpp/type | `drivers/firmware/sysfb.c` + `drivers/video/fbdev/simplefb.c`：从 DT/EFI 的 `simple-framebuffer` 读地址/尺寸/pitch/格式 | 教学模型从 Multiboot2 tag 取，Linux 从 DT/EFI 取；格式语义相同 |
| `framebuffer.pitch` | `include/linux/fb.h` 的 `struct fb_fix_screeninfo.line_length` | 两者都把 pitch 与 width 分开存，按 `y*pitch+x*bpp/8` 访问 |
| `FRAMEBUFFER_VA` 双重页表映射 | `simplefb` 用 `devm_ioremap_wc()` 建映射 | Linux 用 ioremap 动态映射；教学模型启动时手工放 PTE |
| PMM 保留（`PMM_MAX_PHYS=16MB`，显存整体留在分配器外） | `memblock_reserve()`/`reserve_region()` 显式保留 framebuffer | 教学模型靠分配器上限定界，效果等价 |
| VGA fallback（`ready/mapped=0/0`） | `screen_info` 回落路径（`efifb`→`vesafb`→`vga16fb`） | 教学模型只回退文本层，无多级 fb 驱动链 |
**权威来源**：Multiboot2 规范（type=8 framebuffer tag 字段布局、type=5
framebuffer request 语义）；GNU GRUB（`gfxmode`/`gfxpayload`）；Intel SDM
（x86-64 分页 PTE 结构）。

**教学模型简化了什么**：不探测真实显卡（依赖 GRUB 切模式）；只支持 32bpp RGB；
无 `ioremap` 分配器（固定 VA 常量）；无 backbuffer/同步/pan（62 课引入）；无
VGA/VBE IO 端口编程（`bochs_vbe_init` 属 Lesson 67）。

---

## 8. 思考题与练习

1. **概念理解**：`MB2_TAG_END` 六条件为何必须同时检查 `pitch>=width*4` 和
   `address+bytes>=address`？各防住哪类坏 tag？
2. **源码定位**：映射循环 `first`/`entry` 由 `FRAMEBUFFER_VA` 算出；若改成
   `KERNEL_VMA_BASE+FRAMEBUFFER_VA` 会怎样？`slot>=PAGE_TABLES_PER_ALIAS`
   分支为何要把 `framebuffer_ready` 清 0？
3. **动手实验**：把 grub.cfg `gfxmode` 改成 `1024x768x32`（宽超 1024 上限），
   重建运行 `guiinfo`/`drawtest`，观察 `framebuffer_init` 清空模型并回退，
   然后改回（勿提交）。
4. **Linux 对照**：读 `simplefb.c` 的 `simplefb_parse_dt`，对照本课
   `MB2_TAG_END` 校验清单，列出 Linux 中对应（或不存在的）检查。
5. **设计思考**：`drawtest` 用 `0x00102040U` 全屏填充；R/G/B 为何放 u32 低
   24 位？type_field 为 0（调色板）还能这样写吗？本课为何拒绝 type≠1？

---

## 9. 本课小结与下一课预告

**小结**：本课把「图形输出」的最前置问题解决干净——GRUB 经 Multiboot2 type=8
tag 交出 LFB，32 位阶段在 `MB2_TAG_END` 处做 bpp/type/对齐/pitch/高度/字节数/
地址回绕校验，通过才把物理地址、pitch、几何写进 `long_mode_handoff`；随后把每个
显存物理页同时映射进低地址与高半区页表，`FRAMEBUFFER_VA=0x20000000` 成为内核
画画的唯一窗口。PMM 把上限钉在 16MB，显存整体不被分配；任何一环失败都安全回退
到 VGA 文本层。`guiinfo` 的 `ready/mapped` 与 `drawtest` 的 `passed` 是后续所有
GUI 课的上车凭证；VGA 文本仍是权威诊断通道，`qemu-vga-check.sh` 把 `fallback
reported` 与全黑截图判为失败。

**下一课预告**：[Lesson 62](../lesson-62-stable/README.md) 在可靠 framebuffer
上加入像素格式转换、backbuffer（先画后台再按行提交，避免用户看到中间状态）、
5×7 bitmap font 与 canvas 画布，命令 `fonttest`/`canvastest`。跨课程排错统一
参考 [`docs/gui-debugging-playbook.md`](../../docs/gui-debugging-playbook.md)。
