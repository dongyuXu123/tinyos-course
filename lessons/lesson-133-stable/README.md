# Lesson 133: 异常路径与故障分类 — 精讲文档

> **课号**：Lesson 133（可执行课，checkpoint 快照）
> **主题**：异常路径与故障分类——沿 CPU 异常从触发到诊断输出的完整路径，把内核里
> 已存在的 #BP/#UD/#PF 三条异常路径与「缺页→分类→插页」的页故障模型串成一张
> 「故障分类」图，并追加确定性校验的 checkpoint 模型 `lesson_126_model`。
> **课程主线位置**：诊断/网络主题的「检查点课」序列（Lesson 133–138），位于
> Lesson 132（崩溃诊断快照）之后、Lesson 134（内存压力诊断）之前。
> **前置课程**：[`lesson-132-stable/README.md`](../lesson-132-stable/README.md)
> **后续课程**：[`lesson-134-stable/README.md`](../lesson-134-stable/README.md)
> **一句话目标**：学完本课你能说清 TinyOS 的「异常路径」全貌——谁触发、谁压栈、
> 谁分类、谁打印，以及 checkpoint 模型 `l133test` 校验了什么。

---

## 1. 课程定位（Mission）

**一句话目标**：读透内核里继承下来的三条异常路径（#BP 可恢复、#UD/#PF 终止型）
与 `pf_classify` 的页故障分类模型，理解本课新增的确定性 checkpoint 模型
`lesson_126_model` 及其 `l133test` 断言，并会用 `about`、`pfmodel`、`bptest`、
`vmainfo`、`signaltest` 等命令复现它们。

- **在课程主线中的位置**：与 Lesson 134–138 同属「诊断/网络主题的检查点课」，相邻课
  `kernel64.c` 的 diff 通常只有几行（本课相对 Lesson 132 仅 3 处：`l132test`→`l125test`
  改名、新增 `struct lesson_126_model` 与 `l133test`、exec64/about/banner 文案）。
  机制代码全部继承自早期课程，检查点课的作用是把「已经实现的机制」用断言固化。
- **前置知识清单**：
  1. IDT/gate 结构（`struct idt_gate` 16 字节、`install_idt`、`lidt`、IST1）——
     Lesson 09–10 的异常地基；
  2. `exception_frame` / `exception_frame_ist` 的字段顺序与 CPU 压栈规则（有/无
     error code、CPL3→CPL0 切换、IST 换栈）；
  3. VMA 元数据模型（`vma_table`、`vma_lookup`、`vma_init`）与 PMM 位图分配
     （`pmm_alloc`/`pmm_free_page`）；
  4. syscall/信号子系统（`exception_signal`、`user_process.signals`）。
- **本课交付**：理解三条异常路径的汇编 stub 与 C reporter 分工；理解页故障三类
  分类（not-present / protection / unmapped）与 `fault_insert` 插页；命令
  `l125test`、`l133test` 两个 checkpoint 测试；`about` 文案。

---

## 2. 核心概念精讲

### 2.1 概念一：异常路径（exception path）与故障（fault）

**直觉**：CPU 检测到内部错误时会进入「异常」处理：CPU 依据 vector 号查 IDT、把现场
压栈、跳进内核写好的 stub。TinyOS 把这套机制实现成了三种典型形态，构成了本课的
「异常路径」：

| vector | 异常 | error code | 路径形态 | 结局 |
|--------|------|-----------|---------|------|
| 3 | #BP（断点） | 无 | `exception_bp` stub + `breakpoint_report` | report 后 `iretq` **返回 shell**（可恢复） |
| 6 | #UD（无效指令） | 无 | `exception_ud` stub + `exception_report` | report 后 `cli; hlt`（终止型） |
| 14 | #PF（页故障） | 有 | `exception_pf` stub + `exception_report_ist`（IST1） | report 后 `cli; hlt`（终止型） |

**准确定义**：异常路径 = 「触发指令 → CPU 查 IDT gate → 汇编 stub 拼 frame →
C reporter 打印诊断 → 恢复或停机」这条固定的执行链。三者的 frame 布局统一为
`struct exception_frame`（vector/error/rip/cs/rflags/rsp/ss），但 #BP 走 trap gate
（返回），#UD/#PF 走 interrupt gate（本课设计为不返回）。

**为什么这样设计**：可恢复的 #BP 让调试器语义成立；终止型的 #UD/#PF 刻意不做恢复，
把「未知状态」暴露给 VGA 而非静默死机。这与 Linux 把不可恢复异常交给
`arch/x86/kernel/traps.c` + `kernel/panic.c` 的「oops → panic」路线同构。

### 2.2 概念二：页故障分类（page fault classification）

**直觉**：同样一个 #PF，原因可能完全不同：访问了没映射的地址、写了只读页、或者
地址根本不在任何 VMA 内。TinyOS 用 `pf_classify` 把 #PF 归为三类：

```text
enum pf_class { PF_NOT_PRESENT=1, PF_PROTECTION=2, PF_UNMAPPED=3 };
```

**分类决策树**（`pf_classify`）：
1. `vma_lookup(va)` 找不到 VMA → `PF_UNMAPPED`（地址不属于任何用户区间）；
2. VMA 在，但权限不足（写 `VMA_W` 不存在、或读 `VMA_R` 不存在）→ `PF_PROTECTION`
   （保护违规，对应 Linux 的 P=1 保护错）；
3. VMA 在且权限够，但 `page_present()` 为假 → `PF_NOT_PRESENT`（真正「缺页」，
   Linux 的 demand paging / demand zero）。

**教学模型**：这是纯元数据模型，`pf_classify` 只查 `vma_table` 和 `fault_pages`，
**不执行任何真实故障指令**（`pfmodel` 输出里明确写着 `no real fault instruction
executed`）。分类统计进 `fault_not_present` / `fault_protection` /
`fault_unmapped`，可用 `vmainfo` 查看。

### 2.3 概念三：确定性 checkpoint 模型

**直觉**：检查点课不写新机制，而是用「结构体 + 赋值 + 断言」把本课主题固化为
一行可验证的真假值。Lesson 133 新增：

```c
struct lesson_126_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
```

**工作机制**：`l133test` 把 `lesson_126_state` 整体赋为 `{126U,127U,128U,129U,1,1,1,1}`
（a=126, b=127, c=128, d=129，四个状态位全 1），然后断言 `valid && active && ready &&
accounted && b==a+1`。由于赋值是字面量，断言恒真，输出恒为 `bounded concurrency,
SMP, RCU, and diagnostics checkpoint passed`。「fallback」分支只是保证任何情况下都有
输出（代码可读性），不会真正触发。**这是教学模型：不执行任何并发代码，只校验元数据
自洽**——真实性来自模型字段与断言本身。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 132） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验并加载用户镜像、构造 `long_mode_handoff` | 未变化 |
| `kernel64.c` | 64 位内核本体（959 行）：异常/中断/调度/进程/VFS/GUI/checkpoint 模型 | `l132test`→`l125test`；新增 `struct lesson_126_model`、`l133test`；exec64 增加 `l125test`/`l133test` 分支；about/banner 文案 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局（`.multiboot`/`.text64`/`.data`/`.bss`） | 未变化 |
| `Makefile` | `make all/check/run/clean`；`check` 目标 grep `异常路径与故障分类`、`l133test`、`Lesson 133` | 仅 grep 文案（Lesson 132→133） |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

### 3.2 kernel64.c：异常路径实现精讲

#### 3.2.1 异常现场结构与 IDT 装配

```c
struct exception_frame { u64 vector,error,rip,cs,rflags,rsp,ss; };
/* IST changes the CPU frame: old RSP and SS follow the CPL0 return frame. */
struct exception_frame_ist { u64 vector,error,rip,cs,rflags,rsp,ss; };
```

逐行注释：
- `exception_frame` 是「普通栈」上异常的统一现场：`vector`（stub 补）、`error`
  （CPU 压的 error code）、`rip/cs/rflags`（CPU 必压），以及 `rsp/ss`（仅当从 CPL3
  进入时才由 CPU 压入，CPL0→CPL0 时这两个字段是垃圾值，report 时不要乱读）。
- `exception_frame_ist` 与普通帧字段完全一样，但语义不同：`#PF` 用 IST1 换栈后，
  **保存的用户栈 RSP/SS 跟在 CPL0 返回帧之后**，注释把这一点点破（"old RSP and SS
  follow the CPL0 return frame"）。
- `_Static_assert(sizeof(struct exception_frame_ist)==56,...)`：7 个 u64 恰好 56 字节，
  编译期锁死布局，防止有人改动结构破坏汇编 stub 的偏移假设。

```c
static TEXT64 void set_gate(struct idt_gate *g,u64 target,u8 ist){g->offset_low=(u16)target;g->selector=KERNEL_CS;g->ist=ist;g->type=IDT_GATE_INTERRUPT;g->offset_mid=(u16)(target>>16);g->offset_high=(u32)(target>>32);g->reserved=0;}
```

- 签名与职责：把 64 位目标地址拆成三段填进 16 字节 gate；`selector=KERNEL_CS(0x08)`
  保证跳到内核代码；`ist` 只对 `#PF` 传 `IST1_INDEX(1)`。
- 边界处理：`type` 固定 `IDT_GATE_INTERRUPT(0x8e)`；64 位 gate 的 DPL 位在 type 高字节，
  因此 syscall gate 后面用 `idt[0x80].type=0xee` 单独改 DPL=3。
- 为什么：gate 字段的 offset_high 是 x86-64 专属（32 位 IDT 只有 16+16 位 offset），
  这个拆分是 Intel SDM 3A §6.11 的直接实现。

```c
static TEXT64 void install_idt(struct long_mode_handoff*h){struct idt_gate *idt=(struct idt_gate *)(unsigned long)phys_to_high(h->idt_address);struct idtr d;u32 i;for(i=0;i<IDT_ENTRIES;i++){...全部清零...}set_gate(&idt[3],runtime_bp_address(),0);set_gate(&idt[6],runtime_ud_address(),0);set_gate(&idt[14],runtime_pf_address(),IST1_INDEX);set_gate(&idt[0x80],runtime_syscall_address(),0);idt[0x80].type=0xee;set_gate(&idt[0x20],runtime_irq0_address(),0);set_gate(&idt[0x21],runtime_irq1_address(),0);d.limit=sizeof(struct idt_gate)*IDT_ENTRIES-1;d.base=(u64)(unsigned long)idt;__asm__ volatile("lidt %0"::"m"(d):"memory");}
```

（上文为行文省略了 256 项清零的循环体，源码逐字见 `kernel64.c` 第 539 行。）
- 职责：把 IDT 表从 32 位侧 `idt_backing_store` 的物理地址（`h->idt_address`）换算成
  高半区虚拟地址后，先全清零再装 6 个 gate。
- 本课主题「异常路径」直接对应这里的三个 gate：`idt[3]`=#BP、`idt[6]`=#UD、
  `idt[14]`=#PF（带 IST1）。另外三个是 syscall(`0x80`,DPL=3)、IRQ0、IRQ1。
- 关键细节：目标地址必须用 `runtime_bp_address()` 这类 `leaq exception_*(%rip)` 取址
  函数——裸二进制无 relocation，不能写符号绝对值（见 `hhinfo` 同款技巧）。
- `d.limit=256*16-1=0xfff` 与 `idtinfo` 输出里的 `limit: 0000000000000fff` 一致。

#### 3.2.2 汇编 stub：三条路径的分岔点（inline asm，`kernel64.c` 第 926–939 行）

```c
".global exception_bp\nexception_bp:\n"
"pushq %rbx\npushq $0\npushq $3\n"
"movq %rsp,%rbx\nmovq %rsp,%rdi\nandq $-16,%rsp\ncall breakpoint_report\n"
"movq %rbx,%rsp\naddq $16,%rsp\npopq %rbx\niretq\n"
```

逐行注释：
1. `pushq %rbx`：`breakpoint_report` 的 SysV 序言要用 RBX，先保存；同时让 frame 从
   第 8 字节开始，保证后面 `raw[0]` 对齐 vector 位置（#BP 无 error code，stub 用两个
   `push` 补齐 16 字节，见下）。
2. `pushq $0`：合成 error code 0；`pushq $3`：vector 3。CPU 已压
   rip/cs/rflags，所以栈上依次是 `{vector=3, error=0, rip, cs, rflags}`。
3. `movq %rsp,%rbx` 保存 frame 指针（RBX 是 caller-saved 且我们已保存原值）；
   `movq %rsp,%rdi` 传参；`andq $-16,%rsp` 保证调用前 16 字节对齐；`call
   breakpoint_report`。
4. `movq %rbx,%rsp` 恢复 frame 栈；`addq $16,%rsp` 弹掉 vector+error；`popq %rbx`
   恢复原 RBX；`iretq` 弹 rip/cs/rflags 返回断点下一条指令——这就是「#BP 可恢复路径」。

```c
".global exception_ud\nexception_ud:\n"
"pushq $0\npushq $6\njmp exception_common\n"
".global exception_pf\nexception_pf:\n"
"pushq $14\nmovq %rsp,%rdi\nandq $-16,%rsp\ncall exception_report_ist\n"
"exception_common:\n"
"movq %rsp,%rdi\nandq $-16,%rsp\ncall exception_report\n"
"1: cli\nhlt\njmp 1b\n"
```

逐行注释：
- `#UD`：无 error code，stub 补 `pushq $0` + `pushq $6`（vector 6）后落入
  `exception_common`。
- `#PF`：CPU 已压 14 号 error code，所以 stub 只需 `pushq $14`；随后**先**走
  `exception_report_ist`——因为 #PF 用 IST1 换栈，`exception_frame_ist` 的布局不同，
  且 `exception_report_ist` 会打印 IST1 栈范围与 handler RSP 佐证换栈成功。
- 为什么 `exception_report_ist` 之后还继续落 `exception_common`：`exception_pf`
  的 stub 末尾没有跳转，直接顺序落入 `exception_common` 再调一次
  `exception_report`——后者按普通 frame 再打印一份（ist 帧字段前 7 个 u64 与普通帧
  一致，偏移巧合兼容）。然后 `cli; hlt` 停机。这个「双 report」是教学内核刻意保留的
  冗余，让 IST 路径和普通路径的诊断同时可见。
- `1: cli\nhlt\njmp 1b`：终止型路径的统一归宿——关中断停机，不返回。

#### 3.2.3 C reporter 三兄弟

```c
TEXT64 void breakpoint_report(struct exception_frame*f){u16 c=10*COLS;u64 *raw=(u64 *)f;text64(&c,"TinyOS lesson 27 breakpoint\nexception: #BP\nvector: ");hex64(&c,raw[0]);hex64(&c,"\nerror:  ");hex64(&c,raw[1]);hex64(&c,"\nrip:    ");hex64(&c,raw[3]);hex64(&c,"\ncs:     ");hex64(&c,raw[4]);hex64(&c,"\nrflags: ");hex64(&c,raw[5]);text64(&c,"\nreturning with iretq...\n");}
```

- 职责：把 #BP 现场打印在第 10 行（`10*COLS`，避开顶部 shell 区），用 `u64 *raw`
  直接按 u64 索引读 frame——这是 stub 压栈顺序（vector=raw[0], error=raw[1],
  rip=raw[3], cs=raw[4], rflags=raw[5]）的镜像。
- 边界：打印后**不**清屏不跳转，直接由 stub 的 `iretq` 返回，shell 继续。
- 设计动机：与 Linux `arch/x86/kernel/traps.c` 的 `exc_int3`（int3 返回用户态继续
  执行）同思路——trap gate 语义。

```c
TEXT64 void exception_report(struct exception_frame*f){u16 c=0;u64 cr2=0,rsp;clear64(&c);text64(&c,"TinyOS lesson 28 exception\nexception: ");if(f->vector==6)text64(&c,"#UD");else if(f->vector==14)text64(&c,"#PF");else text64(&c,"unknown");print_exception_frame(&c,f);__asm__ volatile("mov %%rsp,%0":"=r"(rsp));if(f->vector==6&&f->cs==USER_CS){text64(&c,"\nCPL3 #UD proof: user CS and kernel rsp0 active\nhandler rsp: ");hex64(&c,rsp);text64(&c,"\nrsp0: ");hex64(&c,runtime_tss.rsp0);text64(&c,"\nsaved user rsp: ");hex64(&c,f->rsp);text64(&c,"\nsaved user ss: ");hex64(&c,f->ss);text64(&c,"\nCPU halted intentionally.\n");}else {if(f->vector==14){__asm__ volatile("mov %%cr2,%0":"=r"(cr2));text64(&c,"\ncr2:    ");hex64(&c,cr2);}text64(&c,"\nCPU halted intentionally.\n");}for(;;)__asm__ volatile("cli; hlt");}
```

- 算法步骤：清屏 → 按 vector 印异常名 → `print_exception_frame` 印
  vector/error/rip/cs/rflags → 立即读 handler 自己的 RSP（必须在任何可能再 fault 的
  打印之前）→ 分两种诊断：
  - CPL3 下触发 #UD：打印「CPL3 #UD proof」，比对 `handler rsp`（内核 rsp0）与
    `saved user rsp`（用户栈顶 0x00801000 附近），证明 CPU 完成了 CPL3→CPL0 栈切换；
  - 否则若 #PF：读 `CR2`（页故障线性地址，必须最先读，越晚越危险）并打印。
- 边界与错误处理：`f->cs` 判 CPL 来源；`CR2` 只在 vector 14 时读取；停机用
  `cli; hlt` 死循环。

```c
TEXT64 void exception_report_ist(struct exception_frame_ist*f){u16 c=0;u64 cr2=0,rsp;clear64(&c);__asm__ volatile("mov %%rsp,%0":"=r"(rsp));text64(&c,"TinyOS lesson 27 IST exception\nexception: #PF\nvector: ");hex64(&c,f->vector);text64(&c,"\nerror:  ");hex64(&c,f->error);text64(&c,"\nrip:    ");hex64(&c,f->rip);text64(&c,"\nsaved rsp: ");hex64(&c,f->rsp);text64(&c,"\nhandler rsp: ");hex64(&c,rsp);text64(&c,"\nIST1 range: ");hex64(&c,(u64)(unsigned long)__ist1_stack_start);hex64(&c," ");hex64(&c,runtime_tss.ist1);__asm__ volatile("mov %%cr2,%0":"=r"(cr2));text64(&c,"\ncr2:    ");hex64(&c,cr2);text64(&c,"\nCPU halted intentionally.\n");for(;;)__asm__ volatile("cli; hlt");}
```

- 职责：IST1 换栈版 #PF reporter。核心证明点是「IST1 range」：打印
  `__ist1_stack_start`（链接脚本划出的静态 IST 栈）与 `runtime_tss.ist1`
  （`__ist1_stack_end`），两者差一页，证明 handler 跑在 IST1 栈上。
- 边界：同样立即读 `handler rsp` 和 `CR2`；`saved rsp` 在 ist 帧布局里是换栈前栈
  指针（IST 路径 CPU 总是保存旧 RSP）。
- 为什么：IST 机制保证内核栈被写爆/被 #PF 污染时 handler 仍有干净栈可用——Linux
  用 `dereference_stack_descriptor`/`IST_INDEX_NMI` 等同一思路（见 §7）。

#### 3.2.4 触发命令（exec64 分支）

```c
}else if(eq64(word,"bptest")){if(!noargs64(arg))usage64(c,"bptest");else{text64(c,"triggering #BP\n");__asm__ volatile("int3":::"rax","rcx","rdx","rsi","rdi","r8","r9","r10","r11","cc","memory");text64(c,"#BP returned to shell\n");}}else if(eq64(word,"udtest")){...__asm__ volatile("ud2");...}else if(eq64(word,"pftest")){...volatile u64 *bad=(volatile u64 *)0x00400000ULL;text64(c,"triggering #PF\n");p=*bad;(void)p;...}
```

（此处省略 udtest/pftest 分支的括号，源码逐字见 `kernel64.c` 第 920 行。）
- `bptest`：执行 `int3` 触发 #BP，若返回则打印 `#BP returned to shell`——
  可恢复路径的验收点。
- `udtest`：执行 `ud2`（未定义指令），必然 #UD → 停机，输出停在 `triggering #UD`。
- `pftest`：读 `0x00400000`（identity 窗口排他上界内的空洞地址）→ #PF，走 IST1
  路径，`exception_report_ist` 打印后停机。
- 三者合起来覆盖「无 error code 可恢复」「无 error code 终止」「有 error code +
  IST」三条路径——正是本课「异常路径」主题的三个实验标本。

### 3.2.5 故障分类实现（VMA 模型 + pf_classify + fault_insert）

```c
static TEXT64 void vma_init(void){vma_count=3;vma_table[0]=(struct vma_model){VMA_CODE_START,VMA_CODE_END,0,VMA_R|VMA_X,VMA_FILE,1};vma_table[1]=(struct vma_model){VMA_DATA_START,VMA_DATA_END,0,VMA_R|VMA_W,VMA_ANON,1};vma_table[2]=(struct vma_model){VMA_STACK_START,VMA_STACK_END,0,VMA_R|VMA_W,VMA_ANON,1};fault_page_count=0;fault_not_present=fault_protection=fault_unmapped=fault_insertions=0;}
```

- 职责：构造三块教学 VMA——code（0x00400000，r-x/file）、data（0x00600000，
  rw-/anon）、stack（0x00800000，rw-/anon），并把四个故障计数器清零。
- 边界：`vma_count=3`、每块范围一页内（END-START ≤ 0x10000，`vma_range_valid` 有上限
  检查），是「bounded」教学模型的骨架。

```c
static TEXT64 const struct vma_model *vma_lookup(u64 va){u32 i;for(i=0;i<vma_count;i++)if(vma_table[i].valid&&va>=vma_table[i].start&&va<vma_table[i].end)return &vma_table[i];return 0;}
static TEXT64 enum pf_class pf_classify(u64 va,u8 write){const struct vma_model*v=vma_lookup(va);if(!v){fault_unmapped++;return PF_UNMAPPED;}if((write&&!(v->prot&VMA_W))||(!write&&!(v->prot&VMA_R))){fault_protection++;return PF_PROTECTION;}if(!page_present(va)){fault_not_present++;return PF_NOT_PRESENT;}return PF_NOT_PRESENT;}
```

- `vma_lookup`：顺序扫 3 项 VMA，命中返回指针，否则 0。与 Linux
  `find_vma`/`find_vma_intersection` 的语义对应但无红黑树。
- `pf_classify` 算法步骤：①无 VMA→`PF_UNMAPPED`；②权限不够（写但无 `VMA_W`，或读但
  无 `VMA_R`）→`PF_PROTECTION`；③页不在 `fault_pages`→`PF_NOT_PRESENT`。
- 边界：每次分类先递增对应计数器，方便 `vmainfo` 观察「谁在累积」。注意
  ③和兜底都返回 `PF_NOT_PRESENT`——教学模型把「VMA 在、页不在」视为最常规的缺页。

```c
static TEXT64 int fault_insert(u64 va,u8 write){u32 i;u64 p;struct page_model*m;if(fault_page_count>=VMA_MAX_PAGES||!vma_range_valid(down(va),down(va)+PAGE_SIZE,write?VMA_W:VMA_R)||page_present(va))return 0;p=pmm_alloc();if(!p)return 0;for(i=0;i<VMA_MAX_PAGES;i++)if(!fault_pages[i].live){m=&fault_pages[i];m->va=down(va);m->phys=p;m->writable=write;m->live=1;m->backing=VMA_ANON;m->dirty=write;m->accessed=1;m->reclaimable=1;m->refs=1;fault_page_count++;anon_pages++;fault_insertions++;return 1;}return 0;}
```

- 职责：模拟「缺页处理后把页装上」——向 PMM 要一页，登记进 `fault_pages`。
- 边界检查（入口三重防御）：容量满 `VMA_MAX_PAGES(4)`、区间/权限校验不过
  `vma_range_valid`、页已存在 `page_present`，任一成立即返回 0。
- 关键设计：`m->backing=VMA_ANON; m->dirty=write; m->reclaimable=1; m->refs=1;`
  ——新页登记为匿名、按写访问标脏、可回收、引用 1，这是为 Lesson 134「内存压力诊断」
  的 `reclaim_one` 直接铺路（`refs==1 && reclaimable` 才是可回收对象）。

```c
static TEXT64 void pfmodel(u16*c){enum pf_class a=pf_classify(VMA_DATA_START,1),b=pf_classify(VMA_CODE_START,1),d=pf_classify(0x00100000ULL,0);int inserted=fault_insert(VMA_DATA_START,1);text64(c,"pfmodel: ");text64(c,a==PF_NOT_PRESENT&&b==PF_PROTECTION&&d==PF_UNMAPPED&&inserted?"not-present/protection/unmapped classified; bounded page inserted":"BROKEN");putc64(c,'\n');}
```

- 职责：一条命令同时打三个分类靶子 + 一次插页：
  - `pf_classify(VMA_DATA_START,1)`（data，写）→ 页未装 → `PF_NOT_PRESENT`；
  - `pf_classify(VMA_CODE_START,1)`（code，写）→ code 是 `r-x`，写权限不足 →
    `PF_PROTECTION`；
  - `pf_classify(0x00100000,0)`（1 MiB，不在任何 VMA）→ `PF_UNMAPPED`。
- 断言：四者全成立输出 `not-present/protection/unmapped classified; bounded page
  inserted`，否则 `BROKEN`。这就是「故障分类」主题的可运行验收。

### 3.2.6 异常 → 信号（exception_signal）与 signaltest

```c
static TEXT64 int exception_signal(const struct exception_frame *f){u32 signo;u32 i;u64 cr2=0;if(!f||f->cs!=USER_CS)return 0;if(f->vector==3)signo=SIGTRAP;else if(f->vector==6)signo=SIGILL;else if(f->vector==14){signo=SIGSEGV;__asm__ volatile("mov %%cr2,%0":"=r"(cr2));}else return 0;for(i=0;i<SIG_PENDING_MAX;i++)if(!user_process.signals[i].pending){user_process.signals[i]=(struct signal_record){signo,f->vector,f->error,cr2,f->rip,1,0};user_process.signal_queued++;user_process.return_pending=1;if(signo!=SIGTRAP){user_process.state=PROCESS_EXITED;user_thread.state=USER_THREAD_EXITED;}return 1;}user_process.signal_dropped++;return 0;}
```

- 职责：把用户态异常翻译成信号记录（Linux `arch/x86/kernel/traps.c` 的
  `force_sig`/`make_task_dead` 路线）：#BP→SIGTRAP、#UD→SIGILL、#PF→SIGSEGV（带 CR2）。
- 算法：①`f->cs!=USER_CS` 直接拒绝（内核态异常不转信号）；②按 vector 定 `signo`，
  #PF 同时捕获 `CR2` 存入 `fault_address`；③在 2 槽 `signals[]` 里找空槽挂记录，
  非 SIGTRAP 默认动作=进程退出；④槽满则 `signal_dropped++`。
- 与 §3.2.3 的关系：CPU 异常路径是硬件现场，`exception_signal` 是「软件语义层」——
  把同一条异常路径导向进程模型，正是「异常路径与故障分类」主题的收尾。

### 3.2.7 本课新增 checkpoint：lesson_126_model 与 l133test

```c
struct lesson_126_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_126_model lesson_126_state;
static TEXT64 void l133test(u16*c){lesson_126_state=(struct lesson_126_model){126U,127U,128U,129U,1,1,1,1};int ok=lesson_126_state.valid&&lesson_126_state.active&&lesson_126_state.ready&&lesson_126_state.accounted&&lesson_126_state.b==lesson_126_state.a+1U;text64(c,"l133test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 126 fallback reported");putc64(c,'\n');}
```

- `struct lesson_126_model`：4 个 u32（a/b/c/d 连续编号）+ 4 个状态位
  （valid/active/ready/accounted）。字段 `a` 用 `126U` 起头，正好等于本课编号
  133 − 7，是「回锚」到 Lesson 126 检查点模型的记号。
- `l133test` 算法：①整体赋值字面量；②`ok` 五连断言（valid、active、ready、
  accounted、b==a+1）；③按 ok 输出成功串 `bounded concurrency, SMP, RCU, and
  diagnostics checkpoint passed` 或失败串 `Lesson 126 fallback reported`。
- 为什么：检查点课把「继承机制仍然自洽」压缩成一个恒真断言，任何相邻课改坏模型
  字段时输出会翻转为 fallback，起回归探针作用。注意它**不执行任何并发/RCU 代码**，
  消息里的 "concurrency, SMP, RCU" 描述的是整个内核继承机制的覆盖面而非本函数行为。

### 3.2.8 exec64 命令接线（本课增量）

```c
}else if(eq64(word,"l125test")){if(!noargs64(arg))usage64(c,"l125test");else l125test(c);}else if(eq64(word,"l133test")){if(!noargs64(arg))usage64(c,"l133test");else l133test(c);}
```

- 本课把上一课的 `l132test` 分支改名 `l125test`（其模型 `lesson_125_model` 不动），
  并新增 `l133test` 分支。**勘误**：旧 README 写的 `Commands: l126test` 与源码不符，
  源码中可用的 checkpoint 命令是 `l125test` 与 `l133test`。
- about 文案 `else text64(c,"Lesson 133: 异常路径与故障分类\n");` 与开机横幅
  `text64(&c,"Lesson 133: 异常路径与故障分类\nGETTICKS, GETPID, WRITE_CONSOLE,
  EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");` 一起构成主题标识。

### 3.3 构建管线（Makefile / linker）

- `kernel64.o`：`gcc $(CFLAGS64) -c`。`CFLAGS64` 含 `-m64 -ffreestanding -fpie
  -fno-stack-protector -mno-red-zone -mno-sse -mno-sse2 -mno-mmx
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror`——`-fpie` 允许 RIP 相对
  寻址（`leaq exception_bp(%%rip)` 依赖它），`-mno-red-zone` 防止中断路径踩红区。
- `kernel64.bin`：`ld -m elf_x86_64 -T kernel64.ld -nostdlib` 再 `objcopy -O binary`；
  `kernel64.ld` 从 0 开始布局，`.data` 内用 `. = ALIGN(0x1000)` 依次划出
  idle/rsp0/ist1 三块 guard+stack，末尾三条 `ASSERT(...==0x1000)` 锁死每块栈尺寸。
- `boot.o`：`gcc $(CFLAGS)`（32 位），依赖 `build/kernel64.bin`——外层 `.text64`
  段 `kernel_main64` 以 `.incbin` 嵌入二进制。
- `kernel.iso`：`ld -m elf_i386 -T linker.ld` 链接外层 ELF32，`grub-mkrescue` 出 ISO；
  `linker.ld` 保证 `.multiboot` 在 1 MiB 起、8 字节对齐、`.text64` 紧随其后。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`异常路径与故障分类`、`l133test`、`Lesson 133`）——README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**。Makefile 仅 `check` 目标的 grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) 解析 MBI/建页表
  → enter_long_mode (boot.S) CR4.PAE → EFER.LME → CR0.PG → far jump
  → kernel_main64_binary (kernel64.c)
       runtime_gdt_tss_init()（TSS.rsp0/ist1）→ idle_init()
       → install_idt()（#BP/#UD/#PF/syscall/IRQ0/IRQ1 六个 gate）
       → pit_init()+pic_init() → 横幅 "Lesson 133: 异常路径与故障分类\n..."
       → 键盘 shell 循环：kbd_dequeue → exec64(word)
  exec64 分支 → bptest:int3→exception_bp→breakpoint_report→iretq 返回
             → udtest:ud2→exception_ud→exception_report→cli;hlt
             → pftest:读0x00400000→#PF(IST1)→exception_report_ist→cli;hlt
             → pfmodel:pf_classify×3+fault_insert→分类/插页断言
             → l125test/l133test:checkpoint 断言
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径，以本课主题相关的三条命令为例：

1. **`about`** → exec64 命中 `about` 分支 → `text64(c,"Lesson 133: 异常路径与故障分类\n")` → 屏幕打印 `Lesson 133: 异常路径与故障分类`。
2. **`pfmodel`** → `pfmodel(c)`：`pf_classify` 三次（各递增 `fault_unmapped/fault_protection/fault_not_present`）+ `fault_insert(VMA_DATA_START,1)`（`pmm_alloc` 一页、登记匿名页）→ 输出 `pfmodel: not-present/protection/unmapped classified; bounded page inserted`，随后 `no real fault instruction executed; pages: 0000000000000001`。
3. **`l133test`** → `l133test(c)` 对 `lesson_126_state` 赋值并五连断言 → 输出 `l133test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`。

VGA 输出管线全由 `putc64`/`text64`/`hex64` 驱动（0xB8000 文本模式，属性 0x0f 白字黑底，80×25），`clear64` 负责清屏。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-133-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `异常路径与故障分类`、`l133test`、`Lesson 133` 与 kernel64.c 中的 `l133test`，
  全部命中输出 `Multiboot2 and Lesson 133 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。
  启动横幅第一行为 `Lesson 133: 异常路径与故障分类`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 133: 异常路径与故障分类`
  2. `l133test` → `l133test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  3. `l125test` → `l125test: bounded concurrency, SMP, RCU, and diagnostics
     checkpoint passed`
  4. `pfmodel` → `pfmodel: not-present/protection/unmapped classified; bounded page
     inserted`，下一行 `no real fault instruction executed; pages: 0000000000000001`
  5. `vmainfo` → 首行 `VMA table (bounded Linux-style metadata)`，随后三行
     `vma 0 0000000000400000-0000000000401000 r-x file`、`vma 1
     0000000000600000-0000000000602000 rw- anon`、`vma 2 0000000000800000-
     0000000000802000 rw- anon`
  6. `bptest` → `triggering #BP`，随后 `#BP returned to shell`（可恢复路径成功）
  7. `signaltest` → `signaltest: exception notifications queued with bounded default
     actions passed`
  8. `idtinfo` → 首行 `IDT: exceptions + DPL3 syscall gate + PIT IRQ0 + IRQ1`，其中
     `#PF IST: 0000000000000001`
- **如何判断成功**：上述命令逐一打印预期串即成功；`udtest`/`pftest` 预期停机（VGA
  停在诊断报告处，QEMU 不退出——`-no-reboot -no-shutdown` 保证画面保留）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l133test` 输出 `Lesson 126 fallback reported` | checkpoint 模型字段被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l133test` 的赋值 `{126U,127U,128U,129U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| `bptest` 后 shell 无响应且无 `#BP returned to shell` | #BP gate 未装或 `breakpoint_report` 未返回 | `idtinfo` 看 `#BP vector: 0000000000000003`；确认 `exception_bp` stub 的 `iretq` 在 `addq $16,%rsp` 后 |
| `pftest`/`udtest` 黑屏无诊断 | IDT 未 `lidt` 或 gate selector 不是 `0x08` | `idtinfo` 看 base/limit；断 `install_idt` 确认 `lidt` 已执行 |
| `pfmodel` 输出 `BROKEN` | `pf_classify` 任一分类与断言不符（如 VMA 表未初始化） | `vmainfo` 确认 `vma_count=3` 且 code 为 `r-x`；检查 `vma_init()` 是否先于 `pfmodel` 调用 |
| 异常报告里 CR2 为 0 | 打印路径中在读 CR2 前发生任何内存操作（CR2 被破坏） | 确认 `__asm__ volatile("mov %%cr2,%0")` 位于函数最靠前的读操作 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 133: 异常路径与故障分类`；`make check` 的 grep 覆盖此串 |

---

## 7. 与 Linux 源码对照

1. **异常向量与入口 stub**：TinyOS 用 inline asm 的 `exception_bp/exception_ud/
   exception_pf` 三个 stub 按 vector 分派；Linux 用 `arch/x86/entry/entry_64.S` 的
   `idtentry` 宏批量生成同一形态的 stub（`error_entry` 存 GPR、`PUSH_AND_CLEAR_REGS`
   拼 frame）。两者都遵循「CPU 压一部分、软件补一部分」的分工，只是 TinyOS 只写了
   3 个。
2. **IDT gate 装配**：TinyOS `set_gate` 手工拆 64 位偏移写 16 字节 gate；Linux
   `arch/x86/kernel/idt.c` 用 `set_intr_gate`/`set_system_intr_gate` 操作
   `idt_table`，最终 `native_write_idt_entry` 写内存 + `lidt` 装载——机制一一对应。
3. **#PF 处理与 CR2**：TinyOS `exception_report_ist` 优先读 CR2 再打印，终止处理；
   Linux `arch/x86/mm/fault.c` 的 `exc_page_fault` 同样最先读
   `read_cr2()`，之后走 `do_user_addr_fault`（user）或 `kernelmode_fixup`/oops
   （kernel）。TinyOS 省略了完整页表修复与回收逻辑。
4. **缺页分类**：TinyOS `pf_classify` 的 `PF_NOT_PRESENT/PF_PROTECTION/PF_UNMAPPED`
   对应 Linux `fault.c` 中按 `error_code & X86_PF_PRESENT` 区分的
   page-not-present 与 protection-violation 两大分支，以及 `find_vma` 失败时的
   `bad_area`/`unmapped` 路径。`vma_lookup` 对应 `find_vma`（Linux 用红黑树+缓存）。
5. **异常 → 信号**：TinyOS `exception_signal`（#BP→SIGTRAP、#UD→SIGILL、
   #PF→SIGSEGV+CR2）对应 Linux `arch/x86/kernel/traps.c` 的 `do_trap`→
   `force_sig_fault`（`SIGILL`/`SIGSEGV`）与 `int3`→`SIGTRAP`。
6. **IST 换栈**：TinyOS 在 TSS.ist1 放静态栈、`set_gate(...,IST1_INDEX)`；
   Linux `arch/x86/kernel/traps.c` 为 NMI/#MC/#DF 等保留 `IST_INDEX_*`，`TSS` 由
   `cpu_init` 填 `tss.ist[IST_INDEX_NMI]=...`。教学模型只做 1 个 IST 槽、且没有
   双栈/嵌套保护。

**权威来源**：Intel SDM Vol.3A（§6.11 IDT 布局、§6.12.2 异常帧、§6.14 IST）、
Multiboot2 规范（grub.cfg 校验）、GNU GRUB 文档。
**教学模型简化了什么**：真实 #PF 处理要查页表、触发 demand paging/COW、可重入；
TinyOS 只做元数据分类与「登记一页」，不执行任何真实故障指令、不修页表、不恢复。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `exception_pf` 的 stub 先 `call exception_report_ist` 再顺序落入 `exception_common`？如果两个 reporter 都读 CR2，第一次读之后 CR2 还能保证有效吗？（提示：读 CR2 的代码是否还会触发 fault）
2. **源码定位**：在 `kernel64.c` 中找出 `l133test` 使用的模型结构体名字，解释它为什么叫 `lesson_126_model`（与课号 133 的关系），并指出 exec64 中对应的命令分支。
3. **动手实验**：修改 `l133test` 的赋值，把 `b` 从 `127U` 改成 `126U`（即 `b==a`），重新构建运行，观察输出是否翻转为 `Lesson 126 fallback reported`；再改回。
4. **动手实验**：给 `pfmodel` 增加一次 `pf_classify(VMA_STACK_START,0)`（stack 读），预测分类结果并验证 `vmainfo` 中 fault 计数器的变化。
5. **Linux 对照**：阅读 `arch/x86/kernel/traps.c` 的 `do_trap` 与 `arch/x86/mm/fault.c` 的 `do_user_addr_fault`，对比它们与 `exception_signal`/`pf_classify` 的分工边界，指出 TinyOS 砍掉了哪些处理阶段。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是诊断/网络主题的检查点课，`kernel64.c` 相对上一课只有 3 处小增量，机制全部继承自早期课程，主题由 banner/about 文案标识。
2. 异常路径有三种形态：#BP 走 trap gate 可恢复（report 后 `iretq` 返回 shell）；#UD/#PF 走 interrupt gate 终止（report 后 `cli; hlt`）。
3. 异常现场统一为 `exception_frame`，`#PF` 走 IST1 换栈用 `exception_frame_ist`，`_Static_assert` 锁死 56 字节布局。
4. 故障分类模型把 #PF 分成 `PF_NOT_PRESENT/PF_PROTECTION/PF_UNMAPPED` 三类，`pfmodel` 一条命令打三个靶子并插入一页匿名页。
5. `exception_signal` 把用户态异常翻译成 SIGTRAP/SIGILL/SIGSEGV，是异常路径通向进程模型的语义层。
6. 新 checkpoint `l133test` 用字面量赋值 + 五连断言（valid/active/ready/accounted/b==a+1）把「继承机制自洽」固化为恒真回归探针。
7. 旧 README 的 `Commands: l126test` 已勘误为源码实际的 `l125test` 与 `l133test`。

**下一课**：[`lesson-134-stable/README.md`](../lesson-134-stable/README.md) 主题为
「内存压力诊断」，将站在本课 `fault_insert` 登记的匿名页与 `reclaimable` 标志之上，
讲解 `reclaim_one` 页回收、page cache 命中/未命中与 `anoninfo` 诊断命令，并追加新的
checkpoint 模型 `lesson_127_model`（命令 `l134test`）。两课的衔接点是同一张
`fault_pages` 表：本课「插页」，下节课「回收」。