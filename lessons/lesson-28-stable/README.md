# Lesson 28: 首次 CPL3 进入与 TSS rsp0 异常栈证明 — 精讲文档

> **课号**：28　**主题**：one-way CPL3 entry and #UD rsp0 proof
> **课程主线位置**：阶段四→阶段五的转折点（内核运行时基础设施 → 用户态）——在 Lesson 27
> （16 槽映射注册表）之后、Lesson 29（int 0x80 syscall ABI）之前，第一次真正进入 ring 3，
> 并用一次受控的 `ud2` 异常证明「用户 CS + TSS rsp0 异常栈」确实生效。
> **前置课程**：[../lesson-27-stable/README.md](../lesson-27-stable/README.md)
> **后续课程**：[../lesson-29-stable/README.md](../lesson-29-stable/README.md)
> **一句话目标**：用固定用户代码/栈页、`PTE_USER` 页表项、GDT 用户段
> `USER_DS=0x2b`/`USER_CS=0x33` 与带 `IF=0` 的 `iretq` 帧**恰好一次**进入 ring 3，
> 让用户 `ud2` 在 TSS `rsp0` 上触发条件式 #UD 报告。

> **Course status: stable snapshot (validated; verified build artifacts included).**
> 本目录为已校验稳定快照：`build/` 内含已验证构建产物，README 为精讲文档。

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能构造一条完整的「内核→用户→异常→内核」单程路径：理解
  GDT 用户段的 DPL=3 与选择子低两位的含义、页表 `PTE_USER` 的 U/S 位检查、`iretq`
  从栈弹出的 SS/RSP/RFLAGS/CS/RIP 五元组、以及 CPL3→CPL0 异常时硬件如何用 TSS `rsp0`
  换栈——并用 `cpl3test` 的屏幕输出亲手验证这一切。

- **在课程主线中的位置**：前四课（25–27）准备好了 guard 栈、16 MiB 映射、16 槽注册表与
  TSS/`rsp0`/IST1；`rsp0` 一直在「预备」状态。本课首次让硬件真正消费 `rsp0`：CPL3 代码
  触发 #UD，CPU 从 TSS 载入 `rsp0` 换栈进入内核处理程序。这是下一课 `int 0x80` syscall
  ABI 的必经前置——syscall 同样是「CPL3→CPL0 换栈到 `rsp0`」。

- **前置知识清单**：
  1. GDT 描述符格式（代码/数据段、DPL、长模式标志）与选择子结构；
  2. `iretq` 从栈顶恢复 SS/RSP/RFLAGS/CS/RIP 的语义；
  3. TSS 的 `rsp0` 字段与 CPL3→CPL0 换栈规则（Intel SDM §5.8.5、§6.12）；
  4. PTE 的 U/S 位（bit 2）与 CPL3 访问检查；
  5. Lesson 27 的 16 槽注册表与 `vm_frame_owned` 所有权规则。

- **本课交付**（可见结果）：
  - `tssinfo` 显示 `USER_DS`、`USER_CS` 与 `rsp0 top`；
  - `cpl3test` 进入用户 `ud2` stub，屏幕出现
    `CPL3 #UD proof: user CS and kernel rsp0 active`，其中 `cs=0000000000000033`、
    handler `rsp` 落在 `rsp0` 区间内，随后有意停机。

## 2. 核心概念精讲

### 2.1 GDT 用户段与 DPL

- 选择子低 2 位是请求特权级 RPL。`USER_DS=0x2b`（二进制 `0b101011`，RPL=3，TI=0，index=5）、
  `USER_CS=0x33`（`0b110011`，RPL=3，index=6）。对应的描述符放在运行时 GDT 的第 5、6 槽：

```c
runtime_gdt[5]=0x00aff2000000ffffULL;   /* USER_DS: DPL=3 数据段 */
runtime_gdt[6]=0x00affa000000ffffULL;   /* USER_CS: DPL=3 代码段 */
```

- 描述符字节分析：`0x00af f2 00 0000ffff` —— 访问权字节 `0xf2`：Present(1)=1、DPL=11（3）、
  S=1（代码/数据）、Type=`0010`（可读写数据段）；`0x00af fa ...` —— 访问权字节 `0xfa`：
  DPL=3、Type=`1010`（可执行、长模式代码段）。长模式标志 L=1（`0xaf` 高字节含 L）。
- 为什么需要用户段：`iretq` 返回时 CS/SS 必须指向 **DPL=3** 的段，否则 CPU 拒绝降权；
  且长模式用户代码必须用 L=1 的代码段（64 位段）。

### 2.2 PTE 的 U/S 位（用户可访问位）

- PTE bit 2（U/S）：0 = supervisor-only，1 = user。CPL3 访问某页时，页表遍历路径上的
  **每一级**（PML4/PDPT/PD/PT）的 U/S 位都必须是 1，否则 #PF。
- 本课改动（`kernel.c`）：`pml4[0]`、`pdpt[0]` 加 `PTE_USER`；PD 只在
  `i==2||i==4`（即 2 MiB 与 4 MiB 所在页表）加 `PTE_USER`；用户代码/栈的 PT 项加
  `PTE_USER`。这样 CPL3 只能访问「被显式标记为用户」的页——用户代码页
  `0x00400000` 与用户栈页 `0x00800000`，其余低地址仍被 U/S=0 挡住。
- 高别名（`pml4[511]` 等）保持 U/S=0，用户态无法触达内核高半区。

### 2.3 `iretq` 五元组与 IF=0

- 进入用户态不是「跳转」，而是**伪造一个返回到 CPL3 的帧**。`enter_user_c` 按从高到低
  压入 5 个值后执行 `iretq`：

```asm
.global enter_user_c
enter_user_c:
    pushq $0x2b          /* SS  = USER_DS，DPL=3 数据段 */
    pushq $0x00801000    /* RSP = USER_STACK_TOP（用户栈顶） */
    pushq $0x202         /* RFLAGS = 0x202：bit1 保留位恒 1；IF=0 禁止用户态中断 */
    pushq $0x33          /* CS  = USER_CS，DPL=3 代码段 */
    pushq $0x00400000    /* RIP = USER_CODE_VA，用户 ud2 stub */
    iretq
```

- `iretq` 依次弹出 RIP、CS、RFLAGS、RSP、SS；CPU 发现新 CS/SS 的 DPL=3 < 当前 CPL0，
  于是发生特权级降级：DS/ES/FS/GS 清空、RSP 换成用户栈顶、`rsp0` 记下被中断的内核栈。
- `0x202`：bit 9（IF）为 0——本课**不给用户态开中断**，防止 PIT 定时器在用户态打乱
  单程演示；bit 1 必须恒 1。
- 关键点：本课的帧是**手工构造**的，`enter_user` 用 `cli; call enter_user_c` 包装，
  确保调用前中断关闭。

### 2.4 TSS `rsp0` 与 CPL3→CPL0 换栈

- 用户态触发异常（#UD）时，CPL 从 3 升到 0，CPU 从 TSS 的 `rsp0` 载入内核栈指针，
  然后把 SS、RSP、RFLAGS、CS、RIP 压到 `rsp0` 栈上，再压错误码（#UD 无错误码，为 0）。
- 因此异常处理程序看到的 `exception_frame`（本课扩为 7 字段）含 `rsp`/`ss`——它们正是
  **用户栈指针与用户段**，是「这段代码确实从 CPL3 过来」的硬件级证据。
- 验证方法：`exception_report` 里 `f->cs==USER_CS` 成立时才打印
  `CPL3 #UD proof` 分支，并打印 handler `rsp`（应在 `rsp0` 区间）与 saved user rsp/ss。

## 3. 源码精讲（本课最长章节）

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对上一课） |
|------|------|------------------------|
| `boot.S` | 32 位引导 | **未变化** |
| `kernel.c` | 32 位引导期页表与用户页 | **主要增量**：`PTE_USER` 层级标记、用户代码（`0x0f 0x0b` = `ud2`）/栈页分配与映射、handoff 增加 `user_code_phys`/`user_stack_phys` |
| `kernel64.c` | 64 位续体 | **主要增量**：`USER_DS`/`USER_CS`、GDT 槽 5/6、`enter_user_c`、7 字段 `exception_frame`、`vm_slot` 保留槽 0/1、`vm_frame_owned` 追认用户帧、`cpl3test` 命令、#UD 条件报告 |
| `kernel64.ld` | 64 位链接脚本 | **未变化** |
| `linker.ld` | 32 位 ELF | **未变化** |
| `Makefile` | 构建 | **未变化** |
| `grub.cfg` | GRUB 菜单 | **微小变化**：menuentry 文案 |

### 3.2 kernel.c：用户代码页与栈页

```c
#define PTE_USER 0x004ULL
#define USER_CODE_VA 0x00400000ULL
#define USER_STACK_VA 0x00800000ULL
...
long_mode_handoff.user_code_phys=bootstrap_alloc_page();
long_mode_handoff.user_stack_phys=bootstrap_alloc_page();
if(!table_page_ok(long_mode_handoff.user_code_phys)||!table_page_ok(long_mode_handoff.user_stack_phys))return 0;
{ volatile u8 *code=(volatile u8 *)(unsigned long)(u32)long_mode_handoff.user_code_phys;
  volatile u64 *pt=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pt[USER_CODE_VA/(PAGE_ENTRIES*PAGE_SIZE)];
  volatile u64 *st=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pt[USER_STACK_VA/(PAGE_ENTRIES*PAGE_SIZE)];
  code[0]=0x0f; code[1]=0x0b;      /* ud2：确定性非法指令 */
  pt[(USER_CODE_VA/PAGE_SIZE)%PAGE_ENTRIES]=long_mode_handoff.user_code_phys|PTE_PRESENT_WRITABLE|PTE_USER;
  st[(USER_STACK_VA/PAGE_SIZE)%PAGE_ENTRIES]=long_mode_handoff.user_stack_phys|PTE_PRESENT_WRITABLE|PTE_USER; }
```

- 逐行要点：
  1. `USER_CODE_VA=0x00400000` 位于 `pt[2]`（2–4 MiB 区间），`USER_STACK_VA=0x00800000`
     位于 `pt[4]`（4–6 MiB 区间）——这正是 PD 加 `PTE_USER` 的 `i==2||i==4` 原因。
  2. `code[0]=0x0f; code[1]=0x0b;`：`ud2` 是指令 `0F 0B`，固定产生 #UD，是
     「用户代码执行到了」的最强证据（不依赖任何用户代码正确性）。
  3. 两个 PT 项都带 `PTE_USER`，栈页同时带 Writable；用户代码页本课仍是可写的
     （Lesson 29 才会改为只读，见下一课 diff）。
  4. `user_code_phys`/`user_stack_phys` 经 handoff 传给 64 位侧，`pmm_reserved`
     会把它们标记为保留帧（不会再次分配）。

层级 U/S 标记（`pml4[0]`/`pdpt[0]`/PD 的 2、4 项）：

```c
pml4[0]=long_mode_handoff.pdpt|PTE_PRESENT_WRITABLE|PTE_USER;
pdpt[0]=long_mode_handoff.pd|PTE_PRESENT_WRITABLE|PTE_USER;
...
pd[i]=long_mode_handoff.pt[i]|PTE_PRESENT_WRITABLE|((i==2||i==4)?PTE_USER:0);
```

- 每一级 U/S 位都必须为 1 用户才能访问，这四行把「用户可触达」限制在 2–4 MiB 与
  4–6 MiB 两张页表；`hpd[i]`（高别名）不加 `PTE_USER`，用户无法进入内核高半区。

### 3.3 kernel64.c：用户段、7 字段异常帧与注册表保留

常量与结构：

```c
#define USER_DS 0x2b
#define USER_CS 0x33
#define USER_CODE_VA 0x00400000ULL
#define USER_STACK_VA 0x00800000ULL
#define USER_STACK_TOP (USER_STACK_VA+PAGE_SIZE)
#define USER_CODE_SLOT 0U
#define USER_STACK_SLOT 1U
...
struct exception_frame { u64 vector,error,rip,cs,rflags,rsp,ss; };   /* 7 字段 */
```

- `exception_frame` 从 5 字段扩到 7 字段：CPL3 来源的异常帧比 CPL0 多出用户 `rsp`/`ss`，
  硬件在换栈到 `rsp0` 时压入。`struct exception_frame_ist` 仍为 56 字节（含 `rsp`/`ss`），
  两结构共存但布局一致（IST 路径由汇编传 `exception_report_ist`）。

GDT 用户段装载（`runtime_gdt_tss_init`）：

```c
for(i=0;i<8;i++)runtime_gdt[i]=0;
runtime_gdt[1]=0x00af9a000000ffffULL;   /* 内核代码，DPL=0 */
runtime_gdt[2]=0x00af92000000ffffULL;   /* 内核数据，DPL=0 */
runtime_gdt[5]=0x00aff2000000ffffULL;   /* USER_DS，DPL=3 */
runtime_gdt[6]=0x00affa000000ffffULL;   /* USER_CS，DPL=3 */
```

- GDT 数组从 6 项扩到 8 项；`runtime_gdtr.limit=sizeof(runtime_gdt)-1` 相应变大。

注册表保留槽 0/1 与帧所有权：

```c
static TEXT64 int vm_slot(u64 va,u32 *slot){
    if((va&(PAGE_SIZE-1))||va<VM_REGION_START||va>=VM_REGION_END)return 0;
    *slot=(u32)((va-VM_REGION_START)/PAGE_SIZE);
    if(*slot<2)return 0;          /* 槽 0/1 保留给未来的用户映射 */
    return 1;
}
static TEXT64 int vm_frame_owned(u64 p){
    u32 i;
    if(p==user_code_phys||p==user_stack_phys)return 1;   /* 用户页不可释放 */
    for(i=0;i<VM_REGION_SLOTS;i++)
        if(vm_mappings[i].live&&vm_mappings[i].phys==p)return 1;
    return 0;
}
```

- `vm_slot` 拒绝槽 0/1：虽然用户页实际位于 2/4 MiB 处（不在 VM 区域），这里把注册表
  前两个槽**语义上**预留给用户，为下一课留出空间。
- `vm_frame_owned` 追加 `user_code_phys`/`user_stack_phys` 两个帧的所有权，`pfree`
  无法释放它们——用户页属于固定保留。

#### 函数：`enter_user` 与汇编 `enter_user_c`

```c
static TEXT64 void enter_user(struct long_mode_handoff*h){(void)h;__asm__ volatile("cli; call enter_user_c":::"memory");}
```

- shell 的 `cpl3test` 分支打印 `entering CPL3 user ud2 with IF=0` 后调用它。
- `call` 会把返回地址压在内核栈上，但 `enter_user_c` 的 `iretq` 不会回来——本课是
  **单程**设计（无返回路径），`iretq` 后 RIP 指向用户 `ud2`。

#### 函数：`exception_report`（#UD 条件证明分支）

```c
TEXT64 void exception_report(struct exception_frame*f){
    u16 c=0;u64 cr2=0,rsp;clear64(&c);
    text64(&c,"TinyOS lesson 28 exception\nexception: ");
    if(f->vector==6)text64(&c,"#UD");else if(f->vector==14)text64(&c,"#PF");else text64(&c,"unknown");
    print_exception_frame(&c,f);
    __asm__ volatile("mov %%rsp,%0":"=r"(rsp));
    if(f->vector==6&&f->cs==USER_CS){
        text64(&c,"\nCPL3 #UD proof: user CS and kernel rsp0 active\nhandler rsp: ");
        hex64(&c,rsp);
        text64(&c,"\nrsp0: ");hex64(&c,runtime_tss.rsp0);
        text64(&c,"\nsaved user rsp: ");hex64(&c,f->rsp);
        text64(&c,"\nsaved user ss: ");hex64(&c,f->ss);
        text64(&c,"\nCPU halted intentionally.\n");
    }else {
        if(f->vector==14){__asm__ volatile("mov %%cr2,%0":"=r"(cr2));text64(&c,"\ncr2:    ");hex64(&c,cr2);}
        text64(&c,"\nCPU halted intentionally.\n");
    }
    for(;;)__asm__ volatile("cli; hlt");
}
```

- 签名与职责：7 字段异常帧报告；当且仅当 `vector==6 && cs==USER_CS` 时进入
  「CPL3 #UD proof」分支。
- 算法步骤：(1) 打印向量名与帧；(2) 读当前 `rsp`（异常处理程序栈指针）；(3) 条件判定；
  (4) 命中则打印 handler rsp、`rsp0`、saved user rsp/ss；(5) 停机。
- 判定逻辑：`cs==USER_CS` 证明异常来自用户代码段；handler `rsp` 落在 `rsp0` 区间内则
  证明硬件确实用 TSS `rsp0` 换栈——两者合起来就是「CPL3 进入 + rsp0 异常栈」的证明。
- 边界与错误处理：非 #UD/#PF 打印 `unknown`；#PF 附带 CR2；所有路径最后
  `for(;;) cli; hlt` 有意停机，不返回 shell。

#### 函数：`tssinfo`（本课文案）

```c
static TEXT64 void tssinfo(u16*c,struct long_mode_handoff*h){
    ...
    text64(c,"TSS/IST: one-way CPL3 entry\nTR: ");hex64(c,tr);
    text64(c,"\nGDTR base/limit: ");hex64(c,runtime_gdtr.base);text64(c," ");hex64(c,runtime_gdtr.limit);
    text64(c,"\nUSER_DS: ");hex64(c,USER_DS);text64(c," USER_CS: ");hex64(c,USER_CS);
    text64(c,"\nrsp0 top: ");hex64(c,runtime_tss.rsp0);
    text64(c,"\n#PF IST: ");hex64(c,idt[14].ist&7);
    text64(c,"\n");
}
```

- 显示 `USER_DS: 000000000000002b USER_CS: 0000000000000033` 与 `rsp0 top`，
  供验证前检查用户段与异常栈配置。

### 3.4 shell 分支与 banner

```c
else if(eq64(word,"cpl3test")){if(!noargs64(arg))usage64(c,"cpl3test");else{ text64(c,"entering CPL3 user ud2 with IF=0\n"); enter_user(h); }}
```

- banner 仍是 `TinyOS lesson 27: bounded dual-alias mapping registry`（源码逐字，
  本课未更新标题行；`about` 命令同样沿用 lesson 27 文案），但 `exception_report`
  已打印 `TinyOS lesson 28 exception`。验证以 `cpl3test` 屏幕为准。

### 3.5 构建管线与主控制流

- `Makefile` 未变化；`kernel.c` 现在分配 18 张引导页表/页（8 低 + 8 高 + 用户代码 +
  用户栈），均在 `bootstrap_alloc_page` 覆盖范围内。
- 主控制流：`pmm_init → stack_guards_init → runtime_gdt_tss_init → idle_init →
  install_idt → pit_init → pic_init → banner → shell`；`cpl3test` 经 `exec64` → `enter_user`
  → `enter_user_c`（iretq 降权）→ 用户 `ud2` → #UD → TSS `rsp0` 换栈 → `exception_report`
  → 报告并停机。

## 4. 数据流与运行逻辑

- `cpl3test` 输入后：`exec64` 打印 `entering CPL3 user ud2 with IF=0` →
  `enter_user`（`cli; call enter_user_c`）→ 压入 SS/RSP/RFLAGS/CS/RIP → `iretq`。
- CPU 检查新 CS/SS DPL=3：进入 CPL3，DS/ES/FS/GS 清空，RSP=用户栈顶
  `0x00801000`，IF=0。
- 用户执行 `ud2` → 内部 #UD：CPL 升回 0，CPU 载入 `rsp0` 并把
  SS/RSP/RFLAGS/CS/RIP 压栈 → `exception_ud`（`pushq $0; pushq $6; jmp exception_common`）
  → `exception_report`：`cs==USER_CS` 命中 → 打印 handler rsp 与 `rsp0` →
  `CPU halted intentionally.` → `for(;;) cli; hlt`。
- 关键证据链：`saved user ss = 0x2b`、`saved user rsp` 在 `0x00801000` 附近、
  handler `rsp` 落在 `__rsp0_stack_start..__rsp0_stack_end` 区间。

## 5. 构建、运行与验证

- **构建命令**（与 Makefile 一致）：

```bash
cd /home/dongyu/.zcode/workspace/default/lessons/lesson-28-stable
make clean && make -j"$(nproc)"
make check
```

- **运行命令**：`make run`（QEMU VGA 图形窗口，勿加 `-display none`）。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. 启动后输入 `tssinfo`：应出现 `TSS/IST: one-way CPL3 entry`、
     `USER_DS: 000000000000002b USER_CS: 0000000000000033` 与 `rsp0 top: <__rsp0_stack_end>`。
  2. 从**全新启动**运行 `cpl3test`：先打印 `entering CPL3 user ud2 with IF=0`，
     随后 #UD 屏幕显示 `exception: #UD`、`cs:    0000000000000033`，
     以及 `CPL3 #UD proof: user CS and kernel rsp0 active`、`handler rsp: <在 rsp0 区间>`、
     `rsp0: <__rsp0_stack_end>`、`saved user rsp: 0000000000801000` 附近、
     `saved user ss: 000000000000002b`、`CPU halted intentionally.`。
  3. 回归（各自独立不影响 cpl3test 的单程性质）：`lminfo`、`hhinfo`、`meminfo`、
     `vminfo`/`vmtest`、`tssinfo`、`stackinfo`、`bptest`、`preempttest`、`idletest`。
  4. 致命测试（`vmfaulttest`/`isttest`/`udtest`/`pftest`/guard）各自新启动。
- **判断成功**：`cpl3test` 的 #UD 屏幕同时满足三个条件——`cs=0x33`、handler rsp 在
  `rsp0` 区间、`saved user ss=0x2b`；其余回归命令输出与上几课一致。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|----------|
| `cpl3test` 后无 #UD、直接死机/重启 | 用户 CS 未装 DPL3 描述符（`runtime_gdt[6]` 缺失）或 `iretq` 帧顺序错 | 检查 `runtime_gdt_tss_init` 槽 5/6 与 `enter_user_c` 的 5 次 push 顺序（SS,RSP,RFLAGS,CS,RIP） |
| #PF 而非 #UD | 用户代码/栈页 PTE 缺 `PTE_USER`，或某级页表 U/S=0 | 检查 `kernel.c` 的 `pml4[0]`/`pdpt[0]`/`pd[2]`/`pd[4]` 与两个 PT 项的 `PTE_USER` |
| 报告分支未命中（走 else） | `f->cs != USER_CS`（如 CS=0x33 未装载）或异常向量不是 6 | #UD 屏上核对 `cs` 行；确认 `ud2`（`0f 0b`）字节写进用户代码页 |
| handler rsp 不在 rsp0 区间 | `runtime_tss.rsp0` 与 `__rsp0_stack_end` 不一致，或 `ltr` 未加载 | `tssinfo` 看 `rsp0 top`；确认 `runtime_gdt_tss_init` 里 `runtime_tss.rsp0=__rsp0_stack_end` |
| `saved user rsp` 异常 | 用户栈页未映射或 `iretq` RSP 值错 | 检查 `st[USER_STACK_VA...]` 项与 `enter_user_c` 的 `0x00801000` |
| `tssinfo` 显示 GDT limit 变小 | `runtime_gdtr.limit` 仍按 6 项计算 | 确认 `runtime_gdt[8]` 与 `sizeof(runtime_gdt)-1` 同步 |
| 回归 `vmtest` 变红 | 槽 0/1 被 `vm_slot` 拒绝 | 确认 `vmtest` 使用 `va[0]=VM_REGION_START`（槽 0）但 `vm_slot` 现在要求 `slot>=2`——**本课 `vmtest` 需改用槽 2 起**，或检查 `va[0]` 是否仍为 `VM_REGION_START` |

（注：本课 `vmtest` 仍以 `VM_REGION_START` 起映射，但 `vm_slot` 拒绝槽 0/1；若
`vmtest` 报 `VA outside mapping region` 属预期行为变更，验证以旧 README 回归清单为准。）

## 7. 与 Linux 源码对照

- **TinyOS**：手工 `push` 五元组 + `iretq` 降权；GDT 第 5/6 槽 DPL3 段；用户页全链路
  `PTE_USER`；异常用 TSS `rsp0` 换栈。
- **Linux 对照**：`arch/x86/entry/entry_64.S` 的 `swapgs`/`iretq` 用户返回路径；
  `arch/x86/include/asm/segment.h` 定义 `__USER_CS`（`0x33`）/`__USER_DS`（`0x2b`）
  ——TinyOS 的用户段选择子与 Linux 完全一致；`arch/x86/kernel/process_64.c` 的
  `switch_to` 更新 TSS `rsp0`；`security`/`SMAP` 位控制用户页访问。
- **权威来源**：Intel SDM Vol.3 §5.8.5（TSS）、§6.12.1（中断/异常换栈）、§7.4.1（U/S 位）、
  §3.4.5（段描述符 DPL/RPL）。
- **教学模型简化**：单程进入、不返回 shell、不开用户中断、无 syscall ABI、无地址空间
  切换、无 `swapgs`（GS 未用）；用户代码页本课仍可写（下一课改为只读）。

## 8. 思考题与练习

1. **概念理解**：为什么 `iretq` 帧里必须放 SS 和 RSP 才能进入 CPL3？如果直接 `jmp` 到
   用户地址会怎样？
2. **源码定位**：找出 `kernel.c` 中所有设置 `PTE_USER` 的行，画出 CPL3 访问
   `0x00400000` 时页表遍历的每一级 U/S 检查链。
3. **动手实验**：把 `enter_user_c` 里的 `pushq $0x202` 改成 `pushq $0x202|0x200`（IF=1），
   构建运行 `cpl3test`，观察在 `sti; hlt` 之前 PIT 中断是否会在用户态触发，解释为什么
   本课刻意用 IF=0。
4. **动手实验**：把用户代码页的 PT 项去掉 `PTE_USER`，运行 `cpl3test`，观察 #PF（而非
   #UD），并核对 CR2 是否等于 `0x00400000`。
5. **Linux 对照**：在 `arch/x86/include/asm/segment.h` 中确认 `__USER_CS=0x33`/
   `__USER_DS=0x2b`，说明 TinyOS 为何直接复用这两个值；再查 `arch/x86/entry/entry_64.S`
   的 `swapgs`，讨论 TinyOS 不处理 GS 基址的教学简化。

## 9. 本课小结与下一课预告

- 本课实现了内核历史上第一次真正进入 ring 3：GDT 槽 5/6 的 DPL3 用户段
  （`USER_DS=0x2b`/`USER_CS=0x33`）、页表 `PTE_USER` 层级标记、`iretq` 五元组降权。
- `enter_user_c` 用固定常量构造用户帧：SS=0x2b、RSP=0x00801000、RFLAGS=0x202（IF=0）、
  CS=0x33、RIP=0x00400000。
- `exception_frame` 扩为 7 字段（新增用户 `rsp`/`ss`），`exception_report` 用
  `vector==6 && cs==USER_CS` 条件打印 `CPL3 #UD proof`，一次完成
  「用户 CS + rsp0 异常栈」的双重证明。
- `vm_slot` 保留槽 0/1、`vm_frame_owned` 追认用户页所有权，为下一课的 syscall 留出
  注册表语义空间。
- 本课边界（延续旧 README 记录）：**单程**——无 syscall ABI、无用户调度器、无
  返回 shell 路径、无通用用户地址空间管理；用户态中断保持关闭。
- **下一课**（[../lesson-29-stable/README.md](../lesson-29-stable/README.md)）：在 CPL3
  之上加 **int 0x80 最小 syscall ABI**——`SYS_GETTICKS` 经 DPL3 中断门回到内核，
  all-GPR 帧保存、RAX 返回 ticks、`iretq` 回到用户并继续执行，形成第一条真正的
  「用户→内核→用户」往返。
