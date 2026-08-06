# Lesson 31: 受控用户返回与 SYS_EXIT 终止路径 — 精讲文档

> **课号**：Lesson 31
> **主题**：受控用户返回与 SYS_EXIT 终止路径
> **课程主线位置**：第 2 阶段「64 位内核、异常、中断与调度」（08–31）的最后一课，
> 也是「CPL3 用户态」支线的收尾课。
> **前置课程**：[Lesson 30（有界 syscall dispatcher 与错误返回）](../lesson-30-stable/README.md)
> **后续课程**：[Lesson 32（校验后的内置用户程序镜像与最小加载器）](../lesson-32-stable/README.md)
> **一句话目标**：学完本课，你能说清「用户程序调用 `int 0x80` 的 5 个调用点
> （0、1、2、99、3）中，前 4 次如何经 all-GPR 帧 + `iretq` 返回用户态，
> 第 3 号 `SYS_EXIT` 如何走受控终止路径并有意停机」。

---

## 1. 课程定位（Mission）

- **一句话目标**：在 Lesson 30 的 bounded dispatcher 之上，为 syscall 表新增
  `SYS_EXIT`（3），实现「用户主动请求受控退出 → 内核验证返回帧 → 报告 → 有意停机」
  的完整终止路径；前 4 个调用（GETTICKS、GETPID、WRITE_CONSOLE、未知号 99）
  仍按原路经 all-GPR 帧 + `iretq` 正常返回用户态。
- **课程主线位置**：本课属于第 2 阶段（08–31）的 CPL3 支线。该支线的递进是——
  Lesson 28 首次 CPL3 进入（`enter_user_c` 的 `iretq` 下放）、Lesson 29 最小
  `int 0x80` syscall ABI（DPL3 gate + all-GPR 保存）、Lesson 30 有界 dispatcher
  （含 `-ENOSYS` 错误返回）、本课加入「终止路径」。放在这里的原因：内核必须先有
  「安全进入用户态 + 安全返回内核」的完整闭环，才能让用户程序以受控方式退出，
  为第 3 阶段（32–60）的「加载用户程序、进程模型、地址空间隔离」打地基。
- **前置知识清单**（至少 3 项）：
  1. `int 0x80` DPL3 中断门如何把 CPL3 代码带入内核，CPU 用 TSS rsp0 换栈
     （Lesson 24/28/29）；
  2. `struct syscall_frame` 的 20 个 qword 布局与 `syscall_entry` 汇编的
     压栈/弹栈顺序（Lesson 29）；
  3. 有界 syscall 表：`SYS_GETTICKS`(0)、`SYS_GETPID`(1)、`SYS_WRITE_CONSOLE`(2)、
     未知号返回 `-ENOSYS`（Lesson 30）；
  4. 用户代码页只读映射、用户栈可写映射、用户帧 IF=0 策略（Lesson 28/29）。
- **本课交付**（可见结果）：启动后 VGA 显示 banner
  `TinyOS lesson 31: controlled user return and SYS_EXIT`；执行 `cpl3test` 后，
  屏幕上依次出现 4 份 `syscall dispatcher` 报告（调用 0、1、2、99 各一份），
  最后调用 3（EXIT）打印 `TinyOS lesson 31 SYS_EXIT` 三段报告并**有意停机**。

---

## 2. 核心概念精讲

### 2.1 系统调用号与 syscall 表：为什么要给 3 号预留位置

**定义**：syscall 号是用户在 `EAX`/`RAX` 中写入的整数，内核 dispatcher 以它作为
`switch` 的索引。Lesson 31 把 3 号正式定义为 `SYS_EXIT`。

**为什么需要**：Lesson 30 只有 0、1、2 三个合法调用 + 未知号 `-ENOSYS`。一个
「只会问问题、从不退场」的用户程序无法构成受控的生命周期。`SYS_EXIT` 是用户
程序的**终止接口**——它告诉内核「我要结束运行」，内核负责做收尾动作。

**工作机制**：`kernel64.c` 中：

```c
#define SYS_GETTICKS 0U
#define SYS_GETPID 1U
#define SYS_WRITE_CONSOLE 2U
#define SYS_EXIT 3U
#define ENOSYS 38
```

dispatcher 里 `SYS_EXIT` 有独立的 `case`：

```c
static TEXT64 u64 syscall_dispatch(struct syscall_frame*f,u16*c){switch((u32)f->rax){
case SYS_GETTICKS:return ticks;
case SYS_GETPID:return FIXED_PID;
case SYS_WRITE_CONSOLE:text64(c,"kernel-owned console message\n");return 0;
case SYS_EXIT:return 0;
default:return (u64)(-(s64)ENOSYS);}}
```

注意 `case SYS_EXIT:return 0;` 只是让 dispatcher 逻辑完整（任何号都有归宿）；
真正决定「不返回用户态」的是 `syscall_report` 中更早的分支（见 2.4）。
边界处理：未知号（如 99）仍然落到 `default` 返回 `-ENOSYS`，说明加入 EXIT
**不会**改变「未知号 → 错误返回」的既有语义。

### 2.2 all-GPR syscall frame：为什么保存 15 个 GPR + 5 个 CPU 字段

**定义**：`struct syscall_frame` 是 `int 0x80` 从 CPL3 进内核时，由 `syscall_entry`
汇编压栈 15 个通用寄存器、CPU 再自动压入 `rip/cs/rflags/rsp/ss` 形成的 20 个
qword 帧：

```c
struct syscall_frame { u64 r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rdx,rcx,rbx,rax,rip,cs,rflags,rsp,ss; };
_Static_assert(sizeof(struct syscall_frame)==20*sizeof(u64),"syscall frame");
```

**为什么需要**：返回用户态时 `iretq` 必须精确恢复 `rip/cs/rflags/rsp/ss`（特权级
返回的 5 元组），而用户程序的寄存器现场（如 `RAX` 里的返回结果、`RBX` 等）必须
原样交还，否则用户代码无法继续。这 20 个 qword 是「内核 → 用户 → 内核 → 用户」
往返的完整契约。

**工作机制**（`kernel64.c` 尾部汇编，逐行语义）：

```asm
.global syscall_entry
syscall_entry:
pushq %rax  ; 15 个 GPR 依次入栈：rax rbx rcx rdx rbp rsi rdi r8 r9 r10 r11 r12 r13 r14 r15
...
cld               ; 清方向位，保证按地址增序处理帧
movq %rsp,%rdi    ; RDI = frame 指针（SysV 第一个参数）
call syscall_report
movq 112(%rdi),%rax  ; 把 dispatcher 写入 f->rax 的结果装回 RAX
...
popq %r15 ... popq %rbx  ; 弹回 14 个 GPR
addq $8,%rsp      ; 跳过栈上的 rax 槽（值已从 frame 恢复）
iretq             ; 弹出 CPU 压入的 rip/cs/rflags/rsp/ss，返回用户态
```

（说明：CPU 在 `int 0x80` 时先压入 `rip/cs/rflags/rsp/ss` 5 元组，随后汇编依次压入
rax、rbx、…、r15 共 15 个 GPR，所以 `struct syscall_frame` 首字段是 r15，`rax` 位于
第 15 个 GPR 槽，即 qword 偏移 14×8=112。`movq 112(%rdi),%rax` 正是把 dispatcher
写入 `f->rax` 的结果装回 RAX。）

### 2.3 IF=0 用户帧策略：为什么用户态关中断仍然成立

**定义**：`enter_user_c` 手工构造返回帧时把 RFLAGS 压成 `0x002`（只设保留位 1，
IF=0）：

```asm
.global enter_user_c
enter_user_c:
pushq $0x2b      ; SS = USER_DS
pushq $0x00801000; RSP = USER_STACK_TOP（用户栈页顶）
pushq $0x002     ; RFLAGS = 0x002（IF=0，用户态不允许被外部中断打断）
pushq $0x33      ; CS = USER_CS
pushq $0x00400000; RIP = USER_CODE_VA（用户代码页）
iretq
```

**为什么需要**：本课（以及 29/30 课）的定位是「受控的 syscall 闭环」，尚未实现
「用户态收到硬件中断并保存/恢复用户帧」的能力（那是 Lesson 35 的事）。IF=0 保证
用户代码执行期间外部中断被屏蔽，IRQ0 不会在用户态触发，也就不会出现「用户帧被
抢占后无法恢复」的未定义局面。

### 2.4 终端状态（intentional halt）：SYS_EXIT 为什么「不返回」

**定义**：`syscall_report` 在分派前先检查系统调用号；若为 `SYS_EXIT`，直接进入
受控终止路径并**永不返回用户态**。

**为什么需要**：真实 OS 中 `exit_group()` 之后进程进入僵尸态，等待父进程
`wait()` 回收。TinyOS 教学模型这一步先退化成「报告 + 停机」：内核确认用户返回帧
合法（`user return frame is valid`），然后无限循环 `cli; hlt` 有意停机，表明
「终止已受理，不再回到用户程序」。

```c
TEXT64 void syscall_report(struct syscall_frame*f){u16 c=0;u64 number=f->rax,result;
clear64(&c);
if((u32)f->rax==SYS_EXIT){
  text64(&c,"TinyOS lesson 31 SYS_EXIT\nuser requested controlled exit\nuser return frame is valid; halting intentionally\n");
  for(;;)__asm__ volatile("cli; hlt");
}
result=syscall_dispatch(f,&c);
...
}
```

**边界与错误处理**：`for(;;) cli; hlt` 是不可中断的自旋停机——`cli` 屏蔽中断、
`hlt` 暂停 CPU，二者循环不会因任何中断苏醒；这是「终止后不再执行用户代码」的
强保证。`hlt` 停在这个点即验证成功（QEMU 画面冻结在 EXIT 报告上）。

### 2.5 5 段用户 stub：调用序列的字节级构造

**定义**：用户代码不是编译出来的，而是 `kernel.c` 里用 `mov` 立即数 + `int 0x80`
的机器码按字节写入代码页：

```c
u32 calls[5]={0,1,2,99,3};
for(;i<5;i++){
  code[i*7]=0xb8;                    /* mov eax, imm32 */
  code[i*7+1]=(u8)calls[i]; ...      /* imm32 低 4 字节 */
  code[i*7+5]=0xcd; code[i*7+6]=0x80;/* int 0x80 */
}
code[35]=0xeb;code[36]=0xfe;         /* jmp $：5×7=35，自旋 */
```

每段 7 字节：`b8 <imm32> cd 80`。第 5 段之后写入 `jmp .`（`eb fe`）自旋——
本意是「万一 EXIT 返回，也不落到代码页之外」。验证边界：调用 3（EXIT）根本不会
返回，`jmp .` 只是保险丝。

---

## 3. 源码精讲（本课最长的章节）

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 30） |
|---|---|---|
| `kernel64.c` | 64 位主内核：IDT/PIC/PIT/调度/VM/syscall | **核心增量**：`SYS_EXIT` 宏、dispatcher 新增 `case SYS_EXIT`、`syscall_report` 增加 EXIT 终止分支、banner/syscallinfo/cpl3test 文案更新 |
| `kernel.c` | 32 位引导：长模式页表 + 用户代码页/栈页映射 | **小增量**：用户 stub 从 4 个调用改为 5 个（新增 3=EXIT），循环自旋移到 offset 35 |
| `boot.S` | Multiboot2 头 + 进 long mode | 未变化 |
| `Makefile` | 构建 `kernel.iso` | 未变化 |
| `kernel64.ld` | 64 位裸机链接脚本（guard 栈布局） | 未变化 |
| `linker.ld` | 外层 ELF 布局 | 未变化 |
| `grub.cfg` | GRUB 菜单 | 未变化（菜单文案仍写 Lesson 29，属历史遗留文案） |

### 3.2 kernel64.c 精讲

#### 3.2.1 宏与常量

```c
#define SYS_GETTICKS 0U
#define SYS_GETPID 1U
#define SYS_WRITE_CONSOLE 2U
#define SYS_EXIT 3U     /* 本课新增：第 3 号系统调用 = 受控退出 */
#define ENOSYS 38
```

- 3 号是新增的唯一合法号；`ENOSYS=38` 与 Linux `errno.h` 一致。
- `FIXED_PID 1ULL` 沿用（GETPID 恒返回固定 pid 1）。

#### 3.2.2 syscall_dispatch —— 有界 syscall 表（本课增量行）

```c
static TEXT64 u64 syscall_dispatch(struct syscall_frame*f,u16*c){switch((u32)f->rax){
case SYS_GETTICKS:return ticks;
case SYS_GETPID:return FIXED_PID;
case SYS_WRITE_CONSOLE:text64(c,"kernel-owned console message\n");return 0;
case SYS_EXIT:return 0;   /* 本课新增：EXIT 在表内有明确归宿，不再落到 default */
default:return (u64)(-(s64)ENOSYS);}}
```

- **签名与职责**：输入 syscall 帧指针 `f` 与输出游标 `c`；按 `f->rax` 的低 32 位
  分派，返回 64 位结果。
- **算法步骤**：① 取 `f->rax`；② `switch` 命中 0/1/2/3 之一；③ 未知号回退
  `default` 返回 `-ENOSYS`（`(u64)(-(s64)ENOSYS)` 是先取负再做符号扩展，保证
  高位为全 1）。
- **边界检查**：`(u32)f->rax` 只取低 32 位，避免用户在高位带垃圾值时误判；
  没有第四种已知号之外的情况。
- **为什么这样设计**：与 Linux `sys_call_table[]` 数组分发（`arch/x86/entry/`）
  同源；教学用 `switch` 保持表项可见。EXIT 在表内返回 0，但真实退出动作由
  `syscall_report` 提前接管，避免「EXIT 先打印 dispatcher 报告再停机」的双重输出。

#### 3.2.3 syscall_report —— syscall 报告 + EXIT 终止路径（本课核心增量）

```c
TEXT64 void syscall_report(struct syscall_frame*f){u16 c=0;u64 number=f->rax,result;
clear64(&c);
if((u32)f->rax==SYS_EXIT){
  text64(&c,"TinyOS lesson 31 SYS_EXIT\nuser requested controlled exit\nuser return frame is valid; halting intentionally\n");
  for(;;)__asm__ volatile("cli; hlt");
}
result=syscall_dispatch(f,&c);
text64(&c,"TinyOS lesson 31 syscall dispatcher\nsyscall number: ");
hex64(&c,number);text64(&c,"\nreturn rax: ");f->rax=result;
hex64(&c,f->rax);
text64(&c,"\nuser rip: ");hex64(&c,f->rip);text64(&c,"\nuser cs: ");
hex64(&c,f->cs);text64(&c,"\nuser rsp: ");hex64(&c,f->rsp);
text64(&c,"\nuser ss: ");hex64(&c,f->ss);
text64(&c,"\nall-GPR frame; returning with iretq; user IF remains disabled\n");
}
```

- **签名与职责**：被 `syscall_entry` 以 `frame` 为参数调用；打印一次系统调用的
  完整报告；对 EXIT 走终止路径。
- **输入输出**：输入 `f`（20 qword 的 syscall 帧）；通过 `f->rax=result` 把结果
  写回帧，供汇编 `movq 112(%rdi),%rax` 取走；VGA 输出到 `clear64` 清屏后的画面。
- **算法步骤**：① `clear64` 清屏；② 若 `f->rax==SYS_EXIT`，打印三段式 EXIT 报告
  （课号 + 「user requested controlled exit」+「user return frame is valid;
  halting intentionally」）后 `cli; hlt` 停机；③ 否则分派、写回结果、打印
  dispatcher 报告（课号、syscall number、return rax、user rip/cs/rsp/ss、以及
  「all-GPR frame; returning with iretq; user IF remains disabled」）。
- **边界与错误处理**：EXIT 分支在 `syscall_dispatch` 之前，保证 EXIT 不打印普通
  dispatcher 报告；`for(;;) cli; hlt` 是终止的硬边界。
- **为什么这样设计**：把「验证帧合法」以文案形式固化在终止路径上（本课是
  「declared valid」，真正的校验来自 Lesson 34 的 `user_context_valid`）；
  停机而非 `hlt` 单条，是为了让 CPU 在任何被唤醒事件下都不会复活。

#### 3.2.4 exec64 的文案增量

- `syscallinfo` 输出更新为：
  `syscalls: 0=GETTICKS 1=GETPID 2=WRITE_CONSOLE 3=EXIT; unknown=-ENOSYS`
  以及 `WRITE_CONSOLE uses a fixed kernel-owned message and no user pointer`、
  `EXIT reports and intentionally halts; no user IRQ or scheduler handling`。
- `cpl3test` 分支入口文案更新为：
  `entering CPL3 syscall stub with IF=0; calls 0,1,2,99,3 (EXIT)`，
  随后 `enter_user(h)` 进用户态。

#### 3.2.5 kernel_main64_binary —— 启动 banner（本课增量）

```c
text64(&c,"TinyOS lesson 31: controlled user return and SYS_EXIT\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR frame and IF=0\n");
```

其余初始化序列（`pmm_init` → `stack_guards_init` → `runtime_gdt_tss_init` →
`idle_init` → `install_idt` → `pit_init` → `pic_init`）与 Lesson 30 完全一致，
`install_idt` 中 `set_gate(&idt[0x80],runtime_syscall_address(),0); idt[0x80].type=0xee;`
的 DPL3 中断门照旧。

### 3.3 kernel.c 精讲（用户 stub 的 5 调用点）

```c
{ volatile u8 *code=(volatile u8 *)(unsigned long)(u32)long_mode_handoff.user_code_phys;
  volatile u64 *pt=...pt[USER_CODE_VA/...]; volatile u64 *st=...pt[USER_STACK_VA/...];
  u32 i=0; u32 calls[5]={0,1,2,99,3};
  for(;i<5;i++){
    code[i*7]=0xb8; code[i*7+1]=(u8)calls[i];
    code[i*7+2]=(u8)(calls[i]>>8); code[i*7+3]=(u8)(calls[i]>>16);
    code[i*7+4]=(u8)(calls[i]>>24); code[i*7+5]=0xcd; code[i*7+6]=0x80;
  }
  code[35]=0xeb; code[36]=0xfe;   /* jmp $：5×7=35 */
  pt[(USER_CODE_VA/PAGE_SIZE)%PAGE_ENTRIES]=...user_code_phys|PTE_PRESENT|PTE_USER;
  st[(USER_STACK_VA/PAGE_SIZE)%PAGE_ENTRIES]=...user_stack_phys|PTE_PRESENT_WRITABLE|PTE_USER;
}
```

- **算法步骤**：① `calls[5]` 定义调用序列 `0,1,2,99,3`（本课由 4 项扩为 5 项，
  把 3=EXIT 追加到最后）；② 每个调用写 7 字节 `mov eax,imm32; int 0x80`；
  ③ 全部写完在 offset 35 写 `eb fe`（`jmp $`）兜底；④ 把代码页映射为
  `PTE_PRESENT|PTE_USER`（只读），栈页映射为 `PTE_PRESENT_WRITABLE|PTE_USER`。
- **边界检查**：5×7=35 字节，加上 2 字节 `jmp` 共 37 字节，远小于一页（4096），
  不会越界；`0xeb 0xfe` 是相对偏移 -2，正好自指。
- **为什么这样设计**：先手工铺设 4 个调用、再在本课追加第 5 个，说明「用户
  程序内容」第一次可以被渐进扩展；而真正的「内核内嵌镜像 + 校验」要到 Lesson 32
  才实现。

### 3.4 构建管线（Makefile / linker）

与 Lesson 30 完全一致，无新增目标：

| 目标 | 含义 |
|---|---|
| `$(BUILD)/kernel64.o` | `gcc -m64 -ffreestanding -fpie -fno-stack-protector -mno-red-zone ...` 编译 64 位内核 |
| `$(BUILD)/kernel64.bin` | `ld -m elf_x86_64 -T kernel64.ld -nostdlib` 链接后 `objcopy -O binary` 得到裸二进制 |
| `$(BUILD)/boot.o` | `gcc -m32 ...` 编译 `boot.S`（`.incbin "build/kernel64.bin"` 嵌入 64 位二进制） |
| `$(BUILD)/kernel.iso` | `grub-mkrescue` 打包 ISO |
| `check` | `grub-file --is-x86-multiboot2` 校验 Multiboot2 头 |

`-mno-red-zone` 保证中断帧不会被用户/中断代码的红区覆盖；`kernel64.ld` 用
`ASSERT(...==0x1000)` 固定三个 guard 栈尺寸。本课无新增构建步骤。

### 3.5 主控制流

```
_start (boot.S, 32 位)
  └─ kernel_main32 (kernel.c) → setup_long_mode_tables → 铺设 5 段用户 stub
  └─ enter_long_mode → kernel_main64 (boot.S)
        └─ kernel_main64_binary
              ├─ pmm_init / stack_guards_init / runtime_gdt_tss_init / idle_init
              ├─ install_idt（含 DPL3 int 0x80 gate）
              ├─ pit_init / pic_init → banner → shell 循环
              └─ 输入 cpl3test → exec64 → enter_user(h)
                    └─ enter_user_c (汇编 iretq) → 用户 stub
                          ├─ int 0x80 (eax=0) → syscall_entry → syscall_report → iretq → 用户
                          ├─ int 0x80 (eax=1) → ...（同上，2、99 同理）
                          └─ int 0x80 (eax=3) → syscall_report → SYS_EXIT 分支
                                └─ 打印 EXIT 报告 → cli; hlt（停机）
```

---

## 4. 数据流与运行逻辑

1. **启动**：GRUB 加载 `kernel.elf` → `_start`（boot.S）→ `kernel_main32`
   （kernel.c）把 5 段调用 stub 写入 `user_code_phys` 页并映射代码页/栈页 →
   `enter_long_mode` 进 64 位 → `kernel_main64_binary` 初始化并显示 banner。
2. **命令输入**：`cpl3test` 匹配 `exec64` 中 `eq64(word,"cpl3test")` 分支，
   打印 `entering CPL3 syscall stub with IF=0; calls 0,1,2,99,3 (EXIT)`。
3. **进用户态**：`enter_user(h)` → 汇编 `cli; call enter_user_c`；`enter_user_c`
   压入 `SS/RSP/RFLAGS/CS/RIP` 后 `iretq`，CPU 跳到 `0x00400000`（USER_CODE_VA）
   的用户 stub。
4. **调用 0（GETTICKS）**：`int 0x80` → CPU 换栈到 rsp0 → `syscall_entry` 压 15
   个 GPR → `syscall_report` 分派返回 `ticks` → `f->rax=result` → 汇编恢复并
   `iretq` 回到用户下一条指令。
5. **调用 1、2、99**：同理；99 落入 `default` 返回 `-ENOSYS`（0xffffffffffffffda）。
6. **调用 3（EXIT）**：`syscall_report` 首行判断 `f->rax==SYS_EXIT` → 打印
   `TinyOS lesson 31 SYS_EXIT` / `user requested controlled exit` /
   `user return frame is valid; halting intentionally` → `for(;;) cli; hlt`。
   画面冻结，用户 stub 末端的 `jmp .` 永远不会执行。

---

## 5. 构建、运行与验证

**依赖**：`gcc`、`binutils`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
（与 Lesson 30 相同）。

**构建与格式校验**（与 Makefile 一致）：

```bash
make clean && make -j"$(nproc)"
make check
```

`make check` 通过时输出：

```
Multiboot2 header check passed.
```

**运行**（成功画面在 QEMU 图形窗口，勿加 `-display none`）：

```bash
make run
```

**验证步骤**（全新启动后）：

1. 确认 banner（逐字摘自 `kernel_main64_binary`）：

   ```
   TinyOS lesson 31: controlled user return and SYS_EXIT
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR frame and IF=0
   ```

2. 运行 `idtinfo` 确认 `int 0x80` 门：

   ```
   int 0x80 vector: 0000000000000080 DPL: 0000000000000003 gate: interrupt
   ```

3. 运行 `cpl3test`，首行（逐字摘自 `exec64`）：

   ```
   entering CPL3 syscall stub with IF=0; calls 0,1,2,99,3 (EXIT)
   ```

4. 随后画面逐次出现 4 份 dispatcher 报告（课号逐字摘自 `syscall_report`）：
   `syscall number: 0000000000000000` → `return rax: 0000000000000XXX`（ticks）、
   `syscall number: 0000000000000001` → `return rax: 0000000000000001`、
   `syscall number: 0000000000000002`（并打印
   `kernel-owned console message`）、`syscall number: 0000000000000063` →
   `return rax: ffffffffffffffda`（`-ENOSYS`）。每份报告末尾都是
   `all-GPR frame; returning with iretq; user IF remains disabled`。
5. 最后 EXIT 报告（逐字摘自 `syscall_report`）：

   ```
   TinyOS lesson 31 SYS_EXIT
   user requested controlled exit
   user return frame is valid; halting intentionally
   ```

   随后 QEMU 画面冻结（CPU 停在 `cli; hlt`），不再出现 `tinyos>` 提示符——
   这是**预期成功画面**。若还要继续验证其他命令，请重新 `make run` 冷启动。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 第 5 次调用后仍回到 `tinyos>` | `SYS_EXIT` 分支未命中：`syscall_report` 里判断的是 `(u32)f->rax`，而用户压的是 0 而非 3 | 检查 `calls[5]` 末位是否为 3；检查 `SYS_EXIT 3U` 宏 |
| EXIT 报告出现两次或混有 dispatcher 报告 | EXIT 分支写在 `syscall_dispatch` 之后 | 确认 EXIT 分支在 `result=syscall_dispatch(...)` **之前** |
| 只看到 4 份报告，EXIT 报告缺失且画面直接冻结 | 用户 stub 第 5 段偏移写错（如仍用 `calls[4]`） | 数 `i*7`：5×7=35 字节，`code[35]=0xeb` |
| `return rax` 显示 `0000000000000063` 而非结果 | `movq 112(%rdi),%rax` 偏移错 | 数帧布局：rax 是第 15 个压入的 GPR，位于 offset 112 |
| 进用户态后立即 #PF 或三线性错误 | 用户代码页 PTE 未带 `PTE_USER` | 检查 `pt[...]=...user_code_phys|PTE_PRESENT|PTE_USER` |
| 画面每 10ms 闪动或调度行为出现 | 用户帧 IF 不是 0 | 检查 `enter_user_c` 压入的 RFLAGS 是否为 `0x002` |
| `cpl3test` 文案与旧版一样 | 未重新编译或 GRUB 菜单缓冲旧 ISO | `make clean && make -j"$(nproc)"` 后重跑 |

---

## 7. 与 Linux 源码对照

| TinyOS 教学模型 | Linux 对应实现 | 教学模型简化了什么 |
|---|---|---|
| `int 0x80` DPL3 中断门 + all-GPR `struct syscall_frame` | `arch/x86/entry/entry_64.S` 的 `entry_SYSCALL_64` / `pt_regs` | Linux 用 `syscall` 指令与 MSR（STAR/LSTAR），entry 有 iret/paranoid 多路径；TinyOS 只用中断门一条路 |
| `switch` 型有界 syscall 表，未知号返回 `-ENOSYS` | `arch/x86/entry/syscall_64.tbl` + `SYSCALL_DEFINE` 宏展开的表 | Linux 表有 400+ 项且可被 seccomp/audit 过滤；TinyOS 只有 0–3 四表项 |
| `SYS_EXIT` 报告后 `cli; hlt` 停机 | `kernel/exit.c` 的 `do_exit()` / `make_task_dead()` | Linux 回收 mm、file、信号等资源并让父进程 wait；TinyOS 尚未实现资源回收，以「有意停机」作为终止的锚点 |
| 固定 PID 1、固定用户代码页 | `pid 1 = init` 进程 | Linux pid 分配器按 `alloc_pid()` 编号；TinyOS 是常量 |

权威来源：Intel SDM Vol.3 §6.12（中断门/特权转移）、§7.4（TSS/RSP 切换）、
GNU GRUB Multiboot2 规范（DPL3 门与交接）。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `SYS_EXIT` 的终止分支必须放在 `syscall_dispatch` 调用之前？
   如果放在之后会有什么可观察的错误？
2. **源码定位**：在 `kernel64.c` 中找出「把 dispatcher 返回值写回 RAX」的两处代码
   （C 侧 `f->rax=result` 与汇编侧 `movq 112(%rdi),%rax`），说明 112 这个偏移是怎么
   算出来的。
3. **动手实验**：把 `calls[5]` 里的最后一个元素从 3 改成 4，重新构建运行
   `cpl3test`，观察第 5 次调用返回什么、画面是否还冻结——并解释为什么。
4. **动手实验**：把 `enter_user_c` 中的 `0x002` 改成 `0x202`（IF=1）后重跑
   `cpl3test`，观察 IRQ0 在用户态触发后会发生什么（本课预期是进入未定义路径，
   借此理解 Lesson 35 为什么要做 CPL3-origin IRQ0）。
5. **Linux 对照**：阅读 `kernel/exit.c` 中 `do_exit()` 的前几行，列出 Linux 退出
   时至少 4 类要回收的资源，并对照说明 TinyOS 本课「刻意不做回收」的理由。

---

## 9. 本课小结与下一课预告

- 本课为 syscall 表补齐了第 3 号 `SYS_EXIT`，用户 stub 扩展为 0、1、2、99、3
  五个调用点。
- 前四个调用仍走「all-GPR 帧 + `iretq`」的正常返回路径，用户帧 IF=0 策略不变，
  印证了 Lesson 29/30 的 ABI 契约可被反复使用。
- `SYS_EXIT` 是第一个「不返回用户态」的系统调用：它验证（声明）返回帧合法、
  打印三段式报告，然后 `cli; hlt` 有意停机，为后续「进程退出 + 回收」提供了
  终止语义的锚点。
- 本课仍然没有用户程序镜像、进程对象、用户态中断——用户代码还是引导阶段
  手工铺设的 37 字节 stub。

**下一课（Lesson 32）**：把这段手工 stub 换成**内核内嵌的用户镜像**（带 magic /
version / size / entry 的 `user_image_code[]` 数组 + 描述符），加载前先做完整校验，
任何校验失败都会在进入 CPL3 之前报告 `user image validation/load failure`——
「用户程序从哪来」将由可验证的数据结构而非手工字节决定。
