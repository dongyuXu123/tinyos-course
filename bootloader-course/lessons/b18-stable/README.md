# Lesson B18: menuentry 菜单与 timeout/default — 精讲文档

> **课号**：Lesson B18（Mini-GRUB 从零写 GRUB 课程第 18 课，阶段四收尾）
> **主题**：启动菜单：menuentry 列表、timeout 倒计时、default 选择、执行选中项
> **课程位置**：阶段四「配置与交互」第 3 课
> **前置课程**：[`b17-stable/README.md`](../b17-stable/README.md)（grub.cfg 解析执行）
> **后续课程**：[`b19-stable/README.md`](../b19-stable/README.md)（模块系统）
> **一句话目标**：loader 显示一个可选启动菜单，超时后自动选择 default 项——
> TinyOS 主线的 `grub.cfg`（timeout=0/default=0）能原样跑通。

---

## 1. 课程定位（Mission）

**一句话目标**：学完本课，你的 loader 有真正的菜单界面：多条目展示、上下键
选择、回车执行、`timeout` 倒计时后自动执行 `default`。

- **在课程中的位置**：研读支线 0.10 的时序里，GRUB 读 cfg → `menuentry` →
  `timeout=0` 自动选中 → 执行 `multiboot2` + `boot`。B17 识别了块，本课实现
  菜单语义，让 TinyOS 主线的 `grub.cfg` 原样工作。对照 `menuentry.c` +
  `normal/menu.c` + `normal/main.c`。
- **前置知识清单**：
  1. B17：tokenizer/块识别；B16：命令执行；
  2. B15：装载链（选中项最终执行 `multiboot2`+`boot`）；
  3. PIT（8254）：倒计时时钟源。
- **本课交付**：`build/b18.img`（timeout=3 交互菜单）与 `build/b18-t0.img`
  （timeout=0 自动启动）；QEMU 启动显示菜单，上下键选择、回车执行、
  超时自动启动 default。

---

## 2. 核心概念精讲

### 2.1 概念一：菜单数据与块收集

B17 遇到 `menuentry` 只是跳过块体；本课把块体**收集**起来：

```c
struct menu_entry { char title[TITLE_MAX]; char body[BODY_MAX]; u32 body_len; };

/* 执行器看到 menuentry "TITLE" { 时： */
menu_add(argv[1]);            /* 注册 { title, 空 body } */
collecting_menu = 1;          /* 后续行进入收集状态 */

/* 收集状态：块体行追加到最后一个菜单项的 body（'}' 结束收集） */
menu_collect_line(l);
```

这样 cfg 执行完后，`menu_entries[]` 里存着 {标题, 块体文本}——执行选中项
就是把它的块体逐行交给执行器（块内是 `multiboot2` + `boot`）。

### 2.2 概念二：timeout / default 语义

`set timeout=N`（秒）、`set default=K`（第 K 项）由环境变量读取。GRUB 语义：

- **timeout=0**：不显示菜单，立即执行 default（TinyOS 主线 grub.cfg 就是这样）；
- **timeout=N>0**：显示菜单 + 倒计时；任意按键**暂停**倒计时（此后上下键
  导航、回车执行）；超时自动执行 default；
- **default 越界**：回退到第 0 项。

### 2.3 概念三：PIT 倒计时

倒计时用 **8254 PIT 通道 0**（真实时间，不依赖 CPU 速度）：

```c
outb(PIT_CMD, 0x34);              /* ch0, mode 2, 先低后高字节 */
outb(PIT_DATA0, 11932 & 0xFF);    /* 1193182/100 ≈ 11932 → 100 Hz */
outb(PIT_DATA0, 11932 >> 8);

/* 等待 n 个 wrap：计数从 11932 递减到 1 后重载，cur > prev 即一次 wrap */
static void delay_ticks(u32 n) { ... }
```

每个 wrap ≈ 10ms，`delay_ticks(100)` = 1 秒。开发中踩过
`delay_ticks(10)`（100ms）当成 1 秒用——timeout=3 只等 300ms 就自动启动。

### 2.4 概念四：方向键（PS/2 扩展键）

上下键是 PS/2 **扩展键**：`0xE0` 前缀 + 扫描码（0x48=上、0x50=下）。B16
的 `kbd_getc` 丢弃扩展键；本课返回 KEY_UP/KEY_DOWN 供菜单导航，行输入
`kbd_getline` 则跳过这些控制键。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 相对 B17 的增量 |
|---|---|---|
| `stage1.S`/`stage2.S`/`linker.ld` | El Torito 引导 + BIOS 回调 | 消息文本变化 |
| `loader.c` | 菜单数据/渲染/倒计时/选择执行 + PIT + 方向键 | 重写菜单部分 |
| `script.c`/`test_script.c` | tokenizer（B17） | 不变 |
| `grub.cfg` | timeout=3 双 menuentry | 新增 |
| `grub-t0.cfg` | timeout=0 变体 | 新增 |
| `build/b18.img`/`b18-t0.img` | 双镜像 | 新增 |

### 3.2 菜单运行循环

```c
void menu_run(void)
{
    timeout = atoi(env_get("timeout") ?: "0");   /* 缺省 0 */
    default_idx = atoi(env_get("default") ?: "0");
    if (timeout == 0) { menu_execute(default_idx); return; }   /* 立即启动 */

    pit_init_100hz();
    sel = default_idx; remaining = timeout;
    menu_render(sel, remaining);
    for (;;) {
        if (kbd_has_data()) {           /* 非阻塞检查 */
            c = kbd_getc();
            if (c == KEY_UP)   { sel = (sel + menu_count - 1) % menu_count; render; }
            if (c == KEY_DOWN) { sel = (sel + 1) % menu_count; render; }
            if (c == '\n')     { menu_execute(sel); return; }
            /* 其他键：暂停倒计时（GRUB 行为） */
        } else {
            delay_ticks(100);           /* 1 秒 */
            if (remaining) remaining--;
            menu_render(sel, remaining);
            if (remaining == 0) { menu_execute(default_idx); return; }
        }
    }
}
```

`menu_execute(idx)`：`vga_clear()` 后（GRUB 行为——执行输出覆盖菜单）逐行
执行选中项的块体。

### 3.3 双镜像（Makefile）

```text
grub.cfg   (timeout=3) -> cdroot/  -> b18.img   交互菜单
grub-t0.cfg (timeout=0) -> cdroot-t0/ -> b18-t0.img  自动启动
```

两个 CD 树只差 grub.cfg；`make check` 对两个 cfg 都做 `grub-script-check`
与抽取字节对照。

---

## 4. 数据流与运行逻辑

```text
SeaBIOS -> stage1(读 core) -> stage2 -> loader_main:
  挂载 (cd0) -> 注册命令 -> script_run_file("/boot/grub/grub.cfg"):
    set timeout=3 / set default=0 / echo
    menuentry "Mini-GRUB Test Kernel (default)" { ... }   # 收集块体
    menuentry "Second entry (echo only)" { ... }          # 收集块体
  -> menu_run():
    timeout=3 -> 渲染菜单 + 倒计时 3..2..1
      按键: 上/下切换高亮, 回车执行选中项
      超时: 执行 default (第 0 项)
    menu_execute(0): 清屏 -> 块体逐行执行:
      multiboot2 /boot/kernel.elf -> boot -> 内核接管
```

交互场景（QEMU + sendkey 实测）：

```
B18 menu: Mini-GRUB boot menu
B18 menu: use up/down + enter, or wait for timeout
B18 menu: --------------------------------
B18 menu: > Mini-GRUB Test Kernel (default)
B18 menu:   Second entry (echo only)
B18 menu: boot in 02s ...          ← 倒计时
  （按 down + enter）
B18 menu: executing entry Second entry (echo only)
B18: second entry selected
```

timeout=0 变体（对齐 TinyOS）：不显示菜单，直接
`B18 multiboot2: loaded ...` → `B18 boot: jumping ...` → 内核接管。

---

## 5. 构建、运行与验证

### 5.1 命令

```sh
make              # 构建 b18.img + b18-t0.img
make check        # menu_add/menu_run 符号、2+ menuentry、双 cfg 校验
make run          # QEMU 交互菜单（timeout=3，可上下键选择）
make run-t0       # QEMU timeout=0 自动启动
./scripts/validate-course.sh b18 check
./scripts/validate-course.sh b18 qemu    # 用 b18-t0.img 自动启动校验
```

### 5.2 成功判据

1. `make check` 全绿：`menu_add`/`menu_run` 符号、`grub.cfg` ≥ 2 menuentry、
   `grub-script-check` 双 cfg、抽取字节一致；
2. b18-t0.img：无需按键自动启动 default（timeout=0 对齐 TinyOS）；
3. b18.img：菜单显示 + 高亮、上下键切换、回车执行选中项、倒计时到期
   自动启动 default（手动/脚本 sendkey 验证）。

---

## 6. 调试地图

1. **倒计时过快**：`delay_ticks(10)` = 100ms 被当成 1 秒——timeout=3 只等
   300ms。PIT 每个 wrap 是 10ms，1 秒要 100 个 wrap。
2. **菜单一闪而过**：倒计时瞬时完成导致菜单不可见——先在菜单开始处打印
   timeout 与 PIT 采样确认数值（本课调试过程：先确认 timeout=03 正确，
   再用 6 个采样确认 PIT 在 11932 递减回绕，最后定位到 delay 单位）。
3. **`menu_execute` 清屏**：GRUB 行为是执行前清屏——菜单、cfg echo 都会被
   覆盖，验证 marker 要选内核接管后仍可见的行（multiboot2/boot 输出行）。
4. **PS/2 扩展键**：方向键是 `0xE0` 前缀的扩展键，直接按扫描码映射会
   把 `0xE0` 当普通键；行输入要跳过控制键（< 0x20）。

---

## 7. 与 GNU GRUB 源码对照

| 本课实现 | GRUB 对照 | 差异说明 |
|---|---|---|
| `menu_add`/块收集 | `commands/menuentry.c` | 固定槽位 vs 动态分配 |
| `menu_run` 选择循环 | `normal/menu.c` 的 menu 循环 | 无子菜单/搜索/颜色 |
| timeout/default | `normal/main.c`（grub_show_menu） | 语义一致（0=立即、N=倒计时） |
| PIT 倒计时 | `kern/i386/pc/...`（GRUB 用计时器抽象） | 直接驱动 8254 |
| 方向键扩展键 | `kern/i386/pc/...`（PS/2 驱动） | 最小映射 |

---

## 8. 思考题与练习

1. 给菜单加"按键暂停后不再自动启动"的状态显示（GRUB 暂停后倒计时停止且
   不再恢复，除非按 Esc 重新开始）。
2. 实现 `menuentry --id="name"`：给菜单项加标识符，`default` 支持按 id
   而非序号选择。
3. PIT 的 wrap 检测假设计数递减；如果换用 mode 3（方波）或不同 reload，
   检测逻辑要改吗？
4. 倒计时期间按 Esc 回到菜单（从"已暂停"恢复倒计时）——对照 GRUB 的
   `GRUB_MENU_QUICK_BOOT` 相关行为。
5. 把 `menu_render` 改成在固定区域重绘（不清全屏），消除闪烁，并处理
   超过一屏的菜单项。

---

## 9. 本课小结与下一课预告

**小结**：本课给 menuentry 块真实语义——菜单数据（标题+块体收集）、
timeout/default（0=立即、N=倒计时）、PIT 倒计时、PS/2 方向键导航、选中项
块体执行。`timeout=0` 对齐 TinyOS 主线 grub.cfg 无需按键自动启动；交互
菜单的上下键/回车/倒计时全部实测通过。阶段四完成：配置即脚本 + 交互菜单。

**下一课** [`b19-stable/README.md`](../b19-stable/README.md)：**阶段五**开始：
GRUB 的功能大多在 `.mod` 模块里按需加载——B19 定义并实现 Mini-GRUB 自己的
模块格式与加载器，让 `multiboot2`/`iso9660` 等命令以模块形式注册。
