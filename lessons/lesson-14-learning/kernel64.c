/* Lesson 13: PIT IRQ0 timer plus IRQ1 ring-buffer keyboard shell. */
typedef unsigned char u8; typedef unsigned int u32; typedef unsigned short u16; typedef unsigned long long u64;
#define TEXT64 __attribute__((section(".text64"), noinline))
#define ENTRY64 __attribute__((section(".text64.entry"), noinline))
#define VGA ((volatile u16 *)0xb8000ULL)
#define COLS 80
#define ROWS 25
#define PAGE_SIZE 0x1000ULL
#define PMM_MAX_PHYS 0x00400000ULL
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
struct mb2_tag { u32 type; u32 size; } __attribute__((packed));
struct mb2_mmap_tag { u32 type; u32 size; u32 entry_size; u32 entry_version; } __attribute__((packed));
struct mb2_mmap_entry { u64 addr; u64 len; u32 type; u32 reserved; } __attribute__((packed));
struct long_mode_handoff { u64 pml4,pdpt,pd,pt0,pt1,idt_address; u64 kernel_start,kernel_end,stack_start,stack_end; u32 mbi_address,mbi_size; };
struct idt_gate { u16 offset_low, selector; u8 ist, type; u16 offset_mid; u32 offset_high, reserved; } __attribute__((packed));
struct idtr { u16 limit; u64 base; } __attribute__((packed));
struct exception_frame { u64 vector,error,rip,cs,rflags; };
extern void exception_bp(void); extern void exception_ud(void); extern void exception_pf(void); extern void irq0_entry(void); extern void irq1_entry(void);
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
static TEXT64 int command_eq(const char*s,const char*word){while(*s&&*s!=' '&&*word){if(*s++!=*word++)return 0;}return !*word&&(!*s||*s==' ');}
static TEXT64 void clear64(u16*c){u16 i;for(i=0;i<COLS*ROWS;i++)VGA[i]=0x0f20;*c=0;}
static TEXT64 void prompt64(u16*c){text64(c,"tinyos> ");}
static volatile u8 pmm_bitmap[PMM_BITMAP_BYTES];
static volatile u8 pmm_fixed[PMM_BITMAP_BYTES];
static u64 pmm_total,pmm_free,pmm_used;
static TEXT64 int overlap(u64 a,u64 b,u64 c,u64 d){return a<d&&c<b;}
static TEXT64 u64 up(u64 v){return(v+PAGE_SIZE-1)&~(PAGE_SIZE-1);}
static TEXT64 u64 down(u64 v){return v&~(PAGE_SIZE-1);}
static TEXT64 int bit(u32 n){return(pmm_bitmap[n>>3]>>(n&7))&1;}
static TEXT64 void mark(u32 n){pmm_bitmap[n>>3]|=(u8)(1U<<(n&7));}
static TEXT64 void unmark(u32 n){pmm_bitmap[n>>3]&=(u8)~(1U<<(n&7));}
static TEXT64 int fixed(u32 n){return(pmm_fixed[n>>3]>>(n&7))&1;}
static TEXT64 void fix(u32 n){pmm_fixed[n>>3]|=(u8)(1U<<(n&7));}
static TEXT64 int pmm_reserved(struct long_mode_handoff*h,u64 p){u64 e=p+PAGE_SIZE,b=(u64)(unsigned long)pmm_bitmap,z=b+2*PMM_BITMAP_BYTES;if(p<0x100000||e<p||overlap(p,e,h->kernel_start,h->kernel_end)||overlap(p,e,h->stack_start,h->stack_end)||overlap(p,e,h->mbi_address,(u64)h->mbi_address+h->mbi_size)||overlap(p,e,h->pml4,h->pml4+PAGE_SIZE)||overlap(p,e,h->pdpt,h->pdpt+PAGE_SIZE)||overlap(p,e,h->pd,h->pd+PAGE_SIZE)||overlap(p,e,h->pt0,h->pt0+PAGE_SIZE)||overlap(p,e,h->pt1,h->pt1+PAGE_SIZE)||overlap(p,e,h->idt_address,h->idt_address+PAGE_SIZE)||overlap(p,e,b,z))return 1;return 0;}
static TEXT64 void pmm_init(struct long_mode_handoff*h){const struct mb2_mmap_tag*m=0;u32 off,i;for(i=0;i<PMM_BITMAP_BYTES;i++){pmm_bitmap[i]=0xff;pmm_fixed[i]=0;}for(off=8;off<h->mbi_size;){const struct mb2_tag*t=(const struct mb2_tag*)((const u8*)(unsigned long)h->mbi_address+off);u32 r=(t->size+7)&~7U;if(t->type==6){m=(const struct mb2_mmap_tag*)t;break;}if(t->size<8||r>h->mbi_size-off)return;off+=r;}if(!m)return;for(off=0;off<m->size-16;off+=m->entry_size){const struct mb2_mmap_entry*e=(const struct mb2_mmap_entry*)((const u8*)m+16+off);u64 p,end;if(e->type!=1||e->addr+e->len<e->addr)continue;p=up(e->addr);end=down(e->addr+e->len);if(p>=PMM_MAX_PHYS)continue;if(end>PMM_MAX_PHYS)end=PMM_MAX_PHYS;while(p<end){unmark((u32)(p/PAGE_SIZE));p+=PAGE_SIZE;}}pmm_total=pmm_free=pmm_used=0;for(i=0;i<PMM_FRAMES;i++){u64 p=(u64)i*PAGE_SIZE;if(!bit(i)){pmm_total++;if(pmm_reserved(h,p)){mark(i);fix(i);}else pmm_free++;}}pmm_used=pmm_total-pmm_free;}
static TEXT64 u64 pmm_alloc(void){u32 i;for(i=0;i<PMM_FRAMES;i++)if(!bit(i)){mark(i);pmm_free--;pmm_used++;return(u64)i*PAGE_SIZE;}return 0;}
static TEXT64 int pmm_free_page(u64 p){u32 i;if((p&(PAGE_SIZE-1))||p>=PMM_MAX_PHYS)return 0;i=(u32)(p/PAGE_SIZE);if(!bit(i))return 0;/* Reserved pages remain marked and cannot be freed. */if(fixed(i)||pmm_free>=pmm_total)return 0;unmark(i);pmm_free++;pmm_used--;return 1;}
static TEXT64 void meminfo(u16*c){text64(c,"PMM: 4 KiB physical frames in identity window\ntracked: ");hex64(c,pmm_total);text64(c,"\nfree:    ");hex64(c,pmm_free);text64(c,"\nused:    ");hex64(c,pmm_used);text64(c,"\nbitmap:  ");hex64(c,(u64)(unsigned long)pmm_bitmap);text64(c," +");hex64(c,PMM_BITMAP_BYTES);text64(c,"\nfixed:   ");hex64(c,(u64)(unsigned long)pmm_fixed);putc64(c,'\n');}
static TEXT64 int hexarg(const char*s,u64*v){u64 n=0;int d,seen=0;if(s[0]=='0'&&(s[1]=='x'||s[1]=='X'))s+=2;while(*s){char x=*s++;if(x>='0'&&x<='9')d=x-'0';else if(x>='a'&&x<='f')d=x-'a'+10;else if(x>='A'&&x<='F')d=x-'A'+10;else return 0;if(n>(~0ULL>>4))return 0;n=(n<<4)|(u64)d;seen=1;}if(!seen)return 0;*v=n;return 1;}
static TEXT64 void set_gate(struct idt_gate *g,u64 target){g->offset_low=(u16)target;g->selector=0x08;g->ist=0;g->type=IDT_GATE_INTERRUPT;g->offset_mid=(u16)(target>>16);g->offset_high=(u32)(target>>32);g->reserved=0;}
static TEXT64 u64 runtime_bp_address(void){u64 v;__asm__ volatile("leaq exception_bp(%%rip),%0":"=r"(v));return v;}
static TEXT64 u64 runtime_ud_address(void){u64 v;__asm__ volatile("leaq exception_ud(%%rip),%0":"=r"(v));return v;}
static TEXT64 u64 runtime_pf_address(void){u64 v;__asm__ volatile("leaq exception_pf(%%rip),%0":"=r"(v));return v;}
static TEXT64 u64 runtime_irq0_address(void){u64 v;__asm__ volatile("leaq irq0_entry(%%rip),%0":"=r"(v));return v;}
static TEXT64 u64 runtime_irq1_address(void){u64 v;__asm__ volatile("leaq irq1_entry(%%rip),%0":"=r"(v));return v;}
static TEXT64 void pic_masks(u8 master,u8 slave){outb64(PIC1_DATA,master);outb64(PIC2_DATA,slave);}
static TEXT64 void pit_init(void){outb64(PIT_COMMAND,0x36);outb64(PIT_CHANNEL0,(u8)PIT_DIVISOR);outb64(PIT_CHANNEL0,(u8)(PIT_DIVISOR>>8));}
static TEXT64 void pic_init(void){outb64(PIC1_COMMAND,0x11);io_wait64();outb64(PIC2_COMMAND,0x11);io_wait64();outb64(PIC1_DATA,0x20);io_wait64();outb64(PIC2_DATA,0x28);io_wait64();outb64(PIC1_DATA,4);io_wait64();outb64(PIC2_DATA,2);io_wait64();outb64(PIC1_DATA,1);io_wait64();outb64(PIC2_DATA,1);io_wait64();pic_masks(0xfc,0xff);}
static TEXT64 void install_idt(struct long_mode_handoff*h){struct idt_gate *idt=(struct idt_gate *)(unsigned long)h->idt_address;struct idtr d;u32 i;for(i=0;i<IDT_ENTRIES;i++){idt[i].offset_low=0;idt[i].selector=0;idt[i].ist=0;idt[i].type=0;idt[i].offset_mid=0;idt[i].offset_high=0;idt[i].reserved=0;}set_gate(&idt[3],runtime_bp_address());set_gate(&idt[6],runtime_ud_address());set_gate(&idt[14],runtime_pf_address());set_gate(&idt[0x20],runtime_irq0_address());set_gate(&idt[0x21],runtime_irq1_address());d.limit=sizeof(struct idt_gate)*IDT_ENTRIES-1;d.base=(u64)(unsigned long)idt;__asm__ volatile("lidt %0"::"m"(d):"memory");}
static TEXT64 void lminfo(u16*c,struct long_mode_handoff*h){text64(c,"long mode: on\npml4: ");hex64(c,h->pml4);text64(c,"\npdpt: ");hex64(c,h->pdpt);text64(c,"\npd:   ");hex64(c,h->pd);text64(c,"\npt0:  ");hex64(c,h->pt0);text64(c,"\npt1:  ");hex64(c,h->pt1);text64(c,"\nidentity: 0000000000000000 - 0000000000400000\n");}
static TEXT64 void idtinfo(u16*c,struct long_mode_handoff*h){text64(c,"IDT: exceptions + PIT IRQ0 + IRQ1\nbase: ");hex64(c,h->idt_address);text64(c,"\nlimit: 0000000000000fff\n#BP vector: 0000000000000003 (returns)\n#UD vector: 0000000000000006\n#PF vector: 000000000000000e\nIRQ0 vector: 0000000000000020\nIRQ1 vector: 0000000000000021\n");}
static TEXT64 void kbdinfo(u16*c){u8 head,tail;__asm__ volatile("cli":::"memory");head=kbd_head;tail=kbd_tail;__asm__ volatile("sti":::"memory");text64(c,"keyboard: IRQ1 producer, ring-buffer shell consumer\nIRQ1 enabled: yes\nraw bytes: ");hex64(c,irq1_raw_count);text64(c,"\nmake codes: ");hex64(c,irq1_count);text64(c,"\noverflows: ");hex64(c,kbd_overflow_count);text64(c,"\nlast raw: ");hex64(c,irq1_last_scancode);text64(c,"\nqueue head: ");hex64(c,head);text64(c,"\nqueue tail: ");hex64(c,tail);putc64(c,'\n');}
static TEXT64 void tickinfo(u16*c){u64 t;__asm__ volatile("cli":::"memory");t=ticks;__asm__ volatile("sti":::"memory");text64(c,"PIT channel 0: 0000000000000064 Hz\nticks: ");hex64(c,t);text64(c,"\nuptime (centiseconds): ");hex64(c,t);putc64(c,'\n');}
static TEXT64 void mmap64(u16*c,struct long_mode_handoff*h){const struct mb2_mmap_tag*m;u32 off,n=0;text64(c,"Multiboot2 available ranges:\n");for(off=8;off<h->mbi_size;){const struct mb2_tag*t=(const struct mb2_tag*)((const u8*)(unsigned long)h->mbi_address+off);u32 r=(t->size+7)&~7U;if(t->type==6){m=(const struct mb2_mmap_tag*)t;goto found;}if(t->size<8||r>h->mbi_size-off)return;off+=r;}return;found:for(off=0;off<m->size-16&&n<6;off+=m->entry_size){const struct mb2_mmap_entry*e=(const struct mb2_mmap_entry*)((const u8*)m+16+off);if(e->type==1){hex64(c,e->addr);text64(c," +");hex64(c,e->len);putc64(c,'\n');n++;}}}
static TEXT64 void print_exception_frame(u16*c,struct exception_frame*f){text64(c,"\nvector: ");hex64(c,f->vector);text64(c,"\nerror:  ");hex64(c,f->error);text64(c,"\nrip:    ");hex64(c,f->rip);text64(c,"\ncs:     ");hex64(c,f->cs);text64(c,"\nrflags: ");hex64(c,f->rflags);}
TEXT64 void breakpoint_report(struct exception_frame*f){u16 c=10*COLS;u64 *raw=(u64 *)f;text64(&c,"TinyOS lesson 14 breakpoint\nexception: #BP\nvector: ");hex64(&c,raw[0]);text64(&c,"\nerror:  ");hex64(&c,raw[1]);text64(&c,"\nrip:    ");hex64(&c,raw[3]);text64(&c,"\ncs:     ");hex64(&c,raw[4]);text64(&c,"\nrflags: ");hex64(&c,raw[5]);text64(&c,"\nreturning with iretq...\n");}
TEXT64 void exception_report(struct exception_frame*f){u16 c=0;u64 cr2=0;clear64(&c);text64(&c,"TinyOS lesson 14 exception\nexception: ");if(f->vector==6)text64(&c,"#UD");else if(f->vector==14)text64(&c,"#PF");else text64(&c,"unknown");print_exception_frame(&c,f);if(f->vector==14){__asm__ volatile("mov %%cr2,%0":"=r"(cr2));text64(&c,"\ncr2:    ");hex64(&c,cr2);}text64(&c,"\nCPU halted intentionally.\n");for(;;)__asm__ volatile("cli; hlt");}
/* IRQ0 acknowledges each PIT tick; IRQ1 alone reads 0x60 and enqueues make codes. */
TEXT64 void irq0_record(void){ticks++;outb64(PIC1_COMMAND,PIC_EOI);}
TEXT64 void irq1_record(void){u8 raw=inb64(0x60),ch,next;irq1_last_scancode=raw;irq1_raw_count++;if(!(raw&0x80)){irq1_count++;ch=(u8)scan64(raw);if(ch){next=(u8)((kbd_head+1)&(KBD_QUEUE_SIZE-1));if(next==kbd_tail)kbd_overflow_count++;else{kbd_queue[kbd_head]=ch;kbd_head=next;}}}outb64(PIC1_COMMAND,PIC_EOI);}
static TEXT64 int kbd_dequeue(u8 *ch){u8 tail;__asm__ volatile("cli":::"memory");tail=kbd_tail;if(tail==kbd_head){__asm__ volatile("sti":::"memory");return 0;}*ch=kbd_queue[tail];kbd_tail=(u8)((tail+1)&(KBD_QUEUE_SIZE-1));__asm__ volatile("sti":::"memory");return 1;}
static TEXT64 void exec64(u16*c,struct long_mode_handoff*h,const char*s){u64 p;const char*arg=s;while(*arg&&*arg!=' ')arg++;if(*arg)arg++;if(eq64(s,"help"))text64(c,"commands: help about clear lminfo meminfo palloc pfree <hex> pageinfo <hex> mmap idtinfo tickinfo uptime kbdinfo bptest udtest pftest\n");else if(eq64(s,"about"))text64(c,"TinyOS lesson 14: bitmap physical page manager\n");else if(eq64(s,"lminfo"))lminfo(c,h);else if(eq64(s,"idtinfo"))idtinfo(c,h);else if(eq64(s,"tickinfo")||eq64(s,"uptime"))tickinfo(c);else if(eq64(s,"kbdinfo"))kbdinfo(c);else if(eq64(s,"meminfo"))meminfo(c);else if(eq64(s,"palloc")){p=pmm_alloc();if(p){text64(c,"allocated: ");hex64(c,p);putc64(c,'\n');}else text64(c,"allocator exhausted\n");}else if(command_eq(s,"pfree")||command_eq(s,"pageinfo")){if(!hexarg(arg,&p))text64(c,"hex page address required\n");else if(command_eq(s,"pfree")){if(pmm_free_page(p))text64(c,"freed\n");else text64(c,"invalid, reserved, or already-free page\n");}else {text64(c,"page: ");hex64(c,p);text64(c," state: ");if((p&(PAGE_SIZE-1))||p>=PMM_MAX_PHYS)text64(c,"invalid");else if(bit((u32)(p/PAGE_SIZE)))text64(c,"used/reserved");else text64(c,"free");putc64(c,'\n');}}else if(eq64(s,"mmap"))mmap64(c,h);else if(eq64(s,"bptest")){text64(c,"triggering #BP\n");__asm__ volatile("int3":::"rax","rcx","rdx","rsi","rdi","r8","r9","r10","r11","cc","memory");text64(c,"#BP returned to shell\n");}else if(eq64(s,"udtest")){text64(c,"triggering #UD\n");__asm__ volatile("ud2");}else if(eq64(s,"pftest")){volatile u64 *bad=(volatile u64 *)0x00400000ULL;text64(c,"triggering #PF\n");p=*bad;(void)p;}else if(eq64(s,"clear")){clear64(c);prompt64(c);return;}else if(s[0])text64(c,"unknown command\n");prompt64(c);}
ENTRY64 void kernel_main64_binary(struct long_mode_handoff*h){u16 c=0,n=0;pmm_init(h);char cmd[32];u8 ch;__asm__ volatile("cli":::"memory");install_idt(h);pit_init();pic_init();clear64(&c);text64(&c,"TinyOS lesson 14: bitmap physical page manager\n64-bit continuation; 4 KiB frames, timer and keyboard enabled\n");prompt64(&c);__asm__ volatile("sti":::"memory");for(;;){if(!kbd_dequeue(&ch)){__asm__ volatile("sti; hlt":::"memory");continue;}if(ch=='\n'){putc64(&c,ch);cmd[n]=0;exec64(&c,h,cmd);n=0;}else if(ch=='\b'){if(n){n--;c--;VGA[c]=0x0f20;}}else if(n<31){cmd[n++]=(char)ch;putc64(&c,(char)ch);}}}
__asm__(".section .text64\n"
".global exception_bp\nexception_bp:\n"
"pushq %rbx\npushq $0\npushq $3\n"
"movq %rsp,%rbx\nmovq %rsp,%rdi\nandq $-16,%rsp\ncall breakpoint_report\n"
"movq %rbx,%rsp\naddq $16,%rsp\npopq %rbx\niretq\n"
".global exception_ud\nexception_ud:\n"
"pushq $0\npushq $6\njmp exception_common\n"
".global exception_pf\nexception_pf:\n"
"pushq $14\njmp exception_common\n"
"exception_common:\n"
"movq %rsp,%rdi\nandq $-16,%rsp\ncall exception_report\n"
"1: cli\nhlt\njmp 1b\n"
".global irq0_entry\nirq0_entry:\n"
"pushq %rax\npushq %rbx\npushq %rcx\npushq %rdx\npushq %rbp\npushq %rsi\npushq %rdi\npushq %r8\npushq %r9\npushq %r10\npushq %r11\npushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
"cld\nmovq %rsp,%rbp\nandq $-16,%rsp\ncall irq0_record\nmovq %rbp,%rsp\n"
"popq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %r11\npopq %r10\npopq %r9\npopq %r8\npopq %rdi\npopq %rsi\npopq %rbp\npopq %rdx\npopq %rcx\npopq %rbx\npopq %rax\niretq\n"
".global irq1_entry\nirq1_entry:\n"
"pushq %rax\npushq %rbx\npushq %rcx\npushq %rdx\npushq %rbp\npushq %rsi\npushq %rdi\npushq %r8\npushq %r9\npushq %r10\npushq %r11\npushq %r12\npushq %r13\npushq %r14\npushq %r15\n"
"cld\nmovq %rsp,%rbp\nandq $-16,%rsp\ncall irq1_record\nmovq %rbp,%rsp\n"
"popq %r15\npopq %r14\npopq %r13\npopq %r12\npopq %r11\npopq %r10\npopq %r9\npopq %r8\npopq %rdi\npopq %rsi\npopq %rbp\npopq %rdx\npopq %rcx\npopq %rbx\npopq %rax\niretq\n");
