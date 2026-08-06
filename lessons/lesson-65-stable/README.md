# Lesson 65: scene/compositor 与 Xfce 风格桌面 — 精讲文档

> **课号**：Lesson 65（可执行课）
> **主题**：scene/compositor 与 Xfce 风格桌面；命令 `desktest`
> **课程主线位置**：第 4 阶段「图形桌面主线」（61–67）第五课。61–64 把
> framebuffer、字体画布、输入队列、窗口/控件模型都备齐了，本课把它们**画成
> 可见的桌面**：深色背景 + 顶部 panel + 底部 taskbar + 两个窗口矩形。GUI 主线
> 在 Lesson 67 结课。
> **前置课程**：[`lesson-64-stable/README.md`](../lesson-64-stable/README.md)
> **后续课程**：[`lesson-66-stable/README.md`](../lesson-66-stable/README.md)
> **一句话目标**：学完本课你能说清——`desktest` 如何复用 Lesson 64 的
> `window_model` 摆出两个窗口、用四个 `framebuffer_rect` 分层画出
> 背景/panel/taskbar/窗口、成功判据 `a&&b&&d&&e&&framebuffer.pixels` 为什么
> 是「五件事同时成立」，以及**为什么本课还没有真正的 scene/backbuffer/cursor
> 结构**（`framebuffer_scene`/`framebuffer_present_rect` 在 Lesson 67 才出现），
> 「Xfce 风格桌面」在本课只是分层矩形的视觉模拟。

---

## 1. 课程定位（Mission）

**一句话目标**：让屏幕上出现一个**可辨识的桌面布局**——深蓝背景、顶部 24px
panel、底部 24px taskbar、两个有明确边界的窗口矩形，且 `desktest` 输出
`bounded compositor background, taskbar, windows, and cursor ownership passed`
证明每个分层都绘制成功。

- **在课程主线中的位置**：第 4 阶段第五课，是「从模型到画面」的转折点：64 的
  窗口模型在此被真正画出来；66 课的图形 Terminal 窗口将出现在同样的布局里；
  67 课才补齐 scene 快照、backbuffer、光标与真实鼠标。
- **责任边界**（旧 README 责任边界原文）：本课**不实现**完整窗口拖动、resize、
  硬件 vsync 或用户态 Shell。无 page-flip 时整屏 present 仍可能出现扫描级撕裂；
  局部 present 只是减少提交范围。旧 README 声称的 `desktopinfo` 命令**在本课
  源码中不存在**（`grep` 为 0），属于 Lesson 67。
- **前置知识清单**：① Lesson 62 的 `framebuffer_rect`（本课全用它分层绘制）；
  ② Lesson 64 的 `struct window_model` 与 `windows[]`（本课复用并改夹具）；
  ③ 颜色 32bpp 打包（`0x00RRGGBB`）；④ 屏幕坐标（y 向下，底部 taskbar 用
  `height-24` 定位）。
- **本课交付**：新命令 `desktest`；可见的桌面分层画面；成功/回退两条输出串；
  `window_model` 夹具的二次使用（本次 `focused` 标志与 z 序一致）。

---

## 2. 核心概念精讲

### 2.1 概念一：compositor 的「分层绘制」思想

定义：桌面不是一张图，而是**多层矩形的叠加**：背景 → 顶部 panel → 底部
taskbar → 窗口。本课用四个 `framebuffer_rect` 从下到上逐层画进真实显存。

为什么需要：这是「合成（composite）」的最小教学版本——真实 compositor 维护
场景树、按 z 序把各窗口纹理合成到一帧。本课用「四个矩形 + 固定顺序」模拟它，
让「先画谁、后画谁」的顺序关系可见：窗口 1 在窗口 0 之后画，视觉上在上层。

### 2.2 概念二：panel 与 taskbar —— 布局的两个固定锚点

定义：顶部 panel 是 24px 高的全宽条带（`0x00305090U`，蓝紫），底部 taskbar
同样 24px（`0x00203050U`，深蓝），taskbar 的 y 坐标用
`framebuffer.height>32 ? framebuffer.height-24 : 0` 计算。

为什么需要：Xfce 风格桌面有「顶部菜单栏 + 底部任务栏」的固定布局。用全宽矩形
表达它们成本最低；`height>32?` 的三元式防止屏幕过矮时 taskbar 越界（有界纪律）。
playbook §6 强调「保留清晰的桌面布局：顶部 panel、底部 taskbar」——这是后续
窗口/图标视觉验收的基准。

### 2.3 概念三：窗口即矩形 —— 模型与绘制的对接

定义：`desktest` 先给 `windows[0]/[1]` 摆好几何（复用 `struct window_model`），
再把每个窗口画成单色矩形（`0x00406080U`/`0x00608050U`）。

为什么需要：64 课的窗口只有数据没有画面；本课把「窗口」变成「屏幕上一个有
颜色、有边界的矩形区域」。窗口内容（标题栏、文字）留到 66/67；本课证明的是
**窗口几何能被正确提交**。

### 2.4 概念四：为什么还没有真正的 scene/backbuffer/cursor

诚实说明（源码事实）：本课 `desktest` **直接写真实显存**（每个 `framebuffer_rect`
都是 LFB 写），没有 `framebuffer_scene` 快照、没有固定 stride 的 backbuffer、
没有光标结构。旧 README 提到的「scene 快照、cursor 恢复、backbuffer 所有权」
在源码里不存在——`framebuffer_scene`/`backbuffer`/`framebuffer_present_rect`/
`FRAMEBUFFER_MAX_WIDTH` 这些符号全部出现在 **Lesson 67**。

为什么需要说明：playbook §3 的「场景先绘制到固定大小 backbuffer、再按行提交」
「光标更新先从 scene 恢复、再绘制新光标」是为 lesson-67 写的纪律。本课四个
矩形一次性画完，肉眼看不到中间状态，但**架构上仍未解决**中间态与撕裂——成功
串里 "cursor ownership" 一词是设计意图，不是当前实现。

### 2.5 概念五：「Xfce 风格」是布局致敬，不是像素级还原

定义：本课只借用 Xfce 的「顶部 panel + 底部 taskbar + 桌面图标区」三元素布局，
用 4 个矩形表达；没有真实 Xfce 的圆角、渐变、图标贴图。

为什么需要：把「风格」降级为「布局 = 颜色块」，课程才能在没有位图资源的
freestanding 环境里做出可辨识的桌面。验收时按 playbook §8 检查「窗口是否完整、
底部是否截断」，而不是像素级还原。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-64） |
|---|---|---|
| `boot.S` | 引导 | 未变化 |
| `kernel.c` | 32 位阶段 | **未变化**（`diff` 为空） |
| `kernel64.c` | 64 位内核主体 | **核心**：`desktest` 命令 + `exec64` 分支 + `about`/横幅更新；复用 64 的 `window_model` |
| `kernel64.ld` / `linker.ld` | 布局 | 未变化 |
| `Makefile` | 构建 | `check` grep 改为 `scene/compositor 与 Xfce 风格桌面`/`Lesson 65`（`gui` 保留） |
| `grub.cfg` | GRUB 菜单 | menuentry 标题更新为 lesson-65 主题 |

### 3.2 kernel64.c —— desktest：桌面分层绘制（本课全部增量）

命令 `desktest`（源码逐字）：

```c
static TEXT64 void desktest(u16*c){
    int a,b,d,e;
    window_count=2;
    windows[0]=(struct window_model){16,40,140,90,0,1,0,1};
    windows[1]=(struct window_model){176,48,144,96,1,1,1,1};
    a=framebuffer_rect(0,0,framebuffer.width,framebuffer.height,0x00101830U);
    b=framebuffer_rect(0,0,framebuffer.width,24,0x00305090U);
    d=framebuffer_rect(0,framebuffer.height>32?framebuffer.height-24:0,
        framebuffer.width,24,0x00203050U);
    e=framebuffer_rect(windows[0].x,windows[0].y,windows[0].w,windows[0].h,
        0x00406080U)&&framebuffer_rect(windows[1].x,windows[1].y,
        windows[1].w,windows[1].h,0x00608050U);
    text64(c,"desktest: ");
    text64(c,a&&b&&d&&e&&framebuffer.pixels?
        "bounded compositor background, taskbar, windows, and cursor ownership passed":
        "desktop fallback reported");
    putc64(c,'\n'); }
```

分层拆解：
1. **夹具**：`window_count=2`；`windows[0]` 在 (16,40) 起 140×90（z=0，
   visible=1，focused=0，dirty=1）；`windows[1]` 在 (176,48) 起 144×96
   （z=1，visible=1，**focused=1**，dirty=1）。注意这次 `focused` 标志与
   z 序一致（顶层窗口 focused=1），与 lesson-64 的 windowtest 夹具不同；
2. **a 背景**：全屏 `0x00101830U`（R=0x10,G=0x18,B=0x30 深蓝）；
3. **b 顶部 panel**：全宽 × 24px `0x00305090U`（蓝紫）；
4. **d 底部 taskbar**：`framebuffer.height>32?` 判高后从 `height-24` 起画
   24px `0x00203050U`（深蓝）——矮屏防越界；
5. **e 两窗口**：先画窗口 0 `0x00406080U`（灰蓝），再 `&&` 画窗口 1
   `0x00608050U`（灰绿），两个都成功 e 才为真；
6. **判据**：`a&&b&&d&&e&&framebuffer.pixels`——四个分层都成功**且**确实有
   像素被写过（`framebuffer.pixels` 是 Lesson 61 的累计计数器），五件事同时
   成立才输出成功串。

关键设计点：
- 绘制顺序 = 视觉层级：背景→panel→taskbar→窗口 0→窗口 1，后画者覆盖先画者；
- 所有矩形都走 `framebuffer_rect` 的 u64 溢出防护与裁剪（Lesson 61/62 纪律）；
- `d` 的 `height>32` 保护：屏幕高度小于 32px 时 taskbar 从 0 起画（本课屏幕
  600px，恒走 `height-24` 分支）；
- 成功串里的 "cursor ownership" 是**设计意图的措辞**，本课没有任何光标代码
  （源码事实）——如实记录，避免把 67 课的承诺当成现状。

`exec64` 新分支（源码逐字）：

```c
else if(eq64(word,"desktest")){if(!noargs64(arg))usage64(c,"desktest");else{desktest(c);}}
```

- 插在 `windowtest` 之后、`resourceinfo` 之前；`guiinfo`~`windowtest` 全部保留
  （回归）；
- `windowtest` 保留但**夹具问题仍在**（见 Lesson 64 精讲：当前夹具输出 fallback
  串）；`desktest` 是新的、可成功的桌面绘制命令。

横幅与 `about`（源码逐字）：

```c
text64(&c,"Lesson 65: 桌面 compositor 与窗口管理器\nGETTICKS, GETPID,
WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
/* about: text64(c,"Lesson 65: 桌面 compositor 与窗口管理器\n"); */
```

- 主循环与 64 相同，仅横幅更新。

### 3.3 构建管线（Makefile / kernel64.ld / grub.cfg）

- 编译/链接流程与 61–64 相同（`kernel64.o` → `kernel64.bin` → `boot.S incbin`
  → 外层 i386 ELF → `grub-mkrescue`）；
- `check`（本课）：`grub-file --is-x86-multiboot2` + 三条 grep：README 含
  `scene/compositor 与 Xfce 风格桌面`、`gui`、`Lesson 65`；通过打印
  `Multiboot2 and Lesson 65 checks passed.`；
- `run`：`qemu-system-x86_64 -accel tcg -vga std ...`（与 64 相同）；
- `grub.cfg`：`gfxmode/gfxpayload=800x600x32` 不变，menuentry 更名；
- `kernel64.ld`/`linker.ld` 未变。

### 3.4 主控制流

```text
GRUB → _start → kernel_main32（未变）→ 长模式 → kernel64.bin
  ├─ framebuffer_init(h)：ready/mapped（Lesson 61）
  ├─ 横幅 "Lesson 65: 桌面 compositor 与窗口管理器\n..."
  └─ 键盘循环 → exec64:
        desktest → 摆 2 个 window_model
                 → a=背景   b=顶部 panel  d=底部 taskbar
                 → e=窗口0 && 窗口1（先 0 后 1）
                 → 判据 a&&b&&d&&e&&pixels
                 → "bounded compositor background, taskbar, windows, and
                    cursor ownership passed" / "desktop fallback reported"
```

---

## 4. 数据流与运行逻辑

```text
输入 "desktest"
  → window_count=2
  → windows[0]=(16,40,140,90, z=0, visible=1, focused=0)
  → windows[1]=(176,48,144,96, z=1, visible=1, focused=1)
  → a = framebuffer_rect(0,0,800,600,0x00101830)   全屏深蓝背景
  → b = framebuffer_rect(0,0,800,24, 0x00305090)   顶部蓝紫 panel
  → d = framebuffer_rect(0,576,800,24,0x00203050)  底部深蓝 taskbar
  → e = framebuffer_rect(16,40,140,90,0x00406080)  窗口0 灰蓝
      && framebuffer_rect(176,48,144,96,0x00608050) 窗口1 灰绿
  → a&&b&&d&&e&&framebuffer.pixels 全真
  → "desktest: bounded compositor background, taskbar, windows, and cursor ownership passed"
```

画面构成（800×600）：深蓝底上，顶部 0–23 行蓝紫条带，底部 576–599 行深蓝条带，
(16,40) 处灰蓝矩形、(176,48) 处灰绿矩形——一个「桌面三件套 + 两窗口」的布局。

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

`make check` 输出：`Multiboot2 and Lesson 65 checks passed.`（README 必须含
`scene/compositor 与 Xfce 风格桌面`、`gui`、`Lesson 65`，缺一即失败）。

### 5.3 运行与交互验证

```bash
make run
```

**成功画面在 QEMU 图形窗口，勿加 `-display none`。** 开机横幅（源码逐字）：
`Lesson 65: 桌面 compositor 与窗口管理器\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n`

验证步骤（输出串从源码逐字）：

```bash
guiinfo
fonttest
canvastest
inputtest
```

预期：`ready/mapped: ...1/1` 与三条 `passed` 串（61–63 回归）。

```bash
desktest
```

预期：`desktest: bounded compositor background, taskbar, windows, and cursor ownership passed`，
且图形窗口出现「深蓝背景 + 顶部蓝紫 panel + 底部深蓝 taskbar + 两个彩色窗口
矩形」的桌面布局；目视确认顶部/底部/窗口边界没有被截断（playbook §8）。

```bash
windowtest
```

预期：**按 Lesson 64 精讲的源码推导**，当前夹具输出
`windowtest: window model fallback reported`（成功串需先修夹具，勿提交）。

注意：旧 README 声称的 `desktopinfo` 命令**本课不存在**（源码事实），属于
Lesson 67；本课的桌面诊断以 `desktest` 与目视检查为准。

### 5.4 GUI 专项验收（QEMU VGA 自动化）

按教程要求 GUI 课程走专项流程（`docs/gui-debugging-playbook.md` §8 与
`learning-guide.md` §10.2）：单课验收
`scripts/qemu-vga-check.sh lessons/lesson-65-stable desktest`；
第 4 阶段结课验收统一
`scripts/qemu-vga-check.sh lessons/lesson-67-stable guiinfo drawtest fonttest canvastest inputtest windowtest desktest shellgui`。

脚本对 `desktest` 的硬性要求：PPM 截图必须**多色**（非纯黑，地址/stride 损坏
不能伪装成 GUI 输出）；VGA 文本 dump 不能含 `fallback reported`；`M` 字符计数
≤20（防 legacy VGA corruption marker）；全程无 `check_exception`/`triple fault`。

### 5.5 课程实测记录（稳定快照）

旧 README 的学习路径（`make -C lessons/lesson-65-learning` +
`make -C lessons/lesson-65-learning check`）已验证；stable 快照复验：
`make check` 输出 `Multiboot2 and Lesson 65 checks passed.`；`desktest` 输出
成功串，图形窗口可见桌面分层布局。构建产物未改动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `desktest` 输出 `desktop fallback reported` | `a&&b&&d&&e&&pixels` 中一个为假：framebuffer 未 ready/mapped 或某矩形被裁剪 | 先 `guiinfo` 看 `ready/mapped`；逐个 `framebuffer_rect` 检查坐标/尺寸（尤其 `height-24`） |
| 顶部或底部条带缺一半 | panel/taskbar 用错坐标或高度 | `b` 是 `(0,0,width,24)`，`d` 是 `(0,height-24,width,24)`；对照源码逐字段 |
| 窗口被背景盖住 | 绘制顺序错误（背景画在窗口之后） | `desktest` 顺序固定：a→b→d→e，后画者在上 |
| taskbar 在矮屏越界 | `height>32?` 保护分支没生效 | 该三元式在高度≤32 时用 y=0；本课 600px 恒走正常分支 |
| 画面出现条纹/中间态 | 本课**直写 LFB**，无 backbuffer/scene（源码事实） | 接受为当前限制；backbuffer 与 scene 由 Lesson 67 落实（playbook §3） |
| 想用 `desktopinfo` 诊断 | 本课没有该命令（`grep`=0） | 属 Lesson 67；本课用 `desktest` 输出与目视 |
| 分不清「布局正确」与「光标可用」 | 成功串里的 "cursor ownership" 是设计措辞，本课无光标代码 | **VGA 文本仍是权威诊断通道**：先看 `desktest:` 文本标记与 PPM 多色证据（playbook §10） |
| `make check` 报错 | README 缺 `scene/compositor 与 Xfce 风格桌面`/`gui`/`Lesson 65` 之一 | 对照 Makefile `check` 三条 grep |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 实现 | 说明 |
|---|---|---|
| `desktest` 分层矩形（背景/panel/taskbar/窗口） | `drivers/gpu/drm/` 的 compositor 与 `drm_plane`/`drm_connector` 合成 | 教学模型用固定顺序矩形叠加；Linux 是原子提交的 plane 合成 |
| 顶部 panel/底部 taskbar 全宽条带 | 真实桌面 shell（Xfce `xfce4-panel`）的 panel 窗口 | 教学模型用单色矩形占位，无真实 panel 进程 |
| `framebuffer.height>32?` 的 taskbar 越界保护 | DRM 的 mode 校验与 `drm_mode_set_crtcinfo` | 教学模型用三元式做最小边界；Linux 有完整 mode validation |
| 窗口 = 单色矩形 | DRM 的 framebuffer + plane 显示窗口内容 | 教学模型无窗口纹理/内容，只有几何色块 |
| 直写 LFB（无 backbuffer） | DRM 的 `drm_fb`/`fbdev` 的 `fb_pan_display` 与 page-flip | 本课未引入缓冲策略；lesson-67 才补 scene/backbuffer |
| `window_model` 复用（64 课结构） | X11 `Window`/`XVisualInfo` 的几何属性 | 教学模型把窗口压成 8 字段静态表 |

**权威来源**：Linux `drivers/gpu/drm/`（plane/compositor 概念）作为工程对照；
本课无新硬件操作，不涉及 Intel SDM 新内容。

**教学模型简化了什么**：无场景树与原子提交；无窗口内容（只有色块）；无真正
panel/taskbar 进程；无 vsync/page-flip（整屏 present 仍可能有扫描级撕裂，
playbook §3）；无光标/backbuffer/scene——这些全部在 Lesson 67；"Xfce 风格"
只是布局致敬。

---

## 8. 思考题与练习

1. **概念理解**：`desktest` 为什么把窗口 0 画在窗口 1 之前？如果交换顺序，视觉
   上会发生什么？`e` 的表达式中 `&&` 顺序对绘制顺序有没有影响？
2. **源码定位**：指出 `d`（taskbar）的 `framebuffer.height>32? ... :0` 三元式
   在两种分支下分别画在哪里；本课 600px 屏走哪个分支？
3. **动手实验**：把 `b` 的 panel 高度从 24 改成 48，重建运行 `desktest`，观察
   顶部条带变厚且是否盖住窗口 0 的顶部，然后改回（勿提交）。
4. **Linux 对照**：为什么真实 compositor 要先渲染到离屏 buffer 再一次性提交，
   而不是像本课直接写屏？对照 playbook §3 的「条纹、黑闪、撕裂」根因。
5. **设计思考**：成功串里写着 "cursor ownership"；如果 Lesson 67 要给桌面加
   光标，`desktest` 之后的光标绘制需要哪些额外步骤（恢复场景 → 画新光标 →
   局部提交）？为什么不能直接画？

---

## 9. 本课小结与下一课预告

**小结**：本课把 61–64 的成果合成成可见桌面：`desktest` 复用
`struct window_model` 摆出两个窗口（本次夹具 `focused` 与 z 序一致），用四个
`framebuffer_rect` 按「背景→panel→taskbar→窗口」顺序分层直写 LFB，成功判据
`a&&b&&d&&e&&framebuffer.pixels` 五件事同时成立，输出
`bounded compositor background, taskbar, windows, and cursor ownership passed`。
**关键事实**：本课**没有** scene 快照、backbuffer 或光标结构——它们与
`framebuffer_scene`/`framebuffer_present_rect` 一起在 Lesson 67 才出现；旧的
`desktopinfo` 命令也不在本课；成功串里的 "cursor ownership" 是设计措辞。无
page-flip 时整屏 present 仍可能有撕裂，这是本课的已知边界。

**下一课预告**：[Lesson 66](../lesson-66-stable/README.md) 在桌面上加入图形
Terminal 与安全命令 dispatcher，命令 `shellgui` 及图形 shell——届时窗口不再
是色块，而是能回显字符、能接收命令的终端。跨课程排错统一参考
[`docs/gui-debugging-playbook.md`](../../docs/gui-debugging-playbook.md)。
