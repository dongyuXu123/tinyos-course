# Lesson 159: syscall 安全边界 — 精讲文档

> **课号**：Lesson 159（可执行课，checkpoint 快照）
> **主题**：syscall 安全边界——把内核里「用户 → 内核」系统调用入口处的既有防护
> （int 0x80 DPL=3 门、`syscall_dispatch` 对未知号返回 `-ENOSYS`、`WRITE_CONSOLE`
> 拒绝用户指针、`user_context_valid` 校验返回现场）串成一条「安全边界」链，并追加
> 确定性校验的 checkpoint 模型 `lesson_152_model`。
> **课程主线位置**：资源/安全主题的「检查点课」序列（Lesson 157–162），位于
> Lesson 158（capability 权限检查）之后、Lesson 160（审计事件缓冲区）之前。
> **前置课程**：[`lesson-158-stable/README.md`](../lesson-158-stable/README.md)
> **后续课程**：[`lesson-160-stable/README.md`](../lesson-160-stable/README.md)
> **一句话目标**：学完本课你能说清 TinyOS 的「syscall 安全边界」由哪几道防线组成
> ——入口门怎么装、未知 syscall 号怎么拒绝、`WRITE_CONSOLE` 为什么不碰用户指针、
> 返回现场怎么校验，以及 `l159test` 校验了什么。

---

## 1. 课程定位（Mission）

**一句话目标**：读透内核里继承下来的 syscall 入口边界机制——从 `install_idt` 装
`int 0x80` 门（DPL=3）到 `syscall_entry` 汇编 stub 压栈，再到 `syscall_dispatch` 按
号分派、未知号回 `-ENOSYS`、`syscall_report` 的受控退出路径与 `user_context_valid`
的返回现场校验——理解本课新增的确定性 checkpoint 模型 `lesson_152_model` 及其
`l159test` 断言，并会用 `l151test`、`l159test`、`syscallinfo`、`cpl3test`、
`userreturntest`、`signaltest` 等命令复现。

- **在课程主线中的位置**：与 Lesson 157–158、160–162 同属「资源/安全主题的检查点
  课」，相邻课 `kernel64.c` 的 diff 通常只有几行（本课相对 Lesson 158 仅 4 处改动：
  `l158test`→`l151test` 改名、新增 `struct lesson_152_model` 与 `l159test`、
  exec64/about/banner 文案）。syscall 边界机制代码全部继承自早期课程，检查点课的
  作用是把「已经实现的机制」用断言固化。
- **前置知识清单**：
  1. syscall 装配链路：`install_idt` 中 `set_gate(&idt[0x80],runtime_syscall_address(),0)`
     后单独把 `type` 改成 `0xee`（DPL=3）——用户态 `int 0x80` 才能触发；
  2. `syscall_entry` 汇编 stub 的 15 个 GPR 压栈顺序与 `struct syscall_frame` 的
     20 个 u64 布局（`_Static_assert` 锁死）；
  3. `syscall_dispatch`/`syscall_report` 的分派语义（`SYS_GETTICKS/GETPID/
     WRITE_CONSOLE/EXIT`，未知号 `-ENOSYS`）；
  4. `user_context_valid` 对保存帧的四项校验（cs/ss/rip/rsp 范围）；
  5. checkpoint 课固定模式（`struct lesson_K_model` + `lXXtest`，Lesson 133–158）。
- **本课交付**：syscall 安全边界的概念与逐函数精讲（入口门 / 号校验 / 参数指针
  边界 / 返回现场校验）；命令 `l151test`（改名）与 `l159test`（新增）；`about`/
  banner 文案。

---

## 2. 核心概念精讲

### 2.1 概念一：syscall 安全边界（user ↔ kernel 的受控闸口）

**直觉**：用户程序想碰硬件、改内存、操作文件，必须「敲内核的门」。这道门就是
syscall 边界：门怎么开（IDT 门 / `syscall` 指令）、谁有权敲（DPL）、进门后怎么
信任请求（syscall 号与参数）、干完怎么送客（现场校验 + `iretq`）。边界的每一环
都是潜在的攻击面。

**准确定义**：syscall 安全边界 = 从用户态 CPL3 进入内核态 CPL0 执行特权服务的
**受控通道**及其全部防护。TinyOS 用 `int 0x80` 软件中断实现通道本身，边界防护由
四层构成：

| 防线 | 代码 | 防什么 |
|------|------|--------|
| ① 入口门 DPL | `idt[0x80].type=0xee`（DPL=3） | 非用户态无法通过 `int 0x80` 进入；GDT 段选择子限 CPL |
| ② 现场保存 | `syscall_entry` 压 15 个 GPR | 寄存器现场不被破坏，返回可恢复 |
| ③ 号与参数校验 | `syscall_dispatch` 未知号回 `-ENOSYS`；`WRITE_CONSOLE` 不用用户指针 | 越界 syscall 号、恶意指针 |
| ④ 返回现场校验 | `user_context_valid` 四查 cs/ss/rip/rsp | 防止伪造用户帧越权返回 |

**为什么这样设计**：Linux 的 syscall 入口（`arch/x86/entry/entry_64.S` 的
`entry_SYSCALL_64`）同样是一连串防御：`swapgs`、`syscall_table` 查表、号码越界判
`-ENOSYS`、`copy_from_user` 拷参数。TinyOS 把它压缩成 4 个可讲的点。

### 2.2 概念二：syscall 号的「白名单分派」

**直觉**：内核不能信任用户给的任何数字。`syscall_dispatch` 把 `rax` 里的号拿来
做 `switch`，只认 0/1/2/3 四个合法号，其余全部落入 `default` 返回 `-ENOSYS`：

```c
static TEXT64 u64 syscall_dispatch(struct syscall_frame*f,u16*c){switch((u32)f->rax){case SYS_GETTICKS:return ticks;case SYS_GETPID:return FIXED_PID;case SYS_WRITE_CONSOLE:text64(c,"kernel-owned console message\n");return 0;case SYS_EXIT:return 0;default:return (u64)(-(s64)ENOSYS);}}
```

**工作机制**：①`rax` 被截断成 u32 与 4 个 `SYS_*` 宏比较；②命中即执行对应服务；
③`default` 分支返回 `(u64)(-(s64)ENOSYS)`——即 `-38` 的无符号表示，作为错误码返回
给用户。这个「未注册号 → 固定错误码」就是白名单分派的边界语义，与 Linux
`do_syscall_64` 里 `nr >= NR_syscalls` 时返回 `-ENOSYS` 完全同构。

### 2.3 概念三：不碰用户指针的 I/O 边界

**直觉**：`WRITE_CONSOLE` 本该把用户缓冲写到屏幕。真实内核要先 `copy_from_user`
校验地址。TinyOS 的选择是**根本不接受用户指针**——`syscallinfo` 文案明确写着
`WRITE_CONSOLE uses a fixed kernel-owned message and no user pointer`，调用方永远
打印一条内核自有的固定消息。这把「用户传入的任意地址」这一最大攻击面直接关掉，
与 `uaccess_validate`/`copytest`（Lesson 158 复习的 canonical/range/vma/permission
四查 + `no source/destination bytes touched`）互为表里。

### 2.4 概念四：返回现场的完整性校验

**直觉**：`iretq` 会把栈上伪造的 `rip/cs/rflags/rsp/ss` 弹给 CPU。若用户能往内核栈
里塞伪造帧，就能提权。TinyOS 用 `user_context_valid` 在恢复帧之前做四查：

```c
static TEXT64 int user_context_valid(struct saved_user_context *c)
{
    u64 code_end=user_process.entry+(u64)user_process.image_bytes;
    return c && c->valid && c->frame.cs==USER_CS && c->frame.ss==USER_DS &&
        c->frame.rip>=user_process.entry && c->frame.rip<code_end &&
        c->frame.rsp>=USER_STACK_VA && c->frame.rsp<=user_process.stack_top;
}
```

**为什么这样设计**：`cs` 必须等于 `USER_CS`（拒绝伪装内核段）、`ss` 必须等于
`USER_DS`、`rip` 必须落在代码镜像区间内（拒绝跳任意地址）、`rsp` 必须落在用户栈
区间内——四者全满足才允许 `return_pending` 置位，否则 `user_process_exit`/`syscall`
路径直接拒绝。这是「syscall 安全边界」的最后一道闸。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 158） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验并加载用户镜像、构造 `long_mode_handoff` | 未变化 |
| `kernel64.c` | 64 位内核本体（1036 行）：PMM/异常/中断/调度/进程/VFS/GUI/syscall/checkpoint 模型 | `l158test`→`l151test` 改名；新增 `struct lesson_152_model`、`l159test`；exec64 增加 `l151test`/`l159test` 分支；about/banner 文案 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局（`.multiboot`/`.text64`/`.data`/`.bss`） | 未变化 |
| `Makefile` | `make all/check/run/clean`；`check` 目标 grep `syscall 安全边界`、`l159test`、`Lesson 159` | 仅 grep 文案（Lesson 158→159） |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

### 3.2 kernel64.c：syscall 边界实现精讲（继承代码）

> 说明：本节的 syscall 机制全部继承自早期课程，本课没有修改它们；精讲它们是因为
> 它们正是本课主题「syscall 安全边界」的机制载体。

#### 3.2.1 入口门装配：int 0x80 与 DPL=3

```c
set_gate(&idt[0x80],runtime_syscall_address(),0);idt[0x80].type=0xee;
```

- 前一行把 0x80 号 gate 的目标地址装成 `syscall_entry`、`ist=0`（不用换栈，syscall
  已有 `rsp0` 切换）；后一行把 `type` 从 `IDT_GATE_INTERRUPT(0x8e)` 改成 `0xee`——
  十六进制 `0xee` 的 DPL 位（bit 13–14）是 `11`，即 DPL=3，允许 CPL3 的用户代码
  用 `int 0x80` 触发。
- 边界语义：除 0x80 外其他门都是 DPL=0（`IDT_GATE_INTERRUPT`），用户态 `int 3`/
  `int 6` 等直接触发会因 DPL 不符产生 #GP——用户只能敲这一扇门。
- `syscallinfo` 命令的输出固化了这一事实：`int 0x80 vector: 0000000000000080 DPL:
  0000000000000003 gate: interrupt`。

```c
".global syscall_entry\nsyscall_entry:\n"
"pushq %rax\npushq %rbx\npushq %rcx\npushq %rdx\npushq %rbp\npushq %rsi\npushq %rdi\npushq %r8\npushq %r9\npushq %r10\npushq %r11\npushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
"cld\nmovq %rsp,%rdi\ncall syscall_report\nmovq 112(%rdi),%rax\n"
"popq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %r11\npopq %r10\npopq %r9\npopq %r8\npopq %rdi\npopq %rsi\npopq %rbp\npopq %rdx\npopq %rcx\npopq %rbx\naddq $8,%rsp\niretq\n"
```

逐行注释：
1. `pushq %rax … pushq %r15`：按 `struct syscall_frame` 的字段顺序压入 15 个 GPR
   （`_Static_assert(sizeof(struct syscall_frame)==20*sizeof(u64))` 锁死布局，CPU 已压
   rip/cs/rflags/rsp/ss 五个，合计 20 个 u64）。
2. `cld`：清方向标志，保证字符串指令向前；`movq %rsp,%rdi` 把 frame 指针传给
   `syscall_report`（SysV 第一个参数）。
3. `call syscall_report`：C 侧真正处理；返回后 `movq 112(%rdi),%rax`——112 字节偏移
   处正是 `syscall_frame.rax`，把返回值写回用户可见的 rax（`syscall_report` 里
   `f->rax=result`）。
4. `popq %r15 … popq %rbx`：逆序恢复 14 个 GPR（rax 被特意跳过）；`addq $8,%rsp`
   跳过栈上的 rax 槽；`iretq` 弹 rip/cs/rflags/rsp/ss 回用户态。
- 边界：stub 不碰用户指针、不查用户内存，一切参数解释都在 C 侧的
  `syscall_dispatch` 内完成。

#### 3.2.2 分派与未知号拒绝（syscall_dispatch）

```c
static TEXT64 u64 syscall_dispatch(struct syscall_frame*f,u16*c){switch((u32)f->rax){case SYS_GETTICKS:return ticks;case SYS_GETPID:return FIXED_PID;case SYS_WRITE_CONSOLE:text64(c,"kernel-owned console message\n");return 0;case SYS_EXIT:return 0;default:return (u64)(-(s64)ENOSYS);}}
```

- 签名与职责：按 `f->rax` 的 syscall 号分派四个服务，返回结果（作为下一条指令的
  `rax`）。`SYS_GETTICKS(0)` 返回时钟计数、`SYS_GETPID(1)` 返回固定 PID 1、
  `SYS_WRITE_CONSOLE(2)` 打印内核自有消息并返回 0、`SYS_EXIT(3)` 返回 0（真正的
  退出动作在 `syscall_report` 里做）。
- 边界与错误处理：`default` 返回 `(u64)(-(s64)ENOSYS)`，即 `-ENOSYS = -38` 的
  无符号形态——未知 syscall 号被统一拒绝，这是「白名单」边界。
- 为什么这样设计：与 Linux `kernel/entry/common.c` 的 `do_syscall_64` 一致——
  `nr >= NR_syscalls` 时返回 `-ENOSYS`（`kernel/sys.c` 定义该错误码为 38）。

#### 3.2.3 受控退出与 I/O 边界（syscall_report）

```c
TEXT64 void syscall_report(struct syscall_frame*f){u16 c=0;u64 number=f->rax,result;clear64(&c);if((u32)f->rax==SYS_EXIT){user_context_save(f,0);text64(&c,"TinyOS lesson 36 SYS_EXIT\nuser requested controlled exit\n");if(user_process_exit())text64(&c,"saved user context validated; process/thread exited\n");else text64(&c,"controlled exit rejected: invalid lifecycle\n");text64(&c,"halting intentionally\n");for(;;)__asm__ volatile("cli; hlt");}result=syscall_dispatch(f,&c);user_context_save(f,result);text64(&c,"TinyOS lesson 36 syscall dispatcher\nsyscall number: ");hex64(&c,number);text64(&c,"\nreturn rax: ");f->rax=result;hex64(&c,f->rax);text64(&c,"\nuser rip: ");hex64(&c,f->rip);text64(&c,"\nuser cs: ");hex64(&c,f->cs);text64(&c,"\nuser rsp: ");hex64(&c,f->rsp);text64(&c,"\nuser ss: ");hex64(&c,f->ss);text64(&c,"\nall-GPR frame; returning with iretq; user IF remains disabled\n");}
```

- 算法步骤：①先保存用户现场 `user_context_save(f,0)`；②若 `rax==SYS_EXIT` 走受控
  退出路径——保存现场后调用 `user_process_exit()`（它内部先做 `user_context_valid`
  校验，非法生命周期会返回 0 并打印 `controlled exit rejected: invalid lifecycle`），
  然后 `cli; hlt` 停机；③其余号走 `syscall_dispatch`，把结果写回 `f->rax`，打印
  syscall 号/返回/用户 rip/cs/rsp/ss 六行诊断后由 stub 的 `iretq` 返回。
- 边界与错误处理：SYS_EXIT 是「报告 + 停机」而非返回用户；其余路径打印
  `all-GPR frame; returning with iretq; user IF remains disabled`——用户态中断标志
  由 `enter_user_c` 的 `pushq $0x002`（rflags IF=0）保证关闭，防止用户态被 IRQ0
  抢占（TinyOS 刻意保留的安全边界）。
- `syscallinfo` 命令输出把边界规则说透：`WRITE_CONSOLE uses a fixed kernel-owned
  message and no user pointer`、`EXIT reports and intentionally halts; no user IRQ
  callback or cross-address-space scheduler`。

#### 3.2.4 返回现场校验（user_context_valid / user_return_prepare）

```c
static TEXT64 int user_context_valid(struct saved_user_context *c)
{
    u64 code_end=user_process.entry+(u64)user_process.image_bytes;
    return c && c->valid && c->frame.cs==USER_CS && c->frame.ss==USER_DS &&
        c->frame.rip>=user_process.entry && c->frame.rip<code_end &&
        c->frame.rsp>=USER_STACK_VA && c->frame.rsp<=user_process.stack_top;
}
```

- 签名与职责：校验一个已保存的用户上下文是否「形状合法」，合法返回 1。
- 算法步骤（四查）：①`c && c->valid` 帧存在且有效；②`cs==USER_CS && ss==USER_DS`
  段选择子必须指向用户段（防伪造内核帧）；③`rip` 在 `[entry, entry+image_bytes)`
  代码区间内（防跳任意地址）；④`rsp` 在 `[USER_STACK_VA, stack_top]` 用户栈区间内。
- 边界：`user_context_save` 每次保存后都会把 `user_process.context_valid` 重算为
  `user_context_valid(...)` 的结果；`user_return_prepare` 在真正 `iretq` 前依赖它。
- 为什么：这是 syscall 边界的收尾防线——所有校验都是「元数据校验」，不执行任何
  用户代码，与 Linux `ptrace`/`seccomp` 返回路径的寄存器校验同理。

#### 3.2.5 本课新增 checkpoint：lesson_152_model 与 l159test

```c
struct lesson_152_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_152_model lesson_152_state;
static TEXT64 void l159test(u16*c){lesson_152_state=(struct lesson_152_model){152U,153U,154U,155U,1,1,1,1};int ok=lesson_152_state.valid&&lesson_152_state.active&&lesson_152_state.ready&&lesson_152_state.accounted&&lesson_152_state.b==lesson_152_state.a+1U;text64(c,"l159test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 152 fallback reported");putc64(c,'\n');}
```

- `struct lesson_152_model`：4 个 u32 + 4 个状态位，`a` 以 `152U` 起头，152 = 159−7，
  延续 Lesson 157/158 的「回锚」链（150/151/152 连续三课）。
- `l159test` 算法：①字面量赋值；②五连断言（valid/active/ready/accounted/b==a+1）；
  ③成功串 `bounded networking, namespaces, cgroups, and security checkpoint passed`
  或失败串 `Lesson 152 fallback reported`。
- 为什么：回归探针，不执行任何 syscall 代码；消息里的 "networking, namespaces,
  cgroups, and security" 描述继承机制的覆盖面。

#### 3.2.6 exec64 命令接线（本课增量）

```c
}else if(eq64(word,"l151test")){if(!noargs64(arg))usage64(c,"l151test");else l151test(c);}else if(eq64(word,"l159test")){if(!noargs64(arg))usage64(c,"l159test");else l159test(c);}
```

- 本课把上一课的 `l158test` 分支改名 `l151test`（其模型 `lesson_151_state` 不动，
  仍是 `{151,152,153,154}`），并新增 `l159test` 分支。
- **勘误**：旧 README 写的 `Commands: l152test` 与源码不符——源码中**不存在**
  `l152test` 命令（`grep -c l152test` 为 0），可用的 checkpoint 命令是 `l151test`
  与 `l159test`。
- about 文案 `else text64(c,"Lesson 159: syscall 安全边界\n");` 与开机横幅
  `text64(&c,"Lesson 159: syscall 安全边界\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT;
  unknown=-ENOSYS; bounded reclaim metadata\n");` 一起构成主题标识。注意横幅里的
  `unknown=-ENOSYS` 正是本课边界主题的浓缩。

### 3.3 构建管线（Makefile / linker）

- `kernel64.o`：`gcc $(CFLAGS64) -c`。`CFLAGS64` 含 `-m64 -ffreestanding -fpie
  -fno-stack-protector -mno-red-zone -mno-sse -mno-sse2 -mno-mmx
  -fno-asynchronous-unwind-tables -Wall -Wextra -Werror`——`-fpie` 允许 RIP 相对
  寻址（`leaq` 取 stub 地址依赖它），`-mno-red-zone` 防止中断路径踩红区。
- `kernel64.bin`：`ld -m elf_x86_64 -T kernel64.ld -nostdlib` 再 `objcopy -O binary`；
  `kernel64.ld` 从 0 开始布局，`.data` 内用 `. = ALIGN(0x1000)` 依次划出
  idle/rsp0/ist1 三块 guard+stack，末尾三条 `ASSERT(...==0x1000)` 锁死每块栈尺寸。
- `boot.o`：`gcc $(CFLAGS)`（32 位），依赖 `build/kernel64.bin`——外层 `.text64`
  段 `kernel_main64` 以 `.incbin` 嵌入二进制。
- `kernel.iso`：`ld -m elf_i386 -T linker.ld` 链接外层 ELF32，`grub-mkrescue` 出 ISO；
  `linker.ld` 保证 `.multiboot` 在 1 MiB 起、8 字节对齐、`.text64` 紧随其后。
- `check` 目标：`grub-file --is-x86-multiboot2 build/kernel.elf` + 三条 grep
  （`syscall 安全边界`、`l159test`、`Lesson 159`）——README 里这些串必须原样存在。
- 相对上一课新增构建步骤：**无**。Makefile 仅 `check` 目标的 grep 文案变化。

### 3.4 主控制流

```text
_start (boot.S) → kernel_main32 (kernel.c) 解析 MBI/建页表
  → enter_long_mode (boot.S) CR4.PAE → EFER.LME → CR0.PG → far jump
  → kernel_main64_binary (kernel64.c)
       module_init_model() → init_model_start() → wait_model_start()
       → adoption_start() → resource_start()
       → pmm_init() → vma_init() → reclaim_init() → vfs_init()
       → 进程/线程元数据装配 → framebuffer_init
       → stack_guards_init / runtime_gdt_tss_init / idle_init
       → install_idt()（含 int 0x80 DPL=3 门）
       → pit_init()+pic_init() → 横幅 "Lesson 159: syscall 安全边界\n..."
       → 键盘 shell 循环：kbd_dequeue → exec64(word)
  用户侧: enter_user_c iretq 进 CPL3 → int 0x80
       → syscall_entry 压 15 GPR → syscall_report
            SYS_EXIT: user_context_save → user_process_exit(校验) → 停机
            其它:    syscall_dispatch（未知号 -ENOSYS）→ 写回 rax
       → iretq 回用户（现场已由 user_context_valid 校验）
  exec64 分支 → syscallinfo:边界规则文案
             → cpl3test:enter_user 触发 0,1,2,99,3 五个号
             → userreturntest:user_return_prepare 单次投递
             → l151test / l159test:checkpoint 断言
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径，以本课主题相关的三条命令为例：

1. **`about`** → exec64 命中 `about` 分支 → `text64(c,"Lesson 159: syscall 安全边界\n")` → 屏幕打印 `Lesson 159: syscall 安全边界`。
2. **`cpl3test`** → `cpl3test` 分支打印 `entering CPL3 syscall stub with IF=0; calls
   0,1,2,99,3 (EXIT)` 后 `enter_user(h)` 进 CPL3 → 用户镜像依次发 5 个
   `int 0x80`（号 0、1、2、99、3）→ `syscall_dispatch` 对 99 走 `default` 返回
   `-ENOSYS` → 号 3（EXIT）走受控退出路径停机 → VGA 停在
   `TinyOS lesson 36 SYS_EXIT` 报告处。
3. **`l159test`** → `l159test(c)` 对 `lesson_152_state` 赋值并五连断言 → 输出
   `l159test: bounded networking, namespaces, cgroups, and security checkpoint passed`。

VGA 输出管线全由 `putc64`/`text64`/`hex64` 驱动（0xB8000 文本模式，属性 0x0f 白字
黑底，80×25），`clear64` 负责清屏。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-159-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `syscall 安全边界`、`l159test`、`Lesson 159` 与 kernel64.c 中的 `l159test`，
  全部命中输出 `Multiboot2 and Lesson 159 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。
  启动横幅第一行为 `Lesson 159: syscall 安全边界`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 159: syscall 安全边界`
  2. `l159test` → `l159test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  3. `l151test` → `l151test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  4. `syscallinfo` → `syscalls: 0=GETTICKS 1=GETPID 2=WRITE_CONSOLE 3=EXIT;
     unknown=-ENOSYS`，随后两行 `WRITE_CONSOLE uses a fixed kernel-owned message and
     no user pointer`、`EXIT reports and intentionally halts; no user IRQ callback or
     cross-address-space scheduler`
  5. `userreturntest` → `userreturntest: validated user return preserved frame and
     delivered once`
  6. `signaltest` → `signaltest: exception notifications queued with bounded default
     actions passed`
  7. `idtinfo` → 首行 `IDT: exceptions + DPL3 syscall gate + PIT IRQ0 + IRQ1`，末行
     `int 0x80 vector: 0000000000000080 DPL: 0000000000000003 gate: interrupt`
- **如何判断成功**：上述命令逐一打印预期串即成功；`cpl3test` 预期最终停机（VGA 停在
  SYS_EXIT 报告处，QEMU 不退出——`-no-reboot -no-shutdown` 保证画面保留）。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l159test` 输出 `Lesson 152 fallback reported` | checkpoint 模型字段被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l159test` 的赋值 `{152U,153U,154U,155U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| 输入 `l152test` 报 `unknown command` | 旧 README 命令名是笔误，源码无此命令 | 源码中可用命令是 `l151test` 与 `l159test` |
| `cpl3test` 里号 99 没有返回 `-ENOSYS` | `syscall_dispatch` 的 `default` 分支被改动或 `SYS_*` 宏冲突 | 检查 `ENOSYS` 宏（38）与 `default:return (u64)(-(s64)ENOSYS)` |
| `userreturntest` 输出 `BROKEN` | `user_return_prepare` 前置条件不满足（`context_valid` 为 0 或 `return_pending` 为 0） | 检查测试函数内手动置位的顺序；确认 `user_process.state==PROCESS_RUNNING` |
| `cpl3test`/`userpitest` 一进用户态就 #GP | `idt[0x80].type` 不是 `0xee`（DPL=3） | `idtinfo` 看 `int 0x80 ... DPL: 0000000000000003`；检查 `install_idt` 的 `type=0xee` 赋值 |
| `SYS_EXIT` 输出 `controlled exit rejected: invalid lifecycle` | `user_process_exit` 前置校验失败（state 非 RUNNING 或 context 无效） | `processinfo` 看进程/线程状态与 `context_valid`；检查 `user_context_valid` 四查 |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 159: syscall 安全边界`；`make check` 的 grep 覆盖此串 |

---

## 7. 与 Linux 源码对照

1. **syscall 入口**：TinyOS 用 `int 0x80` 软件中断 + `syscall_entry` stub；Linux
   x86-64 用 `syscall` 指令 + `arch/x86/entry/entry_64.S` 的 `entry_SYSCALL_64`
   （`swapgs`、`syscall_enter_from_user_mode`）。两者都走「压寄存器现场 → C 分派 →
   返回寄存器 → `iretq`/`sysretq`」的统一链，TinyOS 没有 `swapgs`/`syscall` 快路径。
2. **syscall 号白名单**：TinyOS `syscall_dispatch` 的 `switch` 只认 4 个号，
   `default` 回 `-ENOSYS`；Linux `kernel/entry/common.c` 的 `do_syscall_64` 用
   `sys_call_table[nr]`，`nr >= NR_syscalls` 时返回 `-ENOSYS`（`include/uapi/asm-generic/
   errno.h` 定义 ENOSYS=38，TinyOS 的 `ENOSYS 38` 与之一致）。
3. **参数指针边界**：TinyOS `WRITE_CONSOLE` 拒绝用户指针、打印内核自有消息；
   Linux 的 `write` 在 `fs/read_write.c` 里经 `import_iovec`/`copy_from_user` 校验
   用户地址（`access_ok` + `copy_from_user`）。TinyOS 选择了「不接受指针」的更强
   简化，`uaccess_validate`（Lesson 158）则是其可检查形态。
4. **DPL 门控**：TinyOS 用 `idt[0x80].type=0xee` 把唯一入口门设为 DPL=3；Linux 的
   syscall 不经过 IDT（用 `MSR_LSTAR` + `IA32_STAR`），但 `int 0x80` 兼容路径
   （`ia32_syscall`，`arch/x86/entry/entry_64_compat.S`）同样依赖 IDT 门 DPL。
5. **返回现场校验**：TinyOS `user_context_valid` 四查 cs/ss/rip/rsp；Linux
   `do_exit`/`ptrace` 路径有完整的 `task_pt_regs` 校验与 `seccomp` 过滤
   （`kernel/seccomp.c` 的 `__secure_computing`），TinyOS 砍掉了 seccomp/ptrace，
   只保留最小形状校验。
6. **审计/seccomp 钩子**：Linux 在 syscall 入口挂 seccomp 与 audit 钩子
   （`kernel/auditsc.c` 的 `__audit_syscall_entry`）；TinyOS 本课没有审计实现，
   这正是 Lesson 160「审计事件缓冲区」的主题预告。

**权威来源**：Intel SDM Vol.3A（§6.11 IDT、§5.8.3 门 DPL、`syscall`/`sysret` 指令）、
Linux `arch/x86/entry/entry_64.S`、`kernel/entry/common.c`、`include/uapi/asm-generic/
errno.h`。
**教学模型简化了什么**：真实 syscall 边界有 `swapgs`、per-cpu `rsp`、`syscall` 指令
快路径、参数结构体拷贝、seccomp 过滤、audit 记录、信号递送；TinyOS 只保留
「int 0x80 + 4 号白名单 + 内核自有消息 + 返回帧四查」四个最小环节。

---

## 8. 思考题与练习

1. **概念理解**：为什么 `idt[0x80].type` 必须单独改成 `0xee` 而不是直接用
   `set_gate` 的 `IDT_GATE_INTERRUPT(0x8e)`？0xee 与 0x8e 的差在哪两个 bit 上？
2. **源码定位**：在 `kernel64.c` 中找出 `syscall_dispatch` 的 `default` 分支，解释
   `(u64)(-(s64)ENOSYS)` 为什么能表示负错误码；再找出 `user_context_valid` 的四项
   校验条件。
3. **动手实验**：修改 `l159test` 的赋值，把 `d` 从 `155U` 改成 `156U`，重新构建运行，
   观察输出是否仍为 passed；再把 `valid` 改成 `0`，观察输出翻转。
4. **动手实验**：给 `syscall_dispatch` 临时加一个 `case 4` 返回 `FIXED_PID`，重新
   构建后用 `cpl3test` 观察号 99 与新增号的输出差异（注意改完要还原）。
5. **Linux 对照**：阅读 `kernel/entry/common.c` 的 `do_syscall_64` 与
   `arch/x86/entry/entry_64.S` 的 `entry_SYSCALL_64`，对比它们与
   `syscall_dispatch`/`syscall_entry` 的分工边界，指出 TinyOS 砍掉了哪些阶段
   （如 `swapgs`、参数 struct 拷贝、`sysret` 返回路径）。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是资源/安全主题的检查点课，`kernel64.c` 相对上一课只有 4 处小增量，机制全部
   继承自早期课程，主题由 banner/about 文案标识。
2. syscall 安全边界由四道防线组成：`int 0x80` 门 DPL=3（入口门控）、`syscall_entry`
   压 15 GPR（现场保存）、`syscall_dispatch` 白名单分派 + 未知号 `-ENOSYS`、
   `user_context_valid` 返回帧四查。
3. `WRITE_CONSOLE` 拒绝用户指针、打印内核自有消息，把「用户任意地址」攻击面直接
   关掉；`SYS_EXIT` 走受控退出路径（保存现场 → 生命周期校验 → 停机）。
4. `user_context_valid` 四查 cs/ss/rip/rsp，`user_return_prepare` 保证信号只投递
   一次——返回路径的完整性校验。
5. 新 checkpoint `l159test` 用字面量赋值 + 五连断言固化回归探针；模型名
   `lesson_152_model` 的 152 = 159−7 延续「回锚」惯例。
6. 旧 README 的 `Commands: l152test` 已勘误为源码实际的 `l151test` 与 `l159test`。

**下一课**：[`lesson-160-stable/README.md`](../lesson-160-stable/README.md) 主题为
「审计事件缓冲区」，将站在本课「syscall 边界」（入口钩子点）之上，讲解内核审计
事件（audit record）的缓冲与记录机制如何被教学模型固化为新的 checkpoint 模型
（命令 `l160test`）。两课的衔接点是「syscall 入口的观测」：本课讲「边界防护」，
下节课讲「边界处的审计记录」。
