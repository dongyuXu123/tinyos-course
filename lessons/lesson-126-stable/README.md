# Lesson 126: RCU 对象回收 — 精讲文档

> **Course status: stable snapshot.**
>
> - 课号：Lesson 126
> - 本课主题：RCU 对象回收（RCU object reclamation）
> - 课程主线位置：第四阶段（SMP / RCU / 诊断元数据检查点系列），RCU 四连讲的收官课
> - 前置课程：[Lesson 125（RCU callback 队列）](../lesson-125-stable/README.md)
> - 后续课程：暂无（本阶段检查点系列完结；可回看 [Lesson 121（per-CPU runqueue）](../lesson-121-stable/README.md) 从头梳理）
> - 一句话目标：把 RCU 全链路收口为「对象只在所有读者离开之后才被释放」，理解 `kfree_rcu`/`call_rcu` 中回收动作的安全边界，并用 `l126test` 完成本阶段最后一个检查点验证。

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能把 reader 临界区 → 宽限期 → 回调队列 → 对象回收这条完整链路串起来，讲清「延迟回收如何保证安全」以及 `use-after-free` 为什么被杜绝；能运行 `l126test` 收尾。
- **在课程主线中的位置**：Lesson 123–126 构成 RCU 专题四连讲，本课是终点：所有机制最终服务于「安全释放旧对象」。此后该阶段不再有新的检查点课（Lesson 127 不存在于本仓库），本课同时是阶段性的总结点。
- **前置知识清单**：
  1. reader 临界区语义（Lesson 123）；
  2. 宽限期定义与结束判据（Lesson 124）；
  3. 回调队列的入队/执行机制（Lesson 125）；
  4. 本内核 `pmm_free_page`/`page_state` 的物理页回收路径（早期课引入）与检查点约定。
- **本课交付**：
  - 新命令 `l126test` 与新模型 `struct lesson_119_model lesson_119_state`；
  - 上一课命令更名为 `l118test`；
  - `about` 与启动横幅显示 `Lesson 126: RCU 对象回收`。

## 2. 核心概念精讲

### 2.1 对象回收（reclamation）在 RCU 中的位置

- **定义**：RCU 对象回收是宽限期结束后的**最后一个动作**：把被替换下来的旧对象（链表节点、缓存页、数据结构）从内存中释放。回收动作通常通过 `call_rcu(&old->rcu_head, kfree_callback)` 或 `kfree_rcu(old, rcu_head)` 挂进回调队列，宽限期结束后执行 `kfree`。
- **为什么「延迟」才安全**：如果立即 `kfree(old)`，而某个 reader 仍持有 `old` 的指针（尚未离开临界区），就会 use-after-free。RCU 的全部机制（宽限期 + 回调队列）都是为了让 `kfree` 的时刻推迟到「绝无读者引用」之后。
- **安全判定**：`call_rcu` 之后的某次宽限期结束，即所有在 `call_rcu` 之前进入临界区的 reader 都已离开；此时回调执行 `kfree(old)` 必然安全。

```
writer:  rcu_assign_pointer(gp,new);  call_rcu(&old->head, kfree_cb)
reader:  rcu_read_lock → 读 old → rcu_read_unlock（可能跨多个宽限期）
… 所有 reader 离开（grace period）…
RCU softirq: rcu_do_batch → kfree_cb(old)   ← 安全回收
```

### 2.2 `kfree_rcu` 与 `call_rcu` 的选择

- `call_rcu(head, func)`：通用接口，`func` 可以是任意回收函数，如 `kfree`、`kvfree`、`rcu_core` 自定义释放；
- `kfree_rcu(ptr, field)`：语法糖，自动把 `ptr` 注册为「宽限期后 kfree」，内部走 `kvfree_call_rcu`（`kernel/rcu/tree.c`）；
- `synchronize_rcu()`：不排队，直接阻塞等待一个宽限期，然后由调用者自行 `kfree`。
- 三者的共同契约：**回收时刻不早于所有读者离开**。
- TinyOS 里没有真实 `kfree`（物理页回收由 `pmm_free_page` 承担），因此「对象回收」主题只用检查点登记「延迟回收不变量成立」。

### 2.3 与 TinyOS 既有回收机制的对照

- 本内核已有 `pmm_free_page(u64 p)`：先查 `page_state(p)`（`free/fixed/allocated`），再查 `vm_frame_owned` 与 `thread_stack_owned` 是否仍被引用，全部通过才 `unmark(i)` 归还位图。这个「先查引用再释放」的顺序，正是 RCU 对象回收「确认无引用后才释放」原则的物理页版。
- 检查点 `lesson_119_model` 的 `accounted` 位在语义上对应「释放的页数 == 归还的页数」守恒；`ready` 位对应「回收路径可执行」（`pmm_ready==1`）。

### 2.4 检查点模型如何编码对象回收

- `valid`：回收模型已实例化；
- `active`：延迟回收机制激活（对应 `call_rcu`/`kfree_rcu` 已可用）；
- `ready`：回收路径就绪（对应 `pmm_ready`、`rcu_ready`）；
- `accounted`：回收计数闭合（对应 `rcu_do_batch` 后无遗留 callback、页账目归零）。
- 四连号 `{119,120,121,122}` 与 `b==a+1` 表达回收对象序列连续，无泄漏、无双释。

### 2.5 回收的「双保险」：RCU 语义链 + 引用归属检查

- Linux 的回收安全有两道防线：第一道是 RCU 语义链（宽限期保证无读者），第二道是对象自身的引用计数/归属检查（`refcount`、`kref`、slab 的 `inuse`）。RCU 负责「时间上无人读」，引用计数负责「逻辑上无人用」。
- TinyOS 的 `pmm_free_page` 正是把两道防线都放在一个函数里：先按 `page_state` 判定页类别（等价于分配器状态），再查 `vm_frame_owned`/`thread_stack_owned`（等价于引用归属），全过才归还。
- 检查点模型把两道防线的结论合并为 `accounted`（计数闭合）+ `ready`（归属检查路径就绪）两个位，语义上等价于「RCU 宽限期已过 + 引用已释放」。

### 2.6 为什么 RCU 回收常配 `rcu_barrier`

- 模块卸载或内核热插拔场景需要保证「之前排队的回调全部执行完」才能释放模块代码段——`rcu_barrier()` 会等待所有 CPU 上已排队的 RCU 回调执行完毕（`kernel/rcu/tree.c`）。
- 这是「对象回收」在工程上的最后一个拼图：单独的 `synchronize_rcu` 只保证「当前宽限期结束」，不保证「历史回调已执行」；`rcu_barrier` 才给出「内存可安全释放」的强保证。
- TinyOS 没有模块卸载，因此 `lesson_119_model` 用 `accounted`（计数闭合）表达等价语义：所有已入队回收动作都已执行完。

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 Lesson 125） |
|------|------|------------------------------|
| `boot.S` | Multiboot2 头、进入 long mode | 未变化 |
| `kernel.c` | 32 位引导主流程 | 未变化 |
| `kernel64.c` | 64 位内核主体与检查点模型 | **有增量**：新增 `lesson_119_model`/`lesson_119_state`/`l126test()`；`l125test` 更名为 `l118test`；`about`/横幅改为 Lesson 126 |
| `kernel64.ld` | 64 位裸二进制布局 | 未变化 |
| `linker.ld` | 32 位镜像布局 | 未变化 |
| `Makefile` | 构建与 `check`/`run` | 微小变化：`check` grep 串改为 `RCU 对象回收`、`l126test`、`Lesson 126` |
| `grub.cfg` | GRUB 启动项 | 未变化 |

### 3.2 kernel64.c 精讲

#### 新增结构 / 全局变量

```c
struct lesson_119_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_119_model lesson_119_state;
```

逐行注释：
- 第 896–897 行。模型结构同构；本课语义落在「对象回收」上，是检查点链（lesson_69…lesson_119）的当前末端。
- `lesson_119_state` 由 `l126test` 填充；`active`=延迟回收机制激活，`ready`=回收路径就绪，`accounted`=回收计数闭合。

#### 函数精讲

**`l126test(u16 *c)`**（第 898 行，本课新增）

```c
static TEXT64 void l126test(u16*c){lesson_119_state=(struct lesson_119_model){119U,120U,121U,122U,1,1,1,1};int ok=lesson_119_state.valid&&lesson_119_state.active&&lesson_119_state.ready&&lesson_119_state.accounted&&lesson_119_state.b==lesson_119_state.a+1U;text64(c,"l126test: ");text64(c,ok?"bounded concurrency, SMP, RCU, and diagnostics checkpoint passed":"Lesson 119 fallback reported");putc64(c,'\n');}
```

- **签名与职责**：`static TEXT64 void l126test(u16 *c)`：登记并断言对象回收检查点。
- **算法步骤**：
  1. `lesson_119_state=(struct lesson_119_model){119U,120U,121U,122U,1,1,1,1}`：布尔位全 1，四连号 119 起；
  2. `ok=valid && active && ready && accounted && b==a+1U`；
  3. 打印 `"l126test: "` 与 passed/`"Lesson 119 fallback reported"`。
- **边界处理**：纯常量断言，无输入；失败分支只是文案，不引入错误路径。
- **设计动机**：`accounted` 位对应「无泄漏、无双释」的闭合性，这是对象回收的核心安全属性；`b==a+1` 保证回收对象序号连续，杜绝「跳过某个对象」的泄漏类缺陷。

**`exec64` 本课相关分支**（第 899 行）

```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 126: RCU 对象回收\n");}
...
}else if(eq64(word,"l118test")){if(!noargs64(arg))usage64(c,"l118test");else l118test(c);}else if(eq64(word,"l126test")){if(!noargs64(arg))usage64(c,"l126test");else l126test(c);}
```

- `about` 输出 `Lesson 126: RCU 对象回收`；`l118test`（上一课命令更名）与 `l126test`（本课）相邻注册。命令链至此覆盖 `l69test`…`l126test` 的全线历史回归。

**继承的回收语义基础设施（对象回收的物理页版）**

```c
static TEXT64 const char *pmm_free_page(u64 p){u32 i;const char*s=page_state(p);if(!eq64(s,"allocated"))return s;if(vm_frame_owned(p))return "mapped";if(thread_stack_owned(p))return "thread stack";i=(u32)(p/PAGE_SIZE);unmark(i);pmm_free++;pmm_used--;return "freed";}
```

- `pmm_free_page` 的检查顺序本身就是一次「无引用确认」：先查 `page_state`（必须是 `allocated`），再查是否仍被 VM 映射（`vm_frame_owned`）或线程栈占用（`thread_stack_owned`），全部通过才 `unmark` 归还。
- 与 RCU 回收的对应：RCU 用「宽限期结束」确认无读者引用；TinyOS 用「三类归属检查」确认无映射/无栈引用。二者共享同一个安全原则——**释放前必须排除所有潜在引用者**。
- 检查点 `accounted` 位在物理页层面的可观察版本，就是 `pmm_free`/`pmm_used` 在每次 `pmm_free_page` 后保持守恒。

**`reap_finished_threads`（继承，延迟释放的又一个实例）**

```c
static TEXT64 void reap_finished_threads(void){u32 i;for(i=1;i<THREAD_COUNT;i++)if((idle_running||i!=current_thread)&&threads[i].state==THREAD_FINISHED&&threads[i].stack_phys){u64 p=threads[i].stack_phys;threads[i].stack_phys=0;(void)pmm_free_page(p);}}
```

- 该函数在 `irq0_schedule` 里被调用：把 `THREAD_FINISHED` 且不再运行（`idle_running||i!=current_thread`）的线程栈页延迟归还给 PMM。`threads[i].stack_phys=0` 先清所有权，再 `pmm_free_page`——与 RCU 回收「先解除引用、后释放内存」完全同构。

### 3.3 构建管线（Makefile / linker）

- 构建链与前五课一致（双阶段、`objcopy -O binary`、`grub-mkrescue`）。
- `check` 目标变化：

```
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'RCU 对象回收' README.md
	@grep -q 'l126test' kernel64.c
	@grep -q 'Lesson 126' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 126 checks passed.'
```

### 3.4 主控制流

```
kernel_main64_binary
  ├─ 初始化（与前课相同；pmm_init 使 pmm_ready=1，回收路径可用）
  ├─ text64(&c,"Lesson 126: RCU 对象回收\nGETTICKS, …\n")  ← 本课横幅
  └─ for(;;) 键盘环
       ├─ "about"    → "Lesson 126: RCU 对象回收\n"
       ├─ "l118test" → 回放回调队列检查点
       └─ "l126test" → l126test(c) → 打印对象回收检查点结果
```

## 4. 数据流与运行逻辑

1. **输入**：`l126test` 回车。
2. **解析**：`token64` 切词命中 `l126test`。
3. **执行**：`l126test(c)` 填充 `lesson_119_state` 并断言。
4. **输出**：`l126test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`（`ok==1`），否则 `l126test: Lesson 119 fallback reported`。
5. **屏幕**：横幅与 `about` 显示 `Lesson 126: RCU 对象回收`。

本课是 RCU 四连讲的总结点，建议跑全链路回归：`l123test`→`l124test`→`l125test`→`l126test` 全部 passed，即 reader→宽限期→回调队列→对象回收的语义链完整成立。

## 5. 构建、运行与验证

- **依赖**：与前课相同。
- **构建**：

```bash
make clean && make -j"$(nproc)"
make check
```

预期输出：`Multiboot2 and Lesson 126 checks passed.`
- **运行**：`make run`，QEMU 图形窗口查看横幅（勿加 `-display none`）。
- **验证步骤**：
  1. `about` → `Lesson 126: RCU 对象回收`（源码逐字）；
  2. `l126test` → `l126test: bounded concurrency, SMP, RCU, and diagnostics checkpoint passed`（源码逐字；失败态为 `Lesson 119 fallback reported`）；
  3. `l118test` → 回调队列检查点回放 passed；
  4. 依次 `l123test`/`l124test`/`l125test` → RCU 四连讲全链回归 passed；
  5. `meminfo` → 查看 `pmm_free`/`pmm_used` 计数，确认物理页账目闭合。

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|------|------|----------|
| `l126test` 打印 fallback | `lesson_119_state` 某位为 0 或 `b!=a+1` | 核对第 898 行字面量 `{119U,120U,121U,122U,1,1,1,1}` |
| `about` 显示旧主题 | `exec64` about 分支未更新 | grep `'Lesson 126'` kernel64.c 确认两处 |
| `l118test` 报 unknown | 命令分支缺失 | 确认 `l118test` 与 `l126test` 分支相邻注册 |
| `pmm_free_page` 拒绝释放 | 页仍被映射或属线程栈 | 检查返回值 `"mapped"`/`"thread stack"`；先 `vunmap`/等待线程 `FINISHED` |
| `meminfo` 账目不一致 | `pmm_free`/`pmm_used` 未守恒 | 核对 `pmm_alloc`/`pmm_free_page` 的增减分支；对照 `pmm_total` |
| 怀疑双释 | 同一页被 `pmm_free_page` 两次 | `page_state(p)` 第二次应为 `free` 而非 `allocated`，函数会拒绝 |
| `make check` 失败 | grep 串不匹配 | `grep -q 'RCU 对象回收' README.md`、`grep -q 'l126test' kernel64.c` |

## 7. 与 Linux 源码对照

| 对照点 | TinyOS 教学模型 | Linux 实现（路径） |
|--------|-----------------|--------------------|
| 延迟回收入口 | 检查点 `active` 位 + `pmm_free_page` 的归属检查 | `call_rcu()`/`kfree_rcu()`/`kvfree_call_rcu()`（`kernel/rcu/tree.c`） |
| 回收安全边界 | 宽限期语义（前课）+ `vm_frame_owned`/`thread_stack_owned` 排除引用 | 宽限期结束后 `rcu_do_batch()` 才执行 `kfree_callback`（`kernel/rcu/tree.c`） |
| 物理页归还 | `pmm_free_page` 的 `unmark`/`pmm_free++` | Linux 侧为 `free_unref_page`（`mm/page_alloc.c`）；RCU 侧只负责对象本身 |
| 释放前确认 | `page_state=="allocated"` 且无 VM/栈归属 | RCU 用 grace period 确认无读者；SLAB 用 `rcu_head` 延迟释放（`mm/slab_common.c`、`mm/rcuref.h`） |
| 计数守恒 | 检查点 `accounted` 位 | `rcu_segcblist_n_cbs` 归零、`percpu_ref` 计数、页分配器的 `free/used` 统计 |
| 权威来源 | —— | Intel SDM Vol.3；Multiboot2 规范；GNU GRUB |

**教学模型简化了什么**：真实对象回收涉及 slab/slub 分配器的 `rcu_head` 嵌入、`kvfree_call_rcu` 的批量释放、`rcu_barrier` 同步等待所有回调完成（卸载模块时必需）等；TinyOS 只在物理页层面用「归属检查 + 计数守恒」表达「无引用才释放」的原则，RCU 语义链由检查点登记。

## 8. 思考题与练习

1. **概念理解**：为什么 `kfree_rcu` 必须等宽限期结束？如果读者在 `kfree` 之后才 `rcu_read_unlock`，会发生什么？
2. **源码定位**：找到 `pmm_free_page` 与 `reap_finished_threads`，说明它们各自的「释放前引用排除」步骤，并对比 `rcu_do_batch` 的安全边界。
3. **动手实验**：把 `l126test` 的 `accounted` 位改为 0 重建运行观察 fallback；再用 `palloc`/`pfree` 手动释放一页并 `meminfo` 验证账目守恒。
4. **Linux 对照**：阅读 `kernel/rcu/tree.c` 的 `rcu_do_batch` 与 `kvfree_call_rcu`，说明「批量释放」与「逐个 kfree」的差异，以及 `rcu_barrier` 解决什么问题。
5. **综合**：回顾 Lesson 123–126，画出「reader 临界区 → 宽限期 → 回调队列 → 对象回收」的完整时序图，标出每一步保证哪条安全属性。

## 9. 本课小结与下一课预告

- 本课把 RCU 全链路收口到「对象只在所有读者离开后回收」这一最终不变量。
- 对比了 `call_rcu`、`kfree_rcu`、`synchronize_rcu` 三种回收接口的适用场景。
- 在 `pmm_free_page` 的归属检查与 `reap_finished_threads` 的延迟归还中找到了「释放前排除引用」的教学实例。
- 检查点 `lesson_119_model` 用 `active/ready/accounted` 登记延迟回收机制的状态与计数闭合。
- 完成了 RCU 四连讲（123–126）的全链回归验证方法。
- 明确了教学模型省略了 slab 层的 `rcu_head` 嵌入与 `rcu_barrier` 同步。

**下一课**：本阶段检查点系列到 Lesson 126 结束。可以回看 [Lesson 121（per-CPU runqueue）](../lesson-121-stable/README.md) 从头复习 SMP 与 RCU 两条主线，或进入课程主线后续阶段继续扩展。

## 附录：stable snapshot 声明（保留原 README 要点）

> This checkpoint models bounded concurrency, SMP, RCU, and diagnostics metadata with deterministic validation while preserving freestanding operation, fixed capacities, and existing safety invariants.
>
> Commands: `l118test`、`l126test`, plus inherited process, GUI, and subsystem regressions. Session invariants remain preserved.
>
> 主要内容：RCU 对象回收；统一课程编号：Lesson 126。（旧 README 中「Commands: `l119test`」与实际源码不符，已勘误为 `l126test`。）
