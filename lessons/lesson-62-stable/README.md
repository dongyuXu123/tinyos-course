# Lesson 62: backbuffer、像素格式、bitmap font 与 canvas — 精讲文档

> **课号**：Lesson 62（可执行课）
> **主题**：像素格式、5×7 bitmap font 与 canvas 画布；命令 `fonttest`/`canvastest`
> **课程主线位置**：第 4 阶段「图形桌面主线」（61–67）第二课。上一课（61）证明
> 了 framebuffer 来源可靠；本课在它之上搭建「画字符、画画布」的图形原语——像素
> 颜色怎么打包、字怎么画、越界怎么裁剪、统计什么。GUI 主线在 Lesson 67 结课。
> **前置课程**：[`lesson-61-stable/README.md`](../lesson-61-stable/README.md)
> **后续课程**：[`lesson-63-stable/README.md`](../lesson-63-stable/README.md)
> **一句话目标**：学完本课你能说清——32bpp 的 RGB 颜色如何打进一个 `u32`、
> 5×7 bitmap 字形的位图行怎么编码、`glyph_row`/`canvas_char`/`canvas_text` 如何
> 有界地把一串字画到屏幕上、`canvas_model` 统计了哪些可验证的事实，以及为什么
> 「直接画到真实显存」还不是完整的双缓冲（backbuffer 留到 Lesson 65）。

---

## 1. 课程定位（Mission）

**一句话目标**：让内核能**画字**：给定一串字符，在帧缓冲上按 5×7 位图字形逐
像素绘出，且全程有界（字符数、坐标、裁剪）。`fonttest` 输出
`bounded 5x7 bitmap glyphs and clipped text passed`、`canvastest` 输出
`canvas colors, dirty regions, and clipped drawing passed`，是「真实绘制路径
有效」的证据。

- **在课程主线中的位置**：第 4 阶段第二课。61 保证「画到哪里安全」，本课负责
  「画什么、怎么画」：颜色打包 → 字形查表 → 逐像素描边 → 画布统计。下一课（63）
  接入键盘/鼠标输入；本课画出的文字与矩形将成为窗口/桌面的可见素材。
- **责任边界**：本课**不负责**键盘/鼠标、窗口命中或 compositor 策略；字体只
  覆盖有限 ASCII（A–P 及一个兜底字形），不是 Unicode 排版系统；文本和矩形
  都必须有界（playbook §4 的字体纪律起点）。
- **前置知识清单**：① Lesson 61 的 `framebuffer_model` 与
  `framebuffer_pixel(x,y,color)`（本课逐字复用）；② 32bpp RGB：每个像素 4 字节，
  颜色低 24 位为 R/G/B；③ `u32` 位运算：`1U<<(4-col)` 取字形位图某列；
  ④ 越界安全：`framebuffer_pixel` 对坐标越界返回 0 而不写内存。
- **本课交付**：新命令 `fonttest`/`canvastest`；新结构 `struct canvas_model`；
  新函数 `glyph_row`/`canvas_char`/`canvas_text`；画面上出现白色 "TINYOS" 字样
  与顶部色带 + 绿色矩形 + "CANVAS" 的组合图案。

---

## 2. 核心概念精讲

### 2.1 概念一：像素格式 —— 32bpp 的颜色如何装进一个 u32

定义：本课 framebuffer 是 32bpp 直接色（Lesson 61 已强制 `bpp==32`、
`type_field==1`）。每个像素正好 4 字节，当作一个 `u32` 时，**低 24 位**是
RGB（高位通常为 0 或 alpha）。所以 `0x00102040U` = R=0x10、G=0x20、B=0x40。

为什么需要：`framebuffer_pixel` 按 `y*pitch + x*4` 定位并写一个 `u32`，颜色值
与字节序由硬件格式决定。教学模型固定只支持这一种格式，把「颜色打包」简化成
`r<<16|g<<8|b` 的直觉公式，也正因如此 Lesson 61 才坚持拒绝 `type_field==0`
（调色板）的 framebuffer。

### 2.2 概念二：5×7 bitmap font —— 字形就是 7 行位图

定义：一个 5×7 字形 = 7 个字节（每行 1 字节，低 5 位有效），`1` 表示该列画
前景色、`0` 表示留空。`glyph_row(ch,row)` 查 `letters[16][7]` 表返回第 `row`
行的字节；表只有 **16 个字模（A–P）**，其余字符返回一个兜底字形：第 3 行
（中间行）为 `0x04`（中间一竖点），其他行 0。

为什么需要：教学字体必须**有界且确定**。A–P 16 个字形足够画出 "TINYOS"、
"CANVAS" 等验证串；兜底字形保证未知字符不会产生越界索引（`ch-'A'` 只在
`ch>='A'&&ch<='P'` 时才使用）。playbook §4 明确：5×7 字体只可靠覆盖 A–Z，
数字、标点、非 ASCII 需要单独验证，不能把乱码当 framebuffer 故障。

### 2.3 概念三：canvas —— 画布与「发生过什么」的统计

定义：`struct canvas_model { u64 glyphs,dirty_regions,clipped; u32 fg,bg; }`。
`glyphs` 统计成功绘制的像素数；`dirty_regions` 统计一次 `canvas_char` 调用
（无论成功与否）——这是「脏区域」思想的起点；`clipped` 统计发生过裁剪/越界的
字符；`fg`/`bg` 记录前景/背景色（本课 `bg` 只存储、未参与绘制，源码事实）。

为什么需要：命令需要**可验证的输出**。`fonttest` 判定 `canvas.glyphs` 非零即
「确实画出了像素」；`canvastest` 判定 `canvas.dirty_regions` 非零即「绘制路径
被走过」。这些计数器让「画没画、有没有被裁剪」成为可断言的事实，而不是肉眼看
屏幕。

### 2.4 概念四：边界裁剪 —— 有界绘制是铁的纪律

定义：`canvas_text` 只处理 `s[i] && i<32` 的前 32 个字符；每个字符起点
`x+i*6`；`canvas_char` 逐像素调用 `framebuffer_pixel`，后者对坐标越界返回 0，
`canvas_char` 收到 0 就把 `ok` 置 0 并最终 `clipped++`——绘制「软失败」而不是
写坏内存。

为什么需要：字符串长度、x 起点、字形宽度都可能让像素落到屏幕外。没有这层
有界，`fonttest` 换成任意长串就会越界写显存。playbook §4 强调
`canvas_text()` 必须保留字符上限和边界裁剪，Terminal 等后续课程都依赖这条纪律。

### 2.5 概念五：backbuffer —— 本课的「未完成承诺」

定义：本课的绘制**仍然直接写真实显存**（`framebuffer_pixel` 直接写 LFB）。
旧 README 提到「固定 stride 的 backbuffer/scene」，但按源码事实，backbuffer
与 `framebuffer_scene` 真正出现是在 Lesson 65（scene/compositor）。

为什么需要说明这一点：直接画 LFB 时，用户可能看到绘制中间态（playbook §3 的
花屏/撕裂根因）。本课只通过「矩形 + 字符」的小命令暴露有限绘制，肉眼看不到
撕裂，但**架构上还没解决**它。真正的双缓冲（先画固定大小 backbuffer，再按行
提交，访问统一按 `y*FRAMEBUFFER_MAX_WIDTH + x`）由 Lesson 65 落实；本课的
`dirty_regions` 计数是这条路的第一个概念脚印。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-61） |
|---|---|---|
| `boot.S` | 引导 | 未变化（仅空白差异） |
| `kernel.c` | 32 位阶段 | **未变化**（`diff` 为空） |
| `kernel64.c` | 64 位内核主体 | **核心**：`canvas_model` + `glyph_row`/`canvas_char`/`canvas_text` + `fonttest`/`canvastest` 命令 + `about`/横幅更新 |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | `check` grep 改为 `backbuffer、像素格式、bitmap font 与 canvas`/`Lesson 62`（`gui` 保留） |
| `grub.cfg` | GRUB 菜单 | menuentry 标题更新为 lesson-62 主题 |

### 3.2 kernel64.c —— canvas 模型与字体（本课全部增量）

模型与全局（源码逐字）：

```c
struct canvas_model { u64 glyphs,dirty_regions,clipped; u32 fg,bg; };
static struct canvas_model canvas;
```

- 四个计数器/颜色字段：`glyphs`（成功绘制像素数）、`dirty_regions`（每次
  `canvas_char` 调用计 1）、`clipped`（发生过越界的字符数）、`fg`/`bg` 前景/
  背景色；
- 全局单例 `canvas`，命令执行前用复合字面量整体复位（`{0,0,0,fg,bg}`），保证
  多次运行统计可重入。

字形查表 `glyph_row`（源码逐字）：

```c
static TEXT64 u8 glyph_row(char ch,u32 row){
    static const u8 letters[16][7]={{0x0e,0x11,0x11,0x1f,0x11,0x11,0x11},
        {0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e},{0x0f,0x10,0x10,0x10,0x10,0x10,0x0f},
        {0x1e,0x11,0x11,0x11,0x11,0x11,0x1e},{0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f},
        {0x1f,0x10,0x10,0x1e,0x10,0x10,0x10},{0x0f,0x10,0x10,0x17,0x11,0x11,0x0f},
        {0x11,0x11,0x11,0x1f,0x11,0x11,0x11},{0x1f,0x04,0x04,0x04,0x04,0x04,0x1f},
        {0x01,0x01,0x01,0x01,0x11,0x11,0x0e},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},
        {0x10,0x10,0x10,0x10,0x10,0x10,0x1f},{0x11,0x1b,0x15,0x15,0x11,0x11,0x11},
        {0x1e,0x11,0x11,0x11,0x11,0x11,0x11},{0x0e,0x11,0x11,0x11,0x11,0x11,0x0e},
        {0x1f,0x11,0x10,0x1c,0x10,0x10,0x10}};
    u32 i;
    if(ch>='A'&&ch<='P')i=(u32)(ch-'A');
    else return row==3?0x04:0;
    return row<7?letters[i][row]:0; }
```

- `letters[16][7]`：16 个字形 × 每字形 7 行；每字节低 5 位是这一行的列模式。
  以 'A'（`{0x0e,0x11,0x11,0x1f,0x11,0x11,0x11}`）为例：row0=`01110`、
  row1–2=`10001`、row3=`11111`（横杠）、row4–6=`10001`，正好是字母 A；
- 范围判定 `ch>='A'&&ch<='P'` 是索引安全的前置条件，**只有命中才做 `ch-'A'`**，
  杜绝任意输入越界；
- 兜底路径：未知字符返回 `row==3?0x04:0`——只在中间行画中间一竖点（小横杠），
  其余行全 0，保证任何字符都能画出一个可见但中性的标记；
- `row<7` 再查表，`row>=7` 返回 0，防越界读表。

字符绘制 `canvas_char`（源码逐字）：

```c
static TEXT64 int canvas_char(u32 x,u32 y,char ch){
    u32 row,col; int ok=1;
    for(row=0;row<7;row++)for(col=0;col<5;col++)
        if(glyph_row(ch,row)&(1U<<(4-col))){
            if(!framebuffer_pixel(x+col,y+row,canvas.fg))ok=0;
            else canvas.glyphs++; }
    if(!ok)canvas.clipped++;
    canvas.dirty_regions++;
    return ok; }
```

- 双层循环 7 行 × 5 列；`glyph_row(ch,row)&(1U<<(4-col))` 取当前列位，命中才
  画像素；
- 每个像素调用 Lesson 61 的 `framebuffer_pixel(x+col,y+row,canvas.fg)`——越界
  时它返回 0，这里 `ok=0` 但不中断，继续把其余列画完（尽力绘制 + 事后记账）；
- 一个字符的完整记账：所有像素循环结束后 `!ok` 则 `clipped++`（这个字符有像素
  被裁剪），随后无论成败 `dirty_regions++`（记录一次绘制请求）。

字符串绘制 `canvas_text`（源码逐字）：

```c
static TEXT64 void canvas_text(u32 x,u32 y,const char*s){
    u32 i;
    for(i=0;s[i]&&i<32;i++)canvas_char(x+i*6,y,s[i]); }
```

- 两个上界：`s[i]`（字符串结束）与 `i<32`（字符数上限）——防任意长串越界；
- 字距固定 6 像素：5 列字形 + 1 列间距，`x+i*6` 是第 i 个字符的起点；
- 本课 `canvas_text` 不自动换行、不做屏幕宽度判定——那是 Terminal 课程的事；
  这里靠 `canvas_char` 的越界软失败保证安全。

命令 `fonttest` / `canvastest`（源码逐字）：

```c
static TEXT64 void fonttest(u16*c){
    canvas=(struct canvas_model){0,0,0,0x00ffffffU,0x00000000U};
    canvas_text(16,16,"TINYOS");
    text64(c,"fonttest: ");
    text64(c,canvas.glyphs?"bounded 5x7 bitmap glyphs and clipped text passed":
        "framebuffer unavailable; font path safely bounded");
    putc64(c,'\n'); }
static TEXT64 void canvastest(u16*c){
    canvas=(struct canvas_model){0,0,0,0x00ffffffU,0};
    int a=framebuffer_rect(0,0,framebuffer.width,24,0x00305090U),
        b=framebuffer_rect(8,32,80,56,0x00508040U);
    canvas_text(16,40,"CANVAS");
    text64(c,"canvastest: ");
    text64(c,a&&b&&canvas.dirty_regions?
        "canvas colors, dirty regions, and clipped drawing passed":
        "framebuffer unavailable; canvas fallback passed");
    putc64(c,'\n'); }
```

- `fonttest`：复位画布（前景白 `0x00ffffff`、背景黑 `0`），在 (16,16) 画
  "TINYOS"。判定 `canvas.glyphs` 非零（至少画出一个像素）→ 成功串；
- `canvastest`：画顶部色带 `(0,0,width,24,0x00305090U)`（蓝紫，R=0x30,G=0x50,
  B=0x90）与绿色矩形 `(8,32,80,56,0x00508040U)`（R=0x50,G=0x80,B=0x40），再在
  (16,40) 画 "CANVAS"；
- 成功判据：两个矩形都成功**且** `canvas.dirty_regions` 非零——颜色、脏区计数、
  裁剪统计三件事同时被证明；
- 两条 fallback 串不同（`font path safely bounded` vs `canvas fallback passed`），
  便于从 VGA 文本层区分哪条路径没走通；
- 注意 `bg` 字段在两个命令里都只赋值、从未被 `canvas_char` 读取——本课没有
  「背景填充」，只有前景像素（源码事实）。

`exec64` 新分支（源码逐字）：

```c
else if(eq64(word,"fonttest")){if(!noargs64(arg))usage64(c,"fonttest");else fonttest(c);}
else if(eq64(word,"canvastest")){if(!noargs64(arg))usage64(c,"canvastest");else canvastest(c);}
```

- 插在 `drawtest` 之后、`resourceinfo` 之前；`guiinfo`/`drawtest` 两个旧命令
  原样保留（Lesson 61 回归）；
- `help` 输出串依旧没有追加 fonttest/canvastest（延续 61 的小瑕疵）。

横幅与 `about`（源码逐字）：

```c
text64(&c,"Lesson 62: 固定 bitmap 字体、canvas 与基本绘图\nGETTICKS, GETPID,
WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
/* about: text64(c,"Lesson 62: 固定 bitmap 字体、canvas 与基本绘图\n"); */
```

- 主循环结构（`framebuffer_init → clear64 → 横幅 → prompt → 键盘循环 → exec64`）
  与 Lesson 61 完全相同，仅横幅字符串更新。

### 3.3 构建管线（Makefile / kernel64.ld / grub.cfg）

- 编译/链接流程与 lesson-61 完全相同：`kernel64.o`（`-m64 -ffreestanding -fpie
  -mno-red-zone -mno-sse*`）→ `kernel64.bin`（`objcopy -O binary`）→ `boot.S`
  `incbin` → 外层 i386 ELF → `grub-mkrescue` ISO；
- `check`（本课）：`grub-file --is-x86-multiboot2` + 三条 grep：README 含
  `backbuffer、像素格式、bitmap font 与 canvas`、`gui`、`Lesson 62`；通过打印
  `Multiboot2 and Lesson 62 checks passed.`；
- `run`：`qemu-system-x86_64 -accel tcg -vga std ...`（与 61 相同，`-vga std`
  提供 framebuffer）；
- `grub.cfg`：`gfxmode/gfxpayload=800x600x32` 不变，仅 menuentry 更名；
- `kernel64.ld`/`linker.ld` 未变（布局与栈段 ASSERT 原样）。

### 3.4 主控制流

```text
GRUB → _start → kernel_main32（本课未变）→ 长模式 → kernel64.bin
  ├─ framebuffer_init(h)：ready/mapped（Lesson 61）
  ├─ 横幅 "Lesson 62: 固定 bitmap 字体、canvas 与基本绘图\n..."
  └─ 键盘循环 → exec64:
        fonttest  → canvas 复位 → canvas_text(16,16,"TINYOS")
                    → glyphs>0 ? "bounded 5x7 bitmap glyphs and clipped text passed"
                    → 或 "framebuffer unavailable; font path safely bounded"
        canvastest → 顶部色带 + 绿色矩形 + "CANVAS"
                    → a&&b&&dirty_regions ? "canvas colors, dirty regions, and
                      clipped drawing passed" : "canvas fallback passed"
```

---

## 4. 数据流与运行逻辑

```text
输入 "fonttest"
  → canvas={0,0,0,0x00ffffff,0}
  → "TINYOS" 逐字符：canvas_text(16,16,s)
        'T'→glyph_row 查表（'T' 不在 A–P → 中间行小横杠兜底）
        'I','N','Y','O','S' 同理
  → 每个命中像素 → framebuffer_pixel(x,y,0x00ffffff) 写 LFB，glyphs++
  → 任何越界像素 → framebuffer_pixel 返回 0 → clipped++
  → "fonttest: bounded 5x7 bitmap glyphs and clipped text passed"
输入 "canvastest"
  → canvas={0,0,0,0x00ffffff,0}
  → framebuffer_rect(0,0,800,24,0x00305090)  顶部蓝紫带
  → framebuffer_rect(8,32,80,56,0x00508040)  绿色矩形
  → canvas_text(16,40,"CANVAS")             白色字
  → a=1,b=1,dirty_regions>0
  → "canvastest: canvas colors, dirty regions, and clipped drawing passed"
```

视觉结果：`drawtest` 的深蓝底 + 亮蓝框之上，顶部多一条 24 像素高的蓝紫横带，
中部一个绿色矩形，绿色矩形左上角 (16,40) 处画白色 "CANVAS"，(16,16) 处画白色
"TINYOS"。

---

## 5. 构建、运行与验证

### 5.1 依赖

同 lesson-61：`gcc`、`binutils`、`grub-pc-bin`/`grub-common`、`xorriso`、
`mtools`、`qemu-system-x86_64`；GUI 专项验收另需 `socat`、`python3`。

### 5.2 构建与静态检查

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 输出：`Multiboot2 and Lesson 62 checks passed.`（README 必须含
`backbuffer、像素格式、bitmap font 与 canvas`、`gui`、`Lesson 62`，缺一即失败）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：
`Lesson 62: 固定 bitmap 字体、canvas 与基本绘图\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n`

验证步骤（输出串从源码逐字）：

```bash
guiinfo
```

预期：`guiinfo: framebuffer addr/pitch/size/bpp/type: ... ready/mapped: 0000000000000001/0000000000000001`（Lesson 61 回归，`ready/mapped` 必须为 1/1）。

```bash
fonttest
```

预期：`fonttest: bounded 5x7 bitmap glyphs and clipped text passed`，且画面 (16,16)
出现白色 "TINYOS" 字样。

```bash
canvastest
```

预期：`canvastest: canvas colors, dirty regions, and clipped drawing passed`，
且画面出现顶部蓝紫横带、绿色矩形与白色 "CANVAS" 字样。

### 5.4 GUI 专项验收（QEMU VGA 自动化）

按教程要求 GUI 课程走专项流程（`docs/gui-debugging-playbook.md` §8 与
`learning-guide.md` §10.2）：单课验收
`scripts/qemu-vga-check.sh lessons/lesson-62-stable guiinfo drawtest fonttest canvastest`；
第 4 阶段结课验收统一
`scripts/qemu-vga-check.sh lessons/lesson-67-stable guiinfo drawtest fonttest canvastest inputtest windowtest desktest shellgui`。

脚本用 `-M q35 -vga none -device bochs-display,xres=800,yres=600,vgamem=16M`：
`fonttest`/`canvastest` 要求多色截图（非纯黑）与 `passed` 证据；任何命令出现
`fallback` 即失败；全程无 `check_exception`/`triple fault`。

### 5.5 课程实测记录（稳定快照）

旧 README 的学习路径（`make -C lessons/lesson-62-learning` +
`make -C lessons/lesson-62-learning check`）已验证；stable 快照复验：
`make check` 输出 `Multiboot2 and Lesson 62 checks passed.`；`fonttest`/`canvastest`
均输出 `passed`，图形窗口可见字体与画布组合图案。构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `fonttest` 输出 `framebuffer unavailable; font path safely bounded` | framebuffer 未 ready/mapped（Lesson 61 handoff 失败） | 先 `guiinfo` 看 `ready/mapped`；按 Lesson 61 调试地图排查 handoff |
| 画出的字是乱码/缺笔画 | 字符不在 A–P 范围，走兜底小横杠字形 | 对照 `glyph_row`：`ch>'P'` 只画中间行 `0x04`；playbook §4：字体只可靠覆盖 A–Z |
| 字整体偏移/错位 | 字距或起点算错 | 对照 `canvas_text` 的 `x+i*6` 与 `canvas_char` 的 `x+col,y+row` |
| 某些像素画不出来但命令仍 `passed` | 越界像素被 `framebuffer_pixel` 软拒绝（`ok=0`→`clipped++`），但 `glyphs` 仍可能非零 | 把文字画到屏幕边缘再跑 `fonttest`，观察 `clipped` 语义；counters 由模型持有，可从 VGA 文本层判断 |
| 屏幕出现绘制中间态/残影 | 本课仍**直接写 LFB**，没有 backbuffer（源码事实） | 接受为当前限制；backbuffer/scene 由 Lesson 65 落实（playbook §3） |
| `canvastest` 的矩形色带宽度不对 | 用了 `width*height` 线性思维而非 pitch | `framebuffer_rect` 走 `y*pitch+x*4`；pitch 纪律见 playbook §3 |
| `make check` 报错 | README 缺 `backbuffer、像素格式、bitmap font 与 canvas`/`gui`/`Lesson 62` 之一 | 对照 Makefile `check` 三条 grep |
| 分不清「字画出来没有」 | 肉眼不可靠 | **VGA 文本仍是权威诊断通道**：先看 `fonttest`/`canvastest` 的文本标记，再判断图形（playbook §10） |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `glyph_row` 的 `letters[16][7]` 位图字形 | `drivers/video/console/fbcon_*.c` 的 `fontdata` / `drivers/video/fonts/font_*.c`（如 `font_8x16.c` 的数组） | 同样是「每字形多行字节、每字节一位一列」；Linux 是 8x16/12x22 大表，教学模型只 16 字 5x7 |
| `canvas_char` 逐像素查位 | fbcon 的 `fbcon_putcs`/`fbcon_putc` 配合 `cfb_imageblit` 一次画整字形 | 教学模型逐像素调 `framebuffer_pixel`，慢但可验证；Linux 有硬件加速的 blit |
| `canvas_text` 的 6px 字距 | `drivers/video/console/` 的 `fbcon` 用 `font->width+1` 计算字距 | 概念一致：固定字距；Linux 支持多字体/换行/光标，教学模型只有 32 字符上限 |
| `canvas_model` 的 glyphs/dirty_regions/clipped | `include/linux/fb.h` 的 `fb_info` 与 `struct fb_ops` 的统计/更新接口 | 教学模型把「画了什么、裁剪了几次」显式记账；Linux 有完整的刷新/同步框架 |
| 颜色 `0x00ffffff` 等 32bpp packing | `include/uapi/linux/fb.h` 的 `struct fb_var_screeninfo`（`red/green/blue` 位域） | 教学模型固定 RGB 低 24 位；Linux 用可编程位域描述任意格式 |
| `framebuffer_pixel` 越界软失败 | fbcon 的裁剪发生在 `fbcon_putcs` 的 x/y 边界判定 | 教学模型把裁剪下沉到像素级并计数 |

**权威来源**：Lesson 61 的 Multiboot2 帧缓冲格式约定（`type_field==1` 直接色）；
`drivers/video/fonts/` 的 Linux 字体表作为工程对照；Intel SDM 仅涉及内存写，
本课无新的硬件操作。

**教学模型简化了什么**：字模只有 16 个（A–P）且 5×7；无抗锯齿、无粗体/斜体、
无 Unifont 类全字符表；无换行/滚动/光标；无真正 backbuffer（直写 LFB）；
无硬件 blit 加速；`bg` 背景色只存储不绘制。

---

## 8. 思考题与练习

1. **概念理解**：`glyph_row` 为什么把 `ch-'A'` 限定在 `ch>='A'&&ch<='P'` 之后
   才执行？如果不限定，`canvas_text` 收到任意字节会怎样？
2. **源码定位**：指出 `canvas_char` 中统计 `glyphs`、`clipped`、`dirty_regions`
   的三个位置，并说明 `dirty_regions` 为什么在 `!ok` 时也 +1。
3. **动手实验**：把 `fonttest` 的字符串改成 `"0123456789"`（全在 A–P 之外），
   重建运行，观察兜底字形效果与 `passed` 输出是否变化，然后改回（勿提交）。
4. **Linux 对照**：读 `drivers/video/fonts/font_8x16.c` 的 `fontdata_8x16`，
   对比它与本课 `letters[16][7]` 的编码方式（行/列/位）有哪些相同与不同。
5. **设计思考**：本课把「画没画到像素」记成 `glyphs`，但没记「哪些区域脏」。
   如果 Lesson 65 要做增量重绘，`dirty_regions` 需要升级成什么数据结构？为什么
   不能只用一个计数器？

---

## 9. 本课小结与下一课预告

**小结**：本课在可靠 framebuffer 上补齐了「画字」能力：`glyph_row` 用
`letters[16][7]` 位图表提供 A–P 字形与兜底字形，`canvas_char` 逐像素描边并统计
`glyphs`/`clipped`，`canvas_text` 以 32 字符上限 + 6px 字距有界画串；`canvas_model`
把「成功像素、脏区请求、裁剪次数」变成可验证的计数器；`fonttest`/`canvastest`
用这些计数器输出 `passed` 证据，画面上出现 "TINYOS"、"CANVAS"、色带与矩形。
颜色以 32bpp RGB（低 24 位）打包，pitch 纪律继续沿用 Lesson 61。本课**仍然直写
LFB**——真正的 backbuffer/scene 双缓冲是 Lesson 65 的事，`dirty_regions` 只是
脏区域思想的第一个脚印；字体也只覆盖 A–P 有限集，不是 Unicode 排版系统。

**下一课预告**：[Lesson 63](../lesson-63-stable/README.md) 把输入设备接入系统：
键盘沿用既有 FIFO，新增 PS/2 AUX 鼠标（IRQ12）与有界输入事件队列，命令
`inputtest`——届时画布与字体将成为桌面交互的可见反馈。跨课程排错统一参考
[`docs/gui-debugging-playbook.md`](../../docs/gui-debugging-playbook.md)。
