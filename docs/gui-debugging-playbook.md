# TinyOS GUI 调试经验手册

> 适用范围：Lesson 61–67。GUI 课程在 Lesson 67 结课，Lesson 68 起回到进程组与 session 主线。

这份手册记录 TinyOS 图形桌面调试中反复出现的问题、根因、修复方式和可观察证据。它不是某一课的实现说明，而是后续课程、回归测试和新硬件适配的共同排错入口。

## 1. 先固定课程边界

| 课程 | 责任 | 不负责的内容 |
|---|---|---|
| 61 | GRUB/Multiboot2 framebuffer handoff、映射和安全 fallback | 字体、窗口和鼠标交互 |
| 62 | 像素格式、backbuffer、scene、bitmap font、canvas | 设备输入和窗口策略 |
| 63 | 键盘、IRQ12 PS/2 AUX 鼠标和有界事件队列 | 窗口命中和 compositor |
| 64 | icon/window/widget 模型、hit-test、focus、z-order、双击状态 | 真实显存提交策略 |
| 65 | scene/compositor、panel、taskbar、cursor 和 dirty 状态 | 完整用户态 Shell |
| 66 | 图形 Terminal、有界输入和安全命令 dispatcher | 任意用户程序执行、VGA 专用后端 |
| 67 | 跨层综合回归和真实 QEMU 验收 | Lesson 68 之后的 GUI 扩展 |

推荐严格按 61 → 62 → 63 → 64 → 65 → 66 → 67 排错；不要在 handoff 未证明时直接修改窗口或鼠标代码。

## 2. 黑屏、`ready/mapped=0/0` 与 framebuffer fallback

### 现象

- QEMU 只有 VGA 文本或黑屏；
- `guiinfo` 显示 framebuffer 未 ready/mapped；
- GUI 命令看似通过，但屏幕没有桌面。

### 根因

- GRUB 没有稳定保留 `800x600x32` 的 graphics payload；
- Multiboot2 framebuffer tag 未做完整边界、格式和溢出校验；
- 猜测 `0xfd000000`、`0xfc000000` 等固定显存地址；
- framebuffer 物理页没有同时纳入高半区页表和 PMM 保留范围。

### 修复

- `grub.cfg` 明确设置 `gfxmode=800x600x32`、`gfxpayload=800x600x32`；
- 32 位阶段严格检查地址、pitch、宽高、32bpp、RGB type、总大小和地址加法溢出；
- 记录 `framebuffer_phys_base` 与 `framebuffer_map_offset`；
- 同时建立低地址和高半区映射，并保留 framebuffer 物理页；
- 无效 handoff 时通过 Bochs PCI BAR 获取 LFB，绝不猜地址；
- 不满足条件就安全退回 VGA，而不是写未知内存。

### 证据

先运行 `guiinfo`，确认 `ready/mapped: 1/1`、尺寸、pitch、bpp 和 RGB mask；再运行 `drawtest`，确认出现 `passed`。只有这两类证据同时成立，才开始排查绘图和窗口。

## 3. 条纹、花屏、黑闪和撕裂

### 现象

- 画面出现重复条纹、颜色错位或大面积黑块；
- 鼠标移动时整屏黑闪；
- 窗口更新时出现扫描方向的撕裂。

### 根因

- 绘制过程中直接写真实 framebuffer，用户看到了中间状态；
- backbuffer 使用固定 stride，却按 `width*height` 做线性复制；
- scene 快照没有按行恢复；
- 光标旧区域和新区域被多次直接提交；
- 当前没有硬件 vsync/page-flip，整屏 present 本身可能被扫描看到。

### 修复

- 场景先绘制到固定大小 backbuffer，再按行提交；
- backbuffer 与 scene 都按 `y * FRAMEBUFFER_MAX_WIDTH + x` 访问；
- 光标更新先从 scene 恢复，再绘制新光标；
- 输入行使用 `framebuffer_present_rect()`，只提交局部矩形；
- 不把“局部 present”误认为硬件 page flip：它减少工作量，但不能保证无撕裂。

## 4. 字体和布局

5×7 bitmap font 是有界教学字体，不是完整 Unicode 字体。当前只可靠覆盖 A–Z；数字、标点和非 ASCII 需要明确验证，不能把乱码当作 framebuffer 故障。

`canvas_text()` 必须保留字符上限和边界裁剪。Terminal 的 prompt、历史行和输入行应共享固定布局，例如历史行按固定行高递增，输入字符串从 prompt 后的固定 x 开始；不能把 `line_count` 错用于 x 坐标。输入缓存上限、可见字符上限和窗口宽度必须分别记录。

## 5. 鼠标无反应、消失或到不了左上角

### 排查规则

1. QEMU 默认提供 i8042/PS2 鼠标；不要添加不存在的 `-device ps2-mouse`。
2. IRQ12 路径负责从 `0x60` 读取 AUX 数据；轮询路径只做在线恢复，不能与 IRQ 同时取数据。
3. 第一字节必须有同步位 `0x08`；解析 X/Y 符号位和 overflow 位；发生 overflow 时丢弃位移。
4. 坐标更新后裁剪到 `[0,width-1] × [0,height-1]`，允许到达 `(0,0)`。
5. 逻辑 hotspot 与视觉箭头主体可能不同，验收时分别检查坐标和光标外观。

`icontest` 只证明固定模型状态转换；真实鼠标验收必须在 QEMU 中使用 `mouse_move`/`mouse_button` 或物理鼠标观察屏幕。

## 6. 窗口、图标和桌面误判

保留一个清晰的桌面布局：顶部 panel、底部 taskbar、SHELL 图标和 Terminal 窗口。若不需要 Settings/Status 窗口，应从 `window_count`、可见标志、z-order 和绘制路径同时移除，不能只隐藏标题。

单击可改变选中状态；有界双击应在明确 tick 窗口内打开或聚焦 Terminal。scene 快照必须在光标绘制前完成，恢复顺序应为“scene → 新桌面变化 → cursor”，否则会留下旧光标或覆盖输入内容。

## 7. 图形 Shell 输入慢

### 根因

最初每个字符和退格都设置 `desktop_dirty`，主循环于是重绘 panel、图标、窗口并复制完整 framebuffer。输入逻辑本身很快，延迟来自错误的重绘粒度。

### 修复

- 增加 `gui_term_input_dirty`；普通字符和退格只标记输入行；
- 从无光标 `framebuffer_scene` 恢复输入行背景；
- 重绘固定 prompt 和有界输入字符串；
- 通过 `framebuffer_present_rect()` 提交输入行矩形；
- Enter 才触发一次完整终端内容重绘。

GUI Shell 使用安全白名单 dispatcher，不能直接复用会写 VGA 光标和 VGA 显存的完整 `exec64()`。会触发异常、停机或破坏回归环境的测试命令不暴露给图形 Terminal。

## 8. QEMU 验证手册

### 构建与启动

```bash
make -C lessons/lesson-67-learning
make -C lessons/lesson-67-learning check
qemu-system-x86_64 -accel tcg -vga std -boot order=d \
  -cdrom lessons/lesson-67-learning/build/kernel.iso \
  -serial stdio -no-reboot -no-shutdown
```

稳定版验证后恢复构建目录只读：

```bash
chmod -R u+w lessons/lesson-67-stable/build
make -C lessons/lesson-67-stable
make -C lessons/lesson-67-stable check
chmod -R a-w lessons/lesson-67-stable/build
```

### Monitor 和屏幕证据

使用持久 monitor socket 时，可发送：

```text
screendump /tmp/tinyos.ppm
mouse_move 0 0
mouse_move 40 100
mouse_button 1
mouse_button 0
sendkey h
sendkey e
sendkey l
sendkey p
sendkey ret
```

PPM 不应只检查 `P6` 或“存在非黑像素”，还要检查 header、`800x600`、maxval、payload 精确长度、多点颜色和明显的 legacy VGA corruption marker。串口/VGA marker、`passed`、异常和 triple fault 检查与截图互相补充，不能互相替代。

`icontest`、`desktest` 等确定性测试只能证明模型；真实 GUI 验收必须检查窗口是否完整、底部是否截断、鼠标是否到左上角、双击是否打开 Terminal、输入是否即时回显，并记录镜像路径、日期和截图证据。

## 9. 推荐排错顺序与禁止事项

1. handoff tag 和 `ready/mapped`；
2. 单像素、矩形和 RGB packing；
3. backbuffer、scene stride 和 present；
4. 键盘/IRQ12/鼠标边界；
5. icon/window hit-test；
6. compositor、cursor 和 dirty 路径；
7. GUI Shell 局部输入和命令 dispatcher。

禁止：猜测显存地址；让轮询和 IRQ 同时读取 AUX；把模型测试当物理验收；把未验证截图当成功证据；在 stable build 留下可写状态；在 GUI 路径执行 VGA 专用输出后端；把任意用户输入当作可执行代码或任意指针。

## 10. 当前限制和后续边界

当前实现固定 framebuffer 最大 `1024x768`、窗口 4 个、widget 8 个、输入队列 16 项、Terminal 输入 96 字符/输出 10 行；只支持 32bpp RGB。compositor 没有硬件 vsync/page-flip，字体和 PS/2 鼠标能力有限，窗口不支持完整拖动/resize/关闭，GUI Shell 也不是完整用户态 Shell。Lesson 68 之后不再继续扩展 GUI，本手册保留为回归和新课程的诊断参考。
