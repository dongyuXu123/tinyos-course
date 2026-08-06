# Lesson 77: priority/nice 优先级状态 — 精讲文档

> **课号**：Lesson 77（对应主线源课 Lesson 70）
> **本课主题**：任务优先级（priority）与 nice 值的元数据 checkpoint
> **课程主线位置**：GUI 支线（Lesson 61–67）结课后的「进程组/session/调度元数据」阶段（Lesson 68 起恢复主线）。调度器元数据子主题第二课：76（调度策略 checkpoint）之后，本课在 priority/nice 主题下再做一次 checkpoint，锁定"调度/COW 累积元数据依然自洽"。
> **前置课程**：[`../lesson-76-stable/README.md`](../lesson-76-stable/README.md)（调度策略元数据：`sched_class` 分派表与 `l76test` checkpoint 惯例）
> **后续课程**：[`../lesson-78-stable/README.md`](../lesson-78-stable/README.md)（runqueue 运行队列统计）
> **本课一句话目标**：学会用「固定元数据 + 确定性验证」模型表达 priority/nice 主题下的调度元数据 checkpoint——理解 Linux 中 `static_prio`/`prio`/`nice` 的数值关系（nice 越大优先级越低，`NICE_TO_PRIO` 映射），并会用 `l77test` 验证累积元数据的一致性。

---

## 1. 课程定位（Mission）

- **一句话目标**：学完本课你能解释 priority 与 nice 的关系、`nice(2)` 为什么只能在一定范围内调整优先级，能用 `l77test` 做确定性 checkpoint，并能指出 Linux 中实现它的文件与宏。
- **在课程主线中的位置**：调度器元数据子主题三连课（76→77→78）。Lesson 76 锁定了 `sched_class` 调度策略基线；本课在 **priority/nice** 主题上再做一次 checkpoint（新增 `lesson_70_model` 记录 + `l77test`，并把上一课 `l76test` 改名为 `l69test` 保留为回归锚点）；Lesson 78 将进入 runqueue。三者共同完成"策略 → 优先级 → 队列统计"的调度器元数据全景。
- **前置知识清单**：
  1. `sched_class` 分派表与 `schedinfo`（Lesson 38 起累积，Lesson 76 复习）；
  2. Linux 优先级模型：静态优先级 `static_prio`、动态优先级 `prio`、`nice` 值域 −20..19（本课概念主体）；
  3. 系列 checkpoint 惯例：`l65test`/`l76test` 这类"元数据一致性冒烟"及其改名规律（`lXXtest` 随课程推进改名保留回归，Lesson 72–73 起一致）；
  4. COW 元数据计数：`anon_pages`、`page_cache_count`、`reclaim_scans`（Lesson 44 起累积）。
- **本课交付**：新增固定容量 `lesson_70_model` 记录与 `l77test` 验证命令；上一课 `l76test` 更名为 `l69test` 作为回归锚点；`about`/banner 更新为「Lesson 77: priority/nice 优先级状态」。

---

## 2. 核心概念精讲

### 2.1 优先级（priority）：谁先执行

**直觉**：调度器手里有一堆就绪任务，优先级就是给它们排队时的"插队权"。数字越小越优先（Linux 约定）。

**准确定义（Linux）**：
- `task->static_prio`：静态优先级，由 nice 值换算（`NICE_TO_PRIO(nice)`），进程生命周期内基本不变；
- `task->prio`：动态优先级，等于 `static_prio` 减去奖励/惩罚（如 RT 任务提升、交互性奖励），是实际调度比较的对象；
- RT 任务（`SCHED_FIFO`/`SCHED_RR`）优先级范围 0–99，CFS 普通任务 100–139，数值越小越优先。

### 2.2 nice 值：用户能动的"旋钮"

- `nice(2)` 系统调用：把进程的 nice 值设为参数，值域 **−20（最优先）到 +19（最不优先）**；
- 权限规则：只有特权进程（`CAP_SYS_NICE`）能设负 nice；普通进程只能提高（变懒惰），不能降低；
- 映射：`static_prio = 120 + nice`（Linux 3.x 后为 `MAX_RT_PRIO + nice`，即 `100 + 20 + nice = 120 + nice`）。`nice=0` → `static_prio=120`。
- 效果：nice 差每 1 级，CFS 中约相当于 1.25 倍的权重比（weight 表 `sched_prio_to_weight`），nice=19 的任务几乎只有 nice=−20 的 1/1000 权重。

### 2.3 教学模型：checkpoint 而不实现优先级

本课的 `lesson_70_model` 与 Lesson 76 的 `lesson_69_model` 同构（a/b/c/d 代际 + valid/active/ready/accounted 四标志），只是代际起点推进到 70、命名对应源课 70。它不真的给 `task_struct` 加 `prio` 字段，而是用「固定元数据 + 确定性验证」守住"调度/COW 元数据依然自洽"这条底线——priority/nice 主题在概念层展开，验证层用 checkpoint 落地：

```
priority/nice 概念层                    checkpoint 验证层
─────────────────────                 ────────────────────
nice 值域 −20..19          ──►        a,b,c,d 代际连续 (b==a+1)
static_prio=120+nice      ──►        valid: 模型已合法初始化
prio 动态调整             ──►        active: 调度策略已激活
调度只认 prio            ──►        ready: 就绪集合存在
count 记账              ──►        accounted: 记账一致
```

### 2.4 命令改名惯例（l76test → l69test）

观察 Lesson 72→73、76→77 的 diff：`l72test` 在第 73 课改名为 `l65test`，`l76test` 在第 77 课改名为 `l69test`，同时新增本课编号的 `l77test`。规律是：**每个 checkpoint 命令标注其源课（Origin）编号**，课程推进时旧命令改名保留为回归锚点，新命令携带新源课编号。这使得 `kernel64.c` 累积数百行后，历史验证仍可一键重放。

---

## 3. 源码精讲

### 3.1 文件清单与职责

| 文件 | 职责 | 本课增量（相对 lesson-76） |
|---|---|---|
| `kernel64.c` | 64 位内核主体：全部元数据模型、`exec64` 命令分派、主循环 | **主要增量**：新增 `lesson_70_model` 结构、`lesson_70_state` 全局、`l77test()`；上一课 `l76test` 改名为 `l69test`；`exec64` 新增/改名两个分支；`about`/banner 文案更新 |
| `kernel.c` | 32 位引导：MB2 内存图解析、页表搭建、用户镜像校验 | 未变化 |
| `boot.S` | Multiboot2 header、进入 long mode、内嵌 `kernel64.bin` | 未变化 |
| `kernel64.ld` | 64 位续传段布局与栈断言 | 未变化 |
| `linker.ld` | 外层 ELF32 镜像段布局 | 未变化 |
| `Makefile` | 构建 + `check` 静态断言 + `run` | 微变化：`check` 目标三条 `grep` 换成 Lesson 77 关键字 |
| `grub.cfg` | GRUB 菜单 | 未变化 |

### 3.2 kernel64.c 精讲

#### (a) 新增结构体与全局变量

```c
struct lesson_70_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_70_model lesson_70_state;
```

逐行注释：
- `a,b,c,d`（u32）：代际/计数序列 70→71→72→73（比 Lesson 76 的 69→72 整体 +1），对应本课源课编号 70 及之后推进；校验用 `b==a+1U` 保证相邻代际连续。
- `valid`（u8）：模型已合法初始化——防止把未初始化状态当通过。
- `active`（u8）：调度策略激活态（`active_sched_class` 非空）。
- `ready`（u8）：就绪集合存在（`THREAD_RUNNABLE` 任务可被挑选）。
- `accounted`（u8）：调度/COW 记账一致。
- `static struct lesson_70_model lesson_70_state;`：单一全局 checkpoint 记录。

为什么字段与 `lesson_69_model` 完全同构？因为本课是**主题 checkpoint**：priority/nice 不改变调度元数据的"健康属性集合"（合法/激活/就绪/记账），只改变排序依据——教学模型在验证层保持同构，把主题差异留在概念层讲解。

#### (b) 上一课 `l76test` 改名：`l69test`

```c
static TEXT64 void l69test(u16*c){lesson_69_state=(struct lesson_69_model){69U,70U,71U,72U,1,1,1,1};int ok=lesson_69_state.valid&&lesson_69_state.active&&lesson_69_state.ready&&lesson_69_state.accounted&&lesson_69_state.b==lesson_69_state.a+1U;text64(c,"l69test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 69 fallback reported");putc64(c,'\n');}
```

逐行注释：
- 函数体与 Lesson 76 的 `l76test` **逐字相同**，仅函数名与输出前缀从 `l76test` 改为 `l69test`。
- 改名依据：命令名遵循"源课编号"约定，本命令守护的是源课 69 的 checkpoint 语义；`exec64` 中 `eq64(word,"l69test")` 分支替代了原 `l76test` 分支。
- 意义：历史验证不丢失、命令语义连续；输入 `l69test` 仍可重放上一课的全部断言。

#### (c) 本课核心验证函数 `l77test`

```c
static TEXT64 void l77test(u16*c){lesson_70_state=(struct lesson_70_model){70U,71U,72U,73U,1,1,1,1};int ok=lesson_70_state.valid&&lesson_70_state.active&&lesson_70_state.ready&&lesson_70_state.accounted&&lesson_70_state.b==lesson_70_state.a+1U;text64(c,"l77test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 70 fallback reported");putc64(c,'\n');}
```

逐行注释：
- `lesson_70_state=(struct lesson_70_model){70U,71U,72U,73U,1,1,1,1};`：聚合初始化代际 70/71/72/73，四标志全 1——理想 checkpoint 场景。
- `int ok=lesson_70_state.valid&&lesson_70_state.active&&lesson_70_state.ready&&lesson_70_state.accounted&&lesson_70_state.b==lesson_70_state.a+1U;`：5 条件：合法、激活、就绪、记账一致、代际连续（`71==70+1`）。
- `text64(c,"l77test: ");`：命令前缀。
- `text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 70 fallback reported");`：成功串与 Lesson 76 相同（checkpoint 覆盖面一致），失败串标注源课 70。
- `putc64(c,'\n');`：换行。

为什么这样设计：priority/nice 主题的 checkpoint 与 Lesson 76 保持同一判定形状，才能把"调度元数据自洽"这条不变量跨课复用；若每课换一套判定，回归就无从谈起。

#### (d) `exec64` 命令分派中的增量分支

```c
}else if(eq64(word,"l69test")){if(!noargs64(arg))usage64(c,"l69test");else l69test(c);}
}else if(eq64(word,"l77test")){if(!noargs64(arg))usage64(c,"l77test");else l77test(c);}
```

逐行注释：
- `l69test` 分支由原 `l76test` 分支改名而来，`l77test` 为新分支；两者都执行"匹配 → 拒绝多余参数 → 调用"。
- 分支顺序：`l69test` 紧跟 `stop68test`，`l77test` 紧随其后，保持源课编号递增（68→69→70）。
- `about` 文案更新为：

```c
}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 77: priority/nice 优先级状态\n");}
```

- `help` 命令列表保持旧字面量（不含 `l69test`/`l77test`），教学简化延续。

#### (e) 内核主入口 `kernel_main64_binary` 的 banner 增量

```c
text64(&c,"Lesson 77: priority/nice 优先级状态\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");
```

- 首行主题换成本课；syscall ABI 与 "bounded reclaim metadata" 边界不变。

#### (f) 继承的关键辅助函数（本课复用）

`eq64`、`noargs64`、`text64`/`putc64`、`token64` 均带 `TEXT64` 段属性；`rr_pick_next`（`tiny_rr` 轮转挑选）仍是调度策略的实际选择逻辑——本课不为它加入优先级比较，这正是教学模型"元数据真实、行为不执行"的体现。

### 3.3 构建管线（Makefile / linker）

```make
check: $(BUILD)/kernel.elf
	grub-file --is-x86-multiboot2 $(BUILD)/kernel.elf
	@grep -q 'priority/nice 优先级状态' README.md
	@grep -q 'l77test' kernel64.c
	@grep -q 'Lesson 77' README.md
	@printf '%s\n' 'Multiboot2 and Lesson 77 checks passed.'
```

- 三条 `grep -q` 分别锁定 README 主题、新符号 `l77test`、课号 77；`printf` 信息逐字来自 Makefile。
- `grub-file --is-x86-multiboot2` 与构建链、`kernel64.ld` 栈断言均与 Lesson 76 一致。

### 3.4 主控制流

```
GRUB ──► boot.S(_start) ──► kernel_main32 ──► enter_long_mode ──► kernel_main64_binary
    ├─ 元数据初始化（...）
    ├─ banner: "Lesson 77: priority/nice 优先级状态\nGETTICKS, ..."
    └─ for(;;) 键盘循环:
        "l77test\n" ──► exec64 ──► eq64(word,"l77test")
        ──► l77test(c)
        ──► lesson_70_state 初始化 {70,71,72,73,1,1,1,1} + 5 条件校验
        ──► VGA: "l77test: bounded scheduling and copy-on-write checkpoint passed"
        ──► "tinyos> "
```

---

## 4. 数据流与运行逻辑

1. **启动**：banner 打印本课主题，`tinyos> ` 就绪。
2. **输入**：`l77test` + 回车（或回归命令 `l69test`）。
3. **分派**：`exec64` 分别命中 `l77test` / `l69test` 分支。
4. **校验**：`lesson_70_state`（或 `lesson_69_state`）聚合初始化，5 条件布尔校验。
5. **输出**：`l77test: bounded scheduling and copy-on-write checkpoint passed`（`l69test` 同样通过），回显 `tinyos> `。

---

## 5. 构建、运行与验证

**依赖**：同全仓库，见 [`docs/local-validation.md`](../../docs/local-validation.md)。

**构建**（与 Makefile 一致）：

```bash
make clean && make -j"$(nproc)"
make check
```

**运行**：

```bash
make run
```

> 成功画面在 QEMU 图形窗口，请勿加 `-display none`。

**验证步骤与预期输出**（输出串从源码逐字抄录）：

1. 开机第一屏：
   ```
   Lesson 77: priority/nice 优先级状态
   GETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata
   tinyos>
   ```
2. 输入 `l77test`，预期输出：
   ```
   l77test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
   （失败场景打印 `l77test: Lesson 70 fallback reported`。）
3. 输入 `l69test`（改名后的回归命令），预期输出：
   ```
   l69test: bounded scheduling and copy-on-write checkpoint passed
   tinyos>
   ```
4. 输入 `about`，预期输出：
   ```
   Lesson 77: priority/nice 优先级状态
   tinyos>
   ```
5. 回归：`l76test` 已改名（输入 `l76test` 会得到 `unknown command`），改用 `l69test`；`schedinfo`、`stop68test`、`ps` 等继承命令仍可用。

**判断成功**：`make check` 打印 `Multiboot2 and Lesson 77 checks passed.`；QEMU 中 `l77test` 与 `l69test` 均打印 `...passed`。

> 备注：旧 README 中曾写 "Commands: `l70test`"，但源码与 Makefile 中的实际命令是 `l77test`（见 `kernel64.c` 的 `exec64` 分支）。本精讲文档以源码为准，并将回归命令标注为改名后的 `l69test`。

---

## 6. 调试地图

| 现象 | 原因 | 检查方法 |
|---|---|---|
| 开机屏停在 32 位 halt | `kernel_main32` 返回 0（页表/镜像校验失败） | VGA 是否显示 `user image validation/load failure:`；查 `kernel.c::validate_user_image()` |
| `make check` 第一条 grep 失败 | README 主题字串与 Makefile 不一致 | `grep 'priority/nice 优先级状态' README.md` 核对字面 |
| `make check` 第二条 grep 失败 | `kernel64.c` 丢失 `l77test` 符号 | `grep -q 'l77test' kernel64.c` |
| `make check` 第三条 grep 失败 | README 课号写错 | `grep -q 'Lesson 77' README.md` |
| 输入 `l76test` 打印 `unknown command` | 该命令在本课已改名为 `l69test` | 输入 `l69test`；确认 `exec64` 中 `eq64(word,"l69test")` 分支存在 |
| 输入 `l70test` 打印 `unknown command` | 沿用了旧 README 的错误命令名 | 实际命令是 `l77test` |
| `l77test` 打印 `usage: l77test` | 命令带多余参数 | 本命令必须无参（`noargs64`） |
| `l77test` 打印 fallback 串 | 5 条件中某项不成立 | 检查 `ok`：`valid`、`active`、`ready`、`accounted`、`b==a+1U`（`71==70+1`） |
| `l69test` 打印 fallback 串 | `lesson_69_state` 场景被误改 | 对照 Lesson 76 的 `l76test` 原文（现 `l69test`）逐字段核对 |

---

## 7. 与 Linux 源码对照

**对照点 1：nice → 静态优先级映射**
- TinyOS 教学模型：概念层讲解 `static_prio = 120 + nice`（`nice=0`→120），验证层用 `b==a+1` 强调"数值关系必须严格单调"。
- Linux 实现：`include/linux/sched/prio.h` 定义 `NICE_TO_PRIO(nice)`（`MAX_RT_PRIO + nice + 20`）、`PRIO_TO_NICE(prio)`；`kernel/sched/core.c::set_user_nice()` 调用 `set_load_weight()` 同步权重。
- 权威来源：Linux v6.12 `include/linux/sched/prio.h`、`kernel/sched/core.c`；POSIX.1 §3.283（nice）。
- 教学简化：TinyOS 没有 `prio` 字段与 `set_user_nice` 实现，只在概念层说明映射关系。

**对照点 2：优先级比较决定调度次序**
- TinyOS：`rr_pick_next` 仍是轮转挑选，不做优先级比较；`lesson_70_state` 的 `active/ready` 只是"就绪集合存在"的健康断言。
- Linux：CFS 按 `task->se.vruntime` 排序（`pick_next_task_fair` 选红黑树最左节点）；RT 调度按 `prio` 排优先级数组；`effective_prio` 计算动态优先级。
- 教学简化：TinyOS 刻意不实现优先级排序——它属于"行为"，而本阶段只承诺"元数据"。

**对照点 3：nice 的权限与范围**
- Linux：nice 值域 −20..19；`nice(2)` 对非特权进程只允许提高 nice；`setpriority(2)` 需要 `CAP_SYS_NICE` 才能设负值。
- TinyOS：概念层照搬该范围与权限规则，元数据层不模拟系统调用。
- 权威来源：POSIX.1 §3.283；Linux `kernel/sys.c::do_nice`。

**对照点 4：checkpoint 跨源课命名**
- 本课的 `l69test`（源课 69）与 `l77test`（源课 70）双命令并存，与 Manifest 中"每个 stable 课保留可学习版本、历史差异可审计"的治理规则一致（见根目录 [`COURSE-MANIFEST.md`](../../COURSE-MANIFEST.md) 与 [`docs/learning-stable-diff-report.md`](../../docs/learning-stable-diff-report.md)）。

---

## 8. 思考题与练习

1. **概念理解**：为什么 nice 值越大优先级越低（+19 最不优先）？结合 `static_prio = 120 + nice` 解释，并说出 nice=5 对应静态优先级是多少。
2. **源码定位**：在 `kernel64.c` 中找出 `l69test`（改名后的 `l76test`）与 `l77test`，对比两者的 `lesson_*_model` 初始化差异只有什么（代际起点与结构名），说明 checkpoint 保持同构的价值。
3. **动手实验**：把 `l77test` 初始化里的 `a` 从 `70U` 改成 `71U`，使 `b==a+1U` 失效，重新 `make run` 观察 fallback 串。请**改回 70U**。
4. **动手实验**：输入 `l76test` 与 `l70test` 验证它们都会得到 `unknown command`，再输入 `l69test` 确认改名后的回归命令有效——体会命令改名惯例。
5. **Linux 对照**：阅读 `include/linux/sched/prio.h` 的 `NICE_TO_PRIO`/`PRIO_TO_NICE` 与 `kernel/sched/core.c::set_user_nice`，说明 nice 值如何同步到 `load_weight`，并指出 TinyOS 教学模型在此链条上省略了什么。

---

## 9. 本课小结与下一课预告

- 本课在 priority/nice 主题下新增 `lesson_70_model` 记录与 `l77test`，完成"调度 + COW 元数据 checkpoint"的第二次重放。
- 你掌握了 priority 与 nice 的核心数值关系（nice 越大优先级越低、`static_prio=120+nice`、值域 −20..19），理解了 Linux `NICE_TO_PRIO`/`PRIO_TO_NICE` 与 `set_user_nice` 的职责。
- 你观察并理解了 checkpoint 命令改名惯例：`l76test` 在本课更名为 `l69test`，新命令 `l77test` 携带源课 70 编号，历史验证可重放、不丢失。
- 你对照了 Linux `include/linux/sched/prio.h` 与 `kernel/sched/core.c`，知道教学模型在验证层保持同构、在行为层（真实优先级排序）刻意留白。
- 你确认了 `make check` 三条静态断言与系列回归命令。

**下一课预告**：Lesson 78「runqueue 运行队列统计」。优先级决定了谁先跑，但"就绪任务在排队"本身需要数据结构——runqueue。下一课将引入运行队列的入队/出队/长度统计元数据，对应 Linux `struct rq`（per-CPU runqueue）与 `nr_running` 等字段。衔接点：本课 `ready=1`（就绪集合存在）正是 runqueue 非空的抽象前提，而 `active/accounted` 将扩展为 runqueue 的入队/出队计数。
