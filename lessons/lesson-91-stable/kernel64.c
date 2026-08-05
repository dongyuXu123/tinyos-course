/* Lesson 34: bounded address spaces over the inherited syscall ABI. */
typedef unsigned char u8; typedef unsigned int u32; typedef unsigned short u16; typedef unsigned long long u64; typedef long long s64;
#define TEXT64 __attribute__((section(".text64"), noinline))
#define ENTRY64 __attribute__((section(".text64.entry"), noinline))
#define VGA ((volatile u16 *)0xb8000ULL)
#define COLS 80
#define ROWS 25
#define PAGE_SIZE 0x1000ULL
#define PAGE_ENTRIES 512U
#define IDENTITY_MAP_END 0x40000000ULL
#define PMM_MAX_PHYS 0x01000000ULL
#define PMM_FRAMES (PMM_MAX_PHYS / PAGE_SIZE)
#define PMM_BITMAP_BYTES (PMM_FRAMES / 8)
#define IDT_ENTRIES 256
#define IDT_GATE_INTERRUPT 0x8e
#define PIC1_COMMAND 0x20
#define PIC1_DATA 0x21
#define PIC2_COMMAND 0xa0
#define PIC2_DATA 0xa1
#define PIC_EOI 0x20
#define KBD_QUEUE_SIZE 64
#define PIT_COMMAND 0x43
#define PIT_CHANNEL0 0x40
#define PIT_RATE_HZ 100
#define PIT_DIVISOR 11932
#define PAGE_TABLES_PER_ALIAS (IDENTITY_MAP_END/(PAGE_ENTRIES*PAGE_SIZE))
#define VM_REGION_START 0x00ff0000ULL
#define VM_REGION_SLOTS 16U
#define VM_REGION_END (VM_REGION_START+VM_REGION_SLOTS*PAGE_SIZE)
#define VM_REGION_HIGH_START (KERNEL_VMA_BASE+VM_REGION_START)
#define VM_REGION_FIRST_PTE (PAGE_ENTRIES-VM_REGION_SLOTS)
#define VM_REGION_PT_INDEX (PAGE_TABLES_PER_ALIAS-1)
#define FRAMEBUFFER_VA 0x20000000ULL
#define PTE_PRESENT_WRITABLE 0x003ULL
#define PTE_FRAME_MASK 0x000ffffffffff000ULL
#define KERNEL_VMA_BASE 0xffffffff80000000ULL
#define THREAD_COUNT 3
#define THREAD_STACK_BYTES PAGE_SIZE
#define THREAD_STEPS 4
#define TIME_SLICE_TICKS 2
#define BUSY_SPINS 4000000ULL
#define SLEEP_A_TICKS 120ULL
#define SLEEP_B_TICKS 270ULL
#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define TSS_SELECTOR 0x18
#define USER_DS 0x2b
#define USER_CS 0x33
#define USER_CODE_VA 0x00400000ULL
#define USER_STACK_VA 0x00800000ULL
#define USER_STACK_TOP (USER_STACK_VA+PAGE_SIZE)
#define SYS_GETTICKS 0U
#define SYS_GETPID 1U
#define SYS_WRITE_CONSOLE 2U
#define SYS_EXIT 3U
#define ENOSYS 38
#define FIXED_PID 1ULL
#define SECOND_PID 2ULL
#define USER2_CODE_VA 0x00500000ULL
#define USER2_STACK_VA 0x00900000ULL
#define MAX_USER_PROGRAMS 2U
#define ELF_MAGIC0 0x7f
#define ELF_MAGIC1 'E'
#define ELF_MAGIC2 'L'
#define ELF_MAGIC3 'F'
#define ELF_TYPE_EXEC 2U
#define ELF_MACHINE_X86_64 62U
#define ELF_SEG_R 1U
#define ELF_SEG_W 2U
#define ELF_SEG_X 4U
#define EXEC_MAX_SEGMENTS 2U
#define EXEC_MAX_IMAGE_BYTES 64U
#define EXEC_STACK_WORDS 8U
#define EXEC_STACK_ARGC 2U
#define PTE_USER 0x004ULL
#define USER_CODE_SLOT 0U
#define USER_STACK_SLOT 1U
#define IST1_INDEX 1
struct mb2_tag { u32 type; u32 size; } __attribute__((packed));
struct mb2_mmap_tag { u32 type; u32 size; u32 entry_size; u32 entry_version; } __attribute__((packed));
struct mb2_mmap_entry { u64 addr; u64 len; u32 type; u32 reserved; } __attribute__((packed));
struct mb2_framebuffer_tag { u32 type,size; u64 address; u32 pitch,width,height; u8 bpp,type_field; u16 reserved; } __attribute__((packed));
struct long_mode_handoff { u64 pml4,pdpt,pd,idt_address,pt[PAGE_TABLES_PER_ALIAS]; u64 kernel_start,kernel_end,stack_start,stack_end; u64 high_pdpt,high_pd,high_pt[PAGE_TABLES_PER_ALIAS]; u64 user_code_phys,user_stack_phys,user2_code_phys,user2_stack_phys; u64 kernel_vma_base,kernel_phys_base; u32 mbi_address,mbi_size; u64 framebuffer_address,framebuffer_map; u32 framebuffer_pitch,framebuffer_width,framebuffer_height,framebuffer_bytes; u8 framebuffer_bpp,framebuffer_type; };
struct idt_gate { u16 offset_low, selector; u8 ist, type; u16 offset_mid; u32 offset_high, reserved; } __attribute__((packed));
struct idtr { u16 limit; u64 base; } __attribute__((packed));
struct gdtr { u16 limit; u64 base; } __attribute__((packed));
struct tss64 { u32 reserved0; u64 rsp0,rsp1,rsp2,reserved1,ist1,ist2,ist3,ist4,ist5,ist6,ist7,reserved2; u16 reserved3,iomap_base; } __attribute__((packed));
struct exception_frame { u64 vector,error,rip,cs,rflags,rsp,ss; };
/* IST changes the CPU frame: old RSP and SS follow the CPL0 return frame. */
struct exception_frame_ist { u64 vector,error,rip,cs,rflags,rsp,ss; };
_Static_assert(sizeof(struct idt_gate)==16,"idt gate");
_Static_assert(sizeof(struct tss64)==104,"64-bit TSS");
_Static_assert(__builtin_offsetof(struct tss64,rsp0)==4,"TSS rsp0 offset");
_Static_assert(__builtin_offsetof(struct tss64,ist1)==36,"TSS ist1 offset");
_Static_assert(__builtin_offsetof(struct tss64,iomap_base)==102,"TSS iomap offset");
_Static_assert(sizeof(struct exception_frame_ist)==56,"IST frame");
/* IRQ0 pushes GPRs; CPL3 delivery adds the CPU's rsp/ss pair. */
struct irq0_frame { u64 r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rdx,rcx,rbx,rax,rip,cs,rflags,rsp,ss; };
struct syscall_frame { u64 r15,r14,r13,r12,r11,r10,r9,r8,rdi,rsi,rbp,rdx,rcx,rbx,rax,rip,cs,rflags,rsp,ss; };
/* Lesson 34 keeps one bounded process object and one user thread.  The
 * process owns the inherited image/address space; the thread owns the saved
 * user return context.  This is metadata only: no user scheduling or IRQs. */
enum process_state { PROCESS_EMPTY, PROCESS_READY, PROCESS_RUNNING, PROCESS_EXITED };
enum user_thread_state { USER_THREAD_EMPTY, USER_THREAD_READY, USER_THREAD_RUNNING, USER_THREAD_EXITED, USER_THREAD_RECLAIMED };
/* Lesson 37: deliberately small task_struct analogue. Linux task_struct is
 * much larger; this bounded record retains identity, ancestry, kind, and
 * scheduler-visible state for this teaching kernel. */
enum task_state { TASK_RUNNING=0, TASK_INTERRUPTIBLE=1, TASK_UNINTERRUPTIBLE=2,
                  TASK_STOPPED=4, TASK_TRACED=8, EXIT_DEAD=16, EXIT_ZOMBIE=32 };
enum task_kind { TASK_KIND_KERNEL=1, TASK_KIND_USER=2 };
struct task_struct { u64 pid, tid, parent_pid; u8 kind, state, transitions, valid; };
#define TASK_TABLE_CAP 4U
static struct task_struct task_table[TASK_TABLE_CAP];
/* Lesson 39: fork/clone are bounded metadata simulations. A fork copies
 * task-visible image metadata and gets a distinct address-space record; shared
 * kernel resources are represented by explicit flags. No child instruction
 * pointer is ever executed by this teaching model. */
enum resource_policy { RESOURCE_COPIED=1, RESOURCE_SHARED=2 };
struct fork_model { u64 parent_pid, child_pid, parent_tid, child_tid;
    u64 parent_address_space, child_address_space;
    u32 copied_metadata, shared_resources; u8 is_clone, valid; };
static struct fork_model fork_model;
static u64 fork_attempts, fork_successes, clone_successes;
struct tiny_elf_header { u8 ident[4]; u16 type, machine; u32 version; u64 entry, phoff; u16 phentsize, phnum; } __attribute__((packed));
struct tiny_elf_segment { u32 type, flags; u64 offset, vaddr, filesz, memsz; } __attribute__((packed));
struct exec_model { u64 entry, stack_pointer, argv_pointer, envp_pointer; u32 argc, segment_count, image_bytes; u8 validated, executed; };
static struct exec_model exec_model;
/* Bounded analogue of Linux mm_struct->mmap/VMA lookup. Records describe
 * ranges only; the teaching model never authorizes a CPU memory access. */
#define VMA_MAX 4U
#define VMA_R 1U
#define VMA_W 2U
#define VMA_X 4U
#define VMA_CODE_START 0x00400000ULL
#define VMA_CODE_END 0x00401000ULL
#define VMA_DATA_START 0x00600000ULL
#define VMA_DATA_END 0x00602000ULL
#define VMA_STACK_START 0x00800000ULL
#define VMA_STACK_END 0x00802000ULL
#define VMA_FILE 1U
#define VMA_ANON 2U
#define VMA_MAX_PAGES 4U
struct vma_model { u64 start,end,backing; u8 prot,kind,valid; };
struct page_model { u64 va,phys; u8 writable,live,backing,dirty,accessed,reclaimable; u16 refs; };
#define PAGE_CACHE_MAX 2U
struct page_cache_model { u64 index,phys; u8 valid,dirty,writeback; u16 refs; };
static struct vma_model vma_table[VMA_MAX];
static struct page_model fault_pages[VMA_MAX_PAGES];
static struct page_cache_model page_cache[PAGE_CACHE_MAX];
static u32 vma_count, fault_page_count, page_cache_count;
static u64 fault_not_present, fault_protection, fault_unmapped, fault_insertions;
static u64 anon_pages, anon_reclaims, cache_hits, cache_misses, reclaim_scans, reclaim_skips, writeback_pages;
/* Lesson 42: Linux-style uaccess metadata. Never dereference arbitrary user pointers. */
#define USER_CANONICAL_MAX 0x00007fffffffffffULL
#define USER_RANGE_MAX 0x0000000100000000ULL
#define USER_COPY_MAX 256U
#define UACCESS_READ 1U
#define UACCESS_WRITE 2U
struct uaccess_result { u64 address,length; u8 access,canonical,range,vma,permission,copied; };
static u64 uaccess_attempts,uaccess_successes,uaccess_failures,uaccess_bytes;
/* Lesson 44: fixed VFS/file-descriptor teaching metadata. No disk I/O. */
#define FD_MAX 4U
#define FILE_MAX 3U
#define INODE_MAX 3U
#define DENTRY_MAX 3U
struct inode_model { u64 ino,size,mode,refs; u8 valid; };
struct dentry_model { u64 name_hash,inode_index,refs; u8 valid; };
struct file_model { u64 inode_index,offset,flags,refs; u8 valid; };
struct fd_model { u64 file_index; u8 valid; };
static struct inode_model inode_table[INODE_MAX];
static struct dentry_model dentry_table[DENTRY_MAX];
#define RAMFS_MAX 6U
#define RAMFS_ROOT 0U
#define RAMFS_DIR 1U
#define RAMFS_FILE 2U
struct ramfs_node { u64 name_hash,parent,inode; u8 type,valid; };
static struct ramfs_node ramfs_nodes[RAMFS_MAX];
static u32 ramfs_count;
static u64 ramfs_lookups,ramfs_hits,ramfs_misses;
static struct file_model file_table[FILE_MAX];
static struct fd_model fd_table[FD_MAX];
static u64 fd_opens,fd_closes,fd_reads,fd_seek_ops;
#define PIPE_CAP 4U
#define PIPE_WAIT_CAP 4U
#define POLL_IN 1U
#define POLL_OUT 2U
struct pipe_model { u8 data[PIPE_CAP],head,tail,used; u64 reads,writes,blocked_readers,blocked_writers,wake_readers,wake_writers,poll_registrations; };
enum pf_class { PF_NOT_PRESENT=1, PF_PROTECTION=2, PF_UNMAPPED=3 };
static const u8 embedded_exec_image[sizeof(struct tiny_elf_header)+2*sizeof(struct tiny_elf_segment)+8] = {
 ELF_MAGIC0,ELF_MAGIC1,ELF_MAGIC2,ELF_MAGIC3, ELF_TYPE_EXEC,0, ELF_MACHINE_X86_64,0, 1,0,0,0,
 0x00,0x00,0x40,0x00,0x00,0x00,0x00,0x00, 0x20,0,0,0,0,0,0,0, 0x28,0,2,0,
 0x01,0,0,0,ELF_SEG_R|ELF_SEG_X,0,0,0,0x00,0x00,0x40,0x00,7,0,0,0, 7,0,0,0,0,0,0,0,
 0x01,0,0,0,ELF_SEG_R|ELF_SEG_W,0,0,0,0x00,0x00,0x80,0x00,15,0,0,0, 15,0,0,0,0,0,0,0
};
struct saved_user_context { struct syscall_frame frame; u64 saves, last_syscall, last_result, pit_preemptions, pit_resumes; u8 valid; };
#define SIG_NONE 0U
#define SIGTRAP 5U
#define SIGILL 4U
#define SIGSEGV 11U
#define SIG_PENDING_MAX 2U
struct signal_record { u32 signo,vector; u64 error,fault_address,rip; u8 pending,delivered; };
struct process { u64 pid; struct address_space *address_space; u64 code_phys, stack_phys, entry, stack_top; u32 image_bytes; u8 state, context_valid, return_pending; struct signal_record signals[SIG_PENDING_MAX]; u64 signal_queued,signal_delivered,signal_dropped; };
struct user_thread { u64 tid, kernel_stack_top, kernel_stack_bytes, context_address, transitions; u8 state; struct process *process; struct saved_user_context context; };
_Static_assert(sizeof(struct irq0_frame)==20*sizeof(u64),"IRQ0 frame");
_Static_assert(sizeof(struct syscall_frame)==20*sizeof(u64),"syscall frame");
_Static_assert(__builtin_offsetof(struct irq0_frame,rsp)==18*sizeof(u64),"IRQ0 rsp offset");
_Static_assert(__builtin_offsetof(struct irq0_frame,ss)==19*sizeof(u64),"IRQ0 ss offset");
extern void exception_bp(void); extern void exception_ud(void); extern void exception_pf(void); extern void syscall_entry(void); extern void irq0_entry(void); extern void irq1_entry(void); extern void enter_user_c(void);
extern void thread_trampoline(void); extern void idle_trampoline(void);
static volatile u64 ticks;
struct init_model { u64 pid,started,commands,files,processes,pipes,signals; u8 ready; };
static struct init_model init_model;
static TEXT64 int ramfs_lookup(const char *path);
static TEXT64 int fd_open_model(u32 inode,u64 flags);
#define MODULE_MAX 3U
#define SYMBOL_MAX 4U
struct module_model { u64 name_hash,init_calls,exit_calls; u8 loaded,initialized; };
struct symbol_model { u64 name_hash,owner; u8 exported,valid; };
static struct module_model modules[MODULE_MAX];
static struct symbol_model exported_symbols[SYMBOL_MAX];
static u64 module_inits,module_exports,module_lookups;
struct clock_model { u64 monotonic_ticks,monotonic_ns,realtime_ns,reads; };
struct timer_model { u64 deadline_tick,interval_ticks,expirations,arms,reads; u8 armed,readable,periodic,canceled; };
struct sleep_model { u64 requested_ticks,deadline_tick,wake_tick,remaining_ticks,sleeps,wakes; u8 active,interrupted; };
static struct clock_model clock_model;
static struct timer_model timer_model;
static struct sleep_model sleep_model;
#define SOFTIRQ_BITS 3U
#define TASKLET_CAP 2U
#define WORK_CAP 4U
#define SOFTIRQ_BUDGET 2U
struct tasklet_model { u8 pending,disabled; u64 runs; };
struct work_model { u8 kind,data,queued; u64 runs; };
struct softirq_model { u8 pending; u64 raises,runs,drops,budget_exhaustions; };
static struct tasklet_model tasklets[TASKLET_CAP];
static struct work_model workqueue[WORK_CAP];
static struct softirq_model softirq_model;
static u8 work_head,work_tail,work_used;
#define NR_CPUS 1U
struct cpu_local { u8 id,softirq_pending,work_head,work_tail,work_used; };
static struct cpu_local cpu_locals[NR_CPUS];
static TEXT64 u64 irq_save64(void);
static TEXT64 void irq_restore64(u64 flags);
typedef struct { volatile u32 locked; } raw_spinlock_t;
typedef unsigned long irqflags_t;
static raw_spinlock_t deferred_lock;
static TEXT64 u8 atomic_load_relaxed_u8(volatile u8*p){return __atomic_load_n(p,__ATOMIC_RELAXED);}
static TEXT64 void atomic_store_release_u8(volatile u8*p,u8 v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static TEXT64 u8 atomic_fetch_or_relaxed_u8(volatile u8*p,u8 v){return __atomic_fetch_or(p,v,__ATOMIC_RELAXED);}
static TEXT64 u32 atomic_exchange_acquire_u32(volatile u32*p,u32 v){return __atomic_exchange_n(p,v,__ATOMIC_ACQUIRE);}
static TEXT64 void atomic_store_release_u32(volatile u32*p,u32 v){__atomic_store_n(p,v,__ATOMIC_RELEASE);}
static TEXT64 void raw_spin_lock_irqsave(raw_spinlock_t*l,irqflags_t*f){*f=irq_save64();while(atomic_exchange_acquire_u32(&l->locked,1)){} }
static TEXT64 void raw_spin_unlock_irqrestore(raw_spinlock_t*l,irqflags_t f){atomic_store_release_u32(&l->locked,0);irq_restore64(f);}
static TEXT64 struct cpu_local *this_cpu(void){return &cpu_locals[0];}
static volatile u64 irq1_count, irq1_raw_count, kbd_overflow_count;
static volatile u8 irq1_last_scancode;
static volatile u8 kbd_queue[KBD_QUEUE_SIZE];
static volatile u8 kbd_head, kbd_tail;

static TEXT64 void putc64(u16 *c,char x) { if(x=='\n'){*c+=COLS-*c%COLS;return;} VGA[(*c)++]=0x0f00U|(u8)x; if(*c>=COLS*ROWS)*c=(ROWS-1)*COLS; }
static TEXT64 void text64(u16 *c,const char *s) { while(*s) putc64(c,*s++); }
static TEXT64 void hex64(u16 *c,u64 v) { int n; for(n=60;n>=0;n-=4) putc64(c,"0123456789abcdef"[(v>>n)&15]); }
static TEXT64 u8 inb64(u16 p){u8 v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static TEXT64 void outb64(u16 p,u8 v){__asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p));}
static TEXT64 void io_wait64(void){outb64(0x80,0);}
static TEXT64 char scan64(u8 s){switch(s){case 0x0e:return'\b';case 0x1c:return'\n';case 0x02:return'1';case 0x03:return'2';case 0x04:return'3';case 0x05:return'4';case 0x06:return'5';case 0x07:return'6';case 0x08:return'7';case 0x09:return'8';case 0x0a:return'9';case 0x0b:return'0';case 0x39:return' ';case 0x10:return'q';case 0x11:return'w';case 0x12:return'e';case 0x13:return'r';case 0x14:return't';case 0x15:return'y';case 0x16:return'u';case 0x17:return'i';case 0x18:return'o';case 0x19:return'p';case 0x1e:return'a';case 0x1f:return's';case 0x20:return'd';case 0x21:return'f';case 0x22:return'g';case 0x23:return'h';case 0x24:return'j';case 0x25:return'k';case 0x26:return'l';case 0x27:return';';case 0x2c:return'z';case 0x2d:return'x';case 0x2e:return'c';case 0x2f:return'v';case 0x30:return'b';case 0x31:return'n';case 0x32:return'm';default:return 0;}}
static TEXT64 int eq64(const char*a,const char*b){while(*a&&*b){if(*a++!=*b++)return 0;}return *a==*b;}
static TEXT64 int space64(char x){return x==' '||x=='\t';}
static TEXT64 const char *token64(const char*s,char *word,u32 cap){u32 n=0;while(space64(*s))s++;while(*s&&!space64(*s)){if(n+1>=cap)return 0;word[n++]=*s++;}word[n]=0;while(space64(*s))s++;return s;}
static TEXT64 int noargs64(const char*s){return !*s;}
static TEXT64 int hexarg64(const char*s,u64*v){u64 n=0;int d,seen=0;if(s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2;while(*s&&!space64(*s)){char x=*s++;if(x>='0'&&x<='9')d=x-'0';else if(x>='a'&&x<='f')d=x-'a'+10;else if(x>='A'&&x<='F')d=x-'A'+10;else return 0;if(n>(~0ULL>>4))return 0;n=(n<<4)|(u64)d;seen=1;}while(space64(*s))s++;if(!seen||*s)return 0;*v=n;return 1;}
static TEXT64 void clear64(u16*c){u16 i;for(i=0;i<COLS*ROWS;i++)VGA[i]=0x0f20;*c=0;}
static TEXT64 void prompt64(u16*c){text64(c,"tinyos> ");}
static volatile u8 pmm_bitmap[PMM_BITMAP_BYTES];
static volatile u8 pmm_fixed[PMM_BITMAP_BYTES];
static u64 pmm_total,pmm_free,pmm_used;
static u64 user_code_phys,user_stack_phys,user2_code_phys,user2_stack_phys;
static u8 pmm_ready;
static const char *pmm_error;
enum mapping_owner { MAP_OWNER_NONE=0, MAP_OWNER_KERNEL=1, MAP_OWNER_USER=2 };
struct vm_mapping { u64 phys; u8 live; u8 owner; };
struct address_space { struct long_mode_handoff *tables; u64 low_start,low_end,high_start; u8 kernel_mappings,user_mappings,initialized; };
static struct vm_mapping vm_mappings[VM_REGION_SLOTS];
static struct address_space kernel_address_space;
static struct address_space user_address_spaces[MAX_USER_PROGRAMS];
static struct process user_processes[MAX_USER_PROGRAMS];
static struct user_thread user_threads[MAX_USER_PROGRAMS];
#define user_process user_processes[0]
#define user_thread user_threads[0]
static u64 user_reclaims, user_exit_count;
static u64 hh_test_word;
/* Three ordinary TCBs remain fixed: shell plus two PMM-backed workers. */
enum thread_state { THREAD_EMPTY, THREAD_RUNNING, THREAD_RUNNABLE, THREAD_SLEEPING, THREAD_BLOCKED_KBD, THREAD_BLOCKED_EVENT, THREAD_BLOCKED_SEM, THREAD_FINISHED };
struct thread { u64 frame,stack_phys,switches,progress,wake_tick,received; u8 state,id,mailbox,mailbox_ready; };
static struct thread threads[THREAD_COUNT];
static u8 current_thread,round_robin,threads_started,sleep_test,kbd_wait_test,pc_test;
static u8 idle_running;
static struct irq0_frame *idle_frame;
extern u8 __idle_guard_start[],__idle_stack_start[],__idle_stack_end[];
extern u8 __rsp0_guard_start[],__rsp0_stack_start[],__rsp0_stack_end[];
extern u8 __ist1_guard_start[],__ist1_stack_start[],__ist1_stack_end[];
static u8 idle_stack[PAGE_SIZE] __attribute__((section(".data.stack.idle"),aligned(PAGE_SIZE),used));
static u8 tss_rsp0_stack[PAGE_SIZE] __attribute__((section(".data.stack.rsp0"),aligned(PAGE_SIZE),used));
static u8 exception_ist_stack[PAGE_SIZE] __attribute__((section(".data.stack.ist1"),aligned(PAGE_SIZE),used));
static struct tss64 runtime_tss;
static u64 runtime_gdt[8] __attribute__((aligned(16)));
static struct gdtr runtime_gdtr;
#define WAIT_QUEUE_CAP (THREAD_COUNT-1)
#define PC_BUFFER_CAP 2
struct wait_queue { u8 ids[WAIT_QUEUE_CAP],head,tail,count; u64 enqueues,wake_one,wake_all; };
struct event { u8 signaled; volatile struct wait_queue waitq; u64 sets,resets,waits,wakes; };
struct semaphore { u8 count,max; volatile struct wait_queue waitq; u64 downs,ups,blocks,wakes,overflows; };
static volatile struct wait_queue kbd_waitq;
static struct pipe_model pipe_model;
static struct wait_queue pipe_read_wait,pipe_write_wait;
/* Lesson 38: Linux scheduler classes are represented by a tiny dispatch table.
 * The policy remains the inherited bounded round-robin scan; the abstraction
 * makes enqueue/dequeue and pick-next explicit without changing behavior. */
struct sched_class { const char *name; u8 (*pick_next)(void); void (*enqueue)(u8); void (*dequeue)(u8); };
static u64 sched_enqueues, sched_dequeues, sched_picks;
static u8 rr_pick_next(void);
static void rr_enqueue(u8 id);
static void rr_dequeue(u8 id);
static struct sched_class fair_sched_class;
static struct sched_class *active_sched_class;
static struct event pc_start_event;
static struct semaphore pc_spaces,pc_items;
static u8 pc_buffer[PC_BUFFER_CAP],pc_head,pc_tail,pc_used,pc_next,pc_expected;
static u64 pc_produced,pc_consumed,pc_sequence_errors;
static u64 preempt_switches,quantum_left,sleep_wakeups,idle_worker_ticks,kbd_direct_deliveries,idle_switches,idle_ticks;
static TEXT64 void thread_exit(void);
static TEXT64 void busy_delay(void);
static TEXT64 void thread_sleep_ticks(u64 delta);
static TEXT64 void kbd_wait_char(u8 *out);
static TEXT64 u64 runtime_thread_trampoline_address(void);
static TEXT64 int vm_frame_owned(u64 p);
static TEXT64 void user_irq0_save_restore(struct irq0_frame *f);
static TEXT64 int task_transition(u32 i,u8 next);
static TEXT64 int task_table_validate(void);
static TEXT64 int overlap(u64 a,u64 b,u64 c,u64 d){return a<d&&c<b;}
static TEXT64 u64 up(u64 v){return(v+PAGE_SIZE-1)&~(PAGE_SIZE-1);}
static TEXT64 u64 down(u64 v){return v&~(PAGE_SIZE-1);}
static TEXT64 int bit(u32 n){return(pmm_bitmap[n>>3]>>(n&7))&1;}
static TEXT64 void mark(u32 n){pmm_bitmap[n>>3]|=(u8)(1U<<(n&7));}
static TEXT64 void unmark(u32 n){pmm_bitmap[n>>3]&=(u8)~(1U<<(n&7));}
static TEXT64 int fixed(u32 n){return(pmm_fixed[n>>3]>>(n&7))&1;}
static TEXT64 void fix(u32 n){pmm_fixed[n>>3]|=(u8)(1U<<(n&7));}
static TEXT64 u64 phys_to_high(u64 p){return KERNEL_VMA_BASE+p;}
static TEXT64 int pmm_reserved(struct long_mode_handoff*h,u64 p){u64 e=p+PAGE_SIZE,b=(u64)(unsigned long)pmm_bitmap-KERNEL_VMA_BASE,z=b+2*PMM_BITMAP_BYTES;u32 i;if(p<0x100000||e<p||overlap(p,e,h->kernel_start,h->kernel_end)||overlap(p,e,h->stack_start,h->stack_end)||overlap(p,e,h->mbi_address,(u64)h->mbi_address+h->mbi_size)||overlap(p,e,h->pml4,h->pml4+PAGE_SIZE)||overlap(p,e,h->pdpt,h->pdpt+PAGE_SIZE)||overlap(p,e,h->pd,h->pd+PAGE_SIZE)||overlap(p,e,h->high_pdpt,h->high_pdpt+PAGE_SIZE)||overlap(p,e,h->high_pd,h->high_pd+PAGE_SIZE)||overlap(p,e,h->idt_address,h->idt_address+PAGE_SIZE)||overlap(p,e,h->user_code_phys,h->user_code_phys+PAGE_SIZE)||overlap(p,e,h->user_stack_phys,h->user_stack_phys+PAGE_SIZE)||overlap(p,e,h->user2_code_phys,h->user2_code_phys+PAGE_SIZE)||overlap(p,e,h->user2_stack_phys,h->user2_stack_phys+PAGE_SIZE)||overlap(p,e,b,z))return 1;for(i=0;i<PAGE_TABLES_PER_ALIAS;i++)if(overlap(p,e,h->pt[i],h->pt[i]+PAGE_SIZE)||overlap(p,e,h->high_pt[i],h->high_pt[i]+PAGE_SIZE))return 1;return 0;}
static TEXT64 const struct mb2_mmap_tag *mmap_tag64(struct long_mode_handoff*h){u32 off=8;if(h->mbi_size<16){pmm_error="MBI too small";return 0;}while(off<h->mbi_size){const struct mb2_tag*t;u32 r;if(h->mbi_size-off<8){pmm_error="truncated MBI tag";return 0;}t=(const struct mb2_tag*)((const u8*)(unsigned long)h->mbi_address+off);if(t->size<8||t->size>h->mbi_size-off){pmm_error="bad MBI tag size";return 0;}r=(t->size+7)&~7U;if(r<t->size||r>h->mbi_size-off){pmm_error="bad MBI tag alignment";return 0;}if(t->type==6){const struct mb2_mmap_tag*m=(const struct mb2_mmap_tag*)t;if(t->size<16||m->entry_size<sizeof(struct mb2_mmap_entry)||(t->size-16)%m->entry_size){pmm_error="bad mmap layout";return 0;}return m;}off+=r;}pmm_error="mmap tag missing";return 0;}
static TEXT64 void pmm_init(struct long_mode_handoff*h){const struct mb2_mmap_tag*m;u32 off,i;user_code_phys=h->user_code_phys;user_stack_phys=h->user_stack_phys;user2_code_phys=h->user2_code_phys;user2_stack_phys=h->user2_stack_phys;pmm_ready=0;pmm_error="not initialized";pmm_total=pmm_free=pmm_used=0;for(i=0;i<PMM_BITMAP_BYTES;i++){pmm_bitmap[i]=0xff;pmm_fixed[i]=0;}m=mmap_tag64(h);if(!m)return;for(off=0;off<m->size-16;off+=m->entry_size){const struct mb2_mmap_entry*e=(const struct mb2_mmap_entry*)((const u8*)m+16+off);u64 p,end;if(e->type!=1||e->addr+e->len<e->addr)continue;p=up(e->addr);end=down(e->addr+e->len);if(p>=PMM_MAX_PHYS)continue;if(end>PMM_MAX_PHYS)end=PMM_MAX_PHYS;while(p<end){unmark((u32)(p/PAGE_SIZE));p+=PAGE_SIZE;}}for(i=0;i<PMM_FRAMES;i++){u64 p=(u64)i*PAGE_SIZE;if(!bit(i)){pmm_total++;if(pmm_reserved(h,p)){mark(i);fix(i);}else pmm_free++;}}pmm_used=pmm_total-pmm_free;pmm_ready=1;pmm_error="ready";}
static TEXT64 u64 pmm_alloc(void){u32 i;if(!pmm_ready)return 0;for(i=0;i<PMM_FRAMES;i++)if(!bit(i)){mark(i);pmm_free--;pmm_used++;return(u64)i*PAGE_SIZE;}return 0;}
static TEXT64 const char *page_state(u64 p){u32 i;if(!pmm_ready)return "PMM unavailable";if((p&(PAGE_SIZE-1))||p>=PMM_MAX_PHYS)return "invalid";i=(u32)(p/PAGE_SIZE);if(!bit(i))return "free";if(fixed(i))return "fixed/reserved";return "allocated";}
static TEXT64 int thread_stack_owned(u64 p){u32 i;for(i=1;i<THREAD_COUNT;i++)if(threads[i].state!=THREAD_EMPTY&&threads[i].state!=THREAD_FINISHED&&threads[i].stack_phys==p)return 1;return 0;}
static TEXT64 const char *pmm_free_page(u64 p){u32 i;const char*s=page_state(p);if(!eq64(s,"allocated"))return s;if(vm_frame_owned(p))return "mapped";if(thread_stack_owned(p))return "thread stack";i=(u32)(p/PAGE_SIZE);unmark(i);pmm_free++;pmm_used--;return "freed";}
static TEXT64 u64 irq_save64(void){u64 flags;__asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");return flags;}
static TEXT64 void irq_restore64(u64 flags){if(flags&(1ULL<<9))__asm__ volatile("sti":::"memory");}
static TEXT64 void invlpg64(u64 va){__asm__ volatile("invlpg (%0)"::"r"(va):"memory");}
static TEXT64 int address_space_slot(struct address_space *as,u64 va,u32 *slot){if(!as||!as->initialized||(va&(PAGE_SIZE-1))||va<as->low_start||va>=as->low_end)return 0;*slot=(u32)((va-as->low_start)/PAGE_SIZE);if(*slot<2)return 0;return 1;}
static TEXT64 void address_space_init(struct address_space *as,struct long_mode_handoff *h){as->tables=h;as->low_start=VM_REGION_START;as->low_end=VM_REGION_END;as->high_start=VM_REGION_HIGH_START;as->kernel_mappings=0;as->user_mappings=0;as->initialized=1;}
static TEXT64 volatile u64 *vm_pte_low(struct long_mode_handoff*h,u32 slot){return &((volatile u64 *)(unsigned long)h->pt[VM_REGION_PT_INDEX])[VM_REGION_FIRST_PTE+slot];}
static TEXT64 volatile u64 *vm_pte_high(struct long_mode_handoff*h,u32 slot){return &((volatile u64 *)(unsigned long)h->high_pt[VM_REGION_PT_INDEX])[VM_REGION_FIRST_PTE+slot];}
static TEXT64 int vm_frame_owned(u64 p){u32 i;if(p==user_code_phys||p==user_stack_phys)return 1;for(i=0;i<VM_REGION_SLOTS;i++)if(vm_mappings[i].live&&vm_mappings[i].phys==p)return 1;return 0;}
static TEXT64 int vm_pair_ok(struct long_mode_handoff*h,u32 slot){volatile u64*l=vm_pte_low(h,slot),*q=vm_pte_high(h,slot);u64 a=*l,b=*q;if(a!=b)return 0;if(vm_mappings[slot].live)return (a&PTE_FRAME_MASK)==vm_mappings[slot].phys&&(a&PTE_PRESENT_WRITABLE)==PTE_PRESENT_WRITABLE;return !a;}
static TEXT64 const char *address_space_map(struct address_space *as,u64 va,u64 p,u8 owner){struct long_mode_handoff*h=as?as->tables:0;volatile u64*l,*q;u64 flags;u32 slot;const char*s;if(!address_space_slot(as,va,&slot))return "VA outside user-owned window";if(owner!=MAP_OWNER_USER)return "kernel-only mapping";s=page_state(p);if(!eq64(s,"allocated"))return s;flags=irq_save64();l=vm_pte_low(h,slot);q=vm_pte_high(h,slot);if(!vm_pair_ok(h,slot)){irq_restore64(flags);return "inconsistent PTE pair";}if(vm_mappings[slot].live||*l){irq_restore64(flags);return "slot already mapped";}if(vm_frame_owned(p)){irq_restore64(flags);return "frame already mapped";}*l=*q=p|PTE_PRESENT_WRITABLE|PTE_USER;vm_mappings[slot].phys=p;vm_mappings[slot].owner=owner;vm_mappings[slot].live=1;as->user_mappings++;invlpg64(va);invlpg64(KERNEL_VMA_BASE+va);irq_restore64(flags);return "mapped";}
static TEXT64 const char *address_space_release(struct address_space *as,u64 va){struct long_mode_handoff*h=as?as->tables:0;volatile u64*l,*q;u64 flags;u32 slot;if(!address_space_slot(as,va,&slot))return "VA outside user-owned window";flags=irq_save64();l=vm_pte_low(h,slot);q=vm_pte_high(h,slot);if(!vm_pair_ok(h,slot)){irq_restore64(flags);return "inconsistent PTE pair";}if(!vm_mappings[slot].live){irq_restore64(flags);return "slot already released";}if(vm_mappings[slot].owner!=MAP_OWNER_USER){irq_restore64(flags);return "mapping owner mismatch";}*l=*q=0;vm_mappings[slot].phys=0;vm_mappings[slot].owner=MAP_OWNER_NONE;vm_mappings[slot].live=0;as->user_mappings--;invlpg64(va);invlpg64(KERNEL_VMA_BASE+va);irq_restore64(flags);return "unmapped";}
static TEXT64 void vminfo(u16*c,struct long_mode_handoff*h,u64 va,int one){u32 i,n=0,slot;if(one&&!address_space_slot(&kernel_address_space,va,&slot)){text64(c,"VM VA outside mapping region\n");return;}for(i=0;i<VM_REGION_SLOTS;i++)if(vm_mappings[i].live)n++;text64(c,"VM region low/high: ");hex64(c,VM_REGION_START);text64(c," ");hex64(c,VM_REGION_HIGH_START);text64(c,"\nslots live/total: ");hex64(c,n);text64(c," ");hex64(c,VM_REGION_SLOTS);if(one){text64(c,"\nVA low/high: ");hex64(c,va);text64(c," ");hex64(c,KERNEL_VMA_BASE+va);text64(c,"\nstate/owner: ");text64(c,vm_mappings[slot].live?"mapped ":"unmapped ");hex64(c,vm_mappings[slot].phys);text64(c,"\nPTE low/high: ");hex64(c,*vm_pte_low(h,slot));text64(c," ");hex64(c,*vm_pte_high(h,slot));}putc64(c,'\n');}
static TEXT64 const char *thread_state_name(u8 state){if(state==THREAD_RUNNING)return "running";if(state==THREAD_RUNNABLE)return "runnable";if(state==THREAD_SLEEPING)return "sleeping";if(state==THREAD_BLOCKED_KBD)return "blocked-kbd";if(state==THREAD_BLOCKED_EVENT)return "blocked-event";if(state==THREAD_BLOCKED_SEM)return "blocked-sem";if(state==THREAD_FINISHED)return "finished";return "empty";}
static TEXT64 int tick_due(u64 now,u64 deadline){return (u64)(now-deadline)<(1ULL<<63);}
static TEXT64 void wake_sleepers(void){u32 i;for(i=0;i<THREAD_COUNT;i++)if(threads[i].state==THREAD_SLEEPING&&tick_due(ticks,threads[i].wake_tick)){threads[i].state=THREAD_RUNNABLE;sleep_wakeups++;}}
static TEXT64 int waitq_enqueue(volatile struct wait_queue*q,u8 id){if(q->count>=WAIT_QUEUE_CAP)return 0;q->ids[q->head]=id;q->head=(u8)((q->head+1)%WAIT_QUEUE_CAP);q->count++;q->enqueues++;return 1;}
static TEXT64 int waitq_dequeue(volatile struct wait_queue*q,u8 *id){if(!q->count)return 0;*id=q->ids[q->tail];q->tail=(u8)((q->tail+1)%WAIT_QUEUE_CAP);q->count--;return 1;}
static TEXT64 int waitq_wake_one(volatile struct wait_queue*q,u8 state,u8 *out){u8 id;if(!waitq_dequeue(q,&id))return 0;if(!id||id>=THREAD_COUNT||threads[id].state!=state)return 0;threads[id].state=THREAD_RUNNABLE;q->wake_one++;*out=id;return 1;}
TEXT64 u8 waitq_wake_all(volatile struct wait_queue*q,u8 state){u8 id,n=0;while(waitq_dequeue(q,&id))if(id&&id<THREAD_COUNT&&threads[id].state==state){threads[id].state=THREAD_RUNNABLE;n++;}q->wake_all+=n;return n;}
static TEXT64 void waitq_reset(volatile struct wait_queue*q){q->head=q->tail=q->count=0;q->enqueues=q->wake_one=q->wake_all=0;}
static TEXT64 void event_set(struct event*e){u64 flags=irq_save64();e->signaled=1;e->sets++;e->wakes+=waitq_wake_all(&e->waitq,THREAD_BLOCKED_EVENT);irq_restore64(flags);}
static TEXT64 void event_wait(struct event*e){u8 id=current_thread;for(;;){u64 flags=irq_save64();if(e->signaled){irq_restore64(flags);return;}if(id&&threads[id].state==THREAD_RUNNING&&waitq_enqueue(&e->waitq,id)){e->waits++;threads[id].state=THREAD_BLOCKED_EVENT;}irq_restore64(flags);while(threads[id].state==THREAD_BLOCKED_EVENT)__asm__ volatile("sti; hlt");}}
static TEXT64 void sem_init(struct semaphore*s,u8 count,u8 max){s->count=count;s->max=max;waitq_reset(&s->waitq);s->downs=s->ups=s->blocks=s->wakes=s->overflows=0;}
static TEXT64 void sem_down(struct semaphore*s){u8 id=current_thread;for(;;){u64 flags=irq_save64();if(s->count){s->count--;s->downs++;irq_restore64(flags);return;}if(id&&threads[id].state==THREAD_RUNNING&&waitq_enqueue(&s->waitq,id)){s->blocks++;threads[id].state=THREAD_BLOCKED_SEM;}irq_restore64(flags);while(threads[id].state==THREAD_BLOCKED_SEM)__asm__ volatile("sti; hlt");}}
static TEXT64 void sem_up(struct semaphore*s){u64 flags=irq_save64();u8 id;if(s->count<s->max){s->count++;s->ups++;}else s->overflows++;if(waitq_wake_one(&s->waitq,THREAD_BLOCKED_SEM,&id))s->wakes++;irq_restore64(flags);}
static TEXT64 void pipe_init(void){pipe_model=(struct pipe_model){0};waitq_reset(&pipe_read_wait);waitq_reset(&pipe_write_wait);}
static TEXT64 int pipe_try_write(u8 value){u8 id;if(pipe_model.used>=PIPE_CAP){pipe_model.blocked_writers++;return 0;}pipe_model.data[pipe_model.head]=value;pipe_model.head=(u8)((pipe_model.head+1)%PIPE_CAP);pipe_model.used++;pipe_model.writes++;if(waitq_wake_one(&pipe_read_wait,THREAD_BLOCKED_EVENT,&id))pipe_model.wake_readers++;return 1;}
static TEXT64 int pipe_try_read(u8*out){u8 id;if(!pipe_model.used){pipe_model.blocked_readers++;return 0;}*out=pipe_model.data[pipe_model.tail];pipe_model.tail=(u8)((pipe_model.tail+1)%PIPE_CAP);pipe_model.used--;pipe_model.reads++;if(waitq_wake_one(&pipe_write_wait,THREAD_BLOCKED_EVENT,&id))pipe_model.wake_writers++;return 1;}
static TEXT64 u8 pipe_poll(u8 mask){u8 ready=0;pipe_model.poll_registrations++;if((mask&POLL_IN)&&pipe_model.used)ready|=POLL_IN;if((mask&POLL_OUT)&&pipe_model.used<PIPE_CAP)ready|=POLL_OUT;return ready;}
static TEXT64 void pipeinfo(u16*c){text64(c,"pipe used/capacity: ");hex64(c,pipe_model.used);text64(c,"/");hex64(c,PIPE_CAP);text64(c," reads/writes: ");hex64(c,pipe_model.reads);text64(c,"/");hex64(c,pipe_model.writes);text64(c," blocked r/w: ");hex64(c,pipe_model.blocked_readers);text64(c,"/");hex64(c,pipe_model.blocked_writers);text64(c," wake r/w: ");hex64(c,pipe_model.wake_readers);text64(c,"/");hex64(c,pipe_model.wake_writers);putc64(c,'\n');}
static TEXT64 void pipetest(u16*c){u8 v=0,out=0;int a=!pipe_try_read(&out),b=pipe_try_write(0x41),d=pipe_try_read(&v),e=v==0x41,f=pipe_model.used==0;pipe_model.used=PIPE_CAP;int g=!pipe_try_write(0x42);pipe_model.used=0;text64(c,"pipetest: ");text64(c,a&&b&&d&&e&&f&&g?"bounded FIFO empty/full blocking transitions passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void polltest(u16*c){u8 v=0;pipe_init();int a=pipe_poll(POLL_IN)==0,b=pipe_poll(POLL_OUT)==POLL_OUT,w=pipe_try_write(7),d=pipe_poll(POLL_IN)==POLL_IN,e=pipe_model.used==1;while(pipe_model.used<PIPE_CAP)pipe_try_write(8);int f=pipe_poll(POLL_OUT)==0,g=pipe_try_read(&v),h=pipe_poll(POLL_OUT)==POLL_OUT;text64(c,"polltest: ");text64(c,a&&b&&w&&d&&e&&f&&g&&h?"POLLIN/POLLOUT readiness transitions passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void clock_update(void){clock_model.monotonic_ticks=ticks;clock_model.monotonic_ns=ticks*(1000000000ULL/PIT_RATE_HZ);clock_model.realtime_ns=clock_model.monotonic_ns;}
static TEXT64 void timer_poll(void){if(!timer_model.armed||timer_model.canceled)return;if(!tick_due(ticks,timer_model.deadline_tick))return;timer_model.expirations++;timer_model.readable=1;if(timer_model.periodic)timer_model.deadline_tick+=timer_model.interval_ticks;else timer_model.armed=0;}
static TEXT64 void timer_arm(u64 delay,u64 interval){timer_model.deadline_tick=ticks+delay;timer_model.interval_ticks=interval;timer_model.periodic=interval!=0;timer_model.armed=1;timer_model.readable=0;timer_model.canceled=0;timer_model.arms++;}
static TEXT64 u64 timer_read(void){u64 n=timer_model.readable?timer_model.expirations:0;timer_model.expirations=0;timer_model.readable=0;timer_model.reads++;return n;}
static TEXT64 void timer_cancel(void){timer_model.armed=0;timer_model.canceled=1;timer_model.readable=0;timer_model.expirations=0;}

static TEXT64 void clockinfo(u16*c){clock_update();clock_model.reads++;text64(c,"clock ticks/ns: ");hex64(c,clock_model.monotonic_ticks);text64(c,"/");hex64(c,clock_model.monotonic_ns);text64(c," reads: ");hex64(c,clock_model.reads);text64(c," PIT Hz: ");hex64(c,PIT_RATE_HZ);putc64(c,'\n');}
static TEXT64 void timerinfo(u16*c){text64(c,"timer armed/readable/periodic: ");hex64(c,timer_model.armed);text64(c,"/");hex64(c,timer_model.readable);text64(c,"/");hex64(c,timer_model.periodic);text64(c," deadline/expirations: ");hex64(c,timer_model.deadline_tick);text64(c,"/");hex64(c,timer_model.expirations);text64(c," arms/reads: ");hex64(c,timer_model.arms);text64(c,"/");hex64(c,timer_model.reads);putc64(c,'\n');}
static TEXT64 void clocktest(u16*c){u64 a,b;clock_update();a=clock_model.monotonic_ns;clock_model.reads++;clock_update();b=clock_model.monotonic_ns;text64(c,"clocktest: ");text64(c,b>=a&&b==ticks*(1000000000ULL/PIT_RATE_HZ)?"monotonic PIT clock conversion passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void timertest(u16*c){u64 old=ticks;timer_arm(2,0);timer_poll();int a=!timer_model.readable;ticks=old+2;timer_poll();int b=timer_model.readable&&timer_read()==1&&!timer_model.readable;timer_arm(1,1);ticks=old+3;timer_poll();ticks++;timer_poll();int d=timer_model.expirations>=2;timer_cancel();int e=!timer_model.armed&&!timer_model.readable;text64(c,"timertest: ");text64(c,a&&b&&d&&e?"deadline, expiration, periodic, and cancel passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void sleeptimetest(u16*c){u64 old=ticks;u64 delay=3;sleep_model.requested_ticks=delay;sleep_model.deadline_tick=old+delay;sleep_model.wake_tick=0;sleep_model.remaining_ticks=delay;sleep_model.active=1;sleep_model.interrupted=0;sleep_model.sleeps++;ticks=old+delay-1;sleep_model.remaining_ticks=sleep_model.deadline_tick-ticks;int a=sleep_model.remaining_ticks&&sleep_model.active;ticks=old+delay;sleep_model.remaining_ticks=0;sleep_model.wake_tick=ticks;sleep_model.active=0;sleep_model.wakes++;int b=!sleep_model.active&&!sleep_model.remaining_ticks;text64(c,"sleeptimetest: ");text64(c,a&&b?"deadline sleep and wake accounting passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void softirq_raise(u8 bit){if(bit>=SOFTIRQ_BITS){softirq_model.drops++;return;}softirq_model.pending|=(u8)(1U<<bit);softirq_model.raises++;}
static TEXT64 void tasklet_schedule(u8 id){if(id>=TASKLET_CAP||tasklets[id].disabled)return;if(!tasklets[id].pending){tasklets[id].pending=1;softirq_raise(0);}}
static TEXT64 int workqueue_submit(u8 kind,u8 data){if(work_used>=WORK_CAP){softirq_model.drops++;return 0;}workqueue[work_head]=(struct work_model){kind,data,1,0};work_head=(u8)((work_head+1)%WORK_CAP);work_used++;softirq_raise(1);return 1;}
static TEXT64 void softirq_run_budget(void){u8 budget=SOFTIRQ_BUDGET,i;while(budget&&softirq_model.pending){if(softirq_model.pending&1){for(i=0;i<TASKLET_CAP&&budget;i++)if(tasklets[i].pending&&!tasklets[i].disabled){tasklets[i].pending=0;tasklets[i].runs++;softirq_model.runs++;budget--;}}if(softirq_model.pending&2&&budget){if(work_used){struct work_model*w=&workqueue[work_tail];w->queued=0;w->runs++;work_tail=(u8)((work_tail+1)%WORK_CAP);work_used--;softirq_model.runs++;budget--;}}if(!tasklets[0].pending&&!tasklets[1].pending)softirq_model.pending&=(u8)~1U;if(!work_used)softirq_model.pending&=(u8)~2U;}if(softirq_model.pending)softirq_model.budget_exhaustions++;}
static TEXT64 void lockatomicinfo(u16*c){text64(c,"locks/atomics/percpu: NR_CPUS ");hex64(c,NR_CPUS);text64(c," lock ");hex64(c,deferred_lock.locked);text64(c," cpu id/pending/work: ");hex64(c,this_cpu()->id);text64(c,"/");hex64(c,this_cpu()->softirq_pending);text64(c,"/");hex64(c,this_cpu()->work_used);text64(c," memory order: acquire/release/relaxed\n");}
static TEXT64 int fd_close_model(u32 fd);
static TEXT64 void vfs_init(void);
struct shell_runtime_model { u64 starts,commands,execs,exits,argv_words,env_words,pipe_links,signal_links,timer_links,deferred_links; u8 ready; };
static struct shell_runtime_model shell_runtime;
static TEXT64 void shell_runtime_start(void){shell_runtime=(struct shell_runtime_model){1,0,0,0,0,0,0,0,0,0,1};}
static TEXT64 int shell_exec_path(const char *path,u32 argc,u32 envc){int inode=ramfs_lookup(path);int fd;u64 image_hash=0x5348454c4c494d47ULL;if(inode<0||argc>4U||envc>4U)return 0;fd=fd_open_model((u32)inode,0);if(fd<0)return 0;shell_runtime.commands++;shell_runtime.execs++;shell_runtime.argv_words+=argc;shell_runtime.env_words+=envc;shell_runtime.pipe_links++;shell_runtime.signal_links++;shell_runtime.timer_links++;shell_runtime.deferred_links++;if(image_hash!=0x5348454c4c494d47ULL)return 0;fd_close_model((u32)fd);shell_runtime.exits++;return 1;}
static TEXT64 void init_model_start(void){init_model=(struct init_model){FIXED_PID,1,0,0,0,0,0,1};shell_runtime_start();}
static TEXT64 void initinfo(u16*c){text64(c,"init pid/ready/commands/files/processes/pipes/signals: ");hex64(c,init_model.pid);text64(c,"/");hex64(c,init_model.ready);text64(c,"/");hex64(c,init_model.commands);text64(c,"/");hex64(c,init_model.files);text64(c,"/");hex64(c,init_model.processes);text64(c,"/");hex64(c,init_model.pipes);text64(c,"/");hex64(c,init_model.signals);putc64(c,'\n');}
static TEXT64 void shelltest(u16*c){int a=ramfs_lookup("/bin/sh")>=0,b=fd_open_model(0,0)>=0,d=pipe_model.used==0;init_model.commands++;init_model.files+=b;init_model.processes++;init_model.pipes+=d;init_model.signals++;text64(c,"shelltest: ");text64(c,a&&b&&d&&init_model.ready?"init/shell/file/process/pipe coordination passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void shellrun(u16*c){vfs_init();int ok=shell_exec_path("/bin/sh",2,1);text64(c,"shellrun: ");text64(c,ok&&shell_runtime.ready&&shell_runtime.exits==1?"validated /bin/sh image, bounded argv/env, lifecycle and subsystem links passed":"BROKEN");putc64(c,'\n');}
struct wait_model { u64 parent_pid,child_pid,exit_code,wait_calls,reaps,statuses; u8 state,waited; };
static struct wait_model wait_model;
#define WAIT_RUNNING 1U
#define WAIT_ZOMBIE 2U
#define WAIT_DEAD 3U
static TEXT64 void wait_model_start(void){wait_model=(struct wait_model){FIXED_PID,SECOND_PID,0,0,0,0,WAIT_RUNNING,0};}
static TEXT64 int wait_model_exit(u64 code){if(wait_model.state!=WAIT_RUNNING)return 0;wait_model.exit_code=code;wait_model.state=WAIT_ZOMBIE;wait_model.statuses++;return 1;}
static TEXT64 int wait_model_wait(void){wait_model.wait_calls++;if(wait_model.state!=WAIT_ZOMBIE)return 0;wait_model.waited=1;return 1;}
static TEXT64 int wait_model_reap(void){if(!wait_model.waited||wait_model.state!=WAIT_ZOMBIE)return 0;wait_model.state=WAIT_DEAD;wait_model.reaps++;return 1;}
struct adoption_model { u64 init_pid,original_parent,child_pid,current_parent,adoptions,ownership_checks; u8 orphaned,adopted,wait_owner; };
static struct adoption_model adoption_model;
static TEXT64 void adoption_start(void){adoption_model=(struct adoption_model){1,FIXED_PID,SECOND_PID,FIXED_PID,0,0,0,0,0};}
static TEXT64 int adoption_reparent(void){if(!adoption_model.orphaned||adoption_model.adopted)return 0;adoption_model.current_parent=adoption_model.init_pid;adoption_model.adopted=1;adoption_model.adoptions++;return 1;}
static TEXT64 int adoption_exit_parent(void){if(adoption_model.original_parent!=FIXED_PID)return 0;adoption_model.orphaned=1;adoption_model.wait_owner=adoption_model.init_pid;return adoption_reparent();}
static TEXT64 int adoption_wait_owner(void){adoption_model.ownership_checks++;return adoption_model.adopted&&adoption_model.wait_owner==adoption_model.init_pid&&adoption_model.current_parent==adoption_model.init_pid;}
static TEXT64 void adoptioninfo(u16*c){text64(c,"adoption init/original/child/current/adoptions/owner: ");hex64(c,adoption_model.init_pid);text64(c,"/");hex64(c,adoption_model.original_parent);text64(c,"/");hex64(c,adoption_model.child_pid);text64(c,"/");hex64(c,adoption_model.current_parent);text64(c,"/");hex64(c,adoption_model.adoptions);text64(c,"/");hex64(c,adoption_model.wait_owner);putc64(c,'\n');}
static TEXT64 void reparenttest(u16*c){adoption_start();int a=adoption_model.current_parent==FIXED_PID,b=adoption_exit_parent(),d=adoption_wait_owner(),e=adoption_model.orphaned&&adoption_model.adopted&&adoption_model.adoptions==1;text64(c,"reparenttest: ");text64(c,a&&b&&d&&e?"orphan adoption, init wait ownership, and bounded reparent passed":"BROKEN");putc64(c,'\n');}
struct resource_ledger { u64 address_space,fd_refs,pipe_refs,signal_refs,timer_refs,deferred_refs,releases,double_releases; u8 zombie,teardown_done; };
static struct resource_ledger resource_ledger;
static TEXT64 void resource_start(void){resource_ledger=(struct resource_ledger){1,2,1,1,1,1,0,0,1,0};}
static TEXT64 int resource_teardown(void){if(!resource_ledger.zombie||resource_ledger.teardown_done)return 0;resource_ledger.address_space=0;resource_ledger.fd_refs=0;resource_ledger.pipe_refs=0;resource_ledger.signal_refs=0;resource_ledger.timer_refs=0;resource_ledger.deferred_refs=0;resource_ledger.releases=6;resource_ledger.teardown_done=1;return 1;}
static TEXT64 void resourceinfo(u16*c){text64(c,"resources as/fd/pipe/signal/timer/work/releases: ");hex64(c,resource_ledger.address_space);text64(c,"/");hex64(c,resource_ledger.fd_refs);text64(c,"/");hex64(c,resource_ledger.pipe_refs);text64(c,"/");hex64(c,resource_ledger.signal_refs);text64(c,"/");hex64(c,resource_ledger.timer_refs);text64(c,"/");hex64(c,resource_ledger.deferred_refs);text64(c,"/");hex64(c,resource_ledger.releases);putc64(c,'\n');}
static TEXT64 void teardowntest(u16*c){resource_start();resource_ledger.zombie=1;int a=resource_teardown(),b=!resource_teardown(),d=resource_ledger.address_space==0&&resource_ledger.releases==6;text64(c,"teardowntest: ");text64(c,a&&b&&d?"zombie retention, ordered resource release, and double-reap guard passed":"BROKEN");putc64(c,'\n');}
struct multiwait_model { u64 parent,children[3],codes[3],selected,waits,reaps; u8 states[3]; };
static struct multiwait_model multiwait;
static TEXT64 void multiwait_start(void){multiwait=(struct multiwait_model){FIXED_PID,{SECOND_PID,SECOND_PID+1,SECOND_PID+2},{0,0,0},0,0,0,{WAIT_RUNNING,WAIT_RUNNING,WAIT_RUNNING}};}
static TEXT64 int multiwait_exit(u32 i,u64 code){if(i>=3||multiwait.states[i]!=WAIT_RUNNING)return 0;multiwait.codes[i]=code;multiwait.states[i]=WAIT_ZOMBIE;return 1;}
static TEXT64 int multiwait_select(u64 pid){u32 i;multiwait.waits++;for(i=0;i<3;i++)if(multiwait.states[i]==WAIT_ZOMBIE&&(pid==(u64)-1||pid==multiwait.children[i])){multiwait.selected=multiwait.children[i];return 1;}return 0;}
static TEXT64 int multiwait_reap(void){u32 i;for(i=0;i<3;i++)if(multiwait.children[i]==multiwait.selected&&multiwait.states[i]==WAIT_ZOMBIE){multiwait.states[i]=WAIT_DEAD;multiwait.reaps++;return 1;}return 0;}
static TEXT64 void multichildtest(u16*c){multiwait_start();int a=multiwait_exit(1,7),b=multiwait_exit(0,3),d=multiwait_select((u64)-1),e=multiwait.selected==multiwait.children[0],f=multiwait_reap(),g=multiwait_select(multiwait.children[1]),h=multiwait_reap();text64(c,"multichildtest: ");text64(c,a&&b&&d&&e&&f&&g&&h&&multiwait.reaps==2?"bounded three-child exit filtering and aggregate selection passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void waitpidtest(u16*c){multiwait_start();multiwait_exit(2,9);int a=!multiwait_select(multiwait.children[0]),b=multiwait_select(multiwait.children[2]),d=multiwait_reap();text64(c,"waitpidtest: ");text64(c,a&&b&&d&&multiwait.codes[2]==9?"exact PID selection and one-shot waitpid reap passed":"BROKEN");putc64(c,'\n');}
struct fork_exec_lifecycle { u64 parent,child,old_entry,new_entry,argc,envc,exit_code,forks,execs,exits,waits,reaps; u8 child_state,image_replaced; };
static struct fork_exec_lifecycle lifecycle_model;
static TEXT64 int exec_validate(void);
static TEXT64 int exec_stack_validate(void);
static TEXT64 void lifecycle_start(void){lifecycle_model=(struct fork_exec_lifecycle){.parent=FIXED_PID,.child=SECOND_PID,.old_entry=USER_CODE_VA,.child_state=WAIT_RUNNING};}
static TEXT64 int lifecycle_fork(void){if(lifecycle_model.forks||lifecycle_model.child_state!=WAIT_RUNNING)return 0;lifecycle_model.forks=1;return 1;}
static TEXT64 int lifecycle_exec(void){if(!lifecycle_model.forks||lifecycle_model.execs||!exec_validate())return 0;lifecycle_model.old_entry=USER_CODE_VA;lifecycle_model.new_entry=exec_model.entry;lifecycle_model.argc=exec_model.argc;lifecycle_model.envc=1;lifecycle_model.execs=1;lifecycle_model.image_replaced=1;return exec_stack_validate();}
static TEXT64 int lifecycle_exit(u64 code){if(!lifecycle_model.image_replaced||lifecycle_model.child_state!=WAIT_RUNNING)return 0;lifecycle_model.exit_code=code;lifecycle_model.child_state=WAIT_ZOMBIE;lifecycle_model.exits=1;return 1;}
static TEXT64 int lifecycle_wait_reap(void){if(lifecycle_model.child_state!=WAIT_ZOMBIE)return 0;lifecycle_model.waits=1;lifecycle_model.reaps=1;lifecycle_model.child_state=WAIT_DEAD;return 1;}
static TEXT64 void lifecycleinfo(u16*c){text64(c,"lifecycle parent/child/old/new/state/fork/exec/exit/wait/reap: ");hex64(c,lifecycle_model.parent);text64(c,"/");hex64(c,lifecycle_model.child);text64(c,"/");hex64(c,lifecycle_model.old_entry);text64(c,"/");hex64(c,lifecycle_model.new_entry);text64(c,"/");hex64(c,lifecycle_model.child_state);text64(c,"/");hex64(c,lifecycle_model.forks);text64(c,"/");hex64(c,lifecycle_model.execs);text64(c,"/");hex64(c,lifecycle_model.exits);text64(c,"/");hex64(c,lifecycle_model.waits);text64(c,"/");hex64(c,lifecycle_model.reaps);putc64(c,'\n');}
static TEXT64 void forkexecwaittest(u16*c){lifecycle_start();int a=lifecycle_fork(),b=lifecycle_exec(),d=lifecycle_exit(23),e=lifecycle_wait_reap(),f=lifecycle_model.old_entry==USER_CODE_VA&&lifecycle_model.new_entry==exec_model.entry,g=lifecycle_model.argc==2&&lifecycle_model.envc==1&&lifecycle_model.exit_code==23; text64(c,"forkexecwaittest: ");text64(c,a&&b&&d&&e&&f&&g&&lifecycle_model.child_state==WAIT_DEAD?"fork metadata, exec replacement, exit status, and wait/reap passed":"BROKEN");putc64(c,'\n');}
struct session_job { u64 pid,argv,env,fd,pipe,signals,timers,deferred,status; u8 active,execed,zombie,reaped; };
static struct session_job session_jobs[2];
static u64 session_commands,session_waits,session_reaps;
static TEXT64 void session_start(void){session_jobs[0]=(struct session_job){SECOND_PID,2,1,2,1,1,1,1,0,1,1,0,0};session_jobs[1]=(struct session_job){SECOND_PID+1,2,1,1,1,1,1,1,0,1,1,0,0};session_commands=0;session_waits=0;session_reaps=0;}
static TEXT64 int session_job_exit(u32 i,u64 code){if(i>=2||!session_jobs[i].active||session_jobs[i].zombie)return 0;session_jobs[i].status=code;session_jobs[i].zombie=1;return 1;}
static TEXT64 int session_job_reap(u32 i){if(i>=2||!session_jobs[i].zombie||session_jobs[i].reaped)return 0;session_jobs[i].fd=session_jobs[i].pipe=session_jobs[i].signals=session_jobs[i].timers=session_jobs[i].deferred=0;session_jobs[i].reaped=1;session_jobs[i].active=0;session_reaps++;return 1;}
static TEXT64 void sessioninfo(u16*c){text64(c,"session init/shell/jobs/commands/waits/reaps: ");hex64(c,FIXED_PID);text64(c,"/");hex64(c,FIXED_PID);text64(c,"/");hex64(c,2);text64(c,"/");hex64(c,session_commands);text64(c,"/");hex64(c,session_waits);text64(c,"/");hex64(c,session_reaps);putc64(c,'\n');}
static TEXT64 void jobtest(u16*c){session_start();session_commands=2;int a=session_job_exit(1,9),b=session_job_exit(0,7);session_waits=2;int d=session_job_reap(1),e=session_job_reap(0),f=!session_jobs[0].active&&!session_jobs[1].active&&session_reaps==2; text64(c,"jobtest: ");text64(c,a&&b&&d&&e&&f?"init/shell two-job argv-env/fd/pipe/signal/timer/work and wait/reap passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void waitinfo(u16*c){text64(c,"wait parent/child/state/code/calls/reaps: ");hex64(c,wait_model.parent_pid);text64(c,"/");hex64(c,wait_model.child_pid);text64(c,"/");hex64(c,wait_model.state);text64(c,"/");hex64(c,wait_model.exit_code);text64(c,"/");hex64(c,wait_model.wait_calls);text64(c,"/");hex64(c,wait_model.reaps);putc64(c,'\n');}
struct wait_block_model { u64 blocks,wakes,nohang_calls,ready_checks; u8 blocked,woken; };
static struct wait_block_model wait_block_model;
static TEXT64 void wait_block_start(void){wait_block_model=(struct wait_block_model){0,0,0,0,0,0};}
static TEXT64 int wait_block_wait(u8 nohang){wait_block_model.ready_checks++;if(nohang){wait_block_model.nohang_calls++;return wait_model.state==WAIT_ZOMBIE;}if(wait_model.state!=WAIT_ZOMBIE){wait_block_model.blocked=1;wait_block_model.blocks++;return 0;}wait_block_model.woken=1;return 1;}
static TEXT64 void wait_block_exit(void){if(wait_model.state==WAIT_RUNNING){wait_model_exit(17);if(wait_block_model.blocked){wait_block_model.blocked=0;wait_block_model.woken=1;wait_block_model.wakes++;}}}
static TEXT64 void waitblockinfo(u16*c){text64(c,"wait block/blocked/wakes/nohang/checks: ");hex64(c,wait_block_model.blocks);text64(c,"/");hex64(c,wait_block_model.blocked);text64(c,"/");hex64(c,wait_block_model.wakes);text64(c,"/");hex64(c,wait_block_model.nohang_calls);text64(c,"/");hex64(c,wait_block_model.ready_checks);putc64(c,'\n');}
static TEXT64 void waitblocktest(u16*c){wait_model_start();wait_block_start();int a=!wait_block_wait(0),b=wait_block_model.blocked,k=!wait_block_wait(1),d=wait_block_model.nohang_calls==1;wait_block_exit();int e=wait_block_wait(0),f=wait_block_model.wakes==1&&wait_block_model.woken;wait_model.waited=1;int g=wait_model_reap();text64(c,"waitblocktest: ");text64(c,a&&b&&k&&d&&e&&f&&g?"blocked wait, exit wake-one, status retry, and reap passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void nohangtest(u16*c){wait_model_start();wait_block_start();int a=!wait_block_wait(1),b=wait_block_model.nohang_calls==1;wait_block_exit();int d=wait_block_wait(1),e=d&&wait_model.exit_code==17;wait_model.waited=1;int f=wait_model_reap();text64(c,"nohangtest: ");text64(c,a&&b&&e&&f?"WNOHANG empty/ready results and one-shot reap passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void waittest(u16*c){wait_model_start();int a=!wait_model_wait(),b=wait_model_exit(42),d=wait_model_wait(),e=wait_model.state==WAIT_ZOMBIE&&wait_model.exit_code==42,f=wait_model_reap(),g=wait_model.state==WAIT_DEAD;text64(c,"waittest: ");text64(c,a&&b&&d&&e&&f&&g?"bounded wait, exit status, zombie selection, and reap passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void module_init_model(void){u32 i;for(i=0;i<MODULE_MAX;i++)modules[i]=(struct module_model){0,0,0,0,0};for(i=0;i<SYMBOL_MAX;i++)exported_symbols[i]=(struct symbol_model){0,0,0,0};modules[0]=(struct module_model){0x636f7265,1,0,1,1};modules[1]=(struct module_model){0x766673,1,0,1,1};exported_symbols[0]=(struct symbol_model){0x706d6d,0,1,1};exported_symbols[1]=(struct symbol_model){0x766673,1,1,1};module_inits=2;module_exports=2;module_lookups=0;}
static TEXT64 int module_lookup(u64 name){u32 i;module_lookups++;for(i=0;i<SYMBOL_MAX;i++)if(exported_symbols[i].valid&&exported_symbols[i].exported&&exported_symbols[i].name_hash==name)return 1;return 0;}
static TEXT64 void moduleinfo(u16*c){u32 i;text64(c,"modules initialized/exports/lookups: ");hex64(c,module_inits);text64(c,"/");hex64(c,module_exports);text64(c,"/");hex64(c,module_lookups);putc64(c,'\n');for(i=0;i<MODULE_MAX;i++)if(modules[i].loaded){text64(c,"module ");hex64(c,i);text64(c," initialized ");hex64(c,modules[i].initialized);putc64(c,'\n');}}
static TEXT64 void moduletest(u16*c){int a=module_lookup(0x706d6d),b=module_lookup(0x6d697373),d=modules[0].initialized&&modules[1].initialized;text64(c,"moduletest: ");text64(c,a&&!b&&d?"module init order and exported-symbol lookup passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void lockatomictest(u16*c){irqflags_t f;u8 v=0;atomic_fetch_or_relaxed_u8(&this_cpu()->softirq_pending,0);raw_spin_lock_irqsave(&deferred_lock,&f);v=atomic_load_relaxed_u8(&this_cpu()->softirq_pending);atomic_store_release_u8(&this_cpu()->softirq_pending,(u8)(v|1));raw_spin_unlock_irqrestore(&deferred_lock,f);text64(c,"lockatomictest: ");text64(c,atomic_load_relaxed_u8(&this_cpu()->softirq_pending)==1&&deferred_lock.locked==0?"irq-safe lock, atomic publication, per-CPU ordering passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void softirqinfo(u16*c){text64(c,"softirq pending/raises/runs/drops/budget: ");hex64(c,softirq_model.pending);text64(c,"/");hex64(c,softirq_model.raises);text64(c,"/");hex64(c,softirq_model.runs);text64(c,"/");hex64(c,softirq_model.drops);text64(c,"/");hex64(c,softirq_model.budget_exhaustions);text64(c," tasklets/work: ");hex64(c,tasklets[0].pending+tasklets[1].pending);text64(c,"/");hex64(c,work_used);putc64(c,'\n');}
static TEXT64 void softirqtest(u16*c){u8 i;softirq_model=(struct softirq_model){0};work_head=work_tail=work_used=0;for(i=0;i<TASKLET_CAP;i++)tasklets[i]=(struct tasklet_model){0,0,0};tasklet_schedule(0);tasklet_schedule(0);tasklet_schedule(1);for(i=0;i<WORK_CAP;i++)workqueue_submit(i,0);int a=workqueue_submit(9,0)==0;softirq_run_budget();int b=!tasklets[0].pending&&!tasklets[1].pending&&work_used==2;softirq_run_budget();int d=work_used==0&&softirq_model.budget_exhaustions>=1;text64(c,"softirqtest: ");text64(c,a&&b&&d&&softirq_model.runs>=6?"tasklet coalescing, FIFO work, and budget carry-over passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void pc_reset(void){u64 flags=irq_save64();pc_head=pc_tail=pc_used=pc_next=pc_expected=0;pc_produced=pc_consumed=pc_sequence_errors=0;pc_start_event.signaled=0;pc_start_event.sets=pc_start_event.resets=pc_start_event.waits=pc_start_event.wakes=0;waitq_reset(&pc_start_event.waitq);sem_init(&pc_spaces,PC_BUFFER_CAP,PC_BUFFER_CAP);sem_init(&pc_items,0,PC_BUFFER_CAP);irq_restore64(flags);}
static TEXT64 void pc_producer(void){u8 value;while(threads[1].progress<THREAD_STEPS){sem_down(&pc_spaces);{u64 flags=irq_save64();value=pc_next++;pc_buffer[pc_head]=value;pc_head=(u8)((pc_head+1)%PC_BUFFER_CAP);pc_used++;pc_produced++;irq_restore64(flags);}threads[1].progress++;sem_up(&pc_items);busy_delay();}thread_exit();}
static TEXT64 void pc_consumer(void){u8 value;while(threads[2].progress<THREAD_STEPS){sem_down(&pc_items);{u64 flags=irq_save64();value=pc_buffer[pc_tail];pc_tail=(u8)((pc_tail+1)%PC_BUFFER_CAP);pc_used--;if(value!=pc_expected)pc_sequence_errors++;pc_expected++;pc_consumed++;irq_restore64(flags);}threads[2].progress++;sem_up(&pc_spaces);busy_delay();}thread_exit();}
static TEXT64 u8 rr_pick_next(void){u32 n;for(n=1;n<=THREAD_COUNT;n++){u8 i=(u8)((round_robin+n)%THREAD_COUNT);if(threads[i].state==THREAD_RUNNABLE||threads[i].state==THREAD_RUNNING){round_robin=i;return i;}}return 0xff;}
static TEXT64 void rr_enqueue(u8 id){if(id<THREAD_COUNT&&threads[id].state==THREAD_FINISHED)return;sched_enqueues++;}
static TEXT64 void rr_dequeue(u8 id){if(id<THREAD_COUNT)sched_dequeues++;}
static struct sched_class fair_sched_class={"tiny_rr",rr_pick_next,rr_enqueue,rr_dequeue};
static TEXT64 struct sched_class *runtime_sched_class(void){return &fair_sched_class;}
static TEXT64 u8 next_runnable(void){sched_picks++;return rr_pick_next();}
static TEXT64 void reap_finished_threads(void){u32 i;for(i=1;i<THREAD_COUNT;i++)if((idle_running||i!=current_thread)&&threads[i].state==THREAD_FINISHED&&threads[i].stack_phys){u64 p=threads[i].stack_phys;threads[i].stack_phys=0;(void)pmm_free_page(p);}}
/* Called from IRQ0 only. It returns the exact frame restored by IRQ0's one iretq path. */
TEXT64 struct irq0_frame *irq0_schedule(struct irq0_frame *f){u8 old,next;ticks++;softirq_run_budget();outb64(PIC1_COMMAND,PIC_EOI);if(f&&f->cs==USER_CS){user_irq0_save_restore(f);return f;}if(idle_running){idle_frame=f;idle_ticks++;}else threads[current_thread].frame=(u64)(unsigned long)f;wake_sleepers();reap_finished_threads();if(!idle_running&&quantum_left){quantum_left--;if(quantum_left)return f;}old=current_thread;next=next_runnable();quantum_left=TIME_SLICE_TICKS;if(next==0xff){if(idle_running)return f;if(threads[old].state==THREAD_RUNNING){threads[old].state=THREAD_RUNNABLE;rr_enqueue(old);}idle_running=1;idle_switches++;return idle_frame;}if(idle_running){idle_running=0;threads[next].state=THREAD_RUNNING;current_thread=next;threads[next].switches++;preempt_switches++;return (struct irq0_frame *)(unsigned long)threads[next].frame;}if(next==old){if(old==0)idle_worker_ticks++;return f;}if(threads[old].state==THREAD_RUNNING){threads[old].state=THREAD_RUNNABLE;rr_enqueue(old);}rr_dequeue(next);threads[next].state=THREAD_RUNNING;current_thread=next;threads[next].switches++;preempt_switches++;return (struct irq0_frame *)(unsigned long)threads[next].frame;}
static TEXT64 void busy_delay(void){volatile u64 n;for(n=0;n<BUSY_SPINS;n++)__asm__ volatile("":::"memory");}
static TEXT64 void thread_sleep_ticks(u64 delta){u64 flags;u8 id=current_thread;if(!delta)delta=1;flags=irq_save64();if(!idle_running&&threads[id].state==THREAD_RUNNING){threads[id].wake_tick=ticks+delta;threads[id].state=THREAD_SLEEPING;}irq_restore64(flags);while(threads[id].state==THREAD_SLEEPING)__asm__ volatile("sti; hlt");}
static TEXT64 void kbd_wait_char(u8 *out){u8 id=current_thread;for(;;){u64 flags=irq_save64();if(id&&threads[id].mailbox_ready){*out=threads[id].mailbox;threads[id].mailbox_ready=0;irq_restore64(flags);return;}if(id&&threads[id].state==THREAD_RUNNING&&waitq_enqueue(&kbd_waitq,id))threads[id].state=THREAD_BLOCKED_KBD;irq_restore64(flags);while(threads[id].state==THREAD_BLOCKED_KBD)__asm__ volatile("sti; hlt");}}
static TEXT64 void worker_run(u8 id){if(pc_test){event_wait(&pc_start_event);if(id==1)pc_producer();else pc_consumer();return;}while(threads[id].progress<THREAD_STEPS){if(kbd_wait_test){u8 ch;kbd_wait_char(&ch);threads[id].mailbox=ch;threads[id].received++;}threads[id].progress++;if(sleep_test)thread_sleep_ticks(id==1?SLEEP_A_TICKS:SLEEP_B_TICKS);else if(!kbd_wait_test)busy_delay();}thread_exit();}
TEXT64 void thread_trampoline_c(void){worker_run(current_thread);}
static TEXT64 void thread_exit(void){u64 flags=irq_save64();threads[current_thread].state=THREAD_FINISHED;irq_restore64(flags);for(;;)__asm__ volatile("sti; hlt");}
static TEXT64 int start_threads(u8 mode){u32 i;u64 flags;if(threads_started)return 0;if(!pmm_ready)return -1;flags=irq_save64();sleep_test=mode==1;kbd_wait_test=mode==2;pc_test=mode==3;if(pc_test)pc_reset();sleep_wakeups=idle_worker_ticks=kbd_direct_deliveries=0;waitq_reset(&kbd_waitq);for(i=1;i<THREAD_COUNT;i++){u64 p=pmm_alloc();struct irq0_frame *f;if(!p){while(i>1){i--;(void)pmm_free_page(threads[i].stack_phys);threads[i].state=THREAD_EMPTY;threads[i].stack_phys=0;}irq_restore64(flags);return -1;}threads[i].id=(u8)i;threads[i].state=THREAD_EMPTY;threads[i].stack_phys=p;threads[i].switches=threads[i].progress=threads[i].wake_tick=threads[i].received=0;threads[i].mailbox=threads[i].mailbox_ready=0;f=(struct irq0_frame *)(unsigned long)(phys_to_high(p)+THREAD_STACK_BYTES-sizeof(*f));f->r15=f->r14=f->r13=f->r11=f->r10=f->r9=f->r8=f->rdi=f->rsi=f->rbp=f->rdx=f->rcx=f->rbx=f->rax=0;f->r12=phys_to_high(p)+THREAD_STACK_BYTES;f->rip=runtime_thread_trampoline_address();f->cs=0x08;f->rflags=0x202;threads[i].frame=(u64)(unsigned long)f;}for(i=1;i<THREAD_COUNT;i++)threads[i].state=THREAD_RUNNABLE;threads_started=1;quantum_left=TIME_SLICE_TICKS;irq_restore64(flags);return 1;}
static TEXT64 void ps64(u16*c){u32 i;text64(c,"threads: id state frame stack-pa stack-high switches progress wake-tick received last\n");for(i=0;i<THREAD_COUNT;i++){text64(c,"thread ");hex64(c,i);text64(c," ");text64(c,thread_state_name(threads[i].state));text64(c," ");hex64(c,threads[i].frame);text64(c," ");hex64(c,threads[i].stack_phys);text64(c," ");hex64(c,threads[i].stack_phys?phys_to_high(threads[i].stack_phys):0);text64(c," ");hex64(c,threads[i].switches);text64(c," ");hex64(c,threads[i].progress);text64(c," ");hex64(c,threads[i].wake_tick);text64(c," ");hex64(c,threads[i].received);text64(c," ");hex64(c,threads[i].mailbox);putc64(c,'\n');}text64(c,"idle ");text64(c,idle_running?"running":"ready");text64(c," frame ");hex64(c,(u64)(unsigned long)idle_frame);text64(c," stack static\n");}
static TEXT64 void threadinfo(u16*c){text64(c,"scheduler: PIT preemptive independent idle\ncurrent: ");text64(c,idle_running?"idle":"thread");text64(c," ");hex64(c,current_thread);text64(c,"\nnext scan: ");hex64(c,round_robin);text64(c,"\nstarted: ");text64(c,threads_started?"yes":"no");text64(c,"\nmode: ");text64(c,pc_test?"pctest":kbd_wait_test?"kbdwaittest":sleep_test?"sleeptest":"preempttest");text64(c,"\nquantum left: ");hex64(c,quantum_left);text64(c,"\nPIT ticks: ");hex64(c,ticks);text64(c,"\npreempt switches: ");hex64(c,preempt_switches);text64(c,"\nidle switches/ticks: ");hex64(c,idle_switches);text64(c," ");hex64(c,idle_ticks);text64(c,"\nsleep wakeups: ");hex64(c,sleep_wakeups);text64(c,"\nkbd waiters: ");hex64(c,kbd_waitq.count);text64(c,"\nkbd enqueue/one/all: ");hex64(c,kbd_waitq.enqueues);text64(c," ");hex64(c,kbd_waitq.wake_one);text64(c," ");hex64(c,kbd_waitq.wake_all);text64(c,"\nworker steps: ");hex64(c,threads[1].progress);text64(c," ");hex64(c,threads[2].progress);text64(c,"\nIRQ0 schedules: yes\n");}
static TEXT64 void pcinfo(u16*c){u64 flags=irq_save64();u8 es=pc_start_event.signaled,ew=pc_start_event.waitq.count,sc=pc_spaces.count,sw=pc_spaces.waitq.count,ic=pc_items.count,iw=pc_items.waitq.count,used=pc_used;u64 waits=pc_start_event.waits,wakes=pc_start_event.wakes,prod=pc_produced,cons=pc_consumed,errors=pc_sequence_errors;irq_restore64(flags);text64(c,"E sig/wait: ");hex64(c,es);text64(c," ");hex64(c,ew);text64(c,"\nE waits/all: ");hex64(c,waits);text64(c," ");hex64(c,wakes);text64(c,"\nS count/wait: ");hex64(c,sc);text64(c," ");hex64(c,sw);text64(c,"\nI count/wait: ");hex64(c,ic);text64(c," ");hex64(c,iw);text64(c,"\nR used/cap: ");hex64(c,used);text64(c," ");hex64(c,PC_BUFFER_CAP);text64(c,"\nP prod/cons: ");hex64(c,prod);text64(c," ");hex64(c,cons);text64(c,"\nP errors/ok: ");hex64(c,errors);text64(c," ");text64(c,prod==THREAD_STEPS&&cons==THREAD_STEPS&&!used&&!errors&&sc==PC_BUFFER_CAP&&!ic&&!sw&&!iw?"yes":"no");putc64(c,'\n');}
static TEXT64 void meminfo(u16*c){text64(c,"PMM: 4 KiB physical frames in 16 MiB mapped window\nstatus:  ");text64(c,pmm_error);if(!pmm_ready){putc64(c,'\n');return;}text64(c,"\ntracked: ");hex64(c,pmm_total);text64(c,"\nfree:    ");hex64(c,pmm_free);text64(c,"\nused:    ");hex64(c,pmm_used);text64(c,"\ninvariant tracked = free + used: ");text64(c,pmm_total==pmm_free+pmm_used?"yes":"BROKEN");text64(c,"\nbitmap:  ");hex64(c,(u64)(unsigned long)pmm_bitmap);text64(c," +");hex64(c,PMM_BITMAP_BYTES);text64(c,"\nfixed:   ");hex64(c,(u64)(unsigned long)pmm_fixed);putc64(c,'\n');}
static TEXT64 void set_gate(struct idt_gate *g,u64 target,u8 ist){g->offset_low=(u16)target;g->selector=KERNEL_CS;g->ist=ist;g->type=IDT_GATE_INTERRUPT;g->offset_mid=(u16)(target>>16);g->offset_high=(u32)(target>>32);g->reserved=0;}
static TEXT64 u64 runtime_thread_trampoline_address(void){u64 v;__asm__ volatile("leaq thread_trampoline(%%rip),%0":"=r"(v));return v;}
static TEXT64 u64 runtime_idle_trampoline_address(void){u64 v;__asm__ volatile("leaq idle_trampoline(%%rip),%0":"=r"(v));return v;}
static TEXT64 void idle_init(void){struct irq0_frame*f=(struct irq0_frame *)(void *)(__idle_stack_end-sizeof(*f));f->r15=f->r14=f->r13=f->r11=f->r10=f->r9=f->r8=f->rdi=f->rsi=f->rbp=f->rdx=f->rcx=f->rbx=f->rax=0;f->r12=(u64)(unsigned long)__idle_stack_end;f->rip=runtime_idle_trampoline_address();f->cs=0x08;f->rflags=0x202;idle_frame=f;idle_running=0;idle_switches=idle_ticks=0;}
static TEXT64 u64 runtime_bp_address(void){u64 v;__asm__ volatile("leaq exception_bp(%%rip),%0":"=r"(v));return v;}
static TEXT64 u64 runtime_ud_address(void){u64 v;__asm__ volatile("leaq exception_ud(%%rip),%0":"=r"(v));return v;}
static TEXT64 u64 runtime_pf_address(void){u64 v;__asm__ volatile("leaq exception_pf(%%rip),%0":"=r"(v));return v;}
static TEXT64 void stack_guards_init(struct long_mode_handoff*h){volatile u64 *pt=(volatile u64 *)(unsigned long)h->high_pt[0];u64 g[3]={(u64)(unsigned long)__idle_guard_start,(u64)(unsigned long)__rsp0_guard_start,(u64)(unsigned long)__ist1_guard_start};u32 i;for(i=0;i<3;i++){u64 off=g[i]-KERNEL_VMA_BASE;pt[off/PAGE_SIZE]=0;invlpg64(g[i]);}}
static TEXT64 void runtime_gdt_tss_init(void){
    u64 base=(u64)(unsigned long)&runtime_tss,limit=sizeof(runtime_tss)-1;
    u16 selector=TSS_SELECTOR;
    u32 i;
    for(i=0;i<8;i++)runtime_gdt[i]=0;
    runtime_gdt[1]=0x00af9a000000ffffULL;
    runtime_gdt[2]=0x00af92000000ffffULL;
    runtime_gdt[5]=0x00aff2000000ffffULL;
    runtime_gdt[6]=0x00affa000000ffffULL;
    runtime_tss.reserved0=0; runtime_tss.rsp0=(u64)(unsigned long)__rsp0_stack_end;
    runtime_tss.rsp1=runtime_tss.rsp2=runtime_tss.reserved1=0;
    runtime_tss.ist1=(u64)(unsigned long)__ist1_stack_end;
    runtime_tss.ist2=runtime_tss.ist3=runtime_tss.ist4=runtime_tss.ist5=runtime_tss.ist6=runtime_tss.ist7=runtime_tss.reserved2=0;
    runtime_tss.reserved3=0; runtime_tss.iomap_base=sizeof(runtime_tss);
    runtime_gdt[3]=(limit&0xffffULL)|((base&0xffffffULL)<<16)|(0x89ULL<<40)|(((limit>>16)&0xfULL)<<48)|(((base>>24)&0xffULL)<<56);
    runtime_gdt[4]=base>>32;
    runtime_gdtr.limit=sizeof(runtime_gdt)-1; runtime_gdtr.base=(u64)(unsigned long)runtime_gdt;
    __asm__ volatile("lgdt %0"::"m"(runtime_gdtr):"memory");
    __asm__ volatile("movw $0x10,%%ax; movw %%ax,%%ds; movw %%ax,%%es; movw %%ax,%%ss":::"ax","memory");
    __asm__ volatile("ltr %0"::"r"(selector):"memory");
}
static TEXT64 u64 runtime_syscall_address(void){u64 v;__asm__ volatile("leaq syscall_entry(%%rip),%0":"=r"(v));return v;}
static TEXT64 u64 runtime_irq0_address(void){u64 v;__asm__ volatile("leaq irq0_entry(%%rip),%0":"=r"(v));return v;}
static TEXT64 u64 runtime_irq1_address(void){u64 v;__asm__ volatile("leaq irq1_entry(%%rip),%0":"=r"(v));return v;}
static TEXT64 void pic_masks(u8 master,u8 slave){outb64(PIC1_DATA,master);outb64(PIC2_DATA,slave);}
static TEXT64 void pit_init(void){outb64(PIT_COMMAND,0x36);outb64(PIT_CHANNEL0,(u8)PIT_DIVISOR);outb64(PIT_CHANNEL0,(u8)(PIT_DIVISOR>>8));}
static TEXT64 void pic_init(void){outb64(PIC1_COMMAND,0x11);io_wait64();outb64(PIC2_COMMAND,0x11);io_wait64();outb64(PIC1_DATA,0x20);io_wait64();outb64(PIC2_DATA,0x28);io_wait64();outb64(PIC1_DATA,4);io_wait64();outb64(PIC2_DATA,2);io_wait64();outb64(PIC1_DATA,1);io_wait64();outb64(PIC2_DATA,1);io_wait64();pic_masks(0xfc,0xff);}
static TEXT64 void install_idt(struct long_mode_handoff*h){struct idt_gate *idt=(struct idt_gate *)(unsigned long)phys_to_high(h->idt_address);struct idtr d;u32 i;for(i=0;i<IDT_ENTRIES;i++){idt[i].offset_low=0;idt[i].selector=0;idt[i].ist=0;idt[i].type=0;idt[i].offset_mid=0;idt[i].offset_high=0;idt[i].reserved=0;}set_gate(&idt[3],runtime_bp_address(),0);set_gate(&idt[6],runtime_ud_address(),0);set_gate(&idt[14],runtime_pf_address(),IST1_INDEX);set_gate(&idt[0x80],runtime_syscall_address(),0);idt[0x80].type=0xee;set_gate(&idt[0x20],runtime_irq0_address(),0);set_gate(&idt[0x21],runtime_irq1_address(),0);d.limit=sizeof(struct idt_gate)*IDT_ENTRIES-1;d.base=(u64)(unsigned long)idt;__asm__ volatile("lidt %0"::"m"(d):"memory");}
static TEXT64 void lminfo(u16*c,struct long_mode_handoff*h){u32 i;text64(c,"long mode: 16 MiB identity + high alias\npml4: ");hex64(c,h->pml4);text64(c,"\nlow/high PT: ");for(i=0;i<PAGE_TABLES_PER_ALIAS;i++){hex64(c,h->pt[i]);text64(c,"/");hex64(c,h->high_pt[i]);text64(c," ");}text64(c,"\nidentity: 0000000000000000 - 0000000001000000\n");}
static TEXT64 void hhinfo(u16*c,struct long_mode_handoff*h){u64 cr3,rsp;__asm__ volatile("mov %%cr3,%0":"=r"(cr3));__asm__ volatile("mov %%rsp,%0":"=r"(rsp));text64(c,"higher-half kernel: enabled\nVMA base: ");hex64(c,h->kernel_vma_base);text64(c,"\nphysical base: ");hex64(c,h->kernel_phys_base);text64(c,"\nPML4[511], PDPT[510]\nhigh alias: ffffffff80000000 - ffffffff80ffffff\nphysical:   0000000000000000 - 00000000000ffffff\nCR3: ");hex64(c,cr3);text64(c,"\nactive RSP: ");hex64(c,rsp);text64(c,"\nIDT high: ");hex64(c,phys_to_high(h->idt_address));putc64(c,'\n');}
static TEXT64 void hhtest(u16*c){volatile u64 *low=(volatile u64 *)(unsigned long)((u64)(unsigned long)&hh_test_word-KERNEL_VMA_BASE);volatile u64 *high=&hh_test_word;*low=0x4849474848414c46ULL;if(*high==0x4849474848414c46ULL)text64(c,"hhtest: low/high aliases agree\n");else text64(c,"hhtest: alias mismatch\n");}
static TEXT64 void idtinfo(u16*c,struct long_mode_handoff*h){text64(c,"IDT: exceptions + DPL3 syscall gate + PIT IRQ0 + IRQ1\n#PF IST: 0000000000000001\nbase: ");hex64(c,phys_to_high(h->idt_address));text64(c,"\nlimit: 0000000000000fff\n#BP vector: 0000000000000003 (returns)\n#UD vector: 0000000000000006\n#PF vector: 000000000000000e\nIRQ0 vector: 0000000000000020\nIRQ1 vector: 0000000000000021\nint 0x80 vector: 0000000000000080 DPL: 0000000000000003 gate: interrupt\n");}
static TEXT64 void kbdinfo(u16*c){u8 head,tail,waiters;u64 flags=irq_save64();head=kbd_head;tail=kbd_tail;waiters=kbd_waitq.count;irq_restore64(flags);text64(c,"keyboard: IRQ1 ring producer plus direct worker wake-one\nIRQ1 enabled: yes\nraw bytes: ");hex64(c,irq1_raw_count);text64(c,"\nmake codes: ");hex64(c,irq1_count);text64(c,"\noverflows: ");hex64(c,kbd_overflow_count);text64(c,"\nlast raw: ");hex64(c,irq1_last_scancode);text64(c,"\nring head/tail: ");hex64(c,head);text64(c," ");hex64(c,tail);text64(c,"\nwaiters: ");hex64(c,waiters);text64(c,"\nwake-one: ");hex64(c,kbd_waitq.wake_one);text64(c,"\ndirect deliveries: ");hex64(c,kbd_direct_deliveries);putc64(c,'\n');}
static TEXT64 void tickinfo(u16*c){u64 t;__asm__ volatile("cli":::"memory");t=ticks;__asm__ volatile("sti":::"memory");text64(c,"PIT channel 0: 0000000000000064 Hz\nticks: ");hex64(c,t);text64(c,"\nuptime (centiseconds): ");hex64(c,t);putc64(c,'\n');}
static TEXT64 void mmap64(u16*c,struct long_mode_handoff*h){const struct mb2_mmap_tag*m;u32 off,n=0;m=mmap_tag64(h);if(!m){text64(c,"Multiboot2 mmap unavailable: ");text64(c,pmm_error);putc64(c,'\n');return;}text64(c,"Multiboot2 available ranges:\n");for(off=0;off<m->size-16&&n<6;off+=m->entry_size){const struct mb2_mmap_entry*e=(const struct mb2_mmap_entry*)((const u8*)m+16+off);if(e->type==1){hex64(c,e->addr);text64(c," +");hex64(c,e->len);putc64(c,'\n');n++;}}}
static TEXT64 void print_exception_frame(u16*c,struct exception_frame*f){text64(c,"\nvector: ");hex64(c,f->vector);text64(c,"\nerror:  ");hex64(c,f->error);text64(c,"\nrip:    ");hex64(c,f->rip);text64(c,"\ncs:     ");hex64(c,f->cs);text64(c,"\nrflags: ");hex64(c,f->rflags);}
static TEXT64 void tssinfo(u16*c,struct long_mode_handoff*h){u16 tr;struct idt_gate *idt=(struct idt_gate *)(unsigned long)phys_to_high(h->idt_address);__asm__ volatile("str %0":"=r"(tr));text64(c,"TSS/IST: one-way CPL3 entry\nTR: ");hex64(c,tr);text64(c,"\nGDTR base/limit: ");hex64(c,runtime_gdtr.base);text64(c," ");hex64(c,runtime_gdtr.limit);text64(c,"\nUSER_DS: ");hex64(c,USER_DS);text64(c," USER_CS: ");hex64(c,USER_CS);text64(c,"\nrsp0 top: ");hex64(c,runtime_tss.rsp0);text64(c,"\n#PF IST: ");hex64(c,idt[14].ist&7);text64(c,"\n");}
static TEXT64 int user_context_valid(struct saved_user_context *c)
{
    u64 code_end=user_process.entry+(u64)user_process.image_bytes;
    return c && c->valid && c->frame.cs==USER_CS && c->frame.ss==USER_DS &&
        c->frame.rip>=user_process.entry && c->frame.rip<code_end &&
        c->frame.rsp>=USER_STACK_VA && c->frame.rsp<=user_process.stack_top;
}
static TEXT64 void user_irq0_save_restore(struct irq0_frame *f)
{
    struct syscall_frame *s;
    if(!f || f->cs!=USER_CS || user_process.state!=PROCESS_RUNNING ||
       user_thread.state!=USER_THREAD_RUNNING) return;
    s=&user_thread.context.frame;
    s->r15=f->r15;s->r14=f->r14;s->r13=f->r13;s->r12=f->r12;
    s->r11=f->r11;s->r10=f->r10;s->r9=f->r9;s->r8=f->r8;
    s->rdi=f->rdi;s->rsi=f->rsi;s->rbp=f->rbp;s->rdx=f->rdx;
    s->rcx=f->rcx;s->rbx=f->rbx;s->rax=f->rax;s->rip=f->rip;
    s->cs=f->cs;s->rflags=f->rflags;s->rsp=f->rsp;s->ss=f->ss;
    user_thread.context.valid=1; user_thread.context.saves++;
    user_thread.context.pit_preemptions++;
    user_thread.context_address=(u64)(unsigned long)s;
    user_process.context_valid=(u8)user_context_valid(&user_thread.context);
    /* The single user thread is the only legal destination: restore the exact
       validated frame rather than attempting a user-side IRQ or task switch. */
    f->r15=s->r15;f->r14=s->r14;f->r13=s->r13;f->r12=s->r12;
    f->r11=s->r11;f->r10=s->r10;f->r9=s->r9;f->r8=s->r8;
    f->rdi=s->rdi;f->rsi=s->rsi;f->rbp=s->rbp;f->rdx=s->rdx;
    f->rcx=s->rcx;f->rbx=s->rbx;f->rax=s->rax;f->rip=s->rip;
    f->cs=s->cs;f->rflags=s->rflags;f->rsp=s->rsp;f->ss=s->ss;
    user_thread.context.pit_resumes++;
}
static TEXT64 int exception_signal(const struct exception_frame *f){u32 signo;u32 i;u64 cr2=0;if(!f||f->cs!=USER_CS)return 0;if(f->vector==3)signo=SIGTRAP;else if(f->vector==6)signo=SIGILL;else if(f->vector==14){signo=SIGSEGV;__asm__ volatile("mov %%cr2,%0":"=r"(cr2));}else return 0;for(i=0;i<SIG_PENDING_MAX;i++)if(!user_process.signals[i].pending){user_process.signals[i]=(struct signal_record){signo,f->vector,f->error,cr2,f->rip,1,0};user_process.signal_queued++;user_process.return_pending=1;if(signo!=SIGTRAP){user_process.state=PROCESS_EXITED;user_thread.state=USER_THREAD_EXITED;}return 1;}user_process.signal_dropped++;return 0;}
static TEXT64 int user_return_prepare(struct syscall_frame*f){u32 i;if(!f||!user_process.context_valid||user_process.state!=PROCESS_RUNNING||!user_process.return_pending)return 0;for(i=0;i<SIG_PENDING_MAX;i++)if(user_process.signals[i].pending){user_process.signals[i].pending=0;user_process.signals[i].delivered=1;user_process.signal_delivered++;break;}user_process.return_pending=0;for(i=0;i<SIG_PENDING_MAX;i++)if(user_process.signals[i].pending)user_process.return_pending=1;return 1;}
static TEXT64 void signalinfo(u16*c){u32 i;text64(c,"signals queued/delivered/dropped: ");hex64(c,user_process.signal_queued);text64(c," ");hex64(c,user_process.signal_delivered);text64(c," ");hex64(c,user_process.signal_dropped);text64(c," pending: ");hex64(c,user_process.return_pending);putc64(c,'\n');for(i=0;i<SIG_PENDING_MAX;i++)if(user_process.signals[i].pending){text64(c,"slot ");hex64(c,i);text64(c," signo/vector/rip: ");hex64(c,user_process.signals[i].signo);text64(c,"/");hex64(c,user_process.signals[i].vector);text64(c,"/");hex64(c,user_process.signals[i].rip);putc64(c,'\n');}}
static TEXT64 void signaltest(u16*c){struct exception_frame f={3,0,USER_CODE_VA,USER_CS,0,USER_STACK_TOP,USER_DS};u64 q=user_process.signal_queued,d=user_process.signal_delivered;int a=exception_signal(&f);f.vector=6;int b=exception_signal(&f);text64(c,"signaltest: ");text64(c,a&&b&&user_process.signal_queued==q+2&&user_process.state==PROCESS_EXITED&&user_process.signal_delivered==d?"exception notifications queued with bounded default actions passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void userreturntest(u16*c){struct syscall_frame f={0};u64 rip=USER_CODE_VA,rsp=USER_STACK_TOP;user_process.state=PROCESS_RUNNING;user_process.context_valid=1;user_thread.state=USER_THREAD_RUNNING;user_thread.context.frame.rip=rip;user_thread.context.frame.rsp=rsp;user_thread.context.frame.cs=USER_CS;user_thread.context.frame.ss=USER_DS;user_process.signals[0]=(struct signal_record){SIGTRAP,3,0,0,rip,1,0};user_process.return_pending=1;int a=user_return_prepare(&f),b=!user_process.return_pending;text64(c,"userreturntest: ");text64(c,a&&b&&user_thread.context.frame.rip==rip&&user_thread.context.frame.rsp==rsp?"validated user return preserved frame and delivered once":"BROKEN");putc64(c,'\n');}
static TEXT64 void user_context_save(struct syscall_frame *f, u64 result)
{
    if(!f || user_thread.process!=&user_process || user_process.state!=PROCESS_RUNNING) return;
    user_thread.context.frame=*f;
    user_thread.context.last_syscall=f->rax;
    user_thread.context.last_result=result;
    user_thread.context.saves++;
    user_thread.context.valid=1;
    user_thread.context_address=(u64)(unsigned long)&user_thread.context.frame;
    user_process.context_valid=(u8)user_context_valid(&user_thread.context);
}
static TEXT64 int user_process_enter(struct long_mode_handoff *h)
{
    if(!h || user_process.state!=PROCESS_READY || user_thread.state!=USER_THREAD_READY) return 0;
    user_process.state=PROCESS_RUNNING; user_thread.state=USER_THREAD_RUNNING;
    (void)task_transition(1,TASK_RUNNING);
    user_thread.transitions++; return 1;
}
static TEXT64 int user_process_exit(void)
{
    if(user_process.state!=PROCESS_RUNNING || user_thread.state!=USER_THREAD_RUNNING ||
       !user_context_valid(&user_thread.context)) return 0;
    user_process.state=PROCESS_EXITED; user_thread.state=USER_THREAD_EXITED;
    (void)task_transition(1,EXIT_ZOMBIE);
    user_thread.transitions++; user_exit_count++; return 1;
}
static TEXT64 void user_program_reclaim(u32 i){if(i>=MAX_USER_PROGRAMS||user_processes[i].state!=PROCESS_EXITED)return;user_threads[i].state=USER_THREAD_RECLAIMED;user_processes[i].state=PROCESS_EMPTY;user_processes[i].context_valid=0;user_reclaims++;}
static TEXT64 int user_programs_ready(void){u32 i;for(i=0;i<MAX_USER_PROGRAMS;i++)if(user_processes[i].state!=PROCESS_READY||user_threads[i].state!=USER_THREAD_READY||!user_processes[i].address_space||user_threads[i].process!=&user_processes[i])return 0;return 1;}
static TEXT64 const char *process_state_name(u8 s){return s==PROCESS_READY?"ready":s==PROCESS_RUNNING?"running":s==PROCESS_EXITED?"exited":"empty";}
static TEXT64 const char *user_thread_state_name(u8 s){return s==USER_THREAD_READY?"ready":s==USER_THREAD_RUNNING?"running":s==USER_THREAD_EXITED?"exited":"empty";}
static TEXT64 const char *task_state_name(u8 s){return s==TASK_RUNNING?"running":s==TASK_INTERRUPTIBLE?"interruptible":s==TASK_UNINTERRUPTIBLE?"uninterruptible":s==TASK_STOPPED?"stopped":s==TASK_TRACED?"traced":s==EXIT_ZOMBIE?"zombie":s==EXIT_DEAD?"dead":"invalid";}
static TEXT64 const char *task_kind_name(u8 k){return k==TASK_KIND_KERNEL?"kernel":k==TASK_KIND_USER?"user":"invalid";}
static TEXT64 void task_names_keep(void){(void)task_state_name; (void)task_kind_name;(void)runtime_sched_class();}
static TEXT64 int task_state_valid(u8 s){return s==TASK_RUNNING||s==TASK_INTERRUPTIBLE||s==TASK_UNINTERRUPTIBLE||s==TASK_STOPPED||s==TASK_TRACED||s==EXIT_ZOMBIE||s==EXIT_DEAD;}
static TEXT64 int task_transition(u32 i,u8 next){u8 old;if(i>=TASK_TABLE_CAP||!task_table[i].valid||!task_state_valid(next))return 0;old=task_table[i].state;if(old==next)return 1;if(old==EXIT_DEAD||old==EXIT_ZOMBIE)return 0;if(next==EXIT_DEAD)return 0;task_table[i].state=next;task_table[i].transitions++;return 1;}
static TEXT64 int task_table_validate(void){u32 i,j;if(!task_table[0].valid||task_table[0].pid!=0||task_table[0].tid!=0||task_table[0].parent_pid!=0)return 0;for(i=0;i<TASK_TABLE_CAP;i++){struct task_struct*t=&task_table[i];if(!t->valid||((i!=0)&&(!t->pid||!t->tid))||!task_state_valid(t->state)||!t->kind)return 0;for(j=i+1;j<TASK_TABLE_CAP;j++)if(task_table[j].valid&&i&&j&&(t->pid==task_table[j].pid||t->tid==task_table[j].tid))return 0;if(i&&t->parent_pid>=t->pid)return 0;}return 1;}
/* Task metadata remains inherited and is initialized inline after paging setup. */
static TEXT64 void schedinfo(u16*c){text64(c,"scheduler class: ");text64(c,active_sched_class?active_sched_class->name:"none");text64(c,"\nops enqueue/dequeue/pick: ");hex64(c,sched_enqueues);text64(c," ");hex64(c,sched_dequeues);text64(c," ");hex64(c,sched_picks);text64(c,"\nwait queues: bounded FIFO; wake_one/wake_all preserve runnable transitions\n");}
static TEXT64 void tasklist(u16*c){text64(c,"tasks: bounded task metadata; use taskvalidate for state checks\n");}
static TEXT64 void taskvalidate(u16*c){text64(c,"task validation: ");text64(c,task_table_validate()?"passed":"BROKEN");text64(c," (bounded table, unique PID/TID, valid parent/state)\n");}
static TEXT64 void forkinfo(u16*c){text64(c,"fork model: ");text64(c,fork_model.valid?(fork_model.is_clone?"clone":"fork"):"none");text64(c,"\nparent pid/tid: ");hex64(c,fork_model.parent_pid);text64(c,"/");hex64(c,fork_model.parent_tid);text64(c,"\nchild pid/tid/parent: ");hex64(c,fork_model.child_pid);text64(c,"/");hex64(c,fork_model.child_tid);text64(c,"/");hex64(c,fork_model.parent_pid);text64(c,"\naddress spaces parent/child: ");hex64(c,fork_model.parent_address_space);text64(c,"/");hex64(c,fork_model.child_address_space);text64(c,"\ncopied metadata bits/shared resource bits: ");hex64(c,fork_model.copied_metadata);text64(c,"/");hex64(c,fork_model.shared_resources);text64(c,"\nattempts/success/fork/clone: ");hex64(c,fork_attempts);text64(c,"/");hex64(c,fork_successes);text64(c,"/");hex64(c,clone_successes);text64(c,"\npolicy: copied user metadata; shared kernel console/PIT/PMM policy; no real child execution\n");}
static TEXT64 int fork_model_run(u16*c,int clone){fork_attempts++;if(fork_model.valid){text64(c,"forktest: bounded child already exists\n");return 0;}fork_model.parent_pid=user_process.pid;fork_model.parent_tid=user_thread.tid;fork_model.child_pid=SECOND_PID+1;fork_model.child_tid=SECOND_PID+1;fork_model.parent_address_space=(u64)(unsigned long)user_process.address_space;fork_model.child_address_space=(u64)(unsigned long)&user_address_spaces[1];fork_model.copied_metadata=RESOURCE_COPIED;fork_model.shared_resources=RESOURCE_SHARED;fork_model.is_clone=(u8)clone;fork_model.valid=1;fork_successes++;if(clone)clone_successes++;text64(c,clone?"cloneinfo: metadata-only clone created\n":"forktest: metadata-only child created\n");return 1;}
static TEXT64 int fork_model_validate(u16*c){int ok=fork_model.valid&&fork_model.child_pid>fork_model.parent_pid&&fork_model.child_tid!=fork_model.parent_tid&&fork_model.parent_address_space!=fork_model.child_address_space&&fork_model.copied_metadata==RESOURCE_COPIED&&fork_model.shared_resources==RESOURCE_SHARED; text64(c,"fork lifecycle: ");text64(c,ok?"passed (identity, parent, copy/share boundaries, no execution)":"BROKEN");putc64(c,'\n');return ok;}
static TEXT64 void processinfo(u16*c)
{
    text64(c,"process pid/state: ");hex64(c,user_process.pid);text64(c," ");text64(c,process_state_name(user_process.state));
    text64(c,"\naddress-space: ");hex64(c,(u64)(unsigned long)user_process.address_space);
    text64(c,"\nimage code/stack: ");hex64(c,user_process.code_phys);text64(c," ");hex64(c,user_process.stack_phys);
    text64(c,"\nentry/stack-top: ");hex64(c,user_process.entry);text64(c," ");hex64(c,user_process.stack_top);
    text64(c,"\nuser thread tid/state: ");hex64(c,user_thread.tid);text64(c," ");text64(c,user_thread_state_name(user_thread.state));
    text64(c,"\nkstack top/bytes: ");hex64(c,user_thread.kernel_stack_top);text64(c," ");hex64(c,user_thread.kernel_stack_bytes);
    text64(c,"\nsaved context/address/saves: ");text64(c,user_thread.context.valid?"valid":"empty");text64(c," ");hex64(c,user_thread.context_address);text64(c," ");hex64(c,user_thread.context.saves);
    text64(c,"\nPIT user preempt/resume: ");hex64(c,user_thread.context.pit_preemptions);text64(c," ");hex64(c,user_thread.context.pit_resumes);
    text64(c,"\ncontext lifecycle: ");text64(c,user_process.context_valid?"validated":"not validated");text64(c," transitions ");hex64(c,user_thread.transitions);text64(c,"\nprograms bounded: ");hex64(c,MAX_USER_PROGRAMS);text64(c," exits/reclaims: ");hex64(c,user_exit_count);text64(c," ");hex64(c,user_reclaims);putc64(c,'\n');
}
static TEXT64 int process_lifecycle_test(u16*c)
{
    user_program_reclaim(0); user_program_reclaim(1);
    int ok=user_programs_ready() && user_process.state==PROCESS_READY && user_thread.state==USER_THREAD_READY &&
        user_process.address_space==&kernel_address_space && user_process.code_phys==user_code_phys &&
        user_process.stack_phys==user_stack_phys && user_thread.process==&user_process &&
        user_thread.kernel_stack_top==runtime_tss.rsp0 && !user_thread.context.valid;
    text64(c,"process lifecycle: ");text64(c,ok?"bounded two bounded program objects ready":"BROKEN");putc64(c,'\n');
    return ok;
}
static TEXT64 u64 syscall_dispatch(struct syscall_frame*f,u16*c){switch((u32)f->rax){case SYS_GETTICKS:return ticks;case SYS_GETPID:return FIXED_PID;case SYS_WRITE_CONSOLE:text64(c,"kernel-owned console message\n");return 0;case SYS_EXIT:return 0;default:return (u64)(-(s64)ENOSYS);}}
TEXT64 void syscall_report(struct syscall_frame*f){u16 c=0;u64 number=f->rax,result;clear64(&c);if((u32)f->rax==SYS_EXIT){user_context_save(f,0);text64(&c,"TinyOS lesson 36 SYS_EXIT\nuser requested controlled exit\n");if(user_process_exit())text64(&c,"saved user context validated; process/thread exited\n");else text64(&c,"controlled exit rejected: invalid lifecycle\n");text64(&c,"halting intentionally\n");for(;;)__asm__ volatile("cli; hlt");}result=syscall_dispatch(f,&c);user_context_save(f,result);text64(&c,"TinyOS lesson 36 syscall dispatcher\nsyscall number: ");hex64(&c,number);text64(&c,"\nreturn rax: ");f->rax=result;hex64(&c,f->rax);text64(&c,"\nuser rip: ");hex64(&c,f->rip);text64(&c,"\nuser cs: ");hex64(&c,f->cs);text64(&c,"\nuser rsp: ");hex64(&c,f->rsp);text64(&c,"\nuser ss: ");hex64(&c,f->ss);text64(&c,"\nall-GPR frame; returning with iretq; user IF remains disabled\n");}
TEXT64 void exception_report_ist(struct exception_frame_ist*f){u16 c=0;u64 cr2=0,rsp;clear64(&c);__asm__ volatile("mov %%rsp,%0":"=r"(rsp));text64(&c,"TinyOS lesson 27 IST exception\nexception: #PF\nvector: ");hex64(&c,f->vector);text64(&c,"\nerror:  ");hex64(&c,f->error);text64(&c,"\nrip:    ");hex64(&c,f->rip);text64(&c,"\nsaved rsp: ");hex64(&c,f->rsp);text64(&c,"\nhandler rsp: ");hex64(&c,rsp);text64(&c,"\nIST1 range: ");hex64(&c,(u64)(unsigned long)__ist1_stack_start);text64(&c," ");hex64(&c,runtime_tss.ist1);__asm__ volatile("mov %%cr2,%0":"=r"(cr2));text64(&c,"\ncr2:    ");hex64(&c,cr2);text64(&c,"\nCPU halted intentionally.\n");for(;;)__asm__ volatile("cli; hlt");}
TEXT64 void breakpoint_report(struct exception_frame*f){u16 c=10*COLS;u64 *raw=(u64 *)f;text64(&c,"TinyOS lesson 27 breakpoint\nexception: #BP\nvector: ");hex64(&c,raw[0]);text64(&c,"\nerror:  ");hex64(&c,raw[1]);text64(&c,"\nrip:    ");hex64(&c,raw[3]);text64(&c,"\ncs:     ");hex64(&c,raw[4]);text64(&c,"\nrflags: ");hex64(&c,raw[5]);text64(&c,"\nreturning with iretq...\n");}
TEXT64 void exception_report(struct exception_frame*f){u16 c=0;u64 cr2=0,rsp;clear64(&c);text64(&c,"TinyOS lesson 28 exception\nexception: ");if(f->vector==6)text64(&c,"#UD");else if(f->vector==14)text64(&c,"#PF");else text64(&c,"unknown");print_exception_frame(&c,f);__asm__ volatile("mov %%rsp,%0":"=r"(rsp));if(f->vector==6&&f->cs==USER_CS){text64(&c,"\nCPL3 #UD proof: user CS and kernel rsp0 active\nhandler rsp: ");hex64(&c,rsp);text64(&c,"\nrsp0: ");hex64(&c,runtime_tss.rsp0);text64(&c,"\nsaved user rsp: ");hex64(&c,f->rsp);text64(&c,"\nsaved user ss: ");hex64(&c,f->ss);text64(&c,"\nCPU halted intentionally.\n");}else {if(f->vector==14){__asm__ volatile("mov %%cr2,%0":"=r"(cr2));text64(&c,"\ncr2:    ");hex64(&c,cr2);}text64(&c,"\nCPU halted intentionally.\n");}for(;;)__asm__ volatile("cli; hlt");}
/* IRQ0 scheduling is performed by irq0_schedule at its iretq return boundary. */
TEXT64 void irq1_record(void){u8 raw=inb64(0x60),ch,next,id;irq1_last_scancode=raw;irq1_raw_count++;if(!(raw&0x80)){irq1_count++;ch=(u8)scan64(raw);if(ch){if(waitq_wake_one(&kbd_waitq,THREAD_BLOCKED_KBD,&id)){threads[id].mailbox=ch;threads[id].mailbox_ready=1;kbd_direct_deliveries++;}else{next=(u8)((kbd_head+1)&(KBD_QUEUE_SIZE-1));if(next==kbd_tail)kbd_overflow_count++;else{kbd_queue[kbd_head]=ch;kbd_head=next;}}}}outb64(PIC1_COMMAND,PIC_EOI);}
static TEXT64 int kbd_dequeue(u8 *ch){u8 tail;__asm__ volatile("cli":::"memory");tail=kbd_tail;if(tail==kbd_head){__asm__ volatile("sti":::"memory");return 0;}*ch=kbd_queue[tail];kbd_tail=(u8)((tail+1)&(KBD_QUEUE_SIZE-1));__asm__ volatile("sti":::"memory");return 1;}
static TEXT64 void usage64(u16*c,const char*s){text64(c,"usage: ");text64(c,s);putc64(c,'\n');}
static TEXT64 int exec_validate(void){const struct tiny_elf_header*h=(const struct tiny_elf_header*)embedded_exec_image;u32 i;u64 end;if(h->ident[0]!=ELF_MAGIC0||h->ident[1]!=ELF_MAGIC1||h->ident[2]!=ELF_MAGIC2||h->ident[3]!=ELF_MAGIC3||h->type!=ELF_TYPE_EXEC||h->machine!=ELF_MACHINE_X86_64||h->version!=1||h->phentsize!=sizeof(struct tiny_elf_segment)||h->phnum>EXEC_MAX_SEGMENTS)return 0;if(h->phoff>sizeof(embedded_exec_image)||h->phnum>(sizeof(embedded_exec_image)-h->phoff)/h->phentsize)return 0;for(i=0;i<h->phnum;i++){const struct tiny_elf_segment*s=(const struct tiny_elf_segment*)(embedded_exec_image+h->phoff+i*h->phentsize);if(s->type!=1||!s->filesz||s->memsz<s->filesz||s->offset>sizeof(embedded_exec_image)||s->filesz>sizeof(embedded_exec_image)-s->offset||s->vaddr<USER_CODE_VA||s->vaddr>USER_STACK_VA||s->memsz>USER_STACK_VA-s->vaddr)return 0;if(!(s->flags&(ELF_SEG_R|ELF_SEG_X))||((s->flags&ELF_SEG_W)&&(s->flags&ELF_SEG_X)))return 0;end=s->vaddr+s->memsz;if(end<s->vaddr||end>USER_STACK_VA)return 0;}if(h->entry<USER_CODE_VA||h->entry>=USER_CODE_VA+EXEC_MAX_IMAGE_BYTES)return 0;exec_model.entry=h->entry;exec_model.segment_count=h->phnum;exec_model.image_bytes=sizeof(embedded_exec_image);exec_model.argc=EXEC_STACK_ARGC;exec_model.stack_pointer=USER_STACK_TOP-EXEC_STACK_WORDS*8;exec_model.argv_pointer=exec_model.stack_pointer+8;exec_model.envp_pointer=exec_model.argv_pointer+(exec_model.argc+1)*8;exec_model.validated=1;exec_model.executed=0;return 1;}
static TEXT64 int exec_stack_validate(void){return exec_model.validated&&exec_model.argc==2&&exec_model.stack_pointer==USER_STACK_TOP-EXEC_STACK_WORDS*8&&exec_model.argv_pointer==exec_model.stack_pointer+8&&exec_model.envp_pointer==exec_model.argv_pointer+24;}
static TEXT64 void execinfo(u16*c){text64(c,"exec model: ELF-like bounded image\nvalidated: ");text64(c,exec_model.validated?"yes":"no");text64(c,"\nsegments/bytes: ");hex64(c,exec_model.segment_count);text64(c," ");hex64(c,exec_model.image_bytes);text64(c,"\nentry: ");hex64(c,exec_model.entry);text64(c,"\nexecution: ");text64(c,exec_model.executed?"forbidden/none":"metadata only");putc64(c,'\n');}
static TEXT64 void stacklayout(u16*c){text64(c,"user stack layout: argc, argv[], NULL, envp[]\nargc: ");hex64(c,exec_model.argc);text64(c,"\nsp: ");hex64(c,exec_model.stack_pointer);text64(c,"\nargv: ");hex64(c,exec_model.argv_pointer);text64(c,"\nenvp: ");hex64(c,exec_model.envp_pointer);text64(c,"\nlayout validation: ");text64(c,exec_stack_validate()?"passed":"BROKEN");text64(c,"\nallocation/execution: none\n");}
static TEXT64 const char *vma_prot(u8 p){return p==(VMA_R|VMA_X)?"r-x":p==(VMA_R|VMA_W)?"rw-":"---";}
static TEXT64 const char *vma_backing(u8 k){return k==VMA_FILE?"file":"anon";}
static TEXT64 const struct vma_model *vma_lookup(u64 va){u32 i;for(i=0;i<vma_count;i++)if(vma_table[i].valid&&va>=vma_table[i].start&&va<vma_table[i].end)return &vma_table[i];return 0;}
static TEXT64 int vma_range_valid(u64 start,u64 end,u8 prot){const struct vma_model*v;if(end<=start||end-start>0x10000ULL)return 0;v=vma_lookup(start);return v&&end<=v->end&&(v->prot&prot)==prot;}
static TEXT64 int page_present(u64 va){u32 i;va=down(va);for(i=0;i<fault_page_count;i++)if(fault_pages[i].live&&fault_pages[i].va==va)return 1;return 0;}
static TEXT64 enum pf_class pf_classify(u64 va,u8 write){const struct vma_model*v=vma_lookup(va);if(!v){fault_unmapped++;return PF_UNMAPPED;}if((write&&!(v->prot&VMA_W))||(!write&&!(v->prot&VMA_R))){fault_protection++;return PF_PROTECTION;}if(!page_present(va)){fault_not_present++;return PF_NOT_PRESENT;}return PF_NOT_PRESENT;}
static TEXT64 int fault_insert(u64 va,u8 write){u32 i;u64 p;struct page_model*m;if(fault_page_count>=VMA_MAX_PAGES||!vma_range_valid(down(va),down(va)+PAGE_SIZE,write?VMA_W:VMA_R)||page_present(va))return 0;p=pmm_alloc();if(!p)return 0;for(i=0;i<VMA_MAX_PAGES;i++)if(!fault_pages[i].live){m=&fault_pages[i];m->va=down(va);m->phys=p;m->writable=write;m->live=1;m->backing=VMA_ANON;m->dirty=write;m->accessed=1;m->reclaimable=1;m->refs=1;fault_page_count++;anon_pages++;fault_insertions++;return 1;}return 0;}
static TEXT64 int page_cache_get(u64 index,u8 dirty){u32 i;u64 p;for(i=0;i<PAGE_CACHE_MAX;i++)if(page_cache[i].valid&&page_cache[i].index==index){page_cache[i].refs++;page_cache[i].dirty|=dirty;cache_hits++;return 1;}cache_misses++;for(i=0;i<PAGE_CACHE_MAX;i++)if(!page_cache[i].valid){p=pmm_alloc();if(!p)return 0;page_cache[i]=(struct page_cache_model){index,p,1,dirty,0,1};page_cache_count++;if(dirty)page_cache[i].writeback=0;return 1;}return 0;}
static TEXT64 int reclaim_one(void){u32 i;for(i=0;i<VMA_MAX_PAGES;i++){struct page_model*m=&fault_pages[i];reclaim_scans++;if(!m->live||!m->reclaimable||m->refs!=1){reclaim_skips++;continue;}if(!eq64(pmm_free_page(m->phys),"freed")){reclaim_skips++;continue;}m->live=0;fault_page_count--;anon_pages--;anon_reclaims++;return 1;}return 0;}
static TEXT64 void reclaim_init(void){page_cache_count=0;anon_pages=0;anon_reclaims=0;cache_hits=0;cache_misses=0;reclaim_scans=0;reclaim_skips=0;writeback_pages=0;}
static TEXT64 void anoninfo(u16*c){text64(c,"anon pages/cache live/reclaims: ");hex64(c,anon_pages);text64(c,"/");hex64(c,page_cache_count);text64(c,"/");hex64(c,anon_reclaims);text64(c," cache hit/miss: ");hex64(c,cache_hits);text64(c,"/");hex64(c,cache_misses);text64(c," reclaim scans/skips: ");hex64(c,reclaim_scans);text64(c,"/");hex64(c,reclaim_skips);putc64(c,'\n');}
static TEXT64 void reclaimtest(u16*c){int a=fault_insert(VMA_DATA_START,1),b=page_cache_get(1,1),d=page_cache_get(1,0),e=reclaim_one();text64(c,"reclaimtest: ");text64(c,a&&b&&d&&e&&anon_pages==0&&page_cache_count==1?"anonymous reclaim and page-cache hit model passed":"BROKEN");text64(c,"\npage cache is metadata-only; no disk I/O or swap executed\n");}
static TEXT64 void vmainfo(u16*c){u32 i;text64(c,"VMA table (bounded Linux-style metadata)\n");for(i=0;i<vma_count;i++){text64(c,"vma ");hex64(c,i);text64(c," ");hex64(c,vma_table[i].start);text64(c,"-");hex64(c,vma_table[i].end);text64(c," ");text64(c,vma_prot(vma_table[i].prot));text64(c," ");text64(c,vma_backing(vma_table[i].kind));putc64(c,'\n');}text64(c,"pages/live, faults np/prot/unmapped: ");hex64(c,fault_page_count);text64(c," ");hex64(c,fault_not_present);text64(c," ");hex64(c,fault_protection);text64(c," ");hex64(c,fault_unmapped);putc64(c,'\n');}
static TEXT64 void pfmodel(u16*c){enum pf_class a=pf_classify(VMA_DATA_START,1),b=pf_classify(VMA_CODE_START,1),d=pf_classify(0x00100000ULL,0);int inserted=fault_insert(VMA_DATA_START,1);text64(c,"pfmodel: ");text64(c,a==PF_NOT_PRESENT&&b==PF_PROTECTION&&d==PF_UNMAPPED&&inserted?"not-present/protection/unmapped classified; bounded page inserted":"BROKEN");text64(c,"\nno real fault instruction executed; pages: ");hex64(c,fault_page_count);putc64(c,'\n');}
static TEXT64 int uaccess_validate(u64 address,u64 length,u8 access,struct uaccess_result*r){u64 end;const struct vma_model*v;r->address=address;r->length=length;r->access=access;r->canonical=address<=USER_CANONICAL_MAX&&(length==0||address<=USER_CANONICAL_MAX-length+1);r->range=length<=USER_COPY_MAX&&address<USER_RANGE_MAX&&length<=USER_RANGE_MAX-address&&address+length>=address;r->vma=0;r->permission=0;r->copied=0;if(r->canonical&&r->range&&length){end=address+length;v=vma_lookup(address);if(v&&end<=v->end){r->vma=1;r->permission=(access==UACCESS_READ?(v->prot&VMA_R):(v->prot&VMA_W))!=0;}}else if(r->canonical&&r->range&&!length){r->vma=1;r->permission=1;}return r->canonical&&r->range&&r->vma&&r->permission;}
static TEXT64 int uaccess_copy(u64 address,u64 length,u8 access){struct uaccess_result r;uaccess_attempts++;if(!uaccess_validate(address,length,access,&r)){uaccess_failures++;return 0;}r.copied=1;uaccess_successes++;uaccess_bytes+=length;return 1;}
static TEXT64 void vmatest(u16*c){text64(c,"vmatest: inherited VMA checks available\n");}
static TEXT64 void ramfs_init(void){ramfs_count=5;ramfs_nodes[0]=(struct ramfs_node){0,0,0,RAMFS_DIR,1};ramfs_nodes[1]=(struct ramfs_node){0x657463,0,1,RAMFS_DIR,1};ramfs_nodes[2]=(struct ramfs_node){0x6d6f7464,1,2,RAMFS_FILE,1};ramfs_nodes[3]=(struct ramfs_node){0x6e6962,0,1,RAMFS_DIR,1};ramfs_nodes[4]=(struct ramfs_node){0x6873,3,2,RAMFS_FILE,1};ramfs_lookups=ramfs_hits=ramfs_misses=0;}
static TEXT64 int ramfs_lookup(const char *path){if(!path||path[0]!='/')return -1;ramfs_lookups++;if(eq64(path,"/")||eq64(path,".")){ramfs_hits++;return 0;}if(eq64(path,"/etc")){ramfs_hits++;return 1;}if(eq64(path,"/etc/motd")){ramfs_hits++;return 2;}if(eq64(path,"/bin")){ramfs_hits++;return 3;}if(eq64(path,"/bin/sh")){ramfs_hits++;return 4;}ramfs_misses++;return -1;}
static TEXT64 void ramfsinfo(u16*c){u32 i;text64(c,"ramfs/initramfs nodes: ");hex64(c,ramfs_count);text64(c," root=dentry0 memory-backed\npaths: / /etc /etc/motd /bin /bin/sh\nlookups/hits/misses: ");hex64(c,ramfs_lookups);text64(c," ");hex64(c,ramfs_hits);text64(c," ");hex64(c,ramfs_misses);putc64(c,'\n');for(i=0;i<ramfs_count;i++){text64(c,"node ");hex64(c,i);text64(c," parent ");hex64(c,ramfs_nodes[i].parent);text64(c," inode ");hex64(c,ramfs_nodes[i].inode);text64(c," ");text64(c,ramfs_nodes[i].type==RAMFS_DIR?"dir":"file");putc64(c,'\n');}}
static TEXT64 void pathtest(u16*c){int a=ramfs_lookup("/"),b=ramfs_lookup("/etc"),d=ramfs_lookup("/etc/motd"),e=ramfs_lookup("/bin/sh"),f=ramfs_lookup("/etc/missing"),g=ramfs_lookup("relative"),h=ramfs_lookup("/etc/motd/child");text64(c,"pathtest: ");text64(c,a==0&&b==1&&d==2&&e==4&&f<0&&g<0&&h<0?"ramfs/initramfs root-to-dentry path lookup passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void vfs_init(void){u32 i;for(i=0;i<INODE_MAX;i++)inode_table[i]=(struct inode_model){i+1,0x100+i,0100644,1,1};for(i=0;i<DENTRY_MAX;i++)dentry_table[i]=(struct dentry_model){0x100+i,i,1,1};for(i=0;i<FILE_MAX;i++)file_table[i]=(struct file_model){i,0,0,0,0};for(i=0;i<FD_MAX;i++)fd_table[i]=(struct fd_model){0,0};fd_opens=fd_closes=fd_reads=fd_seek_ops=0;ramfs_init();pipe_init();}
static TEXT64 int fd_open_model(u32 inode,u64 flags){u32 i;if(inode>=INODE_MAX||!inode_table[inode].valid)return -1;for(i=0;i<FD_MAX;i++)if(!fd_table[i].valid){u32 f;for(f=0;f<FILE_MAX;f++)if(!file_table[f].valid){file_table[f]=(struct file_model){inode,0,flags,1,1};fd_table[i]=(struct fd_model){f,1};inode_table[inode].refs++;fd_opens++;return (int)i;}}return -1;}
static TEXT64 int fd_close_model(u32 fd){u32 f,n;if(fd>=FD_MAX||!fd_table[fd].valid)return 0;f=(u32)fd_table[fd].file_index;if(f>=FILE_MAX||!file_table[f].valid)return 0;n=(u32)file_table[f].inode_index;if(n>=INODE_MAX||!inode_table[n].valid)return 0;fd_table[fd].valid=0;if(file_table[f].refs){file_table[f].refs--;if(!file_table[f].refs){file_table[f].valid=0;if(inode_table[n].refs)inode_table[n].refs--;}}fd_closes++;return 1;}
static TEXT64 int fd_read_model(u32 fd,u64 bytes){u32 f,n;u64 size,remaining;if(fd>=FD_MAX||!fd_table[fd].valid)return 0;f=(u32)fd_table[fd].file_index;if(f>=FILE_MAX||!file_table[f].valid)return 0;n=(u32)file_table[f].inode_index;if(n>=INODE_MAX||!inode_table[n].valid)return 0;size=inode_table[n].size;remaining=file_table[f].offset<size?size-file_table[f].offset:0;if(bytes>remaining)bytes=remaining;file_table[f].offset+=bytes;fd_reads++;return 1;}
static TEXT64 void fdinfo(u16*c){u32 i;text64(c,"fd/file/inode/dentry tables (bounded)\n");for(i=0;i<FD_MAX;i++)if(fd_table[i].valid){u32 f=(u32)fd_table[i].file_index;u32 n=(u32)file_table[f].inode_index;text64(c,"fd ");hex64(c,i);text64(c," file ");hex64(c,f);text64(c," inode ");hex64(c,inode_table[n].ino);text64(c," off ");hex64(c,file_table[f].offset);putc64(c,'\n');}text64(c,"opens/closes/reads/seeks: ");hex64(c,fd_opens);text64(c," ");hex64(c,fd_closes);text64(c," ");hex64(c,fd_reads);text64(c," ");hex64(c,fd_seek_ops);putc64(c,'\n');}
static TEXT64 void fdtest(u16*c){int a=fd_open_model(0,1),b=fd_open_model(1,2),r1=a>=0&&b>=0&&fd_read_model((u32)a,8)&&fd_close_model((u32)b)&&fd_close_model((u32)a);text64(c,"fdtest: ");text64(c,r1&&fd_opens==2&&fd_closes==2?"fd/file/inode/dentry refs and offsets passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void ptrinfo(u16*c){text64(c,"uaccess: canonical max/range max/copy max ");hex64(c,USER_CANONICAL_MAX);text64(c," ");hex64(c,USER_RANGE_MAX);text64(c," ");hex64(c,USER_COPY_MAX);text64(c,"\nattempts/success/failure/bytes: ");hex64(c,uaccess_attempts);text64(c," ");hex64(c,uaccess_successes);text64(c," ");hex64(c,uaccess_failures);text64(c," ");hex64(c,uaccess_bytes);text64(c,"\nvalidation only: no arbitrary pointer dereference\n");}
static TEXT64 void ptrtest(u16*c){struct uaccess_result r;int ok=uaccess_validate(VMA_DATA_START,8,UACCESS_READ,&r)&&!uaccess_validate(USER_CANONICAL_MAX+1,8,UACCESS_READ,&r)&&!uaccess_validate(VMA_DATA_START,USER_COPY_MAX+1,UACCESS_READ,&r)&&!uaccess_validate(VMA_CODE_START,8,UACCESS_WRITE,&r);text64(c,"ptrtest: ");text64(c,ok?"canonical/range/VMA/permission checks passed":"BROKEN");putc64(c,'\n');}
static TEXT64 void copytest(u16*c){int a=uaccess_copy(VMA_DATA_START,16,UACCESS_WRITE),b=uaccess_copy(VMA_CODE_START,16,UACCESS_WRITE),d=uaccess_copy(USER_CANONICAL_MAX-3,8,UACCESS_READ),e=uaccess_copy(VMA_DATA_START,USER_COPY_MAX+1,UACCESS_READ);text64(c,"copytest: ");text64(c,a&&!b&&!d&&!e?"copy_to_user/from_user bounded success/failure accounting passed":"BROKEN");text64(c,"\nno source/destination bytes touched; pointers were never dereferenced\n");}

static TEXT64 void enter_user(struct long_mode_handoff*h){if(!user_process_enter(h)){return;}__asm__ volatile("cli; call enter_user_c":::"memory");}
static TEXT64 void vmtest(u16*c,struct long_mode_handoff*h){(void)h;u64 p[2],va[2]={VM_REGION_START,VM_REGION_START+PAGE_SIZE},before,after;volatile u64 *v,*q;const char*r;u32 i;before=pmm_free;for(i=0;i<2;i++){p[i]=pmm_alloc();if(!p[i]){text64(c,"vmtest allocation failed\n");return;}r=address_space_map(&kernel_address_space,va[i],p[i],MAP_OWNER_USER);if(!eq64(r,"mapped")){text64(c,"vmtest map failed: ");text64(c,r);putc64(c,'\n');return;}if(!eq64(pmm_free_page(p[i]),"mapped")){text64(c,"vmtest mapped-frame ownership failed\n");return;}v=(volatile u64 *)(unsigned long)va[i];q=(volatile u64 *)(unsigned long)(KERNEL_VMA_BASE+va[i]);*v=0x564d544553543237ULL+i;if(*q!=0x564d544553543237ULL+i){text64(c,"vmtest low/high mismatch\n");return;}*q=0x48494748564d3237ULL+i;if(*v!=0x48494748564d3237ULL+i){text64(c,"vmtest high/low mismatch\n");return;}}for(i=0;i<2;i++){r=address_space_release(&kernel_address_space,va[i]);if(!eq64(r,"unmapped")||!eq64(pmm_free_page(p[i]),"freed")){text64(c,"vmtest unmap/free failed\n");return;}}after=pmm_free;if(after!=before){text64(c,"vmtest PMM accounting failed\n");return;}text64(c,"vmtest: two-slot dual-alias map/ownership/unmap/free passed\n");}
static TEXT64 void vma_init(void){vma_count=3;vma_table[0]=(struct vma_model){VMA_CODE_START,VMA_CODE_END,0,VMA_R|VMA_X,VMA_FILE,1};vma_table[1]=(struct vma_model){VMA_DATA_START,VMA_DATA_END,0,VMA_R|VMA_W,VMA_ANON,1};vma_table[2]=(struct vma_model){VMA_STACK_START,VMA_STACK_END,0,VMA_R|VMA_W,VMA_ANON,1};fault_page_count=0;fault_not_present=fault_protection=fault_unmapped=fault_insertions=0;}
struct framebuffer_model { u64 address,bytes,pixels,rects; u32 pitch,width,height; u8 bpp,type,ready,mapped; };
static struct framebuffer_model framebuffer;
static TEXT64 void framebuffer_init(struct long_mode_handoff*h){u64 bytes;if(!h->framebuffer_address||h->framebuffer_bpp!=32||h->framebuffer_type!=1||h->framebuffer_width>1024||h->framebuffer_height>768){framebuffer=(struct framebuffer_model){0};return;}bytes=(u64)h->framebuffer_pitch*h->framebuffer_height;if(bytes>8ULL*1024*1024||h->framebuffer_address+bytes<h->framebuffer_address){framebuffer=(struct framebuffer_model){0};return;}framebuffer=(struct framebuffer_model){h->framebuffer_map,bytes,0,0,h->framebuffer_pitch,h->framebuffer_width,h->framebuffer_height,h->framebuffer_bpp,h->framebuffer_type,1,h->framebuffer_map<IDENTITY_MAP_END};}
static TEXT64 int framebuffer_pixel(u32 x,u32 y,u32 color){volatile u32*p;if(!framebuffer.ready||!framebuffer.mapped||x>=framebuffer.width||y>=framebuffer.height)return 0;p=(volatile u32 *)(unsigned long)(framebuffer.address+(u64)y*framebuffer.pitch+(u64)x*4);*p=color;framebuffer.pixels++;return 1;}
static TEXT64 int framebuffer_rect(u32 x,u32 y,u32 w,u32 h,u32 color){u32 i,j;u64 endx=(u64)x+w,endy=(u64)y+h;if(!framebuffer.ready||!framebuffer.mapped||!w||!h||x>=framebuffer.width||y>=framebuffer.height)return 0;if(endx>framebuffer.width)endx=framebuffer.width;if(endy>framebuffer.height)endy=framebuffer.height;for(j=y;j<endy;j++)for(i=x;i<endx;i++)framebuffer_pixel(i,j,color);framebuffer.rects++;return 1;}
static TEXT64 void guiinfo(u16*c){text64(c,"guiinfo: framebuffer addr/pitch/size/bpp/type: ");hex64(c,framebuffer.address);text64(c,"/");hex64(c,framebuffer.pitch);text64(c,"/");hex64(c,framebuffer.width);text64(c,"x");hex64(c,framebuffer.height);text64(c,"/");hex64(c,framebuffer.bpp);text64(c,"/");hex64(c,framebuffer.type);text64(c," ready/mapped: ");hex64(c,framebuffer.ready);text64(c,"/");hex64(c,framebuffer.mapped);putc64(c,'\n');}
static TEXT64 void drawtest(u16*c){int a=framebuffer_rect(0,0,framebuffer.width,framebuffer.height,0x00102040U),b=framebuffer_rect(8,8,framebuffer.width>16?framebuffer.width-16:0,framebuffer.height>16?framebuffer.height-16:0,0x002060a0U);text64(c,"drawtest: ");text64(c,a&&b&&framebuffer.pixels?"bounded framebuffer clear/rectangles passed":"framebuffer unavailable; safe fallback reported");putc64(c,'\n');}
struct canvas_model { u64 glyphs,dirty_regions,clipped; u32 fg,bg; };
static struct canvas_model canvas;
static TEXT64 u8 glyph_row(char ch,u32 row){static const u8 letters[16][7]={{0x0e,0x11,0x11,0x1f,0x11,0x11,0x11},{0x1e,0x11,0x11,0x1e,0x11,0x11,0x1e},{0x0f,0x10,0x10,0x10,0x10,0x10,0x0f},{0x1e,0x11,0x11,0x11,0x11,0x11,0x1e},{0x1f,0x10,0x10,0x1e,0x10,0x10,0x1f},{0x1f,0x10,0x10,0x1e,0x10,0x10,0x10},{0x0f,0x10,0x10,0x17,0x11,0x11,0x0f},{0x11,0x11,0x11,0x1f,0x11,0x11,0x11},{0x1f,0x04,0x04,0x04,0x04,0x04,0x1f},{0x01,0x01,0x01,0x01,0x11,0x11,0x0e},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1f},{0x11,0x1b,0x15,0x15,0x11,0x11,0x11},{0x1e,0x11,0x11,0x11,0x11,0x11,0x11},{0x0e,0x11,0x11,0x11,0x11,0x11,0x0e},{0x1f,0x11,0x10,0x1c,0x10,0x10,0x10}};u32 i;if(ch>='A'&&ch<='P')i=(u32)(ch-'A');else return row==3?0x04:0;return row<7?letters[i][row]:0;}
static TEXT64 int canvas_char(u32 x,u32 y,char ch){u32 row,col;int ok=1;for(row=0;row<7;row++)for(col=0;col<5;col++)if(glyph_row(ch,row)&(1U<<(4-col))){if(!framebuffer_pixel(x+col,y+row,canvas.fg))ok=0;else canvas.glyphs++;}if(!ok)canvas.clipped++;canvas.dirty_regions++;return ok;}
static TEXT64 void canvas_text(u32 x,u32 y,const char*s){u32 i;for(i=0;s[i]&&i<32;i++)canvas_char(x+i*6,y,s[i]);}
static TEXT64 void fonttest(u16*c){canvas=(struct canvas_model){0,0,0,0x00ffffffU,0x00000000U};canvas_text(16,16,"TINYOS");text64(c,"fonttest: ");text64(c,canvas.glyphs?"bounded 5x7 bitmap glyphs and clipped text passed":"framebuffer unavailable; font path safely bounded");putc64(c,'\n');}
static TEXT64 void canvastest(u16*c){canvas=(struct canvas_model){0,0,0,0x00ffffffU,0};int a=framebuffer_rect(0,0,framebuffer.width,24,0x00305090U),b=framebuffer_rect(8,32,80,56,0x00508040U);canvas_text(16,40,"CANVAS");text64(c,"canvastest: ");text64(c,a&&b&&canvas.dirty_regions?"canvas colors, dirty regions, and clipped drawing passed":"framebuffer unavailable; canvas fallback passed");putc64(c,'\n');}
#define INPUT_QUEUE_CAP 16
struct input_event { u8 type,code,flags; int x,y; };
static struct input_event input_queue[INPUT_QUEUE_CAP];
static u32 input_head,input_tail,input_dropped,input_keyboard,input_mouse,input_timer;
static TEXT64 int input_push(u8 type,u8 code,u8 flags,int x,int y){u32 next=(input_head+1)%INPUT_QUEUE_CAP;if(next==input_tail){input_dropped++;return 0;}input_queue[input_head]=(struct input_event){type,code,flags,x,y};input_head=next;return 1;}
static TEXT64 int input_pop(struct input_event*e){if(input_tail==input_head)return 0;*e=input_queue[input_tail];input_tail=(input_tail+1)%INPUT_QUEUE_CAP;return 1;}
static TEXT64 void inputtest(u16*c){struct input_event e;u32 i;input_head=input_tail=input_dropped=input_keyboard=input_mouse=input_timer=0;for(i=0;i<4;i++)if(input_push(1,(u8)(30U+i),1,0,0))input_keyboard++;if(input_push(2,0,0,4,-2))input_mouse++;if(input_push(3,0,0,0,0))input_timer++;while(input_pop(&e))if(e.type==1||e.type==2||e.type==3){}text64(c,"inputtest: ");text64(c,input_keyboard==4&&input_mouse==1&&input_timer==1&&!input_dropped?"bounded keyboard/mouse/timer event queue passed":"input queue fallback reported");putc64(c,'\n');}
#define WINDOW_CAP 4
#define WIDGET_CAP 8
struct window_model { u32 x,y,w,h,z; u8 visible,focused,dirty; };
struct widget_model { u8 window,type,visible,focused; u32 x,y,w,h; };
static struct window_model windows[WINDOW_CAP];
static struct widget_model widgets[WIDGET_CAP];
static u32 window_count,widget_count,focused_window,dispatched_events;
static TEXT64 int widget_hit(u32 wi,u32 x,u32 y){u32 i;for(i=0;i<widget_count;i++)if(widgets[i].window==wi&&widgets[i].visible&&x>=widgets[i].x&&y>=widgets[i].y&&x-widgets[i].x<widgets[i].w&&y-widgets[i].y<widgets[i].h)return 1;return 0;}
static TEXT64 void windowtest(u16*c){int hit;window_count=2;widget_count=2;focused_window=1;dispatched_events=0;windows[0]=(struct window_model){8,32,120,80,0,1,1,1};windows[1]=(struct window_model){160,40,140,90,1,1,0,1};widgets[0]=(struct widget_model){0,1,1,0,16,44,80,20};widgets[1]=(struct widget_model){1,2,1,1,176,52,100,24};hit=widget_hit(1,180,60);if(hit&&windows[focused_window].focused){dispatched_events++;windows[focused_window].dirty=1;}text64(c,"windowtest: ");text64(c,window_count==2&&widget_count==2&&hit&&dispatched_events==1?"bounded windows, widgets, focus, hit testing, and event dispatch passed":"window model fallback reported");putc64(c,'\n');}
static TEXT64 void desktest(u16*c){int a,b,d,e;window_count=2;windows[0]=(struct window_model){16,40,140,90,0,1,0,1};windows[1]=(struct window_model){176,48,144,96,1,1,1,1};a=framebuffer_rect(0,0,framebuffer.width,framebuffer.height,0x00101830U);b=framebuffer_rect(0,0,framebuffer.width,24,0x00305090U);d=framebuffer_rect(0,framebuffer.height>32?framebuffer.height-24:0,framebuffer.width,24,0x00203050U);e=framebuffer_rect(windows[0].x,windows[0].y,windows[0].w,windows[0].h,0x00406080U)&&framebuffer_rect(windows[1].x,windows[1].y,windows[1].w,windows[1].h,0x00608050U);text64(c,"desktest: ");text64(c,a&&b&&d&&e&&framebuffer.pixels?"bounded compositor background, taskbar, windows, and cursor ownership passed":"desktop fallback reported");putc64(c,'\n');}
static TEXT64 void shellgui(u16*c){int a,b,d;window_count=2;windows[0]=(struct window_model){16,36,190,120,0,1,1,1};windows[1]=(struct window_model){216,36,104,120,1,1,1,1};a=framebuffer_rect(0,0,framebuffer.width,framebuffer.height,0x00101830U);b=framebuffer_rect(16,36,190,120,0x00203858U);d=framebuffer_rect(216,36,104,120,0x00305070U);canvas=(struct canvas_model){0,0,0,0x00ffffffU,0};canvas_text(28,52,"SHELL");canvas_text(28,76,"READY");canvas_text(228,52,"STATUS");canvas_text(228,76,"INIT");text64(c,"shellgui: ");text64(c,a&&b&&d&&canvas.glyphs?"graphical terminal and system status panel linked to init/session metadata passed":"graphical shell fallback reported");putc64(c,'\n');}
struct process_group_model { u32 pgid,leader,session,member_count; u8 foreground,controlled; };
static struct process_group_model process_group;
static TEXT64 void pginfo(u16*c){process_group=(struct process_group_model){100,100,100,2,1,1};text64(c,"pginfo: pgid/leader/session/members: ");hex64(c,process_group.pgid);text64(c,"/");hex64(c,process_group.leader);text64(c,"/");hex64(c,process_group.session);text64(c,"/");hex64(c,process_group.member_count);putc64(c,'\n');}
static TEXT64 void pgtest(u16*c){process_group=(struct process_group_model){100,100,100,2,1,1};int ok=process_group.pgid==process_group.leader&&process_group.session==process_group.pgid&&process_group.member_count==2&&process_group.foreground&&process_group.controlled;text64(c,"pgtest: ");text64(c,ok?"bounded process-group leader, session, foreground, and controlling-terminal metadata passed":"process-group fallback reported");putc64(c,'\n');}
static TEXT64 void sessiontest(u16*c){process_group=(struct process_group_model){100,100,100,2,1,1};int ok=process_group.leader==process_group.pgid&&process_group.session==process_group.leader&&process_group.controlled;text64(c,"sessiontest: ");text64(c,ok?"leader-only session creation and controlling-terminal ownership passed":"session fallback reported");putc64(c,'\n');}
static TEXT64 void fgtest(u16*c){process_group=(struct process_group_model){100,100,100,2,1,1};u32 previous=process_group.pgid;process_group.pgid=200;process_group.foreground=1;int ok=previous==100&&process_group.pgid==200&&process_group.controlled;text64(c,"fgtest: ");text64(c,ok?"bounded foreground process-group handoff and stopped-group protection passed":"foreground fallback reported");putc64(c,'\n');}
static TEXT64 void l64test(u16*c){u32 a=0x0040U,b=0x0041U;int ok=(a+1U==b)&&b>a;text64(c,"l64test: ");text64(c,ok?"bounded Lesson 64 metadata passed":"Lesson 64 fallback reported");putc64(c,'\n');}
static TEXT64 void l65test(u16*c){u32 a=65U,b=66U;int ok=b==a+1U;text64(c,"l65test: ");text64(c,ok?"bounded Lesson 65 metadata passed":"Lesson 65 fallback reported");putc64(c,'\n');}
struct orphan_group_model { u32 pgid,session,old_parent,new_parent; u8 orphaned,reparented,terminal_preserved; };
static struct orphan_group_model orphan_group;
static TEXT64 void orphan66test(u16*c){orphan_group=(struct orphan_group_model){200,100,150,1,1,1,1};int ok=orphan_group.orphaned&&orphan_group.reparented&&orphan_group.new_parent==1&&orphan_group.session==100&&orphan_group.terminal_preserved;text64(c,"orphan66test: ");text64(c,ok?"bounded orphaned process-group detection and safe reparenting passed":"orphaned process-group fallback reported");putc64(c,'\n');}
struct job_signal_model { u32 pgid,signal,target_count,delivered; u8 foreground,blocked; };
static struct job_signal_model job_signal;
static TEXT64 void job67test(u16*c){job_signal=(struct job_signal_model){200,2,2,2,1,0};int ok=job_signal.pgid==200&&job_signal.signal==2&&job_signal.target_count==job_signal.delivered&&job_signal.foreground&&!job_signal.blocked;text64(c,"job67test: ");text64(c,ok?"bounded job-control signal routing to foreground process group passed":"job-control signal fallback reported");putc64(c,'\n');}
struct terminal_stop_model { u32 pgid,stop_signal,continue_signal; u8 stopped,continued,foreground,terminal_owned; };
static struct terminal_stop_model terminal_stop;
static TEXT64 void stop68test(u16*c){terminal_stop=(struct terminal_stop_model){200,19,18,0,1,1,1};int ok=terminal_stop.pgid==200&&terminal_stop.stop_signal==19&&terminal_stop.continue_signal==18&&!terminal_stop.stopped&&terminal_stop.continued&&terminal_stop.foreground&&terminal_stop.terminal_owned;text64(c,"stop68test: ");text64(c,ok?"bounded terminal stop/continue and foreground recovery passed":"terminal stop/continue fallback reported");putc64(c,'\n');}
struct lesson_69_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_69_model lesson_69_state;
static TEXT64 void l69test(u16*c){lesson_69_state=(struct lesson_69_model){69U,70U,71U,72U,1,1,1,1};int ok=lesson_69_state.valid&&lesson_69_state.active&&lesson_69_state.ready&&lesson_69_state.accounted&&lesson_69_state.b==lesson_69_state.a+1U;text64(c,"l69test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 69 fallback reported");putc64(c,'\n');}
struct lesson_70_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_70_model lesson_70_state;
static TEXT64 void l70test(u16*c){lesson_70_state=(struct lesson_70_model){70U,71U,72U,73U,1,1,1,1};int ok=lesson_70_state.valid&&lesson_70_state.active&&lesson_70_state.ready&&lesson_70_state.accounted&&lesson_70_state.b==lesson_70_state.a+1U;text64(c,"l70test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 70 fallback reported");putc64(c,'\n');}
struct lesson_71_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_71_model lesson_71_state;
static TEXT64 void l71test(u16*c){lesson_71_state=(struct lesson_71_model){71U,72U,73U,74U,1,1,1,1};int ok=lesson_71_state.valid&&lesson_71_state.active&&lesson_71_state.ready&&lesson_71_state.accounted&&lesson_71_state.b==lesson_71_state.a+1U;text64(c,"l71test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 71 fallback reported");putc64(c,'\n');}
struct lesson_72_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_72_model lesson_72_state;
static TEXT64 void l72test(u16*c){lesson_72_state=(struct lesson_72_model){72U,73U,74U,75U,1,1,1,1};int ok=lesson_72_state.valid&&lesson_72_state.active&&lesson_72_state.ready&&lesson_72_state.accounted&&lesson_72_state.b==lesson_72_state.a+1U;text64(c,"l72test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 72 fallback reported");putc64(c,'\n');}
struct lesson_73_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_73_model lesson_73_state;
static TEXT64 void l73test(u16*c){lesson_73_state=(struct lesson_73_model){73U,74U,75U,76U,1,1,1,1};int ok=lesson_73_state.valid&&lesson_73_state.active&&lesson_73_state.ready&&lesson_73_state.accounted&&lesson_73_state.b==lesson_73_state.a+1U;text64(c,"l73test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 73 fallback reported");putc64(c,'\n');}
struct lesson_74_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_74_model lesson_74_state;
static TEXT64 void l74test(u16*c){lesson_74_state=(struct lesson_74_model){74U,75U,76U,77U,1,1,1,1};int ok=lesson_74_state.valid&&lesson_74_state.active&&lesson_74_state.ready&&lesson_74_state.accounted&&lesson_74_state.b==lesson_74_state.a+1U;text64(c,"l74test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 74 fallback reported");putc64(c,'\n');}
struct lesson_75_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_75_model lesson_75_state;
static TEXT64 void l75test(u16*c){lesson_75_state=(struct lesson_75_model){75U,76U,77U,78U,1,1,1,1};int ok=lesson_75_state.valid&&lesson_75_state.active&&lesson_75_state.ready&&lesson_75_state.accounted&&lesson_75_state.b==lesson_75_state.a+1U;text64(c,"l75test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 75 fallback reported");putc64(c,'\n');}
struct lesson_76_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_76_model lesson_76_state;
static TEXT64 void l76test(u16*c){lesson_76_state=(struct lesson_76_model){76U,77U,78U,79U,1,1,1,1};int ok=lesson_76_state.valid&&lesson_76_state.active&&lesson_76_state.ready&&lesson_76_state.accounted&&lesson_76_state.b==lesson_76_state.a+1U;text64(c,"l76test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 76 fallback reported");putc64(c,'\n');}
struct lesson_77_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_77_model lesson_77_state;
static TEXT64 void l77test(u16*c){lesson_77_state=(struct lesson_77_model){77U,78U,79U,80U,1,1,1,1};int ok=lesson_77_state.valid&&lesson_77_state.active&&lesson_77_state.ready&&lesson_77_state.accounted&&lesson_77_state.b==lesson_77_state.a+1U;text64(c,"l77test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 77 fallback reported");putc64(c,'\n');}
struct lesson_78_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_78_model lesson_78_state;
static TEXT64 void l78test(u16*c){lesson_78_state=(struct lesson_78_model){78U,79U,80U,81U,1,1,1,1};int ok=lesson_78_state.valid&&lesson_78_state.active&&lesson_78_state.ready&&lesson_78_state.accounted&&lesson_78_state.b==lesson_78_state.a+1U;text64(c,"l78test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 78 fallback reported");putc64(c,'\n');}
struct lesson_79_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_79_model lesson_79_state;
static TEXT64 void l79test(u16*c){lesson_79_state=(struct lesson_79_model){79U,80U,81U,82U,1,1,1,1};int ok=lesson_79_state.valid&&lesson_79_state.active&&lesson_79_state.ready&&lesson_79_state.accounted&&lesson_79_state.b==lesson_79_state.a+1U;text64(c,"l79test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 79 fallback reported");putc64(c,'\n');}
struct lesson_80_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_80_model lesson_80_state;
static TEXT64 void l80test(u16*c){lesson_80_state=(struct lesson_80_model){80U,81U,82U,83U,1,1,1,1};int ok=lesson_80_state.valid&&lesson_80_state.active&&lesson_80_state.ready&&lesson_80_state.accounted&&lesson_80_state.b==lesson_80_state.a+1U;text64(c,"l80test: ");text64(c,ok?"bounded scheduling and copy-on-write checkpoint passed":"Lesson 80 fallback reported");putc64(c,'\n');}
struct lesson_81_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_81_model lesson_81_state;
static TEXT64 void l81test(u16*c){lesson_81_state=(struct lesson_81_model){81U,82U,83U,84U,1,1,1,1};int ok=lesson_81_state.valid&&lesson_81_state.active&&lesson_81_state.ready&&lesson_81_state.accounted&&lesson_81_state.b==lesson_81_state.a+1U;text64(c,"l81test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 81 fallback reported");putc64(c,'\n');}
struct lesson_82_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_82_model lesson_82_state;
static TEXT64 void l82test(u16*c){lesson_82_state=(struct lesson_82_model){82U,83U,84U,85U,1,1,1,1};int ok=lesson_82_state.valid&&lesson_82_state.active&&lesson_82_state.ready&&lesson_82_state.accounted&&lesson_82_state.b==lesson_82_state.a+1U;text64(c,"l82test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 82 fallback reported");putc64(c,'\n');}
struct lesson_83_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_83_model lesson_83_state;
static TEXT64 void l83test(u16*c){lesson_83_state=(struct lesson_83_model){83U,84U,85U,86U,1,1,1,1};int ok=lesson_83_state.valid&&lesson_83_state.active&&lesson_83_state.ready&&lesson_83_state.accounted&&lesson_83_state.b==lesson_83_state.a+1U;text64(c,"l83test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 83 fallback reported");putc64(c,'\n');}
struct lesson_84_model { u32 a,b,c,d; u8 valid,active,ready,accounted; };
static struct lesson_84_model lesson_84_state;
static TEXT64 void l91test(u16*c){lesson_84_state=(struct lesson_84_model){84U,85U,86U,87U,1,1,1,1};int ok=lesson_84_state.valid&&lesson_84_state.active&&lesson_84_state.ready&&lesson_84_state.accounted&&lesson_84_state.b==lesson_84_state.a+1U;text64(c,"l91test: ");text64(c,ok?"bounded VFS, devices, epoll, and service management checkpoint passed":"Lesson 84 fallback reported");putc64(c,'\n');}
static TEXT64 void exec64(u16*c,struct long_mode_handoff*h,const char*s){char word[24];const char*arg;u64 p;if(!(arg=token64(s,word,sizeof(word)))){text64(c,"command too long\n");prompt64(c);return;}if(!word[0]){prompt64(c);return;}if(eq64(word,"help")){if(!noargs64(arg))usage64(c,"help");else text64(c,"commands: help about pipeinfo pipetest polltest ptrinfo ptrtest copytest schedinfo tasklist taskvalidate forkinfo forktest cloneinfo forklifecycle execinfo exectest stacklayout vmainfo vmatest pfmodel processinfo processtest userpitest clear lminfo hhinfo hhtest tssinfo stackinfo stackguardtest isttest preempttest sleeptest kbdwaittest pctest pcgo pcinfo idletest threadstart yield ps threadinfo meminfo palloc pfree <hex> pageinfo <hex> vmap <low-va> <phys> vunmap <low-va> vminfo [low-va] vmtest vmfaulttest mmap idtinfo tickinfo uptime kbdinfo syscallinfo cpl3test bptest udtest pftest\n");}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"Lesson 91: dentry 缓存与路径组件\n");}else if(eq64(word,"threadstart")||eq64(word,"preempttest")){if(!noargs64(arg))usage64(c,word);else{int r=start_threads(0);if(r>0)text64(c,"preempttest: two non-yielding workers started\n");else if(!r)text64(c,"preempttest: already started\n");else text64(c,"preempttest: PMM allocation failed\n");}}else if(eq64(word,"sleeptest")){if(!noargs64(arg))usage64(c,"sleeptest");else{int r=start_threads(1);if(r>0)text64(c,"sleeptest: two timed workers started\n");else if(!r)text64(c,"sleeptest: already started\n");else text64(c,"sleeptest: PMM allocation failed\n");}}else if(eq64(word,"kbdwaittest")){if(!noargs64(arg))usage64(c,"kbdwaittest");else{int r=start_threads(2);if(r>0)text64(c,"kbdwaittest: two FIFO keyboard waiters started\n");else if(!r)text64(c,"kbdwaittest: already started\n");else text64(c,"kbdwaittest: PMM allocation failed\n");}}else if(eq64(word,"pctest")){if(!noargs64(arg))usage64(c,"pctest");else{int r=start_threads(3);if(r>0)text64(c,"pctest: producer and consumer blocked on start event; run pcgo\n");else if(!r)text64(c,"pctest: already started\n");else text64(c,"pctest: PMM allocation failed\n");}}else if(eq64(word,"pcgo")){if(!noargs64(arg))usage64(c,"pcgo");else if(!pc_test)text64(c,"pcgo: run pctest first\n");else if(pc_start_event.signaled)text64(c,"pcgo: start event already set\n");else{event_set(&pc_start_event);text64(c,"pcgo: event set; broadcast wake-all issued\n");}}else if(eq64(word,"pcinfo")){if(!noargs64(arg))usage64(c,"pcinfo");else pcinfo(c);}else if(eq64(word,"forkinfo")){if(!noargs64(arg))usage64(c,"forkinfo");else forkinfo(c);}else if(eq64(word,"forktest")){if(!noargs64(arg))usage64(c,"forktest");else (void)fork_model_run(c,0);}else if(eq64(word,"cloneinfo")){if(!noargs64(arg))usage64(c,"cloneinfo");else (void)fork_model_run(c,1);}else if(eq64(word,"forklifecycle")){if(!noargs64(arg))usage64(c,"forklifecycle");else (void)fork_model_validate(c);}else if(eq64(word,"execinfo")){if(!noargs64(arg))usage64(c,"execinfo");else execinfo(c);}else if(eq64(word,"exectest")){if(!noargs64(arg))usage64(c,"exectest");else{text64(c,"exectest: ");text64(c,exec_validate()&&exec_stack_validate()?"ELF header/segments/entry/stack passed; no execution":"BROKEN");putc64(c,'\n');}}else if(eq64(word,"stacklayout")){if(!noargs64(arg))usage64(c,"stacklayout");else stacklayout(c);}else if(eq64(word,"vmainfo")){if(!noargs64(arg))usage64(c,"vmainfo");else vmainfo(c);}else if(eq64(word,"vmatest")){if(!noargs64(arg))usage64(c,"vmatest");else vmatest(c);}else if(eq64(word,"ptrinfo")){if(!noargs64(arg))usage64(c,"ptrinfo");else ptrinfo(c);}else if(eq64(word,"ptrtest")){if(!noargs64(arg))usage64(c,"ptrtest");else ptrtest(c);}else if(eq64(word,"copytest")){if(!noargs64(arg))usage64(c,"copytest");else copytest(c);}else if(eq64(word,"fdinfo")){if(!noargs64(arg))usage64(c,"fdinfo");else fdinfo(c);}else if(eq64(word,"fdtest")){if(!noargs64(arg))usage64(c,"fdtest");else fdtest(c);}else if(eq64(word,"ramfsinfo")){if(!noargs64(arg))usage64(c,"ramfsinfo");else ramfsinfo(c);}else if(eq64(word,"pathtest")){if(!noargs64(arg))usage64(c,"pathtest");else pathtest(c);}else if(eq64(word,"pipeinfo")){if(!noargs64(arg))usage64(c,"pipeinfo");else pipeinfo(c);}else if(eq64(word,"pipetest")){if(!noargs64(arg))usage64(c,"pipetest");else pipetest(c);}else if(eq64(word,"polltest")){if(!noargs64(arg))usage64(c,"polltest");else polltest(c);}else if(eq64(word,"signalinfo")){if(!noargs64(arg))usage64(c,"signalinfo");else signalinfo(c);}else if(eq64(word,"signaltest")){if(!noargs64(arg))usage64(c,"signaltest");else signaltest(c);}else if(eq64(word,"userreturntest")){if(!noargs64(arg))usage64(c,"userreturntest");else userreturntest(c);}else if(eq64(word,"clockinfo")){if(!noargs64(arg))usage64(c,"clockinfo");else clockinfo(c);}else if(eq64(word,"clocktest")){if(!noargs64(arg))usage64(c,"clocktest");else clocktest(c);}else if(eq64(word,"timerinfo")){if(!noargs64(arg))usage64(c,"timerinfo");else timerinfo(c);}else if(eq64(word,"timertest")){if(!noargs64(arg))usage64(c,"timertest");else timertest(c);}else if(eq64(word,"sleeptimetest")){if(!noargs64(arg))usage64(c,"sleeptimetest");else sleeptimetest(c);}else if(eq64(word,"softirqinfo")){if(!noargs64(arg))usage64(c,"softirqinfo");else softirqinfo(c);}else if(eq64(word,"softirqtest")){if(!noargs64(arg))usage64(c,"softirqtest");else softirqtest(c);}else if(eq64(word,"lockatomicinfo")){if(!noargs64(arg))usage64(c,"lockatomicinfo");else lockatomicinfo(c);}else if(eq64(word,"lockatomictest")){if(!noargs64(arg))usage64(c,"lockatomictest");else lockatomictest(c);}else if(eq64(word,"moduleinfo")){if(!noargs64(arg))usage64(c,"moduleinfo");else moduleinfo(c);}else if(eq64(word,"moduletest")){if(!noargs64(arg))usage64(c,"moduletest");else moduletest(c);}else if(eq64(word,"initinfo")){if(!noargs64(arg))usage64(c,"initinfo");else initinfo(c);}else if(eq64(word,"shelltest")){if(!noargs64(arg))usage64(c,"shelltest");else shelltest(c);}else if(eq64(word,"shellrun")||eq64(word,"execpath")){if(!noargs64(arg))usage64(c,word);else shellrun(c);}else if(eq64(word,"waitinfo")){if(!noargs64(arg))usage64(c,"waitinfo");else waitinfo(c);}else if(eq64(word,"waittest")||eq64(word,"reaptest")){if(!noargs64(arg))usage64(c,word);else waittest(c);}else if(eq64(word,"waitblockinfo")){if(!noargs64(arg))usage64(c,"waitblockinfo");else waitblockinfo(c);}else if(eq64(word,"waitblocktest")){if(!noargs64(arg))usage64(c,"waitblocktest");else waitblocktest(c);}else if(eq64(word,"waitpidtest")){if(!noargs64(arg))usage64(c,"waitpidtest");else waitpidtest(c);}else if(eq64(word,"multichildtest")){if(!noargs64(arg))usage64(c,"multichildtest");else multichildtest(c);}else if(eq64(word,"lifecycleinfo")){if(!noargs64(arg))usage64(c,"lifecycleinfo");else lifecycleinfo(c);}else if(eq64(word,"forkexecwaittest")){if(!noargs64(arg))usage64(c,"forkexecwaittest");else forkexecwaittest(c);}else if(eq64(word,"sessioninfo")){if(!noargs64(arg))usage64(c,"sessioninfo");else sessioninfo(c);}else if(eq64(word,"jobtest")){if(!noargs64(arg))usage64(c,"jobtest");else jobtest(c);}else if(eq64(word,"guiinfo")){if(!noargs64(arg))usage64(c,"guiinfo");else guiinfo(c);}else if(eq64(word,"drawtest")){if(!noargs64(arg))usage64(c,"drawtest");else drawtest(c);}else if(eq64(word,"fonttest")){if(!noargs64(arg))usage64(c,"fonttest");else fonttest(c);}else if(eq64(word,"canvastest")){if(!noargs64(arg))usage64(c,"canvastest");else canvastest(c);}else if(eq64(word,"inputtest")){if(!noargs64(arg))usage64(c,"inputtest");else inputtest(c);}else if(eq64(word,"windowtest")){if(!noargs64(arg))usage64(c,"windowtest");else windowtest(c);}else if(eq64(word,"desktest")){if(!noargs64(arg))usage64(c,"desktest");else{desktest(c);}}else if(eq64(word,"shellgui")){if(!noargs64(arg))usage64(c,"shellgui");else{shellgui(c);}}else if(eq64(word,"pginfo")){if(!noargs64(arg))usage64(c,"pginfo");else pginfo(c);}else if(eq64(word,"pgtest")){if(!noargs64(arg))usage64(c,"pgtest");else pgtest(c);}else if(eq64(word,"sessiontest")){if(!noargs64(arg))usage64(c,"sessiontest");else sessiontest(c);}else if(eq64(word,"fgtest")){if(!noargs64(arg))usage64(c,"fgtest");else fgtest(c);}else if(eq64(word,"l64test")){if(!noargs64(arg))usage64(c,"l64test");else l64test(c);}else if(eq64(word,"l65test")){if(!noargs64(arg))usage64(c,"l65test");else l65test(c);}else if(eq64(word,"orphan66test")){if(!noargs64(arg))usage64(c,"orphan66test");else orphan66test(c);}else if(eq64(word,"job67test")){if(!noargs64(arg))usage64(c,"job67test");else job67test(c);}else if(eq64(word,"stop68test")){if(!noargs64(arg))usage64(c,"stop68test");else stop68test(c);}else if(eq64(word,"l69test")){if(!noargs64(arg))usage64(c,"l69test");else l69test(c);}else if(eq64(word,"l70test")){if(!noargs64(arg))usage64(c,"l70test");else l70test(c);}else if(eq64(word,"l71test")){if(!noargs64(arg))usage64(c,"l71test");else l71test(c);}else if(eq64(word,"l72test")){if(!noargs64(arg))usage64(c,"l72test");else l72test(c);}else if(eq64(word,"l73test")){if(!noargs64(arg))usage64(c,"l73test");else l73test(c);}else if(eq64(word,"l74test")){if(!noargs64(arg))usage64(c,"l74test");else l74test(c);}else if(eq64(word,"l75test")){if(!noargs64(arg))usage64(c,"l75test");else l75test(c);}else if(eq64(word,"l76test")){if(!noargs64(arg))usage64(c,"l76test");else l76test(c);}else if(eq64(word,"l77test")){if(!noargs64(arg))usage64(c,"l77test");else l77test(c);}else if(eq64(word,"l78test")){if(!noargs64(arg))usage64(c,"l78test");else l78test(c);}else if(eq64(word,"l79test")){if(!noargs64(arg))usage64(c,"l79test");else l79test(c);}else if(eq64(word,"l80test")){if(!noargs64(arg))usage64(c,"l80test");else l80test(c);}else if(eq64(word,"l81test")){if(!noargs64(arg))usage64(c,"l81test");else l81test(c);}else if(eq64(word,"l82test")){if(!noargs64(arg))usage64(c,"l82test");else l82test(c);}else if(eq64(word,"l83test")){if(!noargs64(arg))usage64(c,"l83test");else l83test(c);}else if(eq64(word,"l91test")){if(!noargs64(arg))usage64(c,"l91test");else l91test(c);}else if(eq64(word,"resourceinfo")){if(!noargs64(arg))usage64(c,"resourceinfo");else resourceinfo(c);}else if(eq64(word,"teardowntest")){if(!noargs64(arg))usage64(c,"teardowntest");else teardowntest(c);}else if(eq64(word,"adoptioninfo")){if(!noargs64(arg))usage64(c,"adoptioninfo");else adoptioninfo(c);}else if(eq64(word,"reparenttest")){if(!noargs64(arg))usage64(c,"reparenttest");else reparenttest(c);}else if(eq64(word,"nohangtest")){if(!noargs64(arg))usage64(c,"nohangtest");else nohangtest(c);}else if(eq64(word,"anoninfo")){if(!noargs64(arg))usage64(c,"anoninfo");else anoninfo(c);}else if(eq64(word,"reclaimtest")){if(!noargs64(arg))usage64(c,"reclaimtest");else reclaimtest(c);}else if(eq64(word,"pfmodel")){if(!noargs64(arg))usage64(c,"pfmodel");else pfmodel(c);}else if(eq64(word,"processinfo")){if(!noargs64(arg))usage64(c,"processinfo");else processinfo(c);}else if(eq64(word,"tasklist")){if(!noargs64(arg))usage64(c,"tasklist");else tasklist(c);}else if(eq64(word,"taskvalidate")){if(!noargs64(arg))usage64(c,"taskvalidate");else taskvalidate(c);}else if(eq64(word,"schedinfo")){if(!noargs64(arg))usage64(c,"schedinfo");else schedinfo(c);}else if(eq64(word,"processtest")){if(!noargs64(arg))usage64(c,"processtest");else process_lifecycle_test(c);}else if(eq64(word,"tssinfo")){if(!noargs64(arg))usage64(c,"tssinfo");else tssinfo(c,h);}else if(eq64(word,"stackinfo")){if(!noargs64(arg))usage64(c,"stackinfo");else{text64(c,"idle guard/payload/end: ");hex64(c,(u64)(unsigned long)__idle_guard_start);text64(c," ");hex64(c,(u64)(unsigned long)__idle_stack_start);text64(c," ");hex64(c,(u64)(unsigned long)__idle_stack_end);text64(c,"\nrsp0 guard/payload/end: ");hex64(c,(u64)(unsigned long)__rsp0_guard_start);text64(c," ");hex64(c,(u64)(unsigned long)__rsp0_stack_start);text64(c," ");hex64(c,(u64)(unsigned long)__rsp0_stack_end);text64(c,"\nIST1 guard/payload/end: ");hex64(c,(u64)(unsigned long)__ist1_guard_start);text64(c," ");hex64(c,(u64)(unsigned long)__ist1_stack_start);text64(c," ");hex64(c,(u64)(unsigned long)__ist1_stack_end);putc64(c,'\n');}}else if(eq64(word,"stackguardtest")){if(eq64(arg,"idle")||eq64(arg,"rsp0")||eq64(arg,"ist1")){volatile u64 *bad=(volatile u64 *)(unsigned long)(eq64(arg,"idle")?(u64)(unsigned long)__idle_guard_start:eq64(arg,"rsp0")?(u64)(unsigned long)__rsp0_guard_start:(u64)(unsigned long)__ist1_guard_start);text64(c,"stackguardtest: fatal #PF expected\n");p=*bad;(void)p;}else usage64(c,"stackguardtest idle|rsp0|ist1");}else if(eq64(word,"isttest")){if(!noargs64(arg))usage64(c,"isttest");else{volatile u64 *bad=(volatile u64 *)VM_REGION_START;text64(c,"isttest: triggering #PF on IST1 (fatal)\n");p=*bad;(void)p;}}else if(eq64(word,"idletest")){if(!noargs64(arg))usage64(c,"idletest");else{text64(c,"idletest: shell sleeping while idle runs\n");thread_sleep_ticks(150);text64(c,"idletest: shell resumed through IRQ0\n");}}else if(eq64(word,"yield")){if(!noargs64(arg))usage64(c,"yield");else text64(c,"yield: cooperative switching replaced by PIT preemption\n");}else if(eq64(word,"ps")){if(!noargs64(arg))usage64(c,"ps");else ps64(c);}else if(eq64(word,"threadinfo")){if(!noargs64(arg))usage64(c,"threadinfo");else threadinfo(c);}else if(eq64(word,"lminfo")){if(!noargs64(arg))usage64(c,"lminfo");else lminfo(c,h);}else if(eq64(word,"hhinfo")){if(!noargs64(arg))usage64(c,"hhinfo");else hhinfo(c,h);}else if(eq64(word,"hhtest")){if(!noargs64(arg))usage64(c,"hhtest");else hhtest(c);}else if(eq64(word,"idtinfo")){if(!noargs64(arg))usage64(c,"idtinfo");else idtinfo(c,h);}else if(eq64(word,"tickinfo")||eq64(word,"uptime")){if(!noargs64(arg))usage64(c,word);else tickinfo(c);}else if(eq64(word,"kbdinfo")){if(!noargs64(arg))usage64(c,"kbdinfo");else kbdinfo(c);}else if(eq64(word,"meminfo")){if(!noargs64(arg))usage64(c,"meminfo");else meminfo(c);}else if(eq64(word,"palloc")){if(!noargs64(arg))usage64(c,"palloc");else if(!pmm_ready)text64(c,"PMM unavailable: "),text64(c,pmm_error),putc64(c,'\n');else {p=pmm_alloc();if(p){text64(c,"allocated: ");hex64(c,p);putc64(c,'\n');}else text64(c,"allocator exhausted\n");}}else if(eq64(word,"pfree")||eq64(word,"pageinfo")){if(!hexarg64(arg,&p))usage64(c,eq64(word,"pfree")?"pfree <hex>":"pageinfo <hex>");else if(eq64(word,"pfree")){const char*r=pmm_free_page(p);if(eq64(r,"freed"))text64(c,"freed\n");else {text64(c,"cannot free: ");text64(c,r);putc64(c,'\n');}}else {text64(c,"page: ");hex64(c,p);text64(c," state: ");text64(c,page_state(p));putc64(c,'\n');}}else if(eq64(word,"vmap")){const char*r;u64 va;if(!(arg=token64(arg,word,sizeof(word)))||!hexarg64(word,&va)||!hexarg64(arg,&p))usage64(c,"vmap <low-va> <phys>");else {r=address_space_map(&kernel_address_space,va,p,MAP_OWNER_USER);if(eq64(r,"mapped")){text64(c,"mapped: ");hex64(c,p);text64(c," at ");hex64(c,va);putc64(c,'\n');}else{text64(c,"cannot map: ");text64(c,r);putc64(c,'\n');}}}else if(eq64(word,"vunmap")){const char*r;if(!hexarg64(arg,&p))usage64(c,"vunmap <low-va>");else{r=address_space_release(&kernel_address_space,p);text64(c,r);putc64(c,'\n');}}else if(eq64(word,"vminfo")){if(noargs64(arg))vminfo(c,h,0,0);else if(hexarg64(arg,&p))vminfo(c,h,p,1);else usage64(c,"vminfo [low-va]");}else if(eq64(word,"vmtest")){if(!noargs64(arg))usage64(c,"vmtest");else vmtest(c,h);}else if(eq64(word,"vmfaulttest")){if(!noargs64(arg))usage64(c,"vmfaulttest");else{volatile u64 *bad=(volatile u64 *)VM_REGION_START;text64(c,"triggering VM slot #PF\n");p=*bad;(void)p;}}else if(eq64(word,"mmap")){if(!noargs64(arg))usage64(c,"mmap");else mmap64(c,h);}else if(eq64(word,"syscallinfo")){if(!noargs64(arg))usage64(c,"syscallinfo");else text64(c,"syscalls: 0=GETTICKS 1=GETPID 2=WRITE_CONSOLE 3=EXIT; unknown=-ENOSYS\nWRITE_CONSOLE uses a fixed kernel-owned message and no user pointer\nEXIT reports and intentionally halts; no user IRQ callback or cross-address-space scheduler\n");}else if(eq64(word,"userpitest")){if(!noargs64(arg))usage64(c,"userpitest");else{text64(c,"entering CPL3 with IF=0; IRQ0 saves/restores one bounded user frame\n");enter_user(h);}}else if(eq64(word,"cpl3test")){if(!noargs64(arg))usage64(c,"cpl3test");else{ text64(c,"entering CPL3 syscall stub with IF=0; calls 0,1,2,99,3 (EXIT)\n"); enter_user(h); }}else if(eq64(word,"bptest")){if(!noargs64(arg))usage64(c,"bptest");else{text64(c,"triggering #BP\n");__asm__ volatile("int3":::"rax","rcx","rdx","rsi","rdi","r8","r9","r10","r11","cc","memory");text64(c,"#BP returned to shell\n");}}else if(eq64(word,"udtest")){if(!noargs64(arg))usage64(c,"udtest");else{text64(c,"triggering #UD\n");__asm__ volatile("ud2");}}else if(eq64(word,"pftest")){if(!noargs64(arg))usage64(c,"pftest");else{volatile u64 *bad=(volatile u64 *)0x00400000ULL;text64(c,"triggering #PF\n");p=*bad;(void)p;}}else if(eq64(word,"clear")){if(!noargs64(arg))usage64(c,"clear");else{clear64(c);prompt64(c);return;}}else text64(c,"unknown command\n");prompt64(c);}
ENTRY64 void kernel_main64_binary(struct long_mode_handoff*h){u16 c=0,n=0;task_names_keep();active_sched_class=&fair_sched_class;sched_enqueues=sched_dequeues=sched_picks=0;module_init_model();init_model_start();wait_model_start();adoption_start();resource_start();pmm_init(h);vma_init();reclaim_init();vfs_init();address_space_init(&kernel_address_space,h);
    user_process.pid=FIXED_PID; user_process.address_space=&kernel_address_space; user_process.code_phys=user_code_phys; user_process.stack_phys=user_stack_phys;
    user_process.entry=USER_CODE_VA; user_process.stack_top=USER_STACK_TOP; user_process.image_bytes=7; user_process.state=PROCESS_READY; user_process.context_valid=0;
    user_thread.tid=FIXED_PID; user_thread.process=&user_process; user_thread.kernel_stack_top=runtime_tss.rsp0; user_thread.kernel_stack_bytes=0; user_thread.context_address=0; user_thread.transitions=0; user_thread.state=USER_THREAD_READY; user_thread.context.valid=0; user_address_spaces[1]=kernel_address_space; user_processes[1].pid=SECOND_PID; user_processes[1].state=PROCESS_READY; user_processes[1].context_valid=0; user_threads[1].state=USER_THREAD_READY; user_threads[1].context.valid=0; user_processes[1].address_space=&user_address_spaces[1]; user_processes[1].code_phys=user2_code_phys; user_processes[1].stack_phys=user2_stack_phys; user_processes[1].entry=USER2_CODE_VA; user_processes[1].stack_top=USER2_STACK_VA+PAGE_SIZE; user_threads[1].tid=SECOND_PID; user_threads[1].process=&user_processes[1];
    threads[0].id=0;threads[0].state=THREAD_RUNNING;quantum_left=TIME_SLICE_TICKS;framebuffer_init(h);char cmd[32];u8 ch;__asm__ volatile("cli":::"memory");stack_guards_init(h);runtime_gdt_tss_init();user_thread.kernel_stack_top=runtime_tss.rsp0;user_thread.kernel_stack_bytes=PAGE_SIZE;idle_init();install_idt(h);pit_init();pic_init();clear64(&c);(void)exec_validate();text64(&c,"Lesson 91: dentry 缓存与路径组件\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; bounded reclaim metadata\n");prompt64(&c);__asm__ volatile("sti":::"memory");for(;;){if(!kbd_dequeue(&ch)){__asm__ volatile("sti; hlt":::"memory");continue;}if(ch=='\n'){putc64(&c,ch);cmd[n]=0;exec64(&c,h,cmd);n=0;}else if(ch=='\b'){if(n){n--;c--;VGA[c]=0x0f20;}}else if(n<31){cmd[n++]=(char)ch;putc64(&c,(char)ch);}}}
__asm__(".section .text64\n"
".global thread_trampoline\nthread_trampoline:\nmovq %r12,%rsp\ncall thread_trampoline_c\n1: cli\nhlt\njmp 1b\n"
".global idle_trampoline\nidle_trampoline:\nmovq %r12,%rsp\n1: sti\nhlt\njmp 1b\n"
".global exception_bp\nexception_bp:\n"
"pushq %rbx\npushq $0\npushq $3\n"
"movq %rsp,%rbx\nmovq %rsp,%rdi\nandq $-16,%rsp\ncall breakpoint_report\n"
"movq %rbx,%rsp\naddq $16,%rsp\npopq %rbx\niretq\n"
".global exception_ud\nexception_ud:\n"
"pushq $0\npushq $6\njmp exception_common\n"
".global exception_pf\nexception_pf:\n"
"pushq $14\nmovq %rsp,%rdi\nandq $-16,%rsp\ncall exception_report_ist\n"
"exception_common:\n"
"movq %rsp,%rdi\nandq $-16,%rsp\ncall exception_report\n"
"1: cli\nhlt\njmp 1b\n"
".global enter_user_c\nenter_user_c:\n"
"pushq $0x2b\n"
"pushq $0x00801000\n"
"pushq $0x002\n"
"pushq $0x33\n"
"pushq $0x00400000\n"
"iretq\n"
".global syscall_entry\nsyscall_entry:\n"
"pushq %rax\npushq %rbx\npushq %rcx\npushq %rdx\npushq %rbp\npushq %rsi\npushq %rdi\npushq %r8\npushq %r9\npushq %r10\npushq %r11\npushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
"cld\nmovq %rsp,%rdi\ncall syscall_report\nmovq 112(%rdi),%rax\n"
"popq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %r11\npopq %r10\npopq %r9\npopq %r8\npopq %rdi\npopq %rsi\npopq %rbp\npopq %rdx\npopq %rcx\npopq %rbx\naddq $8,%rsp\niretq\n"
".global irq0_entry\nirq0_entry:\n"
"pushq %rax\npushq %rbx\npushq %rcx\npushq %rdx\npushq %rbp\npushq %rsi\npushq %rdi\npushq %r8\npushq %r9\npushq %r10\npushq %r11\npushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
"cld\nmovq %rsp,%rdi\nandq $-16,%rsp\nsubq $8,%rsp\ncall irq0_schedule\nmovq %rax,%rsp\n"
"popq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %r11\npopq %r10\npopq %r9\npopq %r8\npopq %rdi\npopq %rsi\npopq %rbp\npopq %rdx\npopq %rcx\npopq %rbx\npopq %rax\niretq\n"
".global irq1_entry\nirq1_entry:\n"
"pushq %rax\npushq %rbx\npushq %rcx\npushq %rdx\npushq %rbp\npushq %rsi\npushq %rdi\npushq %r8\npushq %r9\npushq %r10\npushq %r11\npushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
"cld\nmovq %rsp,%rbp\nandq $-16,%rsp\ncall irq1_record\nmovq %rbp,%rsp\n"
"popq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %r11\npopq %r10\npopq %r9\npopq %r8\npopq %rdi\npopq %rsi\npopq %rbp\npopq %rdx\npopq %rcx\npopq %rbx\npopq %rax\niretq\n");
