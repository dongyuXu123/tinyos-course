# Lesson 10: 用 iretq 从可恢复的 #BP trap 返回 shell — 精讲文档

> **课号**：Lesson 10（可执行课，x86_64 阶段）
> **主题**：在第九课 exception-only IDT 上增加**可恢复**的 breakpoint trap：`int3` 进入
> vector 3，显示 frame 后以 `iretq` 回到同一份 64 位 polling shell——内核第一次证明
> 「从 CPU exception 中正常返回」是可行的。
> **课程主线位置**：第 4 阶段（异常与中断）的转折点：从「异常=终止」转向「异常=可恢复」；
> #BP 的 gate→stub→report→`iretq` 完整回路是下一课 IRQ 键盘路径的预演。
> **前置课程**：[`lesson-09-stable/README.md`](../lesson-09-stable/README.md)（IDT gate、
> `exception_frame`、stub 压栈、`lidt`、RIP-relative 运行时取址）
> **后续课程**：[`lesson-11-stable/README.md`](../lesson-11-stable/README.md)（8259A PIC
> 重映射与首个 IRQ1 硬件中断路径：`sti` + EOI 第一次登场）
> **一句话目标**：学完本课你能说清「同步 trap 返回」的三件事：CPU 保存的 RIP 已在
> `int3` 之后（单字节指令无需调整）；stub 如何保护 callee-saved 寄存器并精确丢弃
> synthetic 的 16 字节；reporter 如何避免破坏恢复后的 shell 画面。

---

## 1. 课程定位（Mission）

**一句话目标**：新增 `bptest` 命令——执行 `int3` 触发 #BP，在屏幕下半屏打印
vector/error/RIP/CS/RFLAGS，`iretq` 返回 shell 并继续接受 `help` 等命令；同时保留
第九课 `udtest`/`pftest` 的终止型行为作回归验证。

- **在课程主线中的位置**：第九课的 #UD/#PF 证明「异常能打印」，本课证明「异常能回来」。
  可恢复的 #BP 把 IDT 从"诊断工具"升级为"正常控制流的一部分"。这是从
  「同步异常」走向「异步硬件中断」（第十一课）之间最关键的过渡：`iretq` 返回机制
  本身，就是未来中断处理结束后回到被打断代码的手段。
- **前置知识清单**：
  1. 第九课：`set_gate`/`install_idt`/`lidt`、stub 压栈规则（#UD 无 error code 补 0）、
     `struct exception_frame`、RIP-relative `leaq` 取址、零 relocation 约束；
  2. SysV x86_64：callee-saved 寄存器（RBX/RBP/R12–R15）的保存义务、调用前 RSP
     16 字节对齐、volatile asm 的 clobber 列表；
  3. `int3`（`0xCC`）是**单字节**指令——它保存的 RIP 指向 `int3` 之后的下一条指令；
  4. `iretq` 的弹栈语义：依次弹出 RIP、CS、RFLAGS（long mode 还有 RSP/SS 的可选路径）。
- **本课交付**：`bptest` 可恢复命令；`idtinfo` 升级为显示三个 vector；`#BP returned to
  shell` 的可观察恢复证据；第九课终止路径原样保留。

---

## 2. 核心概念精讲

### 2.1 概念一：#BP 与 trap 的本质（为什么能恢复）

**直觉**：断点不是"错误"，是"故意插入的暂停"。CPU 执行 `int3` 后触发 vector 3，就像
按了暂停键；断点调试器处理完再把执行流放回去。所以 #BP 天生适合做「可恢复异常」的实验。

**准确定义**：`INT3` 是一条显式触发 breakpoint 异常（vector 3）的指令，机器码 `0xCC`，
**只有 1 字节**。因为它是单字节，CPU 保存的返回 RIP 已经指向 `int3` 之后的那条指令，
handler 返回时**不需要调整 RIP**（第九课诊断里看到的那种"跳过故障指令"的逻辑在本课
不需要）。#BP 与 #UD 一样不带 CPU error code，所以 stub 必须补压 synthetic error 0。

### 2.2 概念二：可恢复 stub 的栈布局与 RBX 保存

**问题**：stub 要调用 C reporter（`call` 会改 RSP、reporter 可能用寄存器），而返回后
还要精确回到 shell 被打断的状态。其中 RBX 是 SysV 的 callee-saved 寄存器——shell 的
`exec64` 栈帧可能正在用它。第九课的终止型 stub 不用管寄存器死活，本课**必须**保存。

stub 的压栈顺序（对照源码逐条）：

```text
CPU delivery frame（RFLAGS | CS | RIP，栈顶=RIP）
  pushq %rbx          ← 在 frame 下方保存 shell 的 RBX
  pushq $0            ← synthetic error
  pushq $3            ← vector 3

f 布局（RDI=f = 栈顶地址）:
f[0]=vector | f[1]=error | f[2]=saved RBX | f[3]=RIP | f[4]=CS | f[5]=RFLAGS
```

恢复路径：`movq %rbx,%rsp`（回到栈顶=vector）→ `addq $16,%rsp`（**恰好跳过 vector 与
error 两个 synthetic qword**）→ `popq %rbx`（还原 RBX）→ `iretq`（弹出 RIP/CS/RFLAGS
返回）。任何一处多压或少压 8 字节，`iretq` 就会把垃圾当 RIP 弹出 → triple fault。

### 2.3 概念三：私有 VGA cursor 与"下半屏契约"

**问题**：shell 的 VGA cursor（`exec64` 栈帧里的局部 `u16 c`）是 shell 的私有状态，
#BP handler 拿不到它。若 handler 用自己的 cursor 从屏幕顶部开写，返回后 shell 的
cursor 与 handler 写的文字就会互相覆盖。

**解法**（源码注释的原话）：可恢复报告**固定写到第 10 行起**：

```c
TEXT64 void breakpoint_report(struct exception_frame*f){u16 c=10*COLS;...}
```

`c = 10*COLS` 即第 10 行（`10×80=800` 号 cell）。handler 只使用自己的局部 cursor 写
下半屏，绝不碰 `exec64` 的 cursor 与上半屏。这样 `iretq` 回来后 shell 的 cursor 仍在
原处，提示符与输出不打架。第九课的终止型 reporter 相反——它 `clear64` 全屏，因为它
"不回来"。

### 2.4 概念四：volatile asm 的 clobber 列表（为什么 bptest 的 int3 要列一堆寄存器）

`exec64` 里 `bptest` 的内联汇编：

```c
__asm__ volatile("int3":::"rax","rcx","rdx","rsi","rdi","r8","r9","r10","r11","cc","memory");
```

GCC 不知道 `int3` 会触发异常、跳进 C reporter 并返回；reporter 完全可能踩坏这些
**caller-saved** 寄存器。在 clobber 列表里声明它们，等于告诉 GCC「这段 asm 之后这些
寄存器的值都不可信」，GCC 就会在 `int3` 前后保存/恢复自己正在用的寄存器。**注意列表里
没有 RBX**——因为 RBX 由汇编 stub 亲自保存恢复了，C 编译器不需要额外处理。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 09） |
|---|---|---|
| `boot.S` / `kernel.c` / `kernel64.ld` / `linker.ld` / `Makefile` | 启动链与构建 | 未变化（无 diff） |
| `kernel64.c` | 64 位 IDT + 异常 + shell | 新增 `exception_bp` stub、`runtime_bp_address`、`breakpoint_report`、`print_exception_frame`；`install_idt`/`idtinfo`/`exec64`/banner 增量 |
| `grub.cfg` | 菜单标题 | 微小变化（`lesson 9` → `lesson 10`） |

**本课全部增量都集中在 `kernel64.c`**——这是「第九课铺好的 IDT 基础设施在本课被
复用、扩展」的最直观证据。

### 3.2 增量一：gate 装配——第三个 vector

```c
extern void exception_bp(void); extern void exception_ud(void); extern void exception_pf(void);
...
static TEXT64 u64 runtime_bp_address(void){u64 v;__asm__ volatile("leaq exception_bp(%%rip),%0":"=r"(v));return v;}
...
set_gate(&idt[3],runtime_bp_address());
set_gate(&idt[6],runtime_ud_address());
set_gate(&idt[14],runtime_pf_address());
```

- `install_idt` 只多了一行 `set_gate(&idt[3],...)`，与 vector 6/14 共用同一套装配逻辑，
  gate selector 仍为 `0x08`、type 仍为 `0x8e`。
- `runtime_bp_address` 与第九课的 `runtime_ud/pf_address` 同构：`leaq exception_bp(%rip)`
  运行时取址，保证 `kernel64.elf` 无 relocation。

### 3.3 增量二：`breakpoint_report()` —— 下半屏报告（关键函数）

```c
/* exec64 的 cursor 保存在其栈帧中；#BP 未把这个私有 cursor 传入 handler。
 * 因此把可恢复报告固定写到下半屏，避免与恢复后的 shell cursor 相互覆盖。 */
TEXT64 void breakpoint_report(struct exception_frame*f){
    u16 c=10*COLS;u64 *raw=(u64 *)f;
    text64(&c,"TinyOS lesson 10 breakpoint\nexception: #BP\nvector: ");hex64(&c,raw[0]);
    text64(&c,"\nerror:  ");hex64(&c,raw[1]);
    /* INT3's long-mode delivery frame includes the interrupted stack slot here. */
    text64(&c,"\nrip:    ");hex64(&c,raw[3]);
    text64(&c,"\ncs:     ");hex64(&c,raw[4]);
    text64(&c,"\nrflags: ");hex64(&c,raw[5]);
    text64(&c,"\nreturning with iretq...\n");
}
```

- **签名与职责**：入参是 stub 传来的 frame 指针；输出固定从第 10 行开始的报告，然后
  **正常返回**（这是与 `exception_report` 的死循环的根本区别）。
- **为什么用 `raw[]` 而非 `f->vector`**：frame 中 raw[2] 是 stub 额外压入的 saved RBX，
  不是 `exception_frame` 结构体里的字段。用 `raw[i]` 按地址偏移直接取数，才能准确表达
  「vector=raw[0]、error=raw[1]、RIP=raw[3]、CS=raw[4]、RFLAGS=raw[5]」这一真实栈布局。
  raw[2]（saved RBX）是"被中断的栈槽"，注释明确点了它。
- **返回语义**：函数遵循 SysV，不破坏 callee-saved RBX；返回后 stub 用它恢复现场并
  `iretq`。reporter 不清屏、不写第 0–9 行——上半屏的 shell 现场原样保留。

### 3.4 增量三：`exception_bp` stub —— 保存 RBX + 返回路径（本课灵魂）

```asm
".global exception_bp\nexception_bp:\n"
/* Preserve the shell's callee-saved RBX below the normalized CPU frame. */
"pushq %rbx\npushq $0\npushq $3\n"
"movq %rsp,%rbx\nmovq %rsp,%rdi\nandq $-16,%rsp\ncall breakpoint_report\n"
/* breakpoint_report follows SysV and preserves RBX, which retains frame RSP. */
"movq %rbx,%rsp\naddq $16,%rsp\npopq %rbx\niretq\n"
```

逐条含义：
1. `pushq %rbx`：把 shell 正在用的 RBX 压到 frame 下方（保存）；
2. `pushq $0` / `pushq $3`：#BP 无 CPU error code，补 synthetic error 0 与 vector 3；
3. `movq %rsp,%rbx`：**RBX 暂存 frame 基址**（栈顶地址）——它是 callee-saved，
   `breakpoint_report` 不会破坏它，所以可以用 RBX 当"栈锚"；
4. `movq %rsp,%rdi`：frame 指针作为 SysV 第一参数；
5. `andq $-16,%rsp`：`call` 前 16 字节对齐；
6. `call breakpoint_report`：交棒 C；返回后 RSP 在临时对齐栈上；
7. `movq %rbx,%rsp`：丢弃 call 的一切临时栈，回到 frame 基址；
8. `addq $16,%rsp`：**精确跳过 vector + error 两个 synthetic qword**；
9. `popq %rbx`：还原 shell 的 RBX；
10. `iretq`：弹出 CPU 压入的 RIP/CS/RFLAGS，回到 `int3` 的下一条指令。

`exception_ud`/`exception_pf`/`exception_common` 与第九课完全不变——#UD/#PF 仍是终止型，
继续走 `cli; hlt`。

### 3.5 增量四：`print_exception_frame` 提取与终止型 reporter 的收编

```c
static TEXT64 void print_exception_frame(u16*c,struct exception_frame*f){
    text64(c,"\nvector: ");hex64(c,f->vector);text64(c,"\nerror:  ");hex64(c,f->error);
    text64(c,"\nrip:    ");hex64(c,f->rip);text64(c,"\ncs:     ");hex64(c,f->cs);
    text64(c,"\nrflags: ");hex64(c,f->rflags);
}
```

- 第九课 `exception_report` 里的五行打印被抽成共享 helper；`exception_report` 改用
  `f->vector`/`f->error`（对 #UD/#PF 而言栈布局与结构体严格一致，无需 raw 手法）。
- 终止路径行为不变：全屏 `clear64`、打印、`#PF` 读 CR2、`CPU halted intentionally.`、
  `cli; hlt` 死循环。

### 3.6 增量五：命令表与输出串（逐字抄录自源码）

```c
if(eq64(s,"help"))text64(c,"commands: help about clear lminfo pinfo palloc mmap idtinfo bptest udtest pftest\n");
else if(eq64(s,"about"))text64(c,"TinyOS lesson 10: recoverable #BP trap\n");
...
else if(eq64(s,"idtinfo"))idtinfo(c,h);
...
else if(eq64(s,"bptest")){text64(c,"triggering #BP\n");
    __asm__ volatile("int3":::"rax","rcx","rdx","rsi","rdi","r8","r9","r10","r11","cc","memory");
    text64(c,"#BP returned to shell\n");}
```

- `idtinfo` 新串：`#BP vector: 0000000000000003 (returns)`（新增行，括号标注可恢复），
  其余行不变；
- `bptest` 流程：先打印 `triggering #BP` → `int3` 陷入 handler → 下半屏报告 →
  `iretq` 回到 `int3` 之后 → 打印 `#BP returned to shell` → 再打 prompt；
- `udtest`/`pftest` 原样保留（终止型，需单独 boot 验证）。

### 3.7 主控制流

```text
kernel_main64_binary(h)
  → install_idt: gate[3]=exception_bp, gate[6]=exception_ud, gate[14]=exception_pf → lidt
  → shell: "bptest" → 打印 "triggering #BP" → int3
        → CPU: 压 RFLAGS/CS/RIP → IDT[3] → exception_bp
        → stub: push rbx → push 0 → push 3 → rdi=frame → 对齐 → call breakpoint_report
        → breakpoint_report: 从第 10 行打印 vector/error/rip/cs/rflags → 返回
        → stub: rsp=rbx → +16 → pop rbx → iretq
  → 回到 exec64: 打印 "#BP returned to shell" → prompt → 继续轮询键盘
```

---

## 4. 数据流与运行逻辑

- **数据路径**：`int3` 触发 → CPU 压现场 → `exception_bp` stub 追加
  `[vector=3, error=0, saved RBX]` → `breakpoint_report` 用 `raw[i]` 按偏移打印 →
  返回后 stub 逆向拆栈 → `iretq` 恢复执行。
- **命令数据流**：`bptest` 的 inline `int3` 前后由 clobber 列表保证编译器不信任被打断
  时寄存器的值；返回后 `exec64` 继续打印并交回 prompt。
- **输出串索引**（逐字抄录）：
  - banner：`TinyOS lesson 10: recoverable #BP trap` + `64-bit C continuation active`
  - `help` → `commands: help about clear lminfo pinfo palloc mmap idtinfo bptest udtest pftest`
  - `about` → `TinyOS lesson 10: recoverable #BP trap`
  - `idtinfo` → `#BP vector: 0000000000000003 (returns)` / `#UD vector: 0000000000000006` /
    `#PF vector: 000000000000000e`
  - `bptest` → `triggering #BP` → 报告（含 `returning with iretq...`）→ `#BP returned to shell`
  - `exception_report` 头 → `TinyOS lesson 10 exception`（注意：不再是 lesson 9）

---

## 5. 构建、运行与验证

依赖与第九课相同。

```bash
cd lessons/lesson-10-stable
make clean && make -j"$(nproc)"
make check
readelf -h -l -W build/kernel.elf
nm -u build/kernel.elf
readelf -rW build/kernel64.elf       # 必须无 relocation
objdump -d -Mintel build/kernel64.elf # 应能看到 lidt、int3、iretq、ud2、mov cr2
```

运行（成功画面在 QEMU 图形窗口，勿加 `-display none`）：

```bash
make run
```

**主验证（同一 boot 内即可完成恢复验证）**：

```text
idtinfo<Enter>
help<Enter>
bptest<Enter>
help<Enter>
```

- `idtinfo` 应显示 `#BP vector: 0000000000000003 (returns)` 与 6/14 两行；
- `bptest` 应依次出现：`triggering #BP` → 下半屏报告（`exception: #BP`、vector `3`、
  error `0`、RIP/CS/RFLAGS、`returning with iretq...`）→ `#BP returned to shell` →
  新 `tinyos> ` prompt；
- 紧接着的 `help` 能正常列出命令——**这是"真正恢复"的判定标准**。

**回归验证（各需新 QEMU boot，因终止型）**：`udtest` 触发 #UD 后有意停机；
`pftest` 触发 #PF 且 `cr2: 0000000000400000` 后有意停机。

> **本次实际验证记录（旧 README 保留）**：`-Werror` build 成功，Multiboot2 header check
> 通过；外层 ELF32 分离 RX/RW LOAD segments，无 RWX LOAD；`nm -u` 无未定义符号；临时
> `kernel64.elf` 无 relocation；反汇编包含 `lidt`、`int3`、`iretq`，并保留 `ud2` 与
> `mov cr2`。QEMU VGA 中 `bptest` 显示 #BP、vector `3`、error `0`、RIP/CS/RFLAGS 和
> `returning with iretq...`；同一启动内随后出现 `#BP returned to shell`、新 `tinyos>`
> prompt，且 `help` 仍可执行，证明真正恢复。第九课的 #UD/#PF terminal paths 保持在代码
> 与 IDT 中，供新 QEMU boot 回归。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| `int3` 直接 reset | IDT[3] gate 无效 | 检查 gate selector `0x08`、type `0x8e`、Present bit |
| handler 地址接近零 | 零链接符号直接填 gate | 必须用 RIP-relative `leaq exception_bp(%rip)` 运行时取址 |
| `iretq` 后 reset | synthetic 栈没丢干净 | 检查 vector/error 是否恰好被 `addq $16,%rsp` 丢弃 |
| `iretq` 返回错误地址 | RIP 被错误加减 | #BP 的 saved RIP 已在单字节 `int3` 之后，不应加减 |
| shell 寄存器损坏 | callee-saved 没保护或 clobber 漏项 | stub 必须保护 RBX；inline `int3` 必须声明 caller-clobbered 寄存器与 `memory` |
| shell 文本被 handler 覆盖 | handler 用了不该用的 cursor | handler 固定写到独立下半屏（`10*COLS`），不碰 shell 的私有 cursor |
| #BP 被当作 #UD | vector 混淆 | 确认 vector 3（#BP）与 vector 6（#UD） |
| #BP 有非零 error | 没补 synthetic 0 | #BP 不带 CPU error code，stub 必须 `pushq $0` |
| #PF frame 失效 | 把 returning stub 复用到 #PF | #PF 仍走 terminal common path，不可复用 #BP stub |
| 64-bit continuation relocation | 出现绝对寻址 | `readelf -rW build/kernel64.elf` 必须为空 |
| ordinary shell regression | 交互损坏 | 先跑 `idtinfo`/`help`，再跑 `bptest` 后的 `help` |
| accidental IRQ behavior | 中断意外开启 | 本课仍 `cli` 并 polling，不可引入 `sti`、PIC 或 EOI |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux v6.12 对应实现 | 权威来源 | 简化了什么 |
|---|---|---|---|
| vector 3 #BP 作为可恢复 trap | `arch/x86/include/asm/trapnr.h` 的 `X86_TRAP_BP`；`arch/x86/kernel/traps.c` 的 `exc_int3()` | Intel SDM Vol.3 6.15 | Linux 的 `exc_int3` 还要处理 kprobes、uaccess、perf 等 |
| `pushq %rbx` 手工保护寄存器 | 编译器生成的函数序言 prologue 自动 push callee-saved | SysV ABI | 本课在 asm 手工做；Linux entry 用 `PUSH_AND_CLEAR_REGS` 宏 |
| synthetic vector/error + `iretq` | `arch/x86/entry/entry_64.S` 的 `swapgs_restore_regs_and_return_to_usermode` | Linux x86 entry | Linux 处理 syscall/irq 两套 frame 与 GS 切换；本课只有同步 trap |
| 下半屏固定输出 | `arch/x86/kernel/traps.c` 打印到 console（无 cursor 冲突） | Linux 内核 | Linux 有独立 console/printk 缓冲；本课直接裸写 VGA |
| `int3` inline asm 的 clobber 列表 | GCC 对 asm 的通用要求 | GCC 文档 | Linux 用 C 汇编包装器，很少裸 `int3` |

---

## 8. 思考题与练习

1. **概念理解**：为什么 #BP 不需要调整返回 RIP，而如果本课用的是 `int` 指令（3 字节）
   就会遇到 RIP 停在指令中间的差异？（提示：比较单字节 `0xCC` 与多字节 `int` 的取指）
2. **源码定位**：在 `kernel64.c` 的内联 asm 中指出 `addq $16,%rsp` 恰好跳过的两个
   qword 分别是什么，说明若改成 `$8` 会发生什么。
3. **动手实验**：把 `breakpoint_report` 的起点 `10*COLS` 改成 `0`，重新构建运行
   `bptest`，观察恢复后 shell 输出与报告文字如何互相覆盖，用调试地图第 6 行解释。
4. **动手实验**：在 `bptest` 的 clobber 列表里删掉 `"rax"`，用 `-O2` 重新构建，观察
   shell 是否会因寄存器破坏而行为异常（可结合 `#BP returned to shell` 之后的输出）。
5. **Linux 对照**：阅读 `arch/x86/entry/entry_64.S` 中 `iretq` 前后对 frame 的处理，
   对比 Linux 的 `pt_regs` 与本课 `raw[0..5]` 布局的差异。

---

## 9. 本课小结与下一课预告

**小结**：
1. #BP（vector 3）是第一个可恢复异常：`int3` 触发 → 报告 → `iretq` 返回，shell 无损继续。
2. 因为 `int3` 是单字节指令，CPU 保存的 RIP 已在指令之后，本课不调整 RIP。
3. 可恢复 stub 的栈布局多了 saved RBX：`push rbx → push 0 → push 3`，返回时
   `addq $16,%rsp` 恰好丢 vector+error，`pop rbx` 还原寄存器，`iretq` 弹出 CPU frame。
4. handler 不拥有 shell 的私有 VGA cursor，所以可恢复报告固定写下半屏（`10*COLS`），
   避免与恢复后的 shell 输出互相覆盖。
5. inline `int3` 必须用 clobber 列表声明 caller-clobbered 寄存器与 memory；RBX 由
   stub 亲自保护，故不在列表中。
6. `print_exception_frame` 被提取为共享 helper；终止型 #UD/#PF 路径与行为完全不变，
   保留作回归。
7. 「真正恢复」的判定：`bptest` 之后 `help` 依然可用——异常不再杀死执行流。

**下一课**：[`lesson-11-stable/README.md`](../lesson-11-stable/README.md) 将从「同步软件
trap」跨入「异步硬件中断」：8259A PIC 重映射 IRQ 到 vector 32+、`sti` 开中断、EOI 应答，
并点亮第一条 IRQ1 键盘硬件中断路径。本课验证过的「gate→stub→handler→返回」回路，
正是第十一课中断处理结束回到 shell 的基础。
