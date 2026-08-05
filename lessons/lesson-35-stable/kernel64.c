/* Lesson 35: bounded CPL3 PIT preemption over the inherited syscall ABI. */
typedef unsigned char u8; typedef unsigned int u32; typedef unsigned short u16; typedef unsigned long long u64; typedef long long s64;
#define TEXT64 __attribute__((section(".text64"), noinline))
#define ENTRY64 __attribute__((section(".text64.entry"), noinline))
#define VGA ((volatile u16 *)0xb8000ULL)
#define COLS 80
#define ROWS 25
#define PAGE_SIZE 0x1000ULL
#define PAGE_ENTRIES 512U
#define IDENTITY_MAP_END 0x01000000ULL
#define PMM_MAX_PHYS IDENTITY_MAP_END
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
#define PAGE_TABLES_PER_ALIAS 8U
#define VM_REGION_START 0x00ff0000ULL
#define VM_REGION_SLOTS 16U
#define VM_REGION_END (VM_REGION_START+VM_REGION_SLOTS*PAGE_SIZE)
#define VM_REGION_HIGH_START (KERNEL_VMA_BASE+VM_REGION_START)
#define VM_REGION_FIRST_PTE (PAGE_ENTRIES-VM_REGION_SLOTS)
#define VM_REGION_PT_INDEX (PAGE_TABLES_PER_ALIAS-1)
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
#define PTE_USER 0x004ULL
#define USER_CODE_SLOT 0U
#define USER_STACK_SLOT 1U
#define IST1_INDEX 1
struct mb2_tag { u32 type; u32 size; } __attribute__((packed));
struct mb2_mmap_tag { u32 type; u32 size; u32 entry_size; u32 entry_version; } __attribute__((packed));
struct mb2_mmap_entry { u64 addr; u64 len; u32 type; u32 reserved; } __attribute__((packed));
struct long_mode_handoff { u64 pml4,pdpt,pd,idt_address,pt[PAGE_TABLES_PER_ALIAS]; u64 kernel_start,kernel_end,stack_start,stack_end; u64 high_pdpt,high_pd,high_pt[PAGE_TABLES_PER_ALIAS]; u64 user_code_phys,user_stack_phys; u64 kernel_vma_base,kernel_phys_base; u32 mbi_address,mbi_size; };
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
/* Lesson 35 keeps one bounded process object and one user thread. The
 * thread owns the validated CPL3 IRQ0 return context; no user IRQ callback
 * or second user address space is permitted. */
enum process_state { PROCESS_EMPTY, PROCESS_READY, PROCESS_RUNNING, PROCESS_EXITED };
enum user_thread_state { USER_THREAD_EMPTY, USER_THREAD_READY, USER_THREAD_RUNNING, USER_THREAD_EXITED };
struct saved_user_context { struct syscall_frame frame; u64 saves, last_syscall, last_result, pit_preemptions, pit_resumes; u8 valid; };
struct process { u64 pid; struct address_space *address_space; u64 code_phys, stack_phys, entry, stack_top; u32 image_bytes; u8 state, context_valid; };
struct user_thread { u64 tid, kernel_stack_top, kernel_stack_bytes, context_address, transitions; u8 state; struct process *process; struct saved_user_context context; };
_Static_assert(sizeof(struct irq0_frame)==20*sizeof(u64),"IRQ0 frame");
_Static_assert(sizeof(struct syscall_frame)==20*sizeof(u64),"syscall frame");
_Static_assert(__builtin_offsetof(struct irq0_frame,rsp)==18*sizeof(u64),"IRQ0 rsp offset");
_Static_assert(__builtin_offsetof(struct irq0_frame,ss)==19*sizeof(u64),"IRQ0 ss offset");
extern void exception_bp(void); extern void exception_ud(void); extern void exception_pf(void); extern void syscall_entry(void); extern void irq0_entry(void); extern void irq1_entry(void); extern void enter_user_c(void);
extern void thread_trampoline(void); extern void idle_trampoline(void);
static volatile u64 ticks;
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
static u64 user_code_phys,user_stack_phys;
static struct process user_process;
static struct user_thread user_thread;
static u8 pmm_ready;
static const char *pmm_error;
enum mapping_owner { MAP_OWNER_NONE=0, MAP_OWNER_KERNEL=1, MAP_OWNER_USER=2 };
struct vm_mapping { u64 phys; u8 live; u8 owner; };
struct address_space { struct long_mode_handoff *tables; u64 low_start,low_end,high_start; u8 kernel_mappings,user_mappings,initialized; };
static struct vm_mapping vm_mappings[VM_REGION_SLOTS];
static struct address_space kernel_address_space;
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
static TEXT64 int overlap(u64 a,u64 b,u64 c,u64 d){return a<d&&c<b;}
static TEXT64 u64 up(u64 v){return(v+PAGE_SIZE-1)&~(PAGE_SIZE-1);}
static TEXT64 u64 down(u64 v){return v&~(PAGE_SIZE-1);}
static TEXT64 int bit(u32 n){return(pmm_bitmap[n>>3]>>(n&7))&1;}
static TEXT64 void mark(u32 n){pmm_bitmap[n>>3]|=(u8)(1U<<(n&7));}
static TEXT64 void unmark(u32 n){pmm_bitmap[n>>3]&=(u8)~(1U<<(n&7));}
static TEXT64 int fixed(u32 n){return(pmm_fixed[n>>3]>>(n&7))&1;}
static TEXT64 void fix(u32 n){pmm_fixed[n>>3]|=(u8)(1U<<(n&7));}
static TEXT64 u64 phys_to_high(u64 p){return KERNEL_VMA_BASE+p;}
static TEXT64 int pmm_reserved(struct long_mode_handoff*h,u64 p){u64 e=p+PAGE_SIZE,b=(u64)(unsigned long)pmm_bitmap-KERNEL_VMA_BASE,z=b+2*PMM_BITMAP_BYTES;u32 i;if(p<0x100000||e<p||overlap(p,e,h->kernel_start,h->kernel_end)||overlap(p,e,h->stack_start,h->stack_end)||overlap(p,e,h->mbi_address,(u64)h->mbi_address+h->mbi_size)||overlap(p,e,h->pml4,h->pml4+PAGE_SIZE)||overlap(p,e,h->pdpt,h->pdpt+PAGE_SIZE)||overlap(p,e,h->pd,h->pd+PAGE_SIZE)||overlap(p,e,h->high_pdpt,h->high_pdpt+PAGE_SIZE)||overlap(p,e,h->high_pd,h->high_pd+PAGE_SIZE)||overlap(p,e,h->idt_address,h->idt_address+PAGE_SIZE)||overlap(p,e,h->user_code_phys,h->user_code_phys+PAGE_SIZE)||overlap(p,e,h->user_stack_phys,h->user_stack_phys+PAGE_SIZE)||overlap(p,e,b,z))return 1;for(i=0;i<PAGE_TABLES_PER_ALIAS;i++)if(overlap(p,e,h->pt[i],h->pt[i]+PAGE_SIZE)||overlap(p,e,h->high_pt[i],h->high_pt[i]+PAGE_SIZE))return 1;return 0;}
static TEXT64 const struct mb2_mmap_tag *mmap_tag64(struct long_mode_handoff*h){u32 off=8;if(h->mbi_size<16){pmm_error="MBI too small";return 0;}while(off<h->mbi_size){const struct mb2_tag*t;u32 r;if(h->mbi_size-off<8){pmm_error="truncated MBI tag";return 0;}t=(const struct mb2_tag*)((const u8*)(unsigned long)h->mbi_address+off);if(t->size<8||t->size>h->mbi_size-off){pmm_error="bad MBI tag size";return 0;}r=(t->size+7)&~7U;if(r<t->size||r>h->mbi_size-off){pmm_error="bad MBI tag alignment";return 0;}if(t->type==6){const struct mb2_mmap_tag*m=(const struct mb2_mmap_tag*)t;if(t->size<16||m->entry_size<sizeof(struct mb2_mmap_entry)||(t->size-16)%m->entry_size){pmm_error="bad mmap layout";return 0;}return m;}off+=r;}pmm_error="mmap tag missing";return 0;}
static TEXT64 void pmm_init(struct long_mode_handoff*h){const struct mb2_mmap_tag*m;u32 off,i;user_code_phys=h->user_code_phys;user_stack_phys=h->user_stack_phys;pmm_ready=0;pmm_error="not initialized";pmm_total=pmm_free=pmm_used=0;for(i=0;i<PMM_BITMAP_BYTES;i++){pmm_bitmap[i]=0xff;pmm_fixed[i]=0;}m=mmap_tag64(h);if(!m)return;for(off=0;off<m->size-16;off+=m->entry_size){const struct mb2_mmap_entry*e=(const struct mb2_mmap_entry*)((const u8*)m+16+off);u64 p,end;if(e->type!=1||e->addr+e->len<e->addr)continue;p=up(e->addr);end=down(e->addr+e->len);if(p>=PMM_MAX_PHYS)continue;if(end>PMM_MAX_PHYS)end=PMM_MAX_PHYS;while(p<end){unmark((u32)(p/PAGE_SIZE));p+=PAGE_SIZE;}}for(i=0;i<PMM_FRAMES;i++){u64 p=(u64)i*PAGE_SIZE;if(!bit(i)){pmm_total++;if(pmm_reserved(h,p)){mark(i);fix(i);}else pmm_free++;}}pmm_used=pmm_total-pmm_free;pmm_ready=1;pmm_error="ready";}
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
static TEXT64 int waitq_push(volatile struct wait_queue*q,u8 id){if(q->count>=WAIT_QUEUE_CAP)return 0;q->ids[q->head]=id;q->head=(u8)((q->head+1)%WAIT_QUEUE_CAP);q->count++;q->enqueues++;return 1;}
static TEXT64 int waitq_pop(volatile struct wait_queue*q,u8 *id){if(!q->count)return 0;*id=q->ids[q->tail];q->tail=(u8)((q->tail+1)%WAIT_QUEUE_CAP);q->count--;return 1;}
static TEXT64 int waitq_wake_one(volatile struct wait_queue*q,u8 state,u8 *out){u8 id;if(!waitq_pop(q,&id))return 0;if(!id||id>=THREAD_COUNT||threads[id].state!=state)return 0;threads[id].state=THREAD_RUNNABLE;q->wake_one++;*out=id;return 1;}
TEXT64 u8 waitq_wake_all(volatile struct wait_queue*q,u8 state){u8 id,n=0;while(waitq_pop(q,&id))if(id&&id<THREAD_COUNT&&threads[id].state==state){threads[id].state=THREAD_RUNNABLE;n++;}q->wake_all+=n;return n;}
static TEXT64 void waitq_reset(volatile struct wait_queue*q){q->head=q->tail=q->count=0;q->enqueues=q->wake_one=q->wake_all=0;}
static TEXT64 void event_set(struct event*e){u64 flags=irq_save64();e->signaled=1;e->sets++;e->wakes+=waitq_wake_all(&e->waitq,THREAD_BLOCKED_EVENT);irq_restore64(flags);}
static TEXT64 void event_wait(struct event*e){u8 id=current_thread;for(;;){u64 flags=irq_save64();if(e->signaled){irq_restore64(flags);return;}if(id&&threads[id].state==THREAD_RUNNING&&waitq_push(&e->waitq,id)){e->waits++;threads[id].state=THREAD_BLOCKED_EVENT;}irq_restore64(flags);while(threads[id].state==THREAD_BLOCKED_EVENT)__asm__ volatile("sti; hlt");}}
static TEXT64 void sem_init(struct semaphore*s,u8 count,u8 max){s->count=count;s->max=max;waitq_reset(&s->waitq);s->downs=s->ups=s->blocks=s->wakes=s->overflows=0;}
static TEXT64 void sem_down(struct semaphore*s){u8 id=current_thread;for(;;){u64 flags=irq_save64();if(s->count){s->count--;s->downs++;irq_restore64(flags);return;}if(id&&threads[id].state==THREAD_RUNNING&&waitq_push(&s->waitq,id)){s->blocks++;threads[id].state=THREAD_BLOCKED_SEM;}irq_restore64(flags);while(threads[id].state==THREAD_BLOCKED_SEM)__asm__ volatile("sti; hlt");}}
static TEXT64 void sem_up(struct semaphore*s){u64 flags=irq_save64();u8 id;if(s->count<s->max){s->count++;s->ups++;}else s->overflows++;if(waitq_wake_one(&s->waitq,THREAD_BLOCKED_SEM,&id))s->wakes++;irq_restore64(flags);}
static TEXT64 void pc_reset(void){u64 flags=irq_save64();pc_head=pc_tail=pc_used=pc_next=pc_expected=0;pc_produced=pc_consumed=pc_sequence_errors=0;pc_start_event.signaled=0;pc_start_event.sets=pc_start_event.resets=pc_start_event.waits=pc_start_event.wakes=0;waitq_reset(&pc_start_event.waitq);sem_init(&pc_spaces,PC_BUFFER_CAP,PC_BUFFER_CAP);sem_init(&pc_items,0,PC_BUFFER_CAP);irq_restore64(flags);}
static TEXT64 void pc_producer(void){u8 value;while(threads[1].progress<THREAD_STEPS){sem_down(&pc_spaces);{u64 flags=irq_save64();value=pc_next++;pc_buffer[pc_head]=value;pc_head=(u8)((pc_head+1)%PC_BUFFER_CAP);pc_used++;pc_produced++;irq_restore64(flags);}threads[1].progress++;sem_up(&pc_items);busy_delay();}thread_exit();}
static TEXT64 void pc_consumer(void){u8 value;while(threads[2].progress<THREAD_STEPS){sem_down(&pc_items);{u64 flags=irq_save64();value=pc_buffer[pc_tail];pc_tail=(u8)((pc_tail+1)%PC_BUFFER_CAP);pc_used--;if(value!=pc_expected)pc_sequence_errors++;pc_expected++;pc_consumed++;irq_restore64(flags);}threads[2].progress++;sem_up(&pc_spaces);busy_delay();}thread_exit();}
static TEXT64 u8 next_runnable(void){u32 n;for(n=1;n<=THREAD_COUNT;n++){u8 i=(u8)((round_robin+n)%THREAD_COUNT);if(threads[i].state==THREAD_RUNNABLE||threads[i].state==THREAD_RUNNING){round_robin=i;return i;}}return 0xff;}
static TEXT64 void reap_finished_threads(void){u32 i;for(i=1;i<THREAD_COUNT;i++)if((idle_running||i!=current_thread)&&threads[i].state==THREAD_FINISHED&&threads[i].stack_phys){u64 p=threads[i].stack_phys;threads[i].stack_phys=0;(void)pmm_free_page(p);}}
/* Called from IRQ0 only. It returns the exact frame restored by IRQ0's one iretq path. */
TEXT64 struct irq0_frame *irq0_schedule(struct irq0_frame *f){u8 old,next;ticks++;outb64(PIC1_COMMAND,PIC_EOI);if(f&&f->cs==USER_CS){user_irq0_save_restore(f);return f;}if(idle_running){idle_frame=f;idle_ticks++;}else threads[current_thread].frame=(u64)(unsigned long)f;wake_sleepers();reap_finished_threads();if(!idle_running&&quantum_left){quantum_left--;if(quantum_left)return f;}old=current_thread;next=next_runnable();quantum_left=TIME_SLICE_TICKS;if(next==0xff){if(idle_running)return f;if(threads[old].state==THREAD_RUNNING)threads[old].state=THREAD_RUNNABLE;idle_running=1;idle_switches++;return idle_frame;}if(idle_running){idle_running=0;threads[next].state=THREAD_RUNNING;current_thread=next;threads[next].switches++;preempt_switches++;return (struct irq0_frame *)(unsigned long)threads[next].frame;}if(next==old){if(old==0)idle_worker_ticks++;return f;}if(threads[old].state==THREAD_RUNNING)threads[old].state=THREAD_RUNNABLE;threads[next].state=THREAD_RUNNING;current_thread=next;threads[next].switches++;preempt_switches++;return (struct irq0_frame *)(unsigned long)threads[next].frame;}
static TEXT64 void busy_delay(void){volatile u64 n;for(n=0;n<BUSY_SPINS;n++)__asm__ volatile("":::"memory");}
static TEXT64 void thread_sleep_ticks(u64 delta){u64 flags;u8 id=current_thread;if(!delta)delta=1;flags=irq_save64();if(!idle_running&&threads[id].state==THREAD_RUNNING){threads[id].wake_tick=ticks+delta;threads[id].state=THREAD_SLEEPING;}irq_restore64(flags);while(threads[id].state==THREAD_SLEEPING)__asm__ volatile("sti; hlt");}
static TEXT64 void kbd_wait_char(u8 *out){u8 id=current_thread;for(;;){u64 flags=irq_save64();if(id&&threads[id].mailbox_ready){*out=threads[id].mailbox;threads[id].mailbox_ready=0;irq_restore64(flags);return;}if(id&&threads[id].state==THREAD_RUNNING&&waitq_push(&kbd_waitq,id))threads[id].state=THREAD_BLOCKED_KBD;irq_restore64(flags);while(threads[id].state==THREAD_BLOCKED_KBD)__asm__ volatile("sti; hlt");}}
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
    user_thread.transitions++; return 1;
}
static TEXT64 int user_process_exit(void)
{
    if(user_process.state!=PROCESS_RUNNING || user_thread.state!=USER_THREAD_RUNNING ||
       !user_context_valid(&user_thread.context)) return 0;
    user_process.state=PROCESS_EXITED; user_thread.state=USER_THREAD_EXITED;
    user_thread.transitions++; return 1;
}
static TEXT64 const char *process_state_name(u8 s){return s==PROCESS_READY?"ready":s==PROCESS_RUNNING?"running":s==PROCESS_EXITED?"exited":"empty";}
static TEXT64 const char *user_thread_state_name(u8 s){return s==USER_THREAD_READY?"ready":s==USER_THREAD_RUNNING?"running":s==USER_THREAD_EXITED?"exited":"empty";}
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
    text64(c,"\ncontext lifecycle: ");text64(c,user_process.context_valid?"validated":"not validated");text64(c," transitions ");hex64(c,user_thread.transitions);putc64(c,'\n');
}
static TEXT64 int process_lifecycle_test(u16*c)
{
    int ok=user_process.state==PROCESS_READY && user_thread.state==USER_THREAD_READY &&
        user_process.address_space==&kernel_address_space && user_process.code_phys==user_code_phys &&
        user_process.stack_phys==user_stack_phys && user_thread.process==&user_process &&
        user_thread.kernel_stack_top==runtime_tss.rsp0 && !user_thread.context.valid;
    text64(c,"process lifecycle: ");text64(c,ok?"bounded one-user-thread object ready":"BROKEN");putc64(c,'\n');
    return ok;
}
static TEXT64 u64 syscall_dispatch(struct syscall_frame*f,u16*c){switch((u32)f->rax){case SYS_GETTICKS:return ticks;case SYS_GETPID:return FIXED_PID;case SYS_WRITE_CONSOLE:text64(c,"kernel-owned console message\n");return 0;case SYS_EXIT:return 0;default:return (u64)(-(s64)ENOSYS);}}
TEXT64 void syscall_report(struct syscall_frame*f){u16 c=0;u64 number=f->rax,result;clear64(&c);if((u32)f->rax==SYS_EXIT){user_context_save(f,0);text64(&c,"TinyOS lesson 35 SYS_EXIT\nuser requested controlled exit\n");if(user_process_exit())text64(&c,"saved user context validated; process/thread exited\n");else text64(&c,"controlled exit rejected: invalid lifecycle\n");text64(&c,"halting intentionally\n");for(;;)__asm__ volatile("cli; hlt");}result=syscall_dispatch(f,&c);user_context_save(f,result);text64(&c,"TinyOS lesson 35 syscall dispatcher\nsyscall number: ");hex64(&c,number);text64(&c,"\nreturn rax: ");f->rax=result;hex64(&c,f->rax);text64(&c,"\nuser rip: ");hex64(&c,f->rip);text64(&c,"\nuser cs: ");hex64(&c,f->cs);text64(&c,"\nuser rsp: ");hex64(&c,f->rsp);text64(&c,"\nuser ss: ");hex64(&c,f->ss);text64(&c,"\nall-GPR frame; returning with iretq; user IF remains disabled\n");}
TEXT64 void exception_report_ist(struct exception_frame_ist*f){u16 c=0;u64 cr2=0,rsp;clear64(&c);__asm__ volatile("mov %%rsp,%0":"=r"(rsp));text64(&c,"TinyOS lesson 27 IST exception\nexception: #PF\nvector: ");hex64(&c,f->vector);text64(&c,"\nerror:  ");hex64(&c,f->error);text64(&c,"\nrip:    ");hex64(&c,f->rip);text64(&c,"\nsaved rsp: ");hex64(&c,f->rsp);text64(&c,"\nhandler rsp: ");hex64(&c,rsp);text64(&c,"\nIST1 range: ");hex64(&c,(u64)(unsigned long)__ist1_stack_start);text64(&c," ");hex64(&c,runtime_tss.ist1);__asm__ volatile("mov %%cr2,%0":"=r"(cr2));text64(&c,"\ncr2:    ");hex64(&c,cr2);text64(&c,"\nCPU halted intentionally.\n");for(;;)__asm__ volatile("cli; hlt");}
TEXT64 void breakpoint_report(struct exception_frame*f){u16 c=10*COLS;u64 *raw=(u64 *)f;text64(&c,"TinyOS lesson 27 breakpoint\nexception: #BP\nvector: ");hex64(&c,raw[0]);text64(&c,"\nerror:  ");hex64(&c,raw[1]);text64(&c,"\nrip:    ");hex64(&c,raw[3]);text64(&c,"\ncs:     ");hex64(&c,raw[4]);text64(&c,"\nrflags: ");hex64(&c,raw[5]);text64(&c,"\nreturning with iretq...\n");}
TEXT64 void exception_report(struct exception_frame*f){u16 c=0;u64 cr2=0,rsp;clear64(&c);text64(&c,"TinyOS lesson 28 exception\nexception: ");if(f->vector==6)text64(&c,"#UD");else if(f->vector==14)text64(&c,"#PF");else text64(&c,"unknown");print_exception_frame(&c,f);__asm__ volatile("mov %%rsp,%0":"=r"(rsp));if(f->vector==6&&f->cs==USER_CS){text64(&c,"\nCPL3 #UD proof: user CS and kernel rsp0 active\nhandler rsp: ");hex64(&c,rsp);text64(&c,"\nrsp0: ");hex64(&c,runtime_tss.rsp0);text64(&c,"\nsaved user rsp: ");hex64(&c,f->rsp);text64(&c,"\nsaved user ss: ");hex64(&c,f->ss);text64(&c,"\nCPU halted intentionally.\n");}else {if(f->vector==14){__asm__ volatile("mov %%cr2,%0":"=r"(cr2));text64(&c,"\ncr2:    ");hex64(&c,cr2);}text64(&c,"\nCPU halted intentionally.\n");}for(;;)__asm__ volatile("cli; hlt");}
/* IRQ0 scheduling is performed by irq0_schedule at its iretq return boundary. */
TEXT64 void irq1_record(void){u8 raw=inb64(0x60),ch,next,id;irq1_last_scancode=raw;irq1_raw_count++;if(!(raw&0x80)){irq1_count++;ch=(u8)scan64(raw);if(ch){if(waitq_wake_one(&kbd_waitq,THREAD_BLOCKED_KBD,&id)){threads[id].mailbox=ch;threads[id].mailbox_ready=1;kbd_direct_deliveries++;}else{next=(u8)((kbd_head+1)&(KBD_QUEUE_SIZE-1));if(next==kbd_tail)kbd_overflow_count++;else{kbd_queue[kbd_head]=ch;kbd_head=next;}}}}outb64(PIC1_COMMAND,PIC_EOI);}
static TEXT64 int kbd_dequeue(u8 *ch){u8 tail;__asm__ volatile("cli":::"memory");tail=kbd_tail;if(tail==kbd_head){__asm__ volatile("sti":::"memory");return 0;}*ch=kbd_queue[tail];kbd_tail=(u8)((tail+1)&(KBD_QUEUE_SIZE-1));__asm__ volatile("sti":::"memory");return 1;}
static TEXT64 void usage64(u16*c,const char*s){text64(c,"usage: ");text64(c,s);putc64(c,'\n');}
static TEXT64 void enter_user(struct long_mode_handoff*h){if(!user_process_enter(h)){return;}__asm__ volatile("cli; call enter_user_c":::"memory");}
static TEXT64 void vmtest(u16*c,struct long_mode_handoff*h){(void)h;u64 p[2],va[2]={VM_REGION_START,VM_REGION_START+PAGE_SIZE},before,after;volatile u64 *v,*q;const char*r;u32 i;before=pmm_free;for(i=0;i<2;i++){p[i]=pmm_alloc();if(!p[i]){text64(c,"vmtest allocation failed\n");return;}r=address_space_map(&kernel_address_space,va[i],p[i],MAP_OWNER_USER);if(!eq64(r,"mapped")){text64(c,"vmtest map failed: ");text64(c,r);putc64(c,'\n');return;}if(!eq64(pmm_free_page(p[i]),"mapped")){text64(c,"vmtest mapped-frame ownership failed\n");return;}v=(volatile u64 *)(unsigned long)va[i];q=(volatile u64 *)(unsigned long)(KERNEL_VMA_BASE+va[i]);*v=0x564d544553543237ULL+i;if(*q!=0x564d544553543237ULL+i){text64(c,"vmtest low/high mismatch\n");return;}*q=0x48494748564d3237ULL+i;if(*v!=0x48494748564d3237ULL+i){text64(c,"vmtest high/low mismatch\n");return;}}for(i=0;i<2;i++){r=address_space_release(&kernel_address_space,va[i]);if(!eq64(r,"unmapped")||!eq64(pmm_free_page(p[i]),"freed")){text64(c,"vmtest unmap/free failed\n");return;}}after=pmm_free;if(after!=before){text64(c,"vmtest PMM accounting failed\n");return;}text64(c,"vmtest: two-slot dual-alias map/ownership/unmap/free passed\n");}
static TEXT64 void exec64(u16*c,struct long_mode_handoff*h,const char*s){char word[16];const char*arg;u64 p;if(!(arg=token64(s,word,sizeof(word)))){text64(c,"command too long\n");prompt64(c);return;}if(!word[0]){prompt64(c);return;}if(eq64(word,"help")){if(!noargs64(arg))usage64(c,"help");else text64(c,"commands: help about processinfo processtest userpitest clear lminfo hhinfo hhtest tssinfo stackinfo stackguardtest isttest preempttest sleeptest kbdwaittest pctest pcgo pcinfo idletest threadstart yield ps threadinfo meminfo palloc pfree <hex> pageinfo <hex> vmap <low-va> <phys> vunmap <low-va> vminfo [low-va] vmtest vmfaulttest mmap idtinfo tickinfo uptime kbdinfo syscallinfo cpl3test bptest udtest pftest\n");}else if(eq64(word,"about")){if(!noargs64(arg))usage64(c,"about");else text64(c,"TinyOS lesson 35: bounded CPL3 PIT preemption with saved user context\n");}else if(eq64(word,"threadstart")||eq64(word,"preempttest")){if(!noargs64(arg))usage64(c,word);else{int r=start_threads(0);if(r>0)text64(c,"preempttest: two non-yielding workers started\n");else if(!r)text64(c,"preempttest: already started\n");else text64(c,"preempttest: PMM allocation failed\n");}}else if(eq64(word,"sleeptest")){if(!noargs64(arg))usage64(c,"sleeptest");else{int r=start_threads(1);if(r>0)text64(c,"sleeptest: two timed workers started\n");else if(!r)text64(c,"sleeptest: already started\n");else text64(c,"sleeptest: PMM allocation failed\n");}}else if(eq64(word,"kbdwaittest")){if(!noargs64(arg))usage64(c,"kbdwaittest");else{int r=start_threads(2);if(r>0)text64(c,"kbdwaittest: two FIFO keyboard waiters started\n");else if(!r)text64(c,"kbdwaittest: already started\n");else text64(c,"kbdwaittest: PMM allocation failed\n");}}else if(eq64(word,"pctest")){if(!noargs64(arg))usage64(c,"pctest");else{int r=start_threads(3);if(r>0)text64(c,"pctest: producer and consumer blocked on start event; run pcgo\n");else if(!r)text64(c,"pctest: already started\n");else text64(c,"pctest: PMM allocation failed\n");}}else if(eq64(word,"pcgo")){if(!noargs64(arg))usage64(c,"pcgo");else if(!pc_test)text64(c,"pcgo: run pctest first\n");else if(pc_start_event.signaled)text64(c,"pcgo: start event already set\n");else{event_set(&pc_start_event);text64(c,"pcgo: event set; broadcast wake-all issued\n");}}else if(eq64(word,"pcinfo")){if(!noargs64(arg))usage64(c,"pcinfo");else pcinfo(c);}else if(eq64(word,"processinfo")){if(!noargs64(arg))usage64(c,"processinfo");else processinfo(c);}else if(eq64(word,"processtest")){if(!noargs64(arg))usage64(c,"processtest");else process_lifecycle_test(c);}else if(eq64(word,"tssinfo")){if(!noargs64(arg))usage64(c,"tssinfo");else tssinfo(c,h);}else if(eq64(word,"stackinfo")){if(!noargs64(arg))usage64(c,"stackinfo");else{text64(c,"idle guard/payload/end: ");hex64(c,(u64)(unsigned long)__idle_guard_start);text64(c," ");hex64(c,(u64)(unsigned long)__idle_stack_start);text64(c," ");hex64(c,(u64)(unsigned long)__idle_stack_end);text64(c,"\nrsp0 guard/payload/end: ");hex64(c,(u64)(unsigned long)__rsp0_guard_start);text64(c," ");hex64(c,(u64)(unsigned long)__rsp0_stack_start);text64(c," ");hex64(c,(u64)(unsigned long)__rsp0_stack_end);text64(c,"\nIST1 guard/payload/end: ");hex64(c,(u64)(unsigned long)__ist1_guard_start);text64(c," ");hex64(c,(u64)(unsigned long)__ist1_stack_start);text64(c," ");hex64(c,(u64)(unsigned long)__ist1_stack_end);putc64(c,'\n');}}else if(eq64(word,"stackguardtest")){if(eq64(arg,"idle")||eq64(arg,"rsp0")||eq64(arg,"ist1")){volatile u64 *bad=(volatile u64 *)(unsigned long)(eq64(arg,"idle")?(u64)(unsigned long)__idle_guard_start:eq64(arg,"rsp0")?(u64)(unsigned long)__rsp0_guard_start:(u64)(unsigned long)__ist1_guard_start);text64(c,"stackguardtest: fatal #PF expected\n");p=*bad;(void)p;}else usage64(c,"stackguardtest idle|rsp0|ist1");}else if(eq64(word,"isttest")){if(!noargs64(arg))usage64(c,"isttest");else{volatile u64 *bad=(volatile u64 *)VM_REGION_START;text64(c,"isttest: triggering #PF on IST1 (fatal)\n");p=*bad;(void)p;}}else if(eq64(word,"idletest")){if(!noargs64(arg))usage64(c,"idletest");else{text64(c,"idletest: shell sleeping while idle runs\n");thread_sleep_ticks(150);text64(c,"idletest: shell resumed through IRQ0\n");}}else if(eq64(word,"yield")){if(!noargs64(arg))usage64(c,"yield");else text64(c,"yield: cooperative switching replaced by PIT preemption\n");}else if(eq64(word,"ps")){if(!noargs64(arg))usage64(c,"ps");else ps64(c);}else if(eq64(word,"threadinfo")){if(!noargs64(arg))usage64(c,"threadinfo");else threadinfo(c);}else if(eq64(word,"lminfo")){if(!noargs64(arg))usage64(c,"lminfo");else lminfo(c,h);}else if(eq64(word,"hhinfo")){if(!noargs64(arg))usage64(c,"hhinfo");else hhinfo(c,h);}else if(eq64(word,"hhtest")){if(!noargs64(arg))usage64(c,"hhtest");else hhtest(c);}else if(eq64(word,"idtinfo")){if(!noargs64(arg))usage64(c,"idtinfo");else idtinfo(c,h);}else if(eq64(word,"tickinfo")||eq64(word,"uptime")){if(!noargs64(arg))usage64(c,word);else tickinfo(c);}else if(eq64(word,"kbdinfo")){if(!noargs64(arg))usage64(c,"kbdinfo");else kbdinfo(c);}else if(eq64(word,"meminfo")){if(!noargs64(arg))usage64(c,"meminfo");else meminfo(c);}else if(eq64(word,"palloc")){if(!noargs64(arg))usage64(c,"palloc");else if(!pmm_ready)text64(c,"PMM unavailable: "),text64(c,pmm_error),putc64(c,'\n');else {p=pmm_alloc();if(p){text64(c,"allocated: ");hex64(c,p);putc64(c,'\n');}else text64(c,"allocator exhausted\n");}}else if(eq64(word,"pfree")||eq64(word,"pageinfo")){if(!hexarg64(arg,&p))usage64(c,eq64(word,"pfree")?"pfree <hex>":"pageinfo <hex>");else if(eq64(word,"pfree")){const char*r=pmm_free_page(p);if(eq64(r,"freed"))text64(c,"freed\n");else {text64(c,"cannot free: ");text64(c,r);putc64(c,'\n');}}else {text64(c,"page: ");hex64(c,p);text64(c," state: ");text64(c,page_state(p));putc64(c,'\n');}}else if(eq64(word,"vmap")){const char*r;u64 va;if(!(arg=token64(arg,word,sizeof(word)))||!hexarg64(word,&va)||!hexarg64(arg,&p))usage64(c,"vmap <low-va> <phys>");else {r=address_space_map(&kernel_address_space,va,p,MAP_OWNER_USER);if(eq64(r,"mapped")){text64(c,"mapped: ");hex64(c,p);text64(c," at ");hex64(c,va);putc64(c,'\n');}else{text64(c,"cannot map: ");text64(c,r);putc64(c,'\n');}}}else if(eq64(word,"vunmap")){const char*r;if(!hexarg64(arg,&p))usage64(c,"vunmap <low-va>");else{r=address_space_release(&kernel_address_space,p);text64(c,r);putc64(c,'\n');}}else if(eq64(word,"vminfo")){if(noargs64(arg))vminfo(c,h,0,0);else if(hexarg64(arg,&p))vminfo(c,h,p,1);else usage64(c,"vminfo [low-va]");}else if(eq64(word,"vmtest")){if(!noargs64(arg))usage64(c,"vmtest");else vmtest(c,h);}else if(eq64(word,"vmfaulttest")){if(!noargs64(arg))usage64(c,"vmfaulttest");else{volatile u64 *bad=(volatile u64 *)VM_REGION_START;text64(c,"triggering VM slot #PF\n");p=*bad;(void)p;}}else if(eq64(word,"mmap")){if(!noargs64(arg))usage64(c,"mmap");else mmap64(c,h);}else if(eq64(word,"syscallinfo")){if(!noargs64(arg))usage64(c,"syscallinfo");else text64(c,"syscalls: 0=GETTICKS 1=GETPID 2=WRITE_CONSOLE 3=EXIT; unknown=-ENOSYS\nWRITE_CONSOLE uses a fixed kernel-owned message and no user pointer\nEXIT reports and intentionally halts; no user IRQ or scheduler handling\n");}else if(eq64(word,"userpitest")){if(!noargs64(arg))usage64(c,"userpitest");else{text64(c,"entering CPL3 with IF=0; IRQ0 saves/restores one bounded user frame\n");enter_user(h);}}else if(eq64(word,"cpl3test")){if(!noargs64(arg))usage64(c,"cpl3test");else{ text64(c,"entering CPL3 syscall stub with IF=0; calls 0,1,2,99,3 (EXIT)\n"); enter_user(h); }}else if(eq64(word,"bptest")){if(!noargs64(arg))usage64(c,"bptest");else{text64(c,"triggering #BP\n");__asm__ volatile("int3":::"rax","rcx","rdx","rsi","rdi","r8","r9","r10","r11","cc","memory");text64(c,"#BP returned to shell\n");}}else if(eq64(word,"udtest")){if(!noargs64(arg))usage64(c,"udtest");else{text64(c,"triggering #UD\n");__asm__ volatile("ud2");}}else if(eq64(word,"pftest")){if(!noargs64(arg))usage64(c,"pftest");else{volatile u64 *bad=(volatile u64 *)0x00400000ULL;text64(c,"triggering #PF\n");p=*bad;(void)p;}}else if(eq64(word,"clear")){if(!noargs64(arg))usage64(c,"clear");else{clear64(c);prompt64(c);return;}}else text64(c,"unknown command\n");prompt64(c);}
ENTRY64 void kernel_main64_binary(struct long_mode_handoff*h){u16 c=0,n=0;pmm_init(h);address_space_init(&kernel_address_space,h);
    user_process.pid=FIXED_PID; user_process.address_space=&kernel_address_space; user_process.code_phys=user_code_phys; user_process.stack_phys=user_stack_phys;
    user_process.entry=USER_CODE_VA; user_process.stack_top=USER_STACK_TOP; user_process.image_bytes=7; user_process.state=PROCESS_READY; user_process.context_valid=0;
    user_thread.tid=FIXED_PID; user_thread.process=&user_process; user_thread.kernel_stack_top=runtime_tss.rsp0; user_thread.kernel_stack_bytes=0; user_thread.context_address=0; user_thread.transitions=0; user_thread.state=USER_THREAD_READY; user_thread.context.valid=0;
    threads[0].id=0;threads[0].state=THREAD_RUNNING;quantum_left=TIME_SLICE_TICKS;char cmd[32];u8 ch;__asm__ volatile("cli":::"memory");stack_guards_init(h);runtime_gdt_tss_init();user_thread.kernel_stack_top=runtime_tss.rsp0;user_thread.kernel_stack_bytes=PAGE_SIZE;idle_init();install_idt(h);pit_init();pic_init();clear64(&c);text64(&c,"TinyOS lesson 35: validated user image with bounded PIT preemption\nGETTICKS, GETPID, WRITE_CONSOLE, EXIT; unknown=-ENOSYS; all-GPR CPL3 IRQ0 frame; IF policy preserved\n");prompt64(&c);__asm__ volatile("sti":::"memory");for(;;){if(!kbd_dequeue(&ch)){__asm__ volatile("sti; hlt":::"memory");continue;}if(ch=='\n'){putc64(&c,ch);cmd[n]=0;exec64(&c,h,cmd);n=0;}else if(ch=='\b'){if(n){n--;c--;VGA[c]=0x0f20;}}else if(n<31){cmd[n++]=(char)ch;putc64(&c,(char)ch);}}}
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
