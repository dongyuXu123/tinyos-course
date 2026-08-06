# Lesson 161: 安全策略决策 — 精讲文档

> **课号**：Lesson 161（可执行课，checkpoint 快照）
> **主题**：安全策略决策——把 Linux LSM（Linux Security Module）的「主体 → 客体 →
> 操作 → 策略裁决」决策模型作为概念模型精讲，并回顾 TinyOS 内核里既有的「决策型」
> 函数（`pf_classify` 故障分类、`uaccess_validate` 放行/拒绝、`module_lookup` 符号
> 可见性、`task_transition` 状态机等），追加确定性校验的 checkpoint 模型
> `lesson_154_model`。
> **课程主线位置**：资源/安全主题的「检查点课」序列（Lesson 157–162），位于
> Lesson 160（审计事件缓冲区）之后、Lesson 162（网络、namespace、cgroup、安全综合
> checkpoint，全课程最终一课）之前。
> **前置课程**：[`lesson-160-stable/README.md`](../lesson-160-stable/README.md)
> **后续课程**：[`lesson-162-stable/README.md`](../lesson-162-stable/README.md)
> **一句话目标**：学完本课你能说清「安全策略决策」在 Linux 里的样子（LSM 钩子 +
> 主体/客体/操作 + 允许/拒绝），以及 TinyOS 用哪些既有「裁决函数」近似它、`l161test`
> 校验了什么。

---

## 1. 课程定位（Mission）

**一句话目标**：读懂「主题宣告 + checkpoint 增量」模式下本课新增的确定性模型
`lesson_154_model` 及其 `l161test` 断言，会用 `l153test`、`l161test`、`pfmodel`、
`ptrtest`、`moduletest`、`taskvalidate`、`pipetest` 等命令复现与「决策」相关的既有
机制，并理解 LSM 策略决策概念与它们的对应关系。

- **在课程主线中的位置**：与 Lesson 157–160、162 同属「资源/安全主题的检查点课」，
  相邻课 `kernel64.c` 的 diff 通常只有几行（本课相对 Lesson 160 仅 4 处改动：
  `l160test`→`l153test` 改名、新增 `struct lesson_154_model` 与 `l161test`、
  exec64/about/banner 文案）。**注意：源码中没有 `security_*` 钩子或策略结构**——
  本课主题是「宣告 + 概念模型」，机制载体是继承自早期课程的各类「裁决函数」。
- **前置知识清单**：
  1. LSM 概念：安全钩子（hook）、主体（subject）/客体（object）/操作（action）三元组、
     允许（0）/拒绝（-EPERM）——`security/` 目录与 SELinux/AppArmor 的关系；
  2. TinyOS 决策型函数：`pf_classify`（返回枚举分类）、`uaccess_validate`（返回
     布尔）、`module_lookup`（返回布尔）、`task_transition`（返回状态机合法性）——
     Lesson 133/140/145/158 继承；
  3. 状态机类决策：`wait_model_*`、`adoption_*`、`lifecycle_*` 的「前置状态检查」
     模式；
  4. checkpoint 课固定模式（`struct lesson_K_model` + `lXXtest`，Lesson 133–160）。
- **本课交付**：LSM 策略决策的概念模型（钩子点、主体/客体/操作、允许/拒绝语义）；
  既有决策型函数的逐函数精讲；命令 `l153test`（改名）与 `l161test`（新增）；
  `about`/banner 文案。

---

## 2. 核心概念精讲

### 2.1 概念一：LSM 与安全策略决策（policy decision）

**直觉**：安全系统回答的问题只有一个——「这个主体对那个客体做这个操作，允许吗？」
LSM 把这个问题标准化：在敏感操作入口挂「钩子」，让安全模块做裁决，返回「允许」或
「拒绝」。

**准确定义**：LSM（Linux Security Module）是在内核关键路径上插入的安全钩子框架。
`security/` 目录下的模块（SELinux、AppArmor、Smack、Yama）通过注册 `struct
security_hook_list` 拦截特定操作。一次策略决策的输入是三元组
**（subject 主体, object 客体, action 操作）**，输出是**允许/拒绝**（配合 `-EPERM`/
`-EACCES` 等错误码）。

**为什么需要**：内核功能太多，光靠「uid==0 全放行」太粗糙；LSM 让安全策略与内核
逻辑解耦——内核只负责「在哪儿问」，策略模块负责「怎么答」。

### 2.2 概念二：LSM 钩子的执行流

**直觉**：以文件打开为例——`sys_open` 走到 VFS 层调用 `security_file_open()` 钩子，
SELinux 查 AV 决策缓存（AVC）得到 allow/deny；deny 则 `open` 直接返回 `-EACCES`，
文件对象根本不会建立。**「先裁决，后执行」**是安全策略决策的铁律。

**工作机制**（Linux 侧）：
1. 钩子注册：`security_add_hooks()` 把模块的钩子函数挂进 `security_hook_heads`；
2. 调用点：内核各子系统（VFS、IPC、网络）在动作前调 `security_xxx()`；
3. 裁决：`call_int_hook` 顺序调用所有模块，任一拒绝即整体拒绝；
4. 审计：拒绝/允许事件交给 audit（Lesson 160 的主题）记录。

### 2.3 概念三：TinyOS 的「裁决函数」——策略决策的本地近似

**直觉**：TinyOS 没有 LSM，但它几乎每个子系统都有「先裁决后执行」的函数——输入
条件，输出布尔或枚举，调用方根据裁决决定是否继续。这些就是 LSM 决策模型的
教学投影：

| TinyOS 裁决函数 | 输入（subject/object/action） | 输出（决策） | 观察命令 |
|-----------------|------------------------------|-------------|---------|
| `pf_classify(va,write)` | 当前执行流 / 地址 / 写操作 | `PF_NOT_PRESENT/PF_PROTECTION/PF_UNMAPPED` 三分类 | `pfmodel` |
| `uaccess_validate(addr,len,access)` | 调用者 / 目标地址 / 读写 | 布尔放行（canonical+range+vma+permission） | `ptrtest` |
| `module_lookup(name)` | 调用者 / 符号 / 查找 | 布尔（仅 exported 可见） | `moduletest` |
| `task_transition(i,next)` | 任务 / 任务 / 状态迁移 | 布尔（状态机合法才放行） | `taskvalidate` |
| `wait_model_wait()` | 父进程 / 子进程 / wait | 布尔（仅 zombie 可 wait） | `waittest` |
| `pipe_try_write(value)` | 生产者 / 管道 / 写 | 布尔（满则阻塞/拒绝） | `pipetest` |

**为什么这样设计**：教学内核把「安全策略」化简成每个函数入口的显式条件检查，
注释反复强调 `metadata only`、不执行真实动作——与 LSM「先裁决后执行」的骨架一致。

### 2.4 概念四：检查点模型（checkpoint model）

**直觉**：与 Lesson 157–160 完全相同的模式——本课新增：

```c
struct lesson_154_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
```

**工作机制**：`l161test` 把 `lesson_154_state` 整体赋为 `{154U,155U,156U,157U,1,1,1,1}`
（a=154, b=155, c=156, d=157，四个状态位全 1），断言 `valid && active && ready &&
accounted && b==a+1`。字面量赋值使断言恒真，输出恒为 `bounded networking,
namespaces, cgroups, and security checkpoint passed`。模型名 `lesson_154_model`
的 154 = 161−7，延续「回锚」链（150/151/152/153/154 连续五课）。**教学模型：不执行
任何策略引擎代码，只校验元数据自洽。**

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 160） |
|------|------|------------------------------|
| `boot.S` | 32 位 multiboot2 头、进入 long mode、`.incbin` 嵌入 64 位 continuation | 未变化 |
| `kernel.c` | 32 位侧：解析 MBI、建页表、校验并加载用户镜像、构造 `long_mode_handoff` | 未变化 |
| `kernel64.c` | 64 位内核本体（1042 行）：PMM/异常/中断/调度/进程/VFS/GUI/裁决函数/checkpoint 模型 | `l160test`→`l153test` 改名；新增 `struct lesson_154_model`、`l161test`；exec64 增加 `l153test`/`l161test` 分支；about/banner 文案 |
| `kernel64.ld` | 64 位裸二进制布局（`.text64`/三块 guard+stack/ASSERT） | 未变化 |
| `linker.ld` | 外层 ELF32 镜像布局（`.multiboot`/`.text64`/`.data`/`.bss`） | 未变化 |
| `Makefile` | `make all/check/run/clean`；`check` 目标 grep `安全策略决策`、`l161test`、`Lesson 161` | 仅 grep 文案（Lesson 160→161） |
| `grub.cfg` | menuentry "TinyOS lesson 52: integrated init, shell, files, processes, and pipes" | 未变化 |

### 3.2 kernel64.c：决策型函数精讲（继承代码）

> 说明：本课**没有**新增安全策略代码；以下函数都是早期课程继承的既有机制，但
> 它们正是本课主题「安全策略决策」的载体，逐函数精讲其「先裁决后执行」语义。

#### 3.2.1 页故障三分类（pf_classify）：枚举型决策

```c
static TEXT64 enum pf_class pf_classify(u64 va,u8 write){const struct vma_model*v=vma_lookup(va);if(!v){fault_unmapped++;return PF_UNMAPPED;}if((write&&!(v->prot&VMA_W))||(!write&&!(v->prot&VMA_R))){fault_protection++;return PF_PROTECTION;}if(!page_present(va)){fault_not_present++;return PF_NOT_PRESENT;}return PF_NOT_PRESENT;}
```

- 签名与职责：对一次「地址访问」做三类裁决——地址无 VMA → `PF_UNMAPPED`；权限不足
  → `PF_PROTECTION`；页未装 → `PF_NOT_PRESENT`。这是「决策函数」最标准的形态：
  输入（va, write）→ 枚举输出。
- 决策树（三条规则）：①`vma_lookup` 找不到 → unmapped；②`write` 但无 `VMA_W` 或
  读但无 `VMA_R` → protection；③`page_present` 为假 → not-present。
- 与 LSM 的对应：`pf_classify` 每次裁决前先递增对应计数器（`fault_unmapped++` 等）
  ——即「裁决 + 审计」双动作，对应 LSM 决策后交给 audit 记录。
- `pfmodel` 一条命令打三个靶子（data 写 → not-present、code 写 → protection、
  0x100000 读 → unmapped），输出 `pfmodel: not-present/protection/unmapped classified;
  bounded page inserted`。

#### 3.2.2 用户访问放行/拒绝（uaccess_validate）：布尔型决策

```c
static TEXT64 int uaccess_validate(u64 address,u64 length,u8 access,struct uaccess_result*r){u64 end;const struct vma_model*v;r->address=address;r->length=length;r->access=access;r->canonical=address<=USER_CANONICAL_MAX&&(length==0||address<=USER_CANONICAL_MAX-length+1);r->range=length<=USER_COPY_MAX&&address<USER_RANGE_MAX&&length<=USER_RANGE_MAX-address&&address+length>=address;r->vma=0;r->permission=0;r->copied=0;if(r->canonical&&r->range&&length){end=address+length;v=vma_lookup(address);if(v&&end<=v->end){r->vma=1;r->permission=(access==UACCESS_READ?(v->prot&VMA_R):(v->prot&VMA_W))!=0;}}else if(r->canonical&&r->range&&!length){r->vma=1;r->permission=1;}return r->canonical&&r->range&&r->vma&&r->permission;}
```

- 签名与职责：对一次用户侧访问做四层级联裁决——canonical、range、vma、permission，
  全过才放行（返回 1）。
- 决策语义：这是 TinyOS 最接近 LSM `security_file_permission` 的函数——「对象
  （目标地址区间）上的操作（读/写）是否被允许」，`permission` 位即
  `(access==UACCESS_READ ? (v->prot&VMA_R) : (v->prot&VMA_W)) != 0`。
- `ptrtest` 用四个用例（data 读放行、非规范拒绝、超长拒绝、code 写拒绝）验证裁决器，
  输出 `ptrtest: canonical/range/VMA/permission checks passed`。

#### 3.2.3 状态机决策（task_transition / wait_model_wait）：前置状态检查

```c
static TEXT64 int task_transition(u32 i,u8 next){u8 old;if(i>=TASK_TABLE_CAP||!task_table[i].valid||!task_state_valid(next))return 0;old=task_table[i].state;if(old==next)return 1;if(old==EXIT_DEAD||old==EXIT_ZOMBIE)return 0;if(next==EXIT_DEAD)return 0;task_table[i].state=next;task_table[i].transitions++;return 1;}
```

- 签名与职责：把任务从当前状态迁移到 `next`，非法迁移返回 0——这是「状态机策略」。
- 决策规则：①索引越界 / 任务无效 / `next` 非法 → 拒绝；②`old==next` → 幂等放行；
  ③`old` 已是 EXIT_DEAD/ZOMBIE → 拒绝（死亡后不可迁移）；④`next==EXIT_DEAD` 必须
  经 ZOMBIE 中转 → 拒绝直达。
- 与 LSM 的对应：状态迁移策略与 `security_task_fix_setuid`/`cap_task_prctl` 这类
  「任务属性变更策略」同型——变更前先裁决。
- `task_table_validate` 把整套任务表约束固化成布尔（唯一 PID/TID、父 PID 小于子
  PID、状态合法），`taskvalidate` 输出 `task validation: passed (bounded table,
  unique PID/TID, valid parent/state)`。

```c
static TEXT64 int wait_model_wait(void){wait_model.wait_calls++;if(wait_model.state!=WAIT_ZOMBIE)return 0;wait_model.waited=1;return 1;}
```

- 单行裁决：只有子进程处于 `WAIT_ZOMBIE` 才允许 wait 成功——「状态即策略」，
  `waittest` 用 run → exit → wait → reap 四步验证整条策略链。

#### 3.2.4 本课新增 checkpoint：lesson_154_model 与 l161test

```c
struct lesson_154_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_154_model lesson_154_state;
static TEXT64 void l161test(u16*c){lesson_154_state=(struct lesson_154_model){154U,155U,156U,157U,1,1,1,1};int ok=lesson_154_state.valid&&lesson_154_state.active&&lesson_154_state.ready&&lesson_154_state.accounted&&lesson_154_state.b==lesson_154_state.a+1U;text64(c,"l161test: ");text64(c,ok?"bounded networking, namespaces, cgroups, and security checkpoint passed":"Lesson 154 fallback reported");putc64(c,'\n');}
```

- `struct lesson_154_model`：4 个 u32 + 4 个状态位，`a` 以 `154U` 起头，154 = 161−7，
  延续「回锚」链（150/151/152/153/154 连续五课）。
- `l161test` 算法：①字面量赋值；②五连断言（valid/active/ready/accounted/b==a+1）；
  ③成功串 `bounded networking, namespaces, cgroups, and security checkpoint passed`
  或失败串 `Lesson 154 fallback reported`。
- 为什么：回归探针，不执行任何策略引擎代码；消息里的 "networking, namespaces,
  cgroups, and security" 描述继承机制的覆盖面。

#### 3.2.5 exec64 命令接线（本课增量）

```c
}else if(eq64(word,"l153test")){if(!noargs64(arg))usage64(c,"l153test");else l153test(c);}else if(eq64(word,"l161test")){if(!noargs64(arg))usage64(c,"l161test");else l161test(c);}
```

- 本课把上一课的 `l160test` 分支改名 `l153test`（其模型 `lesson_153_state` 不动，
  仍是 `{153,154,155,156}`），并新增 `l161test` 分支。
- **勘误**：旧 README 写的 `Commands: l154test` 与源码不符——源码中**不存在**
  `l154test` 命令（`grep -c l154test` 为 0），可用的 checkpoint 命令是 `l153test`
  与 `l161test`。
- about 文案 `else text64(c,"Lesson 161: 安全策略决策\n");` 与开机横幅
  `text64(&c,"Lesson 161: 安全策略决策\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT;
  unknown=-ENOSYS; bounded reclaim metadata\n");` 一起构成主题标识。

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
  （`安全策略决策`、`l161test`、`Lesson 161`）——README 里这些串必须原样存在。
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
       → stack_guards_init / runtime_gdt_tss_init / idle_init / install_idt
       → pit_init()+pic_init() → 横幅 "Lesson 161: 安全策略决策\n..."
       → 键盘 shell 循环：kbd_dequeue → exec64(word)
  exec64 分支 → pfmodel:pf_classify 三次裁决（unmapped/protection/not-present）
             → ptrtest:uaccess_validate 四用例裁决
             → taskvalidate:task_table_validate 全表约束裁决
             → waittest:wait_model 状态机裁决
             → moduletest:module_lookup 符号可见性裁决
             → l153test / l161test:checkpoint 断言
```

---

## 4. 数据流与运行逻辑

「启动 → 命令 → 数据处理 → VGA 输出」的完整路径，以本课主题相关的三条命令为例：

1. **`about`** → exec64 命中 `about` 分支 → `text64(c,"Lesson 161: 安全策略决策\n")` → 屏幕打印 `Lesson 161: 安全策略决策`。
2. **`pfmodel`** → `pfmodel(c)` 连续三次 `pf_classify` 裁决：data 段写（not-present）、
   code 段写（protection）、1 MiB 空洞读（unmapped），各递增对应计数器；再
   `fault_insert` 插入一页 → 断言四者成立 → 输出 `pfmodel: not-present/protection/
   unmapped classified; bounded page inserted`。
3. **`l161test`** → `l161test(c)` 对 `lesson_154_state` 赋值并五连断言 → 输出
   `l161test: bounded networking, namespaces, cgroups, and security checkpoint passed`。

VGA 输出管线全由 `putc64`/`text64`/`hex64` 驱动（0xB8000 文本模式，属性 0x0f 白字
黑底，80×25），`clear64` 负责清屏。

---

## 5. 构建、运行与验证

- **依赖**：`gcc`、`ld`、`objcopy`、`grub-mkrescue`、`grub-file`、`qemu-system-x86_64`
  （本课不需要额外新工具）。
- **构建**：`cd lessons/lesson-161-stable && make clean && make -j"$(nproc)"`
- **静态检查**：`make check`——`grub-file` 验证 multiboot2，随后 grep README 中的
  `安全策略决策`、`l161test`、`Lesson 161` 与 kernel64.c 中的 `l161test`，
  全部命中输出 `Multiboot2 and Lesson 161 checks passed.`
- **运行**：`make run`。**成功画面在 QEMU 图形窗口，勿加 `-display none`**。
  启动横幅第一行为 `Lesson 161: 安全策略决策`。
- **验证步骤与预期输出**（输出串从源码逐字抄录）：
  1. `about` → `Lesson 161: 安全策略决策`
  2. `l161test` → `l161test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  3. `l153test` → `l153test: bounded networking, namespaces, cgroups, and security
     checkpoint passed`
  4. `pfmodel` → `pfmodel: not-present/protection/unmapped classified; bounded page
     inserted`
  5. `ptrtest` → `ptrtest: canonical/range/VMA/permission checks passed`
  6. `taskvalidate` → `task validation: passed (bounded table, unique PID/TID, valid
     parent/state)`
  7. `waittest` → `waittest: bounded wait, exit status, zombie selection, and reap
     passed`
  8. `moduletest` → `moduletest: module init order and exported-symbol lookup passed`
- **如何判断成功**：上述命令逐一打印预期串即成功；`make check` 三条 grep 全命中即
  通过。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|---------|
| `l161test` 输出 `Lesson 154 fallback reported` | checkpoint 模型字段被改坏（a/b/c/d 不连续或状态位清零） | 比对 `l161test` 的赋值 `{154U,155U,156U,157U,1,1,1,1}` 与五连断言；确认 `b==a+1` |
| 输入 `l154test` 报 `unknown command` | 旧 README 命令名是笔误，源码无此命令 | 源码中可用命令是 `l153test` 与 `l161test` |
| `pfmodel` 输出 `BROKEN` | `pf_classify` 某条决策规则与断言不符（如 VMA 未初始化、prot 被改） | `vmainfo` 确认三块 VMA 存在且 code 为 `r-x`；检查 `vma_init` 调用顺序 |
| `ptrtest` 输出 `BROKEN` | `uaccess_validate` 某层裁决与断言不符 | 检查 `USER_CANONICAL_MAX/USER_RANGE_MAX/USER_COPY_MAX`；确认 data 段 `rw-`、code 段 `r-x` |
| `taskvalidate` 输出 `BROKEN` | `task_table` 有重复 PID/TID 或父 PID 不小于子 PID | 检查 `task_table_validate` 的双重循环去重逻辑与 `parent_pid>=pid` 判定 |
| `waittest` 输出 `BROKEN` | `wait_model` 状态迁移顺序错乱 | 确认 wait 前先 exit（`WAIT_ZOMBIE`）、reap 前先 wait（`waited==1`） |
| 启动横幅仍是旧课文案 | 没重建或看错课程目录 | 重建后横幅应为 `Lesson 161: 安全策略决策`；`make check` 的 grep 覆盖此串 |

---

## 7. 与 Linux 源码对照

1. **LSM 钩子框架**：Linux `security/security.c` 定义 `security_hook_heads` 与
   `call_int_hook`，`security_add_hooks` 注册模块；TinyOS 没有钩子表，但它的
   「裁决函数」（`pf_classify`/`uaccess_validate`/`task_transition`）扮演了「在动作
   前调用裁决」的等价角色——只是裁决逻辑直接写死在函数里而非可插拔。
2. **主体/客体/操作三元组**：SELinux 的策略决策基于 `(subject_sid, object_sid,
   class, permission)`（`security/selinux/ss/services.c` 的 `avc_has_perm`）；
   TinyOS 的 `uaccess_validate(address,length,access)` 是
   `(调用者, 目标地址区间, 读写操作)` 的三元组极简版，输出布尔而非 SID 鉴权。
3. **先裁决后执行**：Linux 在 `sys_open` → VFS `security_file_open` 钩子后才会建立
   file 对象；TinyOS `wait_model_wait` 只在 `WAIT_ZOMBIE` 时置 `waited`、
   `resource_teardown` 只在 `zombie` 时释放——都是「状态/权限不满足则不做」。
4. **拒绝的返回值语义**：Linux LSM 拒绝返回 `-EPERM`/`-EACCES` 等 errno；TinyOS
   「拒绝」是返回 0 / `BROKEN` / 枚举分支，没有 errno 语义——`syscall_dispatch` 的
   `-ENOSYS`（Lesson 159）是唯一一处负错误码。
5. **审计联动**：Linux LSM 决策结果交给 audit（Lesson 160 主题）；TinyOS
   `pf_classify` 每次裁决都递增对应 fault 计数器、`uaccess_validate` 递增
   `uaccess_attempts`——「裁决即记账」的最小审计形态。
6. **状态机安全**：Linux 有 `sched_setscheduler`/`setuid` 等的权限与状态检查、
   `security_task_fix_setuid` 钩子；TinyOS `task_transition` 的「死亡态不可迁移」
   规则对应内核「任务状态转换受内核代码控制」的语义。

**权威来源**：Linux `security/security.c`（LSM 框架）、`security/selinux/ss/services.c`
（AVC 决策）、`include/linux/lsm_hooks.h`、`Documentation/admin-guide/LSM`。
**教学模型简化了什么**：真实 LSM 有可插拔钩子链、模块优先级、AVC 缓存、subject/object
SID 与安全上下文（`security/xattr`），还有 SELinux/AppArmor 复杂策略语言；TinyOS
只保留「入口条件检查 + 返回布尔/枚举」的裁决骨架，没有策略语言、没有钩子注册表。

---

## 8. 思考题与练习

1. **概念理解**：LSM 决策的输入三元组是什么？`uaccess_validate` 的参数里哪个对应
   subject、哪个对应 object、哪个对应 action？
2. **源码定位**：在 `kernel64.c` 中找出至少三个「先裁决后执行」的函数（如
   `pf_classify`、`wait_model_wait`、`resource_teardown`），说明各自的「裁决条件」
   与「不满足时的返回」。
3. **动手实验**：修改 `l161test` 的赋值，把 `a` 从 `154U` 改成 `153U`，重新构建运行，
   观察输出是否仍为 passed（`b==a+1` 仍成立）；再把 `accounted` 改成 `0` 观察翻转。
4. **动手实验**：给 `pf_classify` 的 `PF_PROTECTION` 分支临时删除计数
   （`fault_protection++`），重新构建后运行 `pfmodel` 与 `vmainfo`，观察 fault
   计数器的变化并解释「裁决 + 记账」的耦合。
5. **Linux 对照**：阅读 `security/security.c` 的 `call_int_hook` 与
   `security/selinux/ss/services.c` 的 `avc_has_perm`，对比它们与 TinyOS 直接写死的
   裁决函数的差异，指出「可插拔钩子」与「内联检查」各自的代价。

---

## 9. 本课小结与下一课预告

**小结**：
1. 本课是资源/安全主题的检查点课，`kernel64.c` 相对上一课只有 4 处小增量，主题由
   banner/about 文案标识，**源码中无 LSM 钩子**——本课是「主题宣告 + 概念模型」型
   检查点课。
2. LSM 安全策略决策 = 敏感操作前挂钩子 → 以（subject, object, action）三元组为
   输入 → 返回允许/拒绝（0 或 `-EPERM`）。
3. TinyOS 的本地近似：`pf_classify`（枚举三分类）、`uaccess_validate`（布尔放行）、
   `module_lookup`（符号可见性）、`task_transition`（状态机迁移）、`wait_model_wait`
   （状态即策略）——都是「先裁决后执行」。
4. 决策 + 记账：`pf_classify`/`uaccess_validate` 每次裁决都递增计数器，是「决策即
   审计」的最小形态。
5. 新 checkpoint `l161test` 用字面量赋值 + 五连断言固化回归探针；模型名
   `lesson_154_model` 的 154 = 161−7 延续「回锚」链。
6. 旧 README 的 `Commands: l154test` 已勘误为源码实际的 `l153test` 与 `l161test`。

**下一课**：[`lesson-162-stable/README.md`](../lesson-162-stable/README.md) 主题为
「网络、namespace、cgroup、安全综合 checkpoint」，是本课程的**最终一课**，将把
本主题序列（网络/namespace/cgroup/安全）与全课程主线收束在一起，追加最终的
checkpoint 模型（命令 `l162test`）。两课的衔接点是「安全监控链」：本课讲
「策略决策」，下节课做全课程的综合验收与主线回顾。
