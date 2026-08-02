/* 第八课真正以 -m64 编译的 continuation：identity-mapped VGA、PS/2 和 allocator。 */
typedef unsigned char u8; typedef unsigned int u32; typedef unsigned short u16; typedef unsigned long long u64;
#define TEXT64 __attribute__((section(".text64"), noinline))
#define VGA ((volatile u16 *)0xb8000ULL)
#define COLS 80
#define ROWS 25
#define PAGE_SIZE 0x1000ULL
#define ALLOCATION_HISTORY_MAX 64
struct mb2_tag { u32 type; u32 size; } __attribute__((packed));
struct mb2_mmap_tag { u32 type; u32 size; u32 entry_size; u32 entry_version; } __attribute__((packed));
struct mb2_mmap_entry { u64 addr; u64 len; u32 type; u32 reserved; } __attribute__((packed));
struct long_mode_handoff { u64 pml4,pdpt,pd,pt0,pt1; u64 allocation_cursor,allocation_end,allocation_history[ALLOCATION_HISTORY_MAX]; u64 kernel_start,kernel_end,stack_start,stack_end; u32 mbi_address,mbi_size,allocated_pages; };
static TEXT64 void putc64(u16 *c,char x) { if(x=='\n'){*c+=COLS-*c%COLS;return;} VGA[(*c)++]=0x0f00U|(u8)x; if(*c>=COLS*ROWS)*c=(ROWS-1)*COLS; }
static TEXT64 void text64(u16 *c,const char *s) { while(*s) putc64(c,*s++); }
static TEXT64 void hex64(u16 *c,u64 v) { int n; for(n=60;n>=0;n-=4) putc64(c,"0123456789abcdef"[(v>>n)&15]); }
static TEXT64 u8 inb64(u16 p){u8 v;__asm__ volatile("inb %1,%0":"=a"(v):"Nd"(p));return v;}
static TEXT64 char scan64(u8 s){switch(s){case 0x0e:return'\b';case 0x1c:return'\n';case 0x10:return'q';case 0x11:return'w';case 0x12:return'e';case 0x13:return'r';case 0x14:return't';case 0x15:return'y';case 0x16:return'u';case 0x17:return'i';case 0x18:return'o';case 0x19:return'p';case 0x1e:return'a';case 0x1f:return's';case 0x20:return'd';case 0x21:return'f';case 0x22:return'g';case 0x23:return'h';case 0x24:return'j';case 0x25:return'k';case 0x26:return'l';case 0x2c:return'z';case 0x2d:return'x';case 0x2e:return'c';case 0x2f:return'v';case 0x30:return'b';case 0x31:return'n';case 0x32:return'm';default:return 0;}}
static TEXT64 int eq64(const char*a,const char*b){while(*a&&*b){if(*a++!=*b++)return 0;}return *a==*b;}
static TEXT64 void clear64(u16*c){u16 i;for(i=0;i<COLS*ROWS;i++)VGA[i]=0x0f20;*c=0;}
static TEXT64 void prompt64(u16*c){text64(c,"tinyos> ");}
static TEXT64 int overlap(u64 a,u64 b,u64 c,u64 d){return a<d&&c<b;}
static TEXT64 u64 up(u64 v){return(v+PAGE_SIZE-1)&~(PAGE_SIZE-1);}
static TEXT64 u64 down(u64 v){return v&~(PAGE_SIZE-1);}
static TEXT64 int reserved(struct long_mode_handoff*h,u64 p){u64 e=p+PAGE_SIZE;u32 i;if(p<0x100000||e<p||overlap(p,e,h->kernel_start,h->kernel_end)||overlap(p,e,h->stack_start,h->stack_end)||overlap(p,e,h->mbi_address,(u64)h->mbi_address+h->mbi_size))return 1;for(i=0;i<h->allocated_pages;i++)if(h->allocation_history[i]==p)return 1;return 0;}
static TEXT64 u64 alloc64(struct long_mode_handoff*h){const struct mb2_mmap_tag*m;u32 off;if(h->allocated_pages==ALLOCATION_HISTORY_MAX)return 0;m=(const struct mb2_mmap_tag *)(unsigned long)h->mbi_address;for(off=8;off<h->mbi_size;){const struct mb2_tag*t=(const struct mb2_tag *)((const u8*)m+off);u32 r=(t->size+7)&~7U;if(t->type==6){m=(const struct mb2_mmap_tag*)t;break;}if(t->size<8||r>h->mbi_size-off)return 0;off+=r;}if(((const struct mb2_tag*)m)->type!=6)return 0;for(off=0;off<m->size-16;off+=m->entry_size){const struct mb2_mmap_entry*e=(const struct mb2_mmap_entry*)((const u8*)m+16+off);u64 p,end;if(e->type!=1||e->addr+e->len<e->addr)continue;p=up(e->addr);end=down(e->addr+e->len);while(p&&p<end){if(!reserved(h,p)){h->allocation_history[h->allocated_pages++]=p;return p;}p+=PAGE_SIZE;}}return 0;}
static TEXT64 void lminfo(u16*c,struct long_mode_handoff*h){text64(c,"long mode: on\npml4: ");hex64(c,h->pml4);text64(c,"\npdpt: ");hex64(c,h->pdpt);text64(c,"\npd:   ");hex64(c,h->pd);text64(c,"\npt0:  ");hex64(c,h->pt0);text64(c,"\npt1:  ");hex64(c,h->pt1);text64(c,"\nidentity: 0000000000000000 - 0000000000400000\n");}
static TEXT64 void mmap64(u16*c,struct long_mode_handoff*h){const struct mb2_mmap_tag*m;u32 off,n=0;text64(c,"Multiboot2 available ranges:\n");for(off=8;off<h->mbi_size;){const struct mb2_tag*t=(const struct mb2_tag*)((const u8*)(unsigned long)h->mbi_address+off);u32 r=(t->size+7)&~7U;if(t->type==6){m=(const struct mb2_mmap_tag*)t;goto found;}if(t->size<8||r>h->mbi_size-off)return;off+=r;}return;found:for(off=0;off<m->size-16&&n<6;off+=m->entry_size){const struct mb2_mmap_entry*e=(const struct mb2_mmap_entry*)((const u8*)m+16+off);if(e->type==1){hex64(c,e->addr);text64(c," +");hex64(c,e->len);putc64(c,'\n');n++;}}}
static TEXT64 void exec64(u16*c,struct long_mode_handoff*h,const char*s){u64 p;if(eq64(s,"help"))text64(c,"commands: help about clear lminfo pinfo palloc mmap\n");else if(eq64(s,"about"))text64(c,"TinyOS lesson 8: x86_64 long mode\n");else if(eq64(s,"lminfo"))lminfo(c,h);else if(eq64(s,"pinfo")){text64(c,"page size: 0000000000001000\nallocated pages: ");hex64(c,h->allocated_pages);putc64(c,'\n');}else if(eq64(s,"palloc")){p=alloc64(h);if(p){text64(c,"allocated: ");hex64(c,p);putc64(c,'\n');}else text64(c,"allocator exhausted\n");}else if(eq64(s,"mmap"))mmap64(c,h);else if(eq64(s,"clear")){clear64(c);prompt64(c);return;}else if(s[0])text64(c,"unknown command\n");prompt64(c);}
__attribute__((section(".text64.entry"), noinline)) void kernel_main64_binary(struct long_mode_handoff*h){u16 c=0,n=0;char cmd[32];u8 code;char ch;clear64(&c);text64(&c,"TinyOS lesson 8: x86_64 long mode\n64-bit C continuation active\n");prompt64(&c);for(;;){if(!(inb64(0x64)&1))continue;code=inb64(0x60);if(code&0x80)continue;ch=scan64(code);if(!ch)continue;if(ch=='\n'){putc64(&c,ch);cmd[n]=0;exec64(&c,h,cmd);n=0;}else if(ch=='\b'){if(n){n--;c--;VGA[c]=0x0f20;}}else if(n<31){cmd[n++]=ch;putc64(&c,ch);}}}
