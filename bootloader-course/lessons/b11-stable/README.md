# Lesson B11: 装载框架（load/boot 分离）— 课程规划（设计中）

> **课号**：Lesson B11（Mini-GRUB 从零写 GRUB 课程第 11 课）
> **主题**：装载框架：内核装载（load）注册与启动（boot）执行分离、错误状态机
> **课程位置**：阶段二「ELF 与 Multiboot2 装载」第 6 课
> **前置课程**：[`b10-stable/README.md`](../b10-stable/README.md)（E820 → mmap tag）
> **后续课程**：[`b12-stable/README.md`](../b12-stable/README.md)（综合 checkpoint：
> 启动 TinyOS L01/L05）
> **一句话目标**：把散落的装载步骤整理成 `loader_set()`/`loader_boot()` 两阶段
> 接口，与 GRUB `grub_loader_set`/`grub_loader_boot` 对齐。
>
> **状态**：本课当前为课程设计文档，代码待实现（不生成可执行产物）。

## 1. 课程定位（Mission）

**一句话目标**：学完本课，loader 的启动流程变成"先 load（校验+装载+准备 MBI），
确认无误后再 boot（交接）"，中间任何一步失败都能干净地返回报错，而不是半途
跳飞。

- **在课程中的位置**：B06–B10 把装载能力逐块搭起来了，但它们是直通式调用。
  GRUB 的关键设计是 `multiboot2` 命令只做 **load 注册**，`boot` 命令才真正
  **执行交接**（研读支线 0.2）。本课复刻这个两阶段框架，B12 用它启动 TinyOS。
- **前置知识清单**：
  1. B08：交接 ABI 与装载语义；
  2. B09/B10：MBI 与 mmap tag；
  3. 研读支线 0.2（`multiboot2` 加载、`boot` 执行的命令分工）。
- **预期交付**：`build/b11.img`；loader 提供 `loader_set`（校验+装载+建 MBI，
  返回错误码）与 `loader_boot`（交接）；演示错误路径（坏内核）返回后打印
  `error: ...` 而不跳飞。

## 2. 核心概念要点

- **两阶段语义**：`load` = 只读内核、校验、把段搬进内存、构建 MBI，但**不**
  交出控制权；`boot` = 校验 load 已成功，然后设置交接寄存器并跳转；
- **错误状态机**：loader 内部维护 `state`（NONE → LOADED → BOOTED）；load
  失败置错并返回，boot 在非 LOADED 状态拒绝执行；
- **为什么需要**：脚本/菜单场景中"先装载、确认成功、再启动"是必须的（GRUB
  的 `multiboot2` 与 `boot` 是两条命令）；也便于将来 B17/B18 的配置脚本复用；
- **错误字符串**：loader 的 `printf`-like 错误输出（B04 VGA 库扩展），
  对应 GRUB 的 `grub_error` 消息（`error: ...`）。

## 3. 对照 GRUB 源码

- `$GRUB_SRC/grub-core/kern/loader.c`：`grub_loader_set`（注册 boot 函数与
  数据）、`grub_loader_boot`（执行已注册的 boot）；
- `$GRUB_SRC/grub-core/commands/boot.c`：`boot` 命令实现；
- `$GRUB_SRC/grub-core/loader/multiboot.c`：`multiboot2` 命令的 load 侧；
- 研读支线 0.2（命令分发与 load/boot 分工）、0.10（时序）。

**简化边界**：不实现模块级的 loader 注册表（一个 loader 即可）；错误码沿用
简单 int（0 成功/负值失败），不引入 `grub_errno` 全套。

## 4. 产物与验证标准

- 文件：`stage1.S`、`stage2.S`、`loader.c`（重构为 `loader_set`/`loader_boot`
  + 错误字符串表）、`Makefile`、`build/b11.img`、`build/bad.elf`（错误注入）；
- `make check` 断言：`loader_set`/`loader_boot` 符号存在；`bad.elf` 的
  `grub-file` 判定非 0；
- QEMU 冒烟 + VGA 文本 marker：`B11`（正常路径成功交接）；错误路径打印
  `B11 error: ...` 后挂起（不执行未校验跳转）；
- 关键判据：正常内核完整启动；坏内核被拒绝且 trace 无异常。

## 5. 设计要点与风险

- 状态机唯一入口：所有交接只发生在 `loader_boot` 的末尾，跳转前的寄存器
  设置在一条直线路径上完成（避免中途被错误分支破坏）；
- 错误恢复：load 失败后允许重新 load 新内核（状态回 NONE）；
- 风险点：load 成功但 boot 前状态被意外改写（用显式 state 字段而不是"假设
  成功"）；错误消息与 VGA 输出并发（无并发，单线程，无此问题）；
- 开放问题：是否预留"多个候选内核依次 load 尝试"（B18 菜单会需要）。

## 6. 后续课程预告

下一课 [`b12-stable/README.md`](../b12-stable/README.md)：**阶段二 checkpoint**：
用自写引导器（B01–B11 全部能力）启动 TinyOS 主线的 `lesson-01`（VGA hello）
与 `lesson-05`（mmap 显示）——首次以"Mini-GRUB"替代真 GRUB。
