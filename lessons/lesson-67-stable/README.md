# Lesson 67: 图形桌面综合验收 — 精讲文档

> **课号**：Lesson 67
> **主题**：图形桌面综合验收与 GUI 主线结课
> **课程主线位置**：第 4 阶段「图形桌面主线（61–67）」的收尾课
> **前置课程**：[Lesson 66（图形 Terminal 与安全命令 dispatcher）](../lesson-66-stable/README.md)
> **后续课程**：[Lesson 68（进程组与 session 元数据）](../lesson-68-stable/README.md)
> **本课一句话目标**：统一回归 framebuffer、绘图、输入、鼠标、icon、窗口、scene/compositor 与图形 Terminal 全链路，完成真实 QEMU 桌面验收，并为 GUI 主线画上句号。

> **Course status: stable snapshot (validated; verified build artifacts included).**

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能把 GUI 全链路（handoff → framebuffer → backbuffer/scene → 输入 → 桌面对象 → 图形 Shell）逐层串起来，用 `guiinfo`/`drawtest` 等确定性测试加真实 QEMU 画面完成结课验收，并说清哪些证据能证明 GUI、哪些只证明模型。
- **主线位置**：第 4 阶段（61–67）的最后一课。61 课解决 framebuffer handoff 与安全 fallback，62 课引入像素/backbuffer/scene/字体/canvas，63 课解决键盘、IRQ12 鼠标与事件队列，64 课解决 icon/window/widget 模型与 hit-test，65 课解决 compositor 与 cursor，66 课解决图形 Terminal。本课不再堆新功能，而是**跨层综合回归**：跑通所有子系统的确定性测试，再上真实 QEMU 桌面验收。Lesson 68 起回到进程组与 session 元数据主线，GUI 不再扩展。
- **前置知识清单**：① Multiboot2 framebuffer tag 解析与 `ready/mapped` 判定（61）；② backbuffer/scene 双缓冲、5×7 字体与 `framebuffer_present_rect()` 局部刷新（62/66）；③ 键盘 IRQ1 队列、PS/2 AUX 三字节包与事件队列（63）；④ window/widget hit-test、focus、z-order 与 icon 双击状态（64/65）；⑤ 图形 Terminal 安全白名单，禁止复用写 VGA 显存的 `exec64()`（66）。
- **本课交付（可见结果）**：
  - 确定性证据：`guiinfo` 显示 `ready/mapped: 1/1`，`drawtest`/`fonttest`/`canvastest`/`inputtest`/`mousetest`/`icontest`/`windowtest`/`desktest`/`shellgui` 全部输出 `passed`；
  - 真实证据：QEMU 图形窗口显示完整桌面（panel/taskbar/SHELL 图标/Terminal 窗口），鼠标能到 `(0,0)`，单击选中、双击打开 Terminal，输入即时回显、安全命令有输出；
  - 全链路回归：`help`、`about`、`guiinfo`、`mouseinfo`、`clear`、`shellrun` 以及 Lesson 60 回归命令继续可用。

---

## 2. 核心概念精讲

### 2.1 确定性模型测试 vs 真实 GUI 验收（本课最重要的概念）

- **定义**：`icontest`/`desktest`/`mousetest` 等命令通过把固定输入喂给固定状态机，只验证「代码路径和元数据正确」；真实 GUI 验收则在 QEMU 图形窗口里检查**像素级的可见结果**。
- **为什么需要（动机）**：教学内核不能靠截图跑 CI，所以用模型测试做回归保护；但模型测试无法证明「framebuffer 真的映射了、鼠标真的到达屏幕左上角、窗口真的没被截断」。两类证据互为补充、不可互相替代。
- **工作机制**：`icontest` 直接调用 `desktop_mouse_click()` 两次并断言状态标志；真实鼠标路径由 IRQ12 → `mouse_packet_byte()` → `desktop_mouse_click()` → `desktop_dirty=1` → 主循环重绘桌面。

### 2.2 backbuffer + scene 双缓冲与局部 present

- **定义**：`framebuffer_backbuffer[]` 是绘制目标，`framebuffer_scene[]` 是「无光标」场景快照，`framebuffer_present()`/`framebuffer_present_rect()` 才把 backbuffer 提交到真实显存。
- **为什么需要**：Lesson 66 之前整屏重绘会撕裂、闪烁且拖慢输入；光标每帧叠加在场景上，若不记录 scene 就无法干净恢复旧光标。
- **工作机制**：`framebuffer_pixel()` 在 `framebuffer.rendering==1 && backbuffer` 时写 backbuffer，否则直写显存；桌面绘制完先 `framebuffer_scene_copy()` 冻结场景，再画光标、`framebuffer_present()` 提交；光标移动只做 `xfce_cursor_update()`。
- **边界**：backbuffer 固定用 `FRAMEBUFFER_MAX_WIDTH(1024)*FRAMEBUFFER_MAX_HEIGHT(768)` 的 stride，与真实分辨率解耦；所有索引一律 `y*FRAMEBUFFER_MAX_WIDTH+x`，禁止按 `width*height` 线性复制。

### 2.3 RGB 颜色打包（framebuffer_pack）

- **定义**：TinyOS 内部统一用 `0x00RRGGBB`（R 在最高 8 位），`framebuffer_pack()` 按 framebuffer 的 RGB 位域（`red_pos/red_mask/green_pos/green_mask/blue_pos/blue_mask`）把它转成显存实际 32bpp 像素值。
- **为什么需要**：800x600x32 的 Bochs VBE 布局是 `0x00RRGGBB`，但不同硬件/GRUB 布局位域不同，直接写裸颜色值在别的 payload 上会花屏。
- **工作机制**：`r=(color>>16)&255` 取分量，`(r>>(8-mask))&((1<<mask)-1)` 截断到该通道位宽，再左移 `red_pos` 后 `|=` 到输出。mask 为 0 的通道直接跳过。
- **例子**：`0x00eef5ff`（浅蓝白）在 8-8-8 且 red_pos=16 时，R=0xee 移位到 bit16、G=0xf5 到 bit8、B=0xff 到 bit0，输出 `0x00eef5ff`。

### 2.4 PS/2 AUX 鼠标与 IRQ12

- **定义**：PS/2 鼠标是 AUX 通道，IRQ12 触发；三字节包为 `[标志, X位移, Y位移]`，第一字节 bit3 必须为 1（同步位）。
- **为什么需要**：图形桌面需要指针。Lesson 63 已有事件队列，本课把「真实硬件初始化 + IRQ12 中断解码 + 坐标裁剪」补齐，并把单击事件接入桌面图标。
- **工作机制**：`mouse_hw_init()` 使能 AUX 并设置 controller command byte（bit1 置 IRQ12 使能、清 bit5），`mouse_aux_command(0xf4)` 等 `0xfa` ACK；`irq12_record()` 从 0x64/0x60 读 AUX 字节交给 `mouse_packet_byte()`（校验同步位 → 拼满三字节 → 解符号位移（bit4=Y、bit5=X）→ 裁剪到 `[0,width-1]×[0,height-1]` → 按下左键调 `desktop_mouse_click()`）；`mouse_poll()` 在 `online==0` 时每 65536 次主循环重试一次初始化（轮询只做在线恢复，绝不与 IRQ12 同时取数据）。
- **边界**：overflow 位（bit6/bit7）置位时丢弃位移；`mouse.x/y` 必须允许到 `(0,0)`（旧 bug 是把左上角裁剪成 1）。

### 2.5 桌面图标与有界双击状态机

- **定义**：`struct desktop_icon_model { x,y,w,h,selected,target }` 描述 SHELL 图标；双击状态由 `shell_icon_pending`（第一次单击）与 `last_icon_click`（时间戳）表示。
- **为什么需要**：桌面需要一个可交互的启动入口，同时要演示「有界状态机」——不依赖真实定时器，用 tick 差值判定双击。
- **工作机制**：单击命中图标 → `selected=1, desktop_dirty++`；若已 pending 且 `when-last_icon_click<=60` tick → `shell_window_open=1, shell_open_count++, focused_window=0, windows[0].visible/focused=1, z=2`；否则置 `pending=1` 记录时间。`icontest` 用 tick=100/120 模拟两次点击验证 60 tick 窗口内的双击。
- **为什么 60**：60 个 PIT tick ≈ 0.6 秒（PIT 100Hz），是教学级的双击窗口。

### 2.6 Xfce 风格桌面合成器

- **定义**：`xfce_desktop()` 是单一重绘函数：背景 → 顶部 panel → 底部 taskbar → 图标 →（可选）Terminal 窗口 → 菜单文字 → scene 快照 → 光标 → present。
- **为什么需要**：把 Lesson 62–65 的绘图原语组装成「一屏可验收的桌面」，并为 `desktop_dirty`/`mouse_cursor_dirty` 提供统一的完整重绘入口。
- **工作机制**：每次重绘把 `windows[0]` 设为 620x390 的 Terminal 窗口（`visible=shell_window_open`），`xfce_window()` 画窗口阴影、标题栏和装饰按钮；`desktop_icon_draw()` 画图标框、窗口小图与「SHELL」文字。
- **边界**：`desktop_initialized` 保证图标/窗口初值只重置一次；`desktop_dirty` 触发整屏重绘，`mouse_cursor_dirty` 触发轻量 `xfce_cursor_update()`，粒度不同。

### 2.7 图形 Terminal 的安全白名单 dispatcher

- **定义**：`gui_term_command()` 是一个**字符串比较白名单**：匹配 `help`/`about`/`clear`/`shellrun`/`guiinfo`/`mouseinfo`/`iconinfo`/`desktopinfo`/`ramfsinfo`/`fdinfo`/`pipeinfo`/`signalinfo`/`clockinfo`/`timerinfo`/`schedinfo`/`tasklist`/`threadinfo`/`meminfo`/`vmainfo`/`vminfo`/`anoninfo`/`sessioninfo`/`resourceinfo`，否则输出 `unknown command`。
- **为什么需要 + 工作机制**：GUI 路径不执行会写 VGA 光标/显存的完整 `exec64()`，也不暴露 `pftest`/`udtest` 等停机命令；每个白名单命令输出一行固定文字（如 `about` → `TinyOS graphical terminal`、`guiinfo` → `framebuffer 800x600 backbuffer enabled`），输出进 `gui_term_lines[]`，由 `gui_term_draw()` 在固定坐标 `(92, 119+i*18)` 用 canvas 绘制。

---

## 3. 源码精讲（本课最长章节）

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 66） |
|---|---|---|
| `boot.S` | Multiboot2 header、32 位启动、进入 long mode | 微小变化：删除了 header 内联的 graphics tag（800x600x32 改由 `grub.cfg` 的 `gfxmode/gfxpayload` 提供） |
| `kernel.c` | 32 位阶段：解析 MBI、framebuffer tag、建页表 | 较大：`mb2_framebuffer_tag` 增加 RGB 位域字段；`prepare_memory_map()` 校验 `size>=32` 并提取 `red/green/blue_pos/mask`（`size>=38` 才读，否则用 8-8-8 缺省）；新增 `bochs_pci_init()` 通过 PCI 配置空间拿 Bochs LFB（绝不猜地址）；页表映射改用 `framebuffer_phys_base+map_offset` |
| `kernel64.c` | 64 位内核：GUI 全栈 + 文本 shell + 全部回归命令 | 最大增量：backbuffer/scene 双缓冲与 present 族、`bochs_vbe_init()`、`framebuffer_pack()` RGB 打包、完整 26 字母位图字体、鼠标模型（IRQ12 + PS/2 硬件初始化）、桌面图标与双击、Xfce 风格桌面、图形 Terminal 白名单、主循环事件泵、`exec64` 新增 `iconinfo/icontest/desktopinfo/mouseinfo/mousetest` |
| `kernel64.ld` | 64 位 continuation 链接脚本 | 未变化（同前：text64/rodata/data，含 idle/rsp0/ist1 栈守卫） |
| `linker.ld` | 外层 32 位 ELF 段布局 | 未变化 |
| `Makefile` | 构建/检查/运行 | 微小变化：`check` grep 改为 `'图形桌面综合验收'`、`'gui'`、`'Lesson 67'`；`run` 去掉 `-vga std`（默认 std VGA，payload 由 grub.cfg 设置） |
| `grub.cfg` | GRUB menuentry | 变化：新增 `set gfxmode=800x600x32`、`set gfxpayload=800x600x32`、`insmod all_video` |

### 3.2 kernel64.c 精讲

#### 3.2.1 交接结构扩展与 framebuffer 基础设施

`struct long_mode_handoff` 是 32 位阶段传给 64 位阶段的唯一交接块，本课给它追加了 framebuffer 位域信息：

```c
u32 user_image_status,user_image_bytes,user_entry_offset,user_entry_length;
u64 framebuffer_address,framebuffer_map,framebuffer_phys_base;
u32 framebuffer_map_offset,framebuffer_pitch,framebuffer_width,framebuffer_height,framebuffer_bytes;
u8 framebuffer_bpp,framebuffer_type;
u8 framebuffer_red_pos,framebuffer_red_mask,framebuffer_green_pos,framebuffer_green_mask,framebuffer_blue_pos,framebuffer_blue_mask;
```

- `framebuffer_phys_base`：LFB 向下按页对齐后的物理基址，供 PMM 保留和页表映射；
- `framebuffer_map_offset`：LFB 起始地址相对页基址的偏移（LFB 通常页对齐为 0）；
- `framebuffer_red_pos` 等 6 个字段：真实 RGB 位域，来自 GRUB framebuffer tag 或 Bochs 缺省值，供 `framebuffer_pack()` 使用。
Bochs VBE 初始化（640x480→800x600 的替代方案是：先用 VBE 把分辨率设成桌面需要的 800x600x32）：

```c
static TEXT64 void bochs_vbe_init(void){outw64(0x1ce,1);outw64(0x1cf,800);outw64(0x1ce,2);outw64(0x1cf,600);outw64(0x1ce,3);outw64(0x1cf,32);outw64(0x1ce,4);outw64(0x1cf,0x41);}
```

- `outw64(0x1ce,1)` 选择索引寄存器 = VBE_DISPI_INDEX_XRES，随后 `0x1cf` 写 800；索引 2/3 是 YRES/ BPP(32)，索引 4 是 `VBE_DISPI_ENABLED`，值 `0x41` = enable + LFB；即使 GRUB 没给 framebuffer，内核也能自建 800x600x32 显存（配合 PCI BAR fallback 获得物理地址）。

`framebuffer_pack()`（RGB 位域打包）：

```c
static TEXT64 u32 framebuffer_pack(u32 color){u32 r=(color>>16)&255U,g=(color>>8)&255U,b=color&255U,out=0;
  if(framebuffer.red_mask)out|=((r>>(8-framebuffer.red_mask))&((1U<<framebuffer.red_mask)-1U))<<framebuffer.red_pos;
  if(framebuffer.green_mask)out|=((g>>(8-framebuffer.green_mask))&((1U<<framebuffer.green_mask)-1U))<<framebuffer.green_pos;
  if(framebuffer.blue_mask)out|=((b>>(8-framebuffer.blue_mask))&((1U<<framebuffer.blue_mask)-1U))<<framebuffer.blue_pos;
  return out;}
```

- 每通道先右移 `8-mask` 位截断到该通道实际位宽，再与 `(1<<mask)-1` 取掩码，最后左移 `red_pos` 归位；`mask==0` 的通道跳过，避免除零/移位异常；
- 设计动机：TinyOS 内部颜色恒为 `0x00RRGGBB`，与硬件布局解耦，换 `-device bochs-display` 或其他 32bpp 布局时只需改位域。

双缓冲写入与提交（`framebuffer_present`/`framebuffer_present_rect` 按行把 backbuffer 拷到 LFB，`presents++` 计数，矩形版先裁剪再只提交局部行）：

```c
static TEXT64 int framebuffer_pixel(u32 x,u32 y,u32 color){volatile u32*p;
  if(!framebuffer.ready||!framebuffer.mapped||x>=framebuffer.width||y>=framebuffer.height)return 0;
  if(framebuffer.rendering&&framebuffer.backbuffer)p=&framebuffer_backbuffer[y*FRAMEBUFFER_MAX_WIDTH+x];
  else p=(volatile u32 *)(unsigned long)(framebuffer.address+(u64)y*framebuffer.pitch+(u64)x*4);
  *p=framebuffer_pack(color);framebuffer.pixels++;return 1;}
```

- `framebuffer_pixel` 先做边界检查，然后按 `framebuffer.rendering` 选择写 backbuffer（固定 stride `FRAMEBUFFER_MAX_WIDTH`）还是直写显存（stride 为真实 `pitch`）——这就是「绘制不可见、提交才可见」；设计动机是避免绘制中间状态被扫描到（撕裂），输入场景用 `framebuffer_present_rect` 局部提交、无需整屏 present。

#### 3.2.2 鼠标模型与 IRQ12

`struct mouse_model` 与三字节包解码（关键函数，≥3 行实质分析）：

```c
struct mouse_model { int x,y; u8 buttons,packet[3],packet_index,online; u64 packets,bytes,moves,clicks,sync_errors,clipped,irq12; };
static TEXT64 int mouse_packet_byte(u8 value){int dx,dy;u8 b,old_buttons;
  if(mouse.packet_index==0&&!(value&0x08)){mouse.sync_errors++;return 0;}
  mouse.packet[mouse.packet_index++]=value;if(mouse.packet_index<3)return 0;
  b=mouse.packet[0];old_buttons=mouse.buttons;
  dx=(b&0x10)?(int)(s64)(signed char)mouse.packet[1]:(int)mouse.packet[1];
  dy=(b&0x20)?(int)(s64)(signed char)mouse.packet[2]:(int)mouse.packet[2];
  mouse.bytes+=3;mouse.packet_index=0;
  if(!(b&0x40)&&!(b&0x80)){int nx=mouse.x+dx,ny=mouse.y-dy;
    if(nx<0){nx=0;mouse.clipped++;}if(ny<0){ny=0;mouse.clipped++;}
    if(nx>=(int)framebuffer.width){nx=framebuffer.width-1;mouse.clipped++;}
    if(ny>=(int)framebuffer.height){ny=framebuffer.height-1;mouse.clipped++;}
    if(nx!=mouse.x||ny!=mouse.y){mouse.moves++;mouse_cursor_dirty=1;}
    mouse.x=nx;mouse.y=ny;}
  if((u8)(b^old_buttons)&7){mouse.clicks++;desktop_dirty++;}
  mouse.buttons=b&7;
  if((b&1)&&!(old_buttons&1))desktop_mouse_click((u32)mouse.x,(u32)mouse.y,ticks);
  mouse.packets++;return 1;}
```

- **算法步骤**：① 首字节无同步位 `0x08` 计 `sync_errors` 丢弃；② 缓冲到 3 字节才解析；③ bit5/bit4 分别为 X/Y 位移符号位，为 1 时按 signed char 解释；④ 无 overflow（bit6/bit7 清）才应用位移并裁剪；⑤ 低 3 位按键变化计 `clicks`，左键按下沿触发 `desktop_mouse_click()`；
- **边界与错误处理**：`sync_errors` 统计不同步包；`clipped` 统计裁剪次数；overflow 时完全丢弃位移；坐标允许等于 0（左上角）；
- **为什么这样设计**：对照 Linux `drivers/input/mouse/psmouse-base.c` 的 `psmouse_process_byte()`——先同步、再解包、overflow 丢弃，语义一致；`mouse.x=mouse.y=0` 初值保证 `mouse_move 0 0` 能到达左上角。

PS/2 硬件初始化：

```c
static TEXT64 void mouse_hw_init(void){u8 cb=0;int stream;
  mouse_reset();mouse_flush();
  if(mouse_wait_input())outb64(0x64,0xa8);           /* enable AUX */
  if(mouse_wait_input()){outb64(0x64,0x20);if(mouse_wait_output())cb=inb64(0x60);} /* read command byte */
  cb=(u8)((cb|2U)&(u8)~0x20U);                        /* set IRQ12 enable, clear disable */
  if(mouse_wait_input()){outb64(0x64,0x60);if(mouse_wait_input())outb64(0x60,cb);} /* write back */
  mouse_command_byte=cb;mouse_flush();
  stream=mouse_aux_command(0xf4);                     /* enable data reporting, wait 0xfa */
  mouse.online=(u8)((cb&2U)!=0);(void)stream;}
```

- 流程：0x64 写 0xa8 使能 AUX 通道 → 0x20 读 controller command byte → `cb|2` 置 IRQ12 使能、清 `~0x20` 的 disable 位 → 0x60 写回 → `mouse_aux_command(0xf4)` 发 enable stream 命令并等待 `0xfa` ACK；
- `mouse_aux_command` 用 0x64 写 `0xd4`（后续字节进 AUX 通道）再写命令字节，轮询 output buffer 里 `status&0x20`（AUX 数据标志）且值等于 `0xfa` 才算成功；
- 设计动机：QEMU 默认就有 i8042/PS2 鼠标，无需 `-device ps2-mouse`；IRQ12 使能位在 command byte bit1。

IRQ12 中断处理（新增 asm 桩 + C 处理）：

```c
static TEXT64 __attribute__((used)) void irq12_record(void){u32 i;u8 status,raw;
  mouse.irq12++;
  for(i=0;i<16;i++){status=inb64(0x64);if(!(status&1))break;raw=inb64(0x60);
    if(status&0x20){if(mouse.online)mouse_packet_byte(raw);}}
  outb64(PIC2_COMMAND,PIC_EOI);outb64(PIC1_COMMAND,PIC_EOI);}
```

- 循环读取 0x64 status，`bit0`=1 表示 output buffer 有数据；`bit5`=1 表示是 AUX 数据，喂给 `mouse_packet_byte()`；
- 必须对 PIC 两个控制器都发 EOI（IRQ12 在从片级联路径，`install_idt` 新增 `set_gate(&idt[0x2c],runtime_irq12_address(),0)`）；主循环 `pic_masks(0xf8,0xef)` 解除从片 IRQ12 屏蔽（`0xef` 即 bit4=0）。

#### 3.2.3 桌面图标、双击与 icontest

```c
struct desktop_icon_model { u32 x,y,w,h; u8 selected,target; };
static struct desktop_icon_model shell_icon;
static u8 shell_window_open,shell_icon_pending;
static u64 shell_open_count,last_icon_click;
static TEXT64 int desktop_icon_hit(u32 x,u32 y){return x>=shell_icon.x&&y>=shell_icon.y&&x-shell_icon.x<shell_icon.w&&y-shell_icon.y<shell_icon.h;}
static TEXT64 void desktop_mouse_click(u32 x,u32 y,u64 when){
  if(!desktop_icon_hit(x,y))return;
  shell_icon.selected=1;desktop_dirty++;
  if(shell_icon_pending&&when-last_icon_click<=60&&x>=shell_icon.x&&x<shell_icon.x+shell_icon.w&&y>=shell_icon.y&&y<shell_icon.y+shell_icon.h){
    shell_window_open=1;shell_icon_pending=0;shell_open_count++;focused_window=0;
    windows[0].visible=1;windows[0].focused=1;windows[0].z=2;desktop_dirty++;}
  else{shell_icon_pending=1;last_icon_click=when;}}
```

- **算法步骤**：① 命中判定用「左闭右开」区间；② 任何命中都设 `selected` 并标脏；③ 若已 pending 且 `when-last_icon_click<=60` 且再次命中 → 双击：打开 Terminal、聚焦窗口 0、z 提到 2；④ 否则记下 pending 与时间戳；
- **为什么这样设计**：双击判定只用 PIT tick 差值（有界状态机），不引入真实定时器中断；`last_icon_click` 是 u64，60 tick 远小于 2^63，回绕不会误判。`icontest` 以 `desktop_model_reset()` 复位后用 `(shell_icon.x+10,shell_icon.y+10)` 先后在 tick 100、120 点击，断言 `selected`/`shell_window_open`/`focused_window==0`/`shell_open_count==1`，输出串（逐字抄录）：

```text
icontest: desktop icon hit, bounded double-click, shell open, and focus passed
```

#### 3.2.4 Xfce 风格桌面合成器与 desktest

```c
static TEXT64 int xfce_desktop(void){u32 a,b,d;
  framebuffer.rendering=1;
  if(!desktop_initialized){mouse_reset();desktop_model_reset();desktop_initialized=1;
    windows[0]=(struct window_model){72,96,360,250,0,1,0,1};
    windows[1]=(struct window_model){470,132,250,190,0,1,0,1};}
  window_count=1;focused_window=0;
  windows[0].x=72;windows[0].y=96;windows[0].w=620;windows[0].h=390;
  windows[0].visible=shell_window_open;windows[0].focused=shell_window_open;windows[0].z=shell_window_open?1:0;
  windows[1].visible=0;windows[1].focused=0;windows[1].z=0;
  a=framebuffer_rect(0,0,framebuffer.width,framebuffer.height,0x001b2638U);   /* 桌面背景 */
  b=framebuffer_rect(0,0,framebuffer.width,34,0x002b394dU);                   /* 顶部 panel */
  d=framebuffer_rect(0,framebuffer.height-34,framebuffer.width,34,0x002b394dU); /* 底部 taskbar */
  framebuffer_rect(0,34,framebuffer.width,2,0x004d6b8aU);                     /* panel 分隔线 */
  framebuffer_rect(0,framebuffer.height-36,framebuffer.width,2,0x004d6b8aU);  /* taskbar 分隔线 */
  canvas=(struct canvas_model){0,0,0,0x00eef5ffU,0};desktop_icon_draw();
  if(shell_window_open)xfce_window(72,96,620,390,0x002b1b3bU,0x00140d1fU);
  canvas=(struct canvas_model){0,0,0,0x00eef5ffU,0};
  canvas_text(16,13,"TINYOS");canvas_text(112,13,"APPLICATIONS");canvas_text(388,13,"WORKSPACE 1");
  canvas_text(16,framebuffer.height-22,"MENU");canvas_text(92,framebuffer.height-22,"TERMINAL");
  if(shell_window_open){canvas_text(92,101,"Welcome to TinyOS");gui_term_draw();}
  framebuffer.rendering=0;framebuffer_scene_copy();framebuffer.scene_ready=1;
  framebuffer.rendering=1;xfce_cursor();framebuffer.rendering=0;
  framebuffer_present();cursor_old_x=mouse.x;cursor_old_y=mouse.y;mouse_cursor_dirty=0;
  return a&&b&&d;}
```

- **算法步骤**：① 首帧重置鼠标/桌面模型；② 设置窗口 0 为 620x390 的 Terminal（visible 跟随 `shell_window_open`）；③ 依次画背景、panel、taskbar 与两条分隔线；④ 画图标；窗口开着才画 Terminal 窗口；⑤ 画 panel/taskbar 文字；⑥ 关 rendering 冻结 scene、开 rendering 画光标、最后整屏 present；
- **关键设计**：`framebuffer.rendering=1` 期间所有 `framebuffer_pixel()` 写 backbuffer；`framebuffer_scene_copy()` 把**无光标**场景存进 `framebuffer_scene[]`，之后 `xfce_cursor_update()` 才能干净恢复；`desktop_initialized` 保证 `desktop_model_reset()` 只执行一次；
- `desktest` 输出串（逐字抄录）：

```text
desktest: Xfce-style panel, taskbar, windows, focus, and pointer rendered
```

#### 3.2.5 图形 Terminal 与安全白名单

```c
#define GUI_TERM_INPUT_MAX 96
#define GUI_TERM_LINES 10
static char gui_term_input[GUI_TERM_INPUT_MAX];
static u32 gui_term_input_len,gui_term_line_count;
static volatile u32 gui_term_input_dirty;
static char gui_term_lines[GUI_TERM_LINES][64];
static TEXT64 void gui_term_command(void){
  if(gui_term_eq("help")){gui_term_line("help about clear shellrun guiinfo");
    gui_term_line("mouseinfo iconinfo desktopinfo ramfsinfo");
    gui_term_line("fdinfo pipeinfo signalinfo clockinfo timerinfo");
    gui_term_line("schedinfo tasklist threadinfo meminfo vmainfo");
    gui_term_line("vminfo anoninfo sessioninfo resourceinfo");}
  else if(gui_term_eq("about"))gui_term_line("TinyOS graphical terminal");
  else if(gui_term_eq("clear"))gui_term_clear();
  else if(gui_term_eq("shellrun"))gui_term_line("/bin/sh: command path validated");
  else if(gui_term_eq("guiinfo"))gui_term_line("framebuffer 800x600 backbuffer enabled");
  /* ...其余白名单分支（mouseinfo/iconinfo/desktopinfo/ramfsinfo/fdinfo/pipeinfo/signalinfo/
     clockinfo/timerinfo/schedinfo/tasklist/threadinfo/meminfo/vmainfo/vminfo/anoninfo/
     sessioninfo/resourceinfo）均为一行固定输出... */
  else if(gui_term_input_len)gui_term_line("unknown command");
  gui_term_input_len=0;gui_term_input_dirty=0;}
```

- **职责**：Terminal 里的回车统一走这里，`gui_term_eq` 精确匹配（空输入不匹配任何命令，`gui_term_input_len` 为 0 时 else 分支被短路）；每个命令只 `gui_term_line()` 一行固定文字，永不调用 `exec64()`；`clear` 只清空 Terminal 缓存，不碰 VGA 光标或显存；
- `gui_term_input_present()` 负责输入行局部刷新：先从 `framebuffer_scene` 恢复输入行背景，再画 prompt `tinyos@localhost:~$` 与输入串，最后 `framebuffer_present_rect(84,119+line*18,596,36)` 提交局部矩形；
- 设计动机：Lesson 66 的教训——整屏重绘导致输入慢；本课保持 `gui_term_input_dirty` 与 `desktop_dirty` 分离，普通字符只动输入行。

#### 3.2.6 主循环事件泵与 exec64 新分支

主循环（`kernel_main64_binary` 尾部）把键盘、鼠标、桌面脏标、Terminal 输入行串起来：

```c
for(;;){
  mouse_poll();                                        /* 鼠标掉线时周期性重试硬件初始化 */
  if(desktop_dirty){desktop_dirty=0;(void)xfce_desktop();}      /* 整屏重绘 */
  else if(mouse_cursor_dirty&&framebuffer.scene_ready)xfce_cursor_update(); /* 仅光标层 */
  if(gui_term_input_dirty&&shell_window_open)gui_term_input_present();        /* 仅输入行 */
  if(!kbd_dequeue(&ch)){__asm__ volatile("sti; pause":::"memory");continue;}
  if(shell_window_open){                                 /* Terminal 打开：输入进 GUI 行 */
    if(ch=='\n'){gui_term_command();desktop_dirty++;}
    else if(ch=='\b'){if(gui_term_input_len)gui_term_input[--gui_term_input_len]=0;gui_term_input_dirty=1;}
    else if(ch>=32&&ch<127&&gui_term_input_len<GUI_TERM_INPUT_MAX-1){gui_term_input[gui_term_input_len++]=(char)ch;gui_term_input[gui_term_input_len]=0;gui_term_input_dirty=1;}
  }else if(ch=='\n'){putc64(&c,ch);cmd[n]=0;exec64(&c,h,cmd);n=0;}   /* 文本 shell */
  else if(ch=='\b'){if(n){n--;c--;VGA[c]=0x0f20;}}
  else if(n<31){cmd[n++]=(char)ch;putc64(&c,(char)ch);}}
```

- **调度优先级**：鼠标掉线恢复（低频）→ 整屏脏标 → 光标层 → 输入行 → 键盘出队；无输入时 `sti; pause` 让出 CPU（比 Lesson 66 的 `hlt` 更省电且不锁死鼠标）；
- **双模式键盘路由**：Terminal 打开时 `\n` 走 `gui_term_command()`、退格只删输入缓存、可打印字符进 `gui_term_input`（上限 `GUI_TERM_INPUT_MAX-1`）；否则回落到文本 shell 的 `exec64()`；启动序列在 `pic_masks(0xf8,0xff)` 基础上又调 `pic_masks(0xf8,0xef)` 解除 IRQ12 屏蔽，并首帧调用 `(void)xfce_desktop()` 让桌面立即可见。

`exec64` 本课新增的命令分支，均遵循 `if(!noargs64(arg)) usage64(c,word); else <handler>(c)` 的统一形态：`guiinfo`/`drawtest`/`fonttest`/`canvastest`/`inputtest`/`mouseinfo`/`mousetest`/`iconinfo`/`icontest`/`desktopinfo`/`windowtest`/`desktest`/`shellgui`。这些命令同时出现在 `help` 命令清单开头（见下），`desktest`/`shellgui` 直接调用桌面重绘函数，其余只打印确定性输出。

`help` 命令清单串（逐字抄录，注意本课把 GUI 命令放在最前）：

```text
commands: help about iconinfo icontest desktopinfo pipeinfo pipetest polltest ptrinfo ptrtest copytest schedinfo tasklist taskvalidate forkinfo forktest cloneinfo forklifecycle execinfo exectest stacklayout vmainfo vmatest pfmodel processinfo processtest userpitest clear lminfo hhinfo hhtest tssinfo stackinfo stackguardtest isttest preempttest sleeptest kbdwaittest pctest pcgo pcinfo idletest threadstart yield ps threadinfo meminfo palloc pfree <hex> pageinfo <hex> vmap <low-va> <phys> vunmap <low-va> vminfo [low-va] vmtest vmfaulttest mmap idtinfo tickinfo uptime kbdinfo syscallinfo cpl3test bptest udtest pftest
```

`about` 与启动横幅（逐字抄录）：

```text
Lesson 67: 图形桌面综合验证
GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
```

### 3.3 kernel.c 精讲

`mb2_framebuffer_tag` 扩展了 RGB 位域（对应 GRUB framebuffer tag 的 `size>=38` 变长部分）：

```c
struct mb2_framebuffer_tag { u32 type,size; u64 address; u32 pitch,width,height; u8 bpp,type_field; u16 reserved; u8 red_pos,red_mask,green_pos,green_mask,blue_pos,blue_mask; } __attribute__((packed));
```

`prepare_memory_map()` 的 framebuffer 分支（本课核心增量）：

```c
if(framebuffer_tag && framebuffer_tag->size>=32 &&
   framebuffer_tag->bpp==32 && framebuffer_tag->type_field==1 &&
   framebuffer_tag->address && framebuffer_tag->width>=320 &&
   framebuffer_tag->width<=1024 && framebuffer_tag->height>=200 &&
   framebuffer_tag->height<=768 &&
   framebuffer_tag->pitch>=framebuffer_tag->width*4U &&
   (bytes=(u64)framebuffer_tag->pitch*framebuffer_tag->height)<=8ULL*1024*1024 &&
   framebuffer_tag->address+bytes>=framebuffer_tag->address){
  /* ...填充 address/pitch/width/height/bpp/type_field/bytes ... */
  long_mode_handoff.framebuffer_red_pos=framebuffer_tag->size>=38?framebuffer_tag->red_pos:16;
  long_mode_handoff.framebuffer_red_mask=framebuffer_tag->size>=38?framebuffer_tag->red_mask:8;
  /* ...green/blue 同理，缺省 8-8-8 ... */
  long_mode_handoff.framebuffer_map=FRAMEBUFFER_VA;
  framebuffer_ready=1;
} else {
  if(!bochs_pci_init()){framebuffer_ready=0;memory_map_ready=1;return 1;}
  /* 用 PCI BAR 里的 LFB，填充 800x600x32 与 8-8-8 位域 ... */
}
```

- **校验清单**：tag size≥32、32bpp、type_field==1（RGB）、地址非 0、宽高在 320–1024/200–768、pitch≥width*4、总字节≤8MiB、地址加法不溢出——任何一条不满足就进入 Bochs PCI fallback；
- **位域缺省**：`size>=38` 时读真实位域，否则用 16/8（R）、8/8（G）、0/8（B）的 8-8-8 缺省——兼容只给 32 字节短 tag 的 GRUB；
- `bochs_pci_init()` 用 PCI 配置空间（CF8/CFC）扫描 vendor 0x1234、device 0x1111（Bochs 显示适配器），把 BAR0 写成 `0xfc000000`，返回 LFB 物理地址——**绝不猜测显存地址**，这是 Lesson 61 修掉的 bug。

`setup_long_mode_tables()` 的 framebuffer 页表映射改为基于 `framebuffer_phys_base`：

```c
if(framebuffer_ready){u32 first=(u32)(FRAMEBUFFER_VA/(PAGE_ENTRIES*PAGE_SIZE)),entry=(u32)((FRAMEBUFFER_VA/PAGE_SIZE)%PAGE_ENTRIES),
  pages=(long_mode_handoff.framebuffer_map_offset+long_mode_handoff.framebuffer_bytes+PAGE_SIZE-1)/PAGE_SIZE,k;
  for(k=0;k<pages;k++){u32 slot=first+(entry+k)/PAGE_ENTRIES,off=(entry+k)%PAGE_ENTRIES;
    if(slot>=PAGE_TABLES_PER_ALIAS){framebuffer_ready=0;break;}
    ((volatile u64*)(unsigned long)(u32)long_mode_handoff.pt[slot])[off]=(long_mode_handoff.framebuffer_phys_base+(u64)k*PAGE_SIZE)|PTE_PRESENT_WRITABLE;
    ((volatile u64*)(unsigned long)(u32)long_mode_handoff.high_pt[slot])[off]=(long_mode_handoff.framebuffer_phys_base+(u64)k*PAGE_SIZE)|PTE_PRESENT_WRITABLE;}}
```

- 低半区和高半区页表**同时**建立 LFB 映射（`kernel64.c` 里 `framebuffer.address=framebuffer_map` 才能直接读写）；
- `kernel_main32` 用 `align_down_page(address)` 计算 `framebuffer_phys_base`、`address-phys_base` 得 `framebuffer_map_offset`，并把 `framebuffer_map=FRAMEBUFFER_VA+map_offset`；
- `pmm_reserved()`（kernel64.c）同步把 `[framebuffer_phys_base, +map_offset+bytes]` 纳入 PMM 保留区，防止 `palloc` 把显存分出去。

### 3.4 构建管线（Makefile / linker）

- `kernel64.o`：`gcc -m64 -ffreestanding -fpie -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -Werror` 编译累积的 64 位内核（`-fno-sse` 系列保证无浮点、无 red-zone 帧）；
- `kernel64.bin`：`ld -m elf_x86_64 -T kernel64.ld` 后用 `objcopy -O binary` 生成裸二进制，由 `boot.S` 的 `.incbin` 嵌入外层 ELF；`boot.o`/`kernel.o` 用 `-m32` 编译；
- `kernel.iso`：`grub-mkrescue` 打包 `boot/kernel.elf` 与 `boot/grub/grub.cfg`；`check` 做 `grub-file --is-x86-multiboot2` + grep README 关键词（`图形桌面综合验收`/`gui`/`Lesson 67`）；
- 相对上一课：`run` 去掉 `-vga std`；`grub.cfg` 用 `gfxmode/gfxpayload=800x600x32` 显式请求图形 payload（替代 boot.S 里删除的 MB2 graphics tag）。

### 3.5 主控制流

```text
GRUB → boot.S(32 位 _start) → kernel.c kernel_main32(解析 framebuffer tag / Bochs PCI fallback)
→ setup_long_mode_tables(建页表 + framebuffer_phys_base 映射) → boot.S enter_long_mode(64 位)
→ kernel64.c kernel_main64_binary(bochs_vbe_init + framebuffer_init + mouse_hw_init + pic_masks 0xf8/0xef)
→ 首帧 xfce_desktop → 主循环事件泵(mouse_poll / desktop_dirty / cursor_dirty / 键盘)
→ Terminal 打开走 gui_term_command 白名单，否则走文本 exec64
```

---

## 4. 数据流与运行逻辑

**路径一：真实鼠标打开 Terminal（端到端）**：QEMU 鼠标移动 → IRQ12 → `irq12_entry` asm 桩 → `irq12_record()` 从 0x60 读 AUX 字节 → `mouse_packet_byte()` 解包 → 坐标裁剪 → `mouse_cursor_dirty=1`；左键按下沿 → `desktop_mouse_click(x,y,ticks)` → 命中图标设 `desktop_dirty`，再次单击（≤60 tick）→ `shell_window_open=1` → 主循环整屏 `xfce_desktop()`：画背景/panel/taskbar/图标/Terminal 窗口 → `framebuffer_scene_copy()` 冻结场景 → `xfce_cursor()` 画光标 → `framebuffer_present()` 提交。

**路径二：Terminal 内输入命令**：键盘 IRQ1 → `kbd_dequeue` → 主循环发现 `shell_window_open` → 可打印字符写 `gui_term_input[]` 置 `gui_term_input_dirty`；回车 → `gui_term_command()` 白名单匹配 → `gui_term_line()` 写历史行 → 清空输入 → `gui_term_input_present()` 从 `framebuffer_scene` 恢复输入行、画 prompt 与输入、`framebuffer_present_rect()` 局部提交。

**路径三：文本 shell 回归命令（VGA 后端）**：Terminal 未打开时键盘走 `exec64()`。输入 `guiinfo` 打印 `guiinfo: framebuffer addr/pitch/size/bpp/type: ... backbuffer/size/pitch/presents/cursor: .../24x24 rgb pos/mask: ... ready/mapped: ...`；输入 `drawtest`/`fonttest`/`canvastest`/`inputtest`/`mousetest`/`icontest`/`windowtest`/`desktest`/`shellgui` 各自断言 → `passed` 或 fallback 文案。

---

## 5. 构建、运行与验证

依赖：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`；真实 GUI 验收需图形窗口（勿加 `-display none`）。

```bash
cd lessons/lesson-67-stable
make -j"$(nproc)"
make check          # 预期：Multiboot2 and Lesson 67 checks passed.
make run            # QEMU 图形窗口 + 串口 stdio
```

GUI 专项自动化验证（可选，来自 `scripts/qemu-vga-check.sh`）：`scripts/qemu-vga-check.sh lessons/lesson-67-stable guiinfo drawtest fonttest canvastest desktest shellgui iconinfo icontest desktopinfo mouseinfo`。脚本以 `-vga none -device bochs-display,xres=800,yres=600,vgamem=16M` 启动 QEMU，用 monitor `sendkey` 逐字符输入命令、`pmemsave` 抓 VGA 显存、`screendump` 抓 PPM，并检查尺寸/header/颜色/legacy VGA corruption marker。

**确定性回归步骤（在 `tinyos>` 文本 shell）**——每步的 `passed` 输出串均逐字摘自 `kernel64.c`：

| 命令 | 预期输出（逐字抄录自源码） |
|---|---|
| `help` | 命令清单，以 `commands: help about iconinfo icontest desktopinfo ...` 开头 |
| `about` | `Lesson 67: 图形桌面综合验证` |
| `guiinfo` | `guiinfo: framebuffer addr/pitch/size/bpp/type: ... ready/mapped: 1/1`（核对 800x600/pitch/bpp=32/`backbuffer/size/pitch/presents/cursor`/rgb pos/mask） |
| `drawtest` | `drawtest: bounded framebuffer clear/rectangles passed` |
| `fonttest` | `fonttest: bounded 5x7 bitmap glyphs and clipped text passed` |
| `canvastest` | `canvastest: canvas colors, dirty regions, and clipped drawing passed` |
| `inputtest` | `inputtest: bounded keyboard/mouse/timer event queue passed` |
| `mousetest` | `mousetest: bounded PS/2 three-byte packet decode and pointer clipping passed` |
| `icontest` | `icontest: desktop icon hit, bounded double-click, shell open, and focus passed` |
| `windowtest` | `windowtest: bounded windows, widgets, focus, hit testing, and event dispatch passed` |
| `desktest` | `desktest: Xfce-style panel, taskbar, windows, focus, and pointer rendered` |
| `shellgui` | `shellgui: graphical terminal and system status panel linked to init/session metadata passed` |
| `mouseinfo` | `mouse: PS/2 AUX online: yes` + pointer 坐标 + `irq12` 计数（移动鼠标后增长） |
| `shellrun` | `/bin/sh` 校验通过（Lesson 60 回归） |

**真实 GUI 验收（强制，成功画面在 QEMU 图形窗口）**：

```bash
qemu-system-x86_64 -accel tcg -boot order=d -cdrom build/kernel.iso -serial stdio -no-reboot -no-shutdown
```

1. 桌面完整：顶部 panel、底部 taskbar 未被截断；SHELL 图标可见；`mouse_move 0 0`（或物理鼠标）→ 指针到达左上角 `(0,0)`；
2. 单击图标 → 选中态高亮；双击（0.6s 内两次）→ Terminal 窗口打开并聚焦；
3. Terminal 内输入长命令 → 字符即时回显（无整屏闪烁）；`help`、`about`、`guiinfo`、`clear`、`shellrun` 输出正确；
4. 保存截图证据（`screendump` PPM）+ 串口/VGA marker + `passed` 记录，三者互为补充。

> 验收结论：`icontest`/`desktest` 等只证明确定性模型，不能替代真实 GUI 验收；必须同时满足「模型测试全 passed」与「真实桌面画面证据」。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 黑屏或只有 VGA 文本，`guiinfo` 显示 `ready/mapped: 0/0` | GRUB 未稳定给 800x600x32 payload，或 framebuffer tag 校验不通过 | 查 `grub.cfg` 的 `gfxmode/gfxpayload=800x600x32`；跑 `qemu-vga-check.sh`；看 `kernel.c` 是否落到 Bochs PCI fallback |
| `drawtest` 输出 `framebuffer unavailable; safe fallback reported` | `framebuffer_init` 因 bpp/type/尺寸/pitch 不匹配而置零模型 | 检查 `h->framebuffer_type==1 && bpp==32 && width<=1024 && height<=768`；确认 `bochs_vbe_init()` 被调用 |
| 画面条纹、花屏或黑闪；鼠标移动时旧光标残留 | 绘制中间态直写显存 / backbuffer stride 错用 `width*height` / scene 冻结顺序错 | 确认 `framebuffer_pixel` 走 backbuffer 分支且索引统一 `y*FRAMEBUFFER_MAX_WIDTH+x`；`framebuffer_scene_copy()` 在 `xfce_cursor()` 之前 |
| 鼠标无反应或到不了左上角；包错乱、`sync_errors` 增长 | IRQ12 未使能（从片 mask）或裁剪把 0 裁成 1；轮询与 IRQ 同时读 AUX | 查 `pic_masks(0xf8,0xef)`；确认 `mouse_packet_byte` 允许 `nx==0, ny==0` 且首字节 `value&0x08` 同步；`mouseinfo` 看 `online/irq12` |
| 双击打不开 Terminal | tick 窗口超限（60）或 `last_icon_click` 更新错位 | `icontest` 用 100/120 tick 复现；检查 `desktop_mouse_click` 的 `when-last_icon_click<=60` 与再次命中判断 |
| Terminal 输入卡顿/整屏重绘 | 每字符都置 `desktop_dirty` | 确认字符/退格只置 `gui_term_input_dirty`，仅 Enter 置 `desktop_dirty`；输入行用 `framebuffer_present_rect` |
| Terminal 输入 `pftest` 等停机的危险命令 | 白名单外命令误落到 `exec64` | 确认 `gui_term_command` 走纯白名单，未调用 `exec64()`；不支持的命令输出 `unknown command` |
| 颜色完全错乱；`palloc` 分到显存页 | RGB 位域用死值而非 framebuffer 实际位域；PMM 未保留 framebuffer 范围 | `guiinfo` 核对 `rgb pos/mask`；确认 `framebuffer_pack` 用 `framebuffer.red_pos` 等；查 `pmm_reserved` 是否含 framebuffer 区间 |

---

## 7. 与 Linux 源码对照

- **PS/2 鼠标解包**：TinyOS `mouse_packet_byte()` 的三字节解码（同步位、X/Y 符号位、overflow 丢弃）对照 Linux `drivers/input/mouse/psmouse-base.c` 的 `psmouse_process_byte()`。教学模型只支持 3 字节标准包，不支持滚轮/4 字节/按键扩展。
- **backbuffer/scene 双缓冲**：对照 Linux DRM 的 scanout buffer + framebuffer 概念。教学模型用固定内存数组做软件合成，无硬件 vsync/page-flip，`framebuffer_present()` 只是按行 memcpy 到 LFB。
- **RGB 位域打包**：对照 Linux DRM 的 `drm_format_info` / XRGB8888 布局。教学模型写死 32bpp、每通道 ≤8 位，不覆盖 16bpp/24bpp。
- **桌面/窗口/图标模型**：对照 X11/Wayland compositor 的窗口树与 hit-test。教学模型固定 `WINDOW_CAP=4`/`WIDGET_CAP=8`，窗口不支持拖动/resize/关闭。
- **双击判定**：对照桌面环境 double-click interval（通常 400ms）。教学模型用 60 个 PIT tick（约 0.6s）做有界近似。
- 权威来源：Intel SDM Vol.3（8042/IRQ12/IDT/PIC）、Multiboot2 规范（framebuffer tag 与 `size>=38` 位域）、GRUB manual（`gfxmode/gfxpayload`）。教学模型简化：不做 DMA、不处理多控制器/ACPI、无真实抢占与多进程合成。

---

## 8. 思考题与练习

1. **概念理解**：`desktest` 与 `xfce_desktop()` 之间是什么关系？为什么 `icontest` 只能证明模型而不能证明真实鼠标能到 `(0,0)`？
2. **源码定位**：`framebuffer_pixel()` 里 `framebuffer.rendering && framebuffer.backbuffer` 两个条件各起什么作用？`framebuffer_present_rect()` 相对 `framebuffer_present()` 省掉了什么？
3. **动手实验**：把 `desktop_mouse_click()` 的双击窗口从 60 tick 改成 20 tick，重新跑 `icontest`（内部 tick=100/120），预期输出如何变化？为什么？
4. **动手实验**：在 `gui_term_command()` 白名单里删掉 `guiinfo` 分支，Terminal 输入 `guiinfo` 会输出什么？为什么它不会像 `exec64` 那样回退执行？
5. **Linux 对照**：对照 `psmouse-base.c`，TinyOS 的 `mouse_packet_byte()` 省略了哪些真实鼠标能力（滚轮、4/5 键、4 字节包）？如果鼠标发 4 字节包，TinyOS 会表现出什么（提示 `packet_index` 上限）？

---

## 9. 本课小结与下一课预告

- 本课把 Lesson 61–66 的 GUI 子栈做了一次跨层综合回归：交接块扩到 RGB 位域与 `phys_base/map_offset`，`kernel.c` 用完整校验 + Bochs PCI BAR fallback 保证 framebuffer 一定可用；
- 64 位侧新增 backbuffer/scene 双缓冲、`framebuffer_pack()` 颜色打包、26 字母位图字体、PS/2 鼠标硬件初始化与 IRQ12 解码、桌面图标与有界双击状态机、Xfce 风格桌面合成器与图形 Terminal 安全白名单；
- 主循环演变成「鼠标掉线恢复 → 整屏脏标 → 光标层 → 输入行 → 键盘」的分级事件泵，键盘在 Terminal 打开时安全路由到白名单，否则回落到文本 `exec64()`；
- 明确了验收纪律：`*test` 命令只证明确定性模型，真实 GUI 验收必须以 QEMU 图形窗口画面（`ready/mapped: 1/1`、`passed`、截图证据）为准；构建侧从 boot.S 内联 graphics tag 迁移到 `grub.cfg` 的 `gfxmode/gfxpayload`，`make run` 不再指定 `-vga std`。

下一课 [Lesson 68（进程组与 session 元数据）](../lesson-68-stable/README.md) 将结束 GUI 主线，回到第 5 阶段的文本内核主线。从 Lesson 68 起课程采用「固定元数据 + 确定性验证」教学模型：`pginfo`/`pgtest` 等命令在固定的小型进程组对象上做确定性状态转换，不执行任意用户代码；GUI 仅作为 `shellgui` 等回归命令保留，不再扩展。
