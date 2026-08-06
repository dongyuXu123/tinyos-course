# Lesson 66: 图形 Terminal 与安全命令 dispatcher — 精讲文档

> **课号**：Lesson 66（可执行课）
> **主题**：图形 Terminal 与安全命令 dispatcher；命令 `shellgui`
> **课程主线位置**：第 4 阶段「图形桌面主线」（61–67）第六课。61–65 依次给出
> framebuffer、画布、输入队列、窗口模型与桌面合成，本课第一次在桌面上画出
> **图形 shell 窗口**（"SHELL/READY"）与**系统状态面板**（"STATUS/INIT"）。
> GUI 主线在 Lesson 67 结课。
> **前置课程**：[`lesson-65-stable/README.md`](../lesson-65-stable/README.md)
> **后续课程**：[`lesson-67-stable/README.md`](../lesson-67-stable/README.md)
> **一句话目标**：学完本课你能说清——`shellgui` 如何用两个
> `struct window_model` + 三个 `framebuffer_rect` + 四个 `canvas_text` 画出
> 「终端窗口 + 状态面板」的图形 shell 雏形，成功判据
> `a&&b&&d&&canvas.glyphs` 意味着什么，以及**为什么本课还没有真正的图形
> Terminal**——`gui_term_*` 输入缓存、`gui_term_input_dirty`、
> `framebuffer_present_rect()` 局部刷新与白名单 dispatcher 全部在 Lesson 67
> 才落地。

---

## 1. 课程定位（Mission）

**一句话目标**：让桌面出现一个「图形 shell」的可辨识雏形：左侧 SHELL 终端窗口
（写 "SHELL"/"READY"）、右侧 STATUS 状态面板（写 "STATUS"/"INIT"），且
`shellgui` 输出 `graphical terminal and system status panel linked to
init/session metadata passed` 证明绘制路径有效。

- **在课程主线中的位置**：第 4 阶段第六课。66 是「可交互图形桌面」的前夜：
  67 课将把这里的 shell 雏形升级成真正能打字、能执行白名单命令的图形
  Terminal，并完成全阶段验收。
- **责任边界**（旧 README 责任边界原文）：GUI dispatcher 不直接复用会写 VGA
  光标/显存的完整 `exec64()`，也不执行危险异常测试或任意用户输入；Terminal
  仍是 bounded metadata 模型，不是完整用户态 Shell。**但要注意**：按源码事实，
  本课 `exec64` 仍是原有文本 console 版，`shellgui` 只是新增的绘制命令——
  「安全 dispatcher」与「图形 Terminal 输入」的实现在 Lesson 67。
- **前置知识清单**：① Lesson 62 的 `canvas_text` 与 `canvas_model`（本课画
  "SHELL"/"STATUS"）；② Lesson 64 的 `struct window_model`（本课摆两个窗口）；
  ③ Lesson 65 的分层直写思想（背景→窗口矩形→文字）；④ 5×7 字体只可靠覆盖
  A–P（playbook §4）——本课字符串里 S/T/U/Y 等字母会走兜底字形，但
  `canvas.glyphs` 仍会计数，不影响判据。
- **本课交付**：新命令 `shellgui`；图形 shell 窗口 + 状态面板的可见画面；
  成功/回退两条输出串。

---

## 2. 核心概念精讲

### 2.1 概念一：图形 shell 雏形 —— 窗口 + 文字 = 终端外观

定义：`shellgui` 把窗口 0（190×120）当作 SHELL 终端、窗口 1（104×120）当作
STATUS 面板，各自填充单色矩形后再用 `canvas_text` 写入白色文字：
"SHELL"/"READY"（终端侧）、"STATUS"/"INIT"（状态侧）。

为什么需要：这是「图形 Terminal」的最小可视模型——先让**外观**成立（有窗口、
有标题、有状态文字），再在 Lesson 67 把**交互**（输入缓存、命令回显、白名单
dispatcher）接进去。教学模型把「外观」与「交互」拆成两课。

### 2.2 概念二：窗口/面板布局 —— 两个 120 高的并列窗口

定义（源码逐字）：`windows[0]={16,36,190,120,z=0,visible=1,focused=1,dirty=1}`、
`windows[1]={216,36,104,120,z=1,visible=1,focused=1,dirty=1}`——两个窗口都从
y=36 起、高 120，左右并列（16..206 与 216..320），z 序窗口 1 在上，两者
`focused` 都置 1（本课夹具一致，与 lesson-64 的 windowtest 不同）。

为什么需要：终端（宽窗口）与状态面板（窄窗口）是常见桌面组合。固定坐标让
`canvas_text` 的 x/y 可以直接写死（终端文字在窗口 0 内、状态文字在窗口 1 内）。

### 2.3 概念三：判据 canvas.glyphs —— 画了字才算数

定义：成功判据 `a&&b&&d&&canvas.glyphs`：背景、终端窗、状态窗三个矩形都成功，
**且** `canvas.glyphs`（Lesson 62 的像素计数器）非零——即确实画出了文字像素。

为什么需要：前三个矩形可能全部成功而文字部分由于画布未初始化等原因一个像素
都没画；`canvas.glyphs` 把关「文字真的被绘制」。这是 Lesson 62 计数思想的延续：
可断言，而非肉眼看。

### 2.4 概念四：字体覆盖的现实 —— S/T/U/Y 走兜底字形

定义：本课字符串 "SHELL"/"READY"/"STATUS"/"INIT" 里，S、T、U、Y 不在 5×7
字表（A–P）内，`glyph_row` 对它们返回兜底字形（中间行一小横），其余字母正常。

为什么需要：playbook §4 明确「5×7 bitmap font 是有界教学字体，当前只可靠覆盖
A–Z；数字、标点和非 ASCII 需要明确验证，不能把乱码当作 framebuffer 故障」。
本课文字在屏幕上表现为「部分字母是完整字形、部分字母是一道短横」，这是**预期
行为**，不是 bug；`canvas.glyphs` 计数照常递增，`shellgui` 仍判成功。

### 2.5 概念五：真正的图形 Terminal 为什么在 Lesson 67

诚实说明（源码事实）：本课 `kernel64.c` 里**没有** `gui_term_*`、没有输入缓存、
没有 `gui_term_input_dirty`、没有 `framebuffer_present_rect()`、没有白名单
dispatcher、没有 `shell_window_open`——这些符号全部出现在 Lesson 67
（`gui_term_command` 的白名单、`gui_term_input_len/GUI_TERM_INPUT_MAX`、
`framebuffer_present_rect()` 局部提交、`mouse_hw_init` 等）。旧 README 声称的
「字符输入通过 gui_term_input_dirty 与 framebuffer_present_rect() 局部刷新」与
「Commands: shellgui, help, about, clear, shellrun, guiinfo, mouseinfo 及各类
*info 安全诊断命令」对应的是 lesson-67 的 `gui_term_command` 白名单，而非本课。

为什么需要说明：playbook §7「图形 Shell 输入慢」的根因（逐键整屏重绘）与修复
（`gui_term_input_dirty` + `framebuffer_present_rect()`）是为 lesson-67 写的排错
经验。本课的 `shellgui` 是**静态画面**，还不具备输入路径。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-65） |
|---|---|---|
| `boot.S` | 引导 | 未变化 |
| `kernel.c` | 32 位阶段 | **未变化**（`diff` 为空） |
| `kernel64.c` | 64 位内核主体 | **核心**：`shellgui` 命令 + `exec64` 分支 + `about`/横幅更新；复用 62 的 canvas/64 的 window_model |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | `check` grep 改为 `图形 Terminal 与安全命令 dispatcher`/`Lesson 66`（`gui` 保留） |
| `grub.cfg` | GRUB 菜单 | menuentry 标题更新为 lesson-66 主题 |

### 3.2 kernel64.c —— shellgui：图形 shell 雏形（本课全部增量）

命令 `shellgui`（源码逐字）：

```c
static TEXT64 void shellgui(u16*c){
    int a,b,d;
    window_count=2;
    windows[0]=(struct window_model){16,36,190,120,0,1,1,1};
    windows[1]=(struct window_model){216,36,104,120,1,1,1,1};
    a=framebuffer_rect(0,0,framebuffer.width,framebuffer.height,0x00101830U);
    b=framebuffer_rect(16,36,190,120,0x00203858U);
    d=framebuffer_rect(216,36,104,120,0x00305070U);
    canvas=(struct canvas_model){0,0,0,0x00ffffffU,0};
    canvas_text(28,52,"SHELL");
    canvas_text(28,76,"READY");
    canvas_text(228,52,"STATUS");
    canvas_text(228,76,"INIT");
    text64(c,"shellgui: ");
    text64(c,a&&b&&d&&canvas.glyphs?
        "graphical terminal and system status panel linked to init/session metadata passed":
        "graphical shell fallback reported");
    putc64(c,'\n'); }
```

分层拆解：
1. **夹具**：`window_count=2`；`windows[0]`（SHELL 终端）在 (16,36) 起
   190×120、z=0、visible=1、**focused=1**；`windows[1]`（STATUS 面板）在
   (216,36) 起 104×120、z=1、visible=1、focused=1——两窗聚焦一致、z 序明确；
2. **a 背景**：全屏 `0x00101830U`（与 65 课相同的深蓝）；
3. **b 终端窗**：(16,36,190,120) 填 `0x00203858U`（深蓝灰）；
4. **d 状态窗**：(216,36,104,120) 填 `0x00305070U`（蓝）；
5. **画布复位**：`canvas={0,0,0,0x00ffffff,0}`（前景白、背景 0，同 62 课
   fonttest 的用法）；
6. **四段文字**：终端窗内 (28,52) 写 "SHELL"、y+24 处 (28,76) 写 "READY"；
   状态窗内 (228,52) 写 "STATUS"、(228,76) 写 "INIT"——x=228 落在窗口 1
   （216..320）内，y=76 落在两窗（36..156）内；
7. **判据**：`a&&b&&d&&canvas.glyphs` 四件事同时成立才输出成功串；否则
   fallback 串。

关键设计点：
- 文字坐标是**写死的屏幕绝对坐标**，窗口矩形与之手工对齐（playbook §4：
  prompt/输入行应共享固定布局，不能把 line_count 错用于 x 坐标）；
- 复用 62 的 `canvas_text`（32 字符上限、6px 字距）与 64 的 `window_model`
  ——本课零新增绘制原语，只有组合；
- 成功串里 "linked to init/session metadata" 是**设计意图措辞**，本课没有真正
  读取 init/session 数据（源码事实）——如实记录，避免把 67 课语义当成现状；
- S/T/U/Y 等字母走兜底字形（见 §2.4），`canvas.glyphs` 仍 >0，判据不受影响。

`exec64` 新分支（源码逐字）：

```c
else if(eq64(word,"shellgui")){if(!noargs64(arg))usage64(c,"shellgui");else{shellgui(c);}}
```

- 插在 `desktest` 之后、`resourceinfo` 之前；`guiinfo`~`desktest` 全部保留
  （回归）；
- `exec64` 本身**未改**（仍是文本 console 版）：图形 Terminal 的输入路径在
  Lesson 67，本课 `shellgui` 是纯输出命令。

横幅与 `about`（源码逐字）：

```c
text64(&c,"Lesson 66: 图形 shell 与系统状态面板\nGETTICKS, GETPID,
WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
/* about: text64(c,"Lesson 66: 图形 shell 与系统状态面板\n"); */
```

- 主循环与 65 相同，仅横幅更新。

### 3.3 构建管线（Makefile / kernel64.ld / grub.cfg）

- 编译/链接流程与 61–65 相同（`kernel64.o` → `kernel64.bin` → `boot.S incbin`
  → 外层 i386 ELF → `grub-mkrescue`）；
- `check`（本课）：`grub-file --is-x86-multiboot2` + 三条 grep：README 含
  `图形 Terminal 与安全命令 dispatcher`、`gui`、`Lesson 66`；通过打印
  `Multiboot2 and Lesson 66 checks passed.`；
- `run`：`qemu-system-x86_64 -accel tcg -vga std ...`（与 65 相同）；
- `grub.cfg`：`gfxmode/gfxpayload=800x600x32` 不变，menuentry 更名；
- `kernel64.ld`/`linker.ld` 未变。

### 3.4 主控制流

```text
GRUB → _start → kernel_main32（未变）→ 长模式 → kernel64.bin
  ├─ framebuffer_init(h)：ready/mapped（Lesson 61）
  ├─ 横幅 "Lesson 66: 图形 shell 与系统状态面板\n..."
  └─ 键盘循环 → exec64（文本版，未变）:
        shellgui → 摆 2 个 window_model（SHELL 终端 + STATUS 面板）
                 → a=背景  b=终端窗  d=状态窗
                 → canvas 复位 → "SHELL"/"READY"/"STATUS"/"INIT"
                 → 判据 a&&b&&d&&canvas.glyphs
                 → "graphical terminal and system status panel linked to
                    init/session metadata passed" / "graphical shell fallback reported"
```

---

## 4. 数据流与运行逻辑

```text
输入 "shellgui"
  → window_count=2
  → windows[0]=(16,36,190,120, z=0, focused=1)   ← SHELL 终端窗
  → windows[1]=(216,36,104,120, z=1, focused=1)  ← STATUS 状态窗
  → a = framebuffer_rect(0,0,800,600,0x00101830)  全屏深蓝背景
  → b = framebuffer_rect(16,36,190,120,0x00203858) 终端窗底色
  → d = framebuffer_rect(216,36,104,120,0x00305070) 状态窗底色
  → canvas={0,0,0,0x00ffffff,0}
  → canvas_text(28,52,"SHELL")   在终端窗内写标题
  → canvas_text(28,76,"READY")
  → canvas_text(228,52,"STATUS") 在状态窗内写标题
  → canvas_text(228,76,"INIT")
  → a&&b&&d&&canvas.glyphs 全真
  → "shellgui: graphical terminal and system status panel linked to
     init/session metadata passed"
```

画面构成：深蓝底上，左窗（16..206, 36..156）为深蓝灰色块并写白色 SHELL/READY，
右窗（216..320, 36..156）为蓝色块并写白色 STATUS/INIT。

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

`make check` 输出：`Multiboot2 and Lesson 66 checks passed.`（README 必须含
`图形 Terminal 与安全命令 dispatcher`、`gui`、`Lesson 66`，缺一即失败）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：
`Lesson 66: 图形 shell 与系统状态面板\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n`

验证步骤（输出串从源码逐字）：

```bash
guiinfo
fonttest
canvastest
inputtest
desktest
```

预期：`ready/mapped: ...1/1` 与四条 `passed` 串（61–65 回归；`windowtest` 仍按
Lesson 64 精讲输出 fallback 串）。

```bash
shellgui
```

预期：`shellgui: graphical terminal and system status panel linked to init/session metadata passed`，
且图形窗口出现「深蓝背景 + 左 SHELL 终端窗（SHELL/READY 文字）+ 右 STATUS 状态
窗（STATUS/INIT 文字）」的画面。注意 S/T/U/Y 等字母显示为兜底短横字形是预期
行为（playbook §4）。

**如实说明**：旧 README 声称本课可「在 QEMU 中打开 Terminal，连续输入较长命令，
确认字符即时回显；再执行 help、about、guiinfo、mouseinfo、clear、shellrun」——
按源码事实，**本课没有图形 Terminal 输入路径**（`gui_term_*`/白名单 dispatcher
都在 Lesson 67）；本课的键盘仍走文本 console，`shellgui` 是静态绘制命令。

### 5.4 GUI 专项验收（QEMU VGA 自动化）

按教程要求 GUI 课程走专项流程（`docs/gui-debugging-playbook.md` §8 与
`learning-guide.md` §10.2）：单课验收
`scripts/qemu-vga-check.sh lessons/lesson-66-stable shellgui`；
第 4 阶段结课验收统一
`scripts/qemu-vga-check.sh lessons/lesson-67-stable guiinfo drawtest fonttest canvastest inputtest windowtest desktest shellgui`。

脚本对 `shellgui` 的硬性要求：PPM 截图必须**多色**（非纯黑）；VGA 文本 dump
不能含 `fallback reported`；`M` 字符计数 ≤20；全程无 `check_exception`/
`triple fault`。

### 5.5 课程实测记录（稳定快照）

旧 README 的学习路径（`make -C lessons/lesson-66-learning` +
`make -C lessons/lesson-66-learning check`）已验证；stable 快照复验：
`make check` 输出 `Multiboot2 and Lesson 66 checks passed.`；`shellgui` 输出
成功串，图形窗口可见 shell/status 双窗布局。构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `shellgui` 输出 `graphical shell fallback reported` | `a&&b&&d&&canvas.glyphs` 中一个为假：framebuffer 未 ready/mapped，或文字一个像素都没画（`glyphs==0`） | 先 `guiinfo`；再 `fonttest` 验证字体路径；逐个 `framebuffer_rect` 与 `canvas_text` 检查 |
| 文字出现短横而非字母 | S/T/U/Y 不在 5×7 字表（A–P），走兜底字形 | playbook §4：字体只可靠覆盖 A–P，属预期行为；`canvas.glyphs` 仍计数 |
| 文字画到窗口外 | 写死的 x/y 与窗口矩形不对齐 | `SHELL` 起点 28 在窗口 0（16..206），`STATUS` 起点 228 在窗口 1（216..320）；对照 §4 数据流 |
| 只有矩形没有文字 | `canvas` 未复位或 `canvas_text` 未调用 | 对照 `shellgui`：`canvas={0,0,0,0x00ffffff,0}` 之后才 `canvas_text` |
| 想在窗口里打字 | 本课**没有**图形 Terminal 输入路径（源码事实） | 输入/回显/白名单 dispatcher 属 Lesson 67（playbook §7） |
| 误以为成功串里的 "linked to init/session metadata" 是真数据联动 | 本课未读取 init/session 数据，措辞是设计意图 | 如实记录；数据联动语义由 Lesson 67 的 `gui_term_command` 落实 |
| `make check` 报错 | README 缺 `图形 Terminal 与安全命令 dispatcher`/`gui`/`Lesson 66` 之一 | 对照 Makefile `check` 三条 grep |
| 分不清「画面有 shell」与「shell 可交互」 | 本课只有静态画面 | **VGA 文本仍是权威诊断通道**：先看 `shellgui:` 文本标记与 PPM 多色证据（playbook §10） |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `shellgui` 的图形 shell 雏形（窗口+标题+状态文字） | 终端模拟器（xterm/gnome-terminal 等）与 `drivers/tty/vt/` 的虚拟终端 | 教学模型只有静态外观；Linux 有完整 tty/termios/输入回显 |
| 终端窗 + 状态面板两个 `window_model` | X11 的顶层窗口 + WM 状态栏 | 教学模型用固定坐标色块表达 |
| `canvas_text` 写 "SHELL"/"READY"/"STATUS"/"INIT" | 终端标题栏与 `stty` 状态行 | 教学模型用白名单文字占位，无真实终端状态机 |
| `a&&b&&d&&canvas.glyphs` 判据 | `drivers/tty/` 的写回显校验 | 教学模型用「矩形成功 + 画了像素」做可断言验证 |
| 本课无输入路径（交互在 67） | Linux 终端有完整输入队列/行编辑（`drivers/tty/n_tty.c`） | 教学模型把输入/回显/白名单拆到 lesson-67（playbook §7） |

**权威来源**：Linux `drivers/tty/`（终端与输入回显）作为工程对照；本课无新
硬件操作，不涉及 Intel SDM 新内容。

**教学模型简化了什么**：无行编辑（退格/光标）、无滚动、无输入回显、无白名单
dispatcher（全在 Lesson 67）；文字坐标写死、无布局引擎；「init/session
metadata 联动」只是成功串措辞；字体覆盖仅 A–P。

---

## 8. 思考题与练习

1. **概念理解**：`shellgui` 的判据为什么要 `canvas.glyphs` 而非只判断三个矩形？
   如果没有它，什么样的故障会被误判为成功？
2. **源码定位**：指出 "SHELL"/"READY"（终端窗）与 "STATUS"/"INIT"（状态窗）
   四段文字的 x/y 坐标分别落在哪个窗口矩形内；如果把 "STATUS" 的 x 改成 120
   会画到哪里？
3. **动手实验**：在 `shellgui` 里把 `canvas_text(28,76,"READY")` 的 y 从 76
   改成 156（窗口 0 下边缘），重建运行，观察文字越界后 `canvas.clipped` 与
   输出判据的变化，然后改回（勿提交）。
4. **Linux 对照**：为什么终端要有输入队列与行编辑（`n_tty`）？对照本课「画
   窗口但没有输入路径」的边界，列出 Lesson 67 实现图形 Terminal 至少需要的
   三件事。
5. **设计思考**：playbook §7 说「每个字符都设置 desktop_dirty 会导致整屏重绘、
   输入延迟」；如果 Lesson 67 要让打字快，为什么「只标记输入行 + 局部提交
   `framebuffer_present_rect()`」能解决？局部提交能消除撕裂吗（playbook §3）？

---

## 9. 本课小结与下一课预告

**小结**：本课把「图形 shell」从概念变成可见雏形：`shellgui` 复用 Lesson 62 的
`canvas_text`/`canvas_model` 与 Lesson 64 的 `window_model`，用背景 + 终端窗 +
状态窗三个矩形和 "SHELL"/"READY"/"STATUS"/"INIT" 四段文字拼出左右双窗布局，
判据 `a&&b&&d&&canvas.glyphs` 同时证明矩形与文字都被绘制。**关键事实**：本课
**没有**真正的图形 Terminal 输入路径——`gui_term_*`、`gui_term_input_dirty`、
`framebuffer_present_rect()` 局部刷新、白名单 dispatcher、`shell_window_open`
全部在 Lesson 67；S/T/U/Y 走兜底字形是预期；成功串里 "linked to init/session
metadata" 是设计措辞。本课是 GUI 主线通向「可交互桌面」的静态台阶。

**下一课预告**：[Lesson 67](../lesson-67-stable/README.md) 做跨层综合回归与
真实 QEMU 验收，补齐 scene/backbuffer、鼠标（IRQ12/PS2 AUX）、图标/双击、
图形 Terminal 输入与白名单 dispatcher——第 4 阶段在此结课。跨课程排错统一
参考 [`docs/gui-debugging-playbook.md`](../../docs/gui-debugging-playbook.md)。
