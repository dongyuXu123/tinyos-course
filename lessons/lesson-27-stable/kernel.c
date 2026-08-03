/* Lesson 27: reserve 16 final-PT slots for synchronized low/high mappings. */
#define MB2_BOOT_MAGIC 0x36d76289U
#define MB2_TAG_END 0
#define MB2_TAG_MMAP 6
#define PAGE_SIZE 0x1000ULL
#define LOW_MEMORY_END 0x00100000ULL
#define PAGE_ENTRIES 512
#define PTE_PRESENT_WRITABLE 0x003ULL
#define IDENTITY_MAP_END 0x01000000ULL
#define PAGE_TABLES_PER_ALIAS (IDENTITY_MAP_END/(PAGE_ENTRIES*PAGE_SIZE))
#define KERNEL_VMA_BASE 0xffffffff80000000ULL
#define VM_REGION_START 0x00ff0000ULL
#define VM_REGION_SLOTS 16U
#define VM_REGION_FIRST_PTE (PAGE_ENTRIES-VM_REGION_SLOTS)

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;
struct mb2_tag { u32 type; u32 size; } __attribute__((packed));
struct mb2_mmap_tag { u32 type; u32 size; u32 entry_size; u32 entry_version; } __attribute__((packed));
struct mb2_mmap_entry { u64 addr; u64 len; u32 type; u32 reserved; } __attribute__((packed));
/* The 64-bit continuation owns the lasting physical-page manager. */
struct long_mode_handoff {
    u64 pml4, pdpt, pd, idt_address, pt[PAGE_TABLES_PER_ALIAS];
    u64 kernel_start, kernel_end, stack_start, stack_end;
    u64 high_pdpt, high_pd, high_pt[PAGE_TABLES_PER_ALIAS];
    u64 kernel_vma_base, kernel_phys_base;
    u32 mbi_address, mbi_size;
};
extern char _kernel_start[], _kernel_end[], stack_bottom[], stack_top[];
struct long_mode_handoff long_mode_handoff;
u8 idt_backing_store[4096] __attribute__((aligned(16)));
static u32 multiboot_magic, multiboot_address, multiboot_total_size;
static const struct mb2_mmap_tag *memory_map;
static int memory_map_ready;
static u64 bootstrap_cursor, bootstrap_end;

static int ranges_overlap(u64 a,u64 b,u64 c,u64 d) { return a < d && c < b; }
static u64 align_up_page(u64 v) { if (v > ~0ULL-(PAGE_SIZE-1)) return 0; return (v+PAGE_SIZE-1)&~(PAGE_SIZE-1); }
static u64 align_down_page(u64 v) { return v&~(PAGE_SIZE-1); }
static int bootstrap_reserved(u64 p)
{
    u64 end=p+PAGE_SIZE;
    return end<p || ranges_overlap(p,end,0,LOW_MEMORY_END) ||
        ranges_overlap(p,end,long_mode_handoff.kernel_start,long_mode_handoff.kernel_end) ||
        ranges_overlap(p,end,long_mode_handoff.stack_start,long_mode_handoff.stack_end) ||
        ranges_overlap(p,end,multiboot_address,(u64)multiboot_address+multiboot_total_size);
}
static int prepare_memory_map(void)
{
    u32 pos,end;
    if(memory_map_ready) return 1;
    if(multiboot_magic!=MB2_BOOT_MAGIC || (multiboot_address&7)!=0) return 0;
    multiboot_total_size=*(const u32 *)(unsigned long)multiboot_address;
    if(multiboot_total_size<16 || multiboot_total_size>0x100000 || multiboot_address+multiboot_total_size<multiboot_address) return 0;
    pos=multiboot_address+8; end=multiboot_address+multiboot_total_size;
    while(pos<end) { const struct mb2_tag *tag; u32 rounded;
        if(end-pos<8) return 0;
        tag=(const struct mb2_tag *)(unsigned long)pos;
        if(tag->size<8 || tag->size>end-pos) return 0;
        if(tag->type==MB2_TAG_END) { if(tag->size!=8 || !memory_map) return 0; memory_map_ready=1; return 1; }
        if(tag->type==MB2_TAG_MMAP && !memory_map) { const struct mb2_mmap_tag *map=(const struct mb2_mmap_tag *)tag; if(tag->size<16 || map->entry_version || map->entry_size<24 || (map->entry_size&7) || ((tag->size-16)%map->entry_size)) return 0; memory_map=map; }
        rounded=(tag->size+7)&~7U;
        if(rounded<tag->size || rounded>end-pos) return 0;
        pos+=rounded;
    }
    return 0;
}
/* Temporary allocator for preallocated low-memory paging pages; no allocator state is handed off. */
static u64 bootstrap_alloc_page(void)
{
    u32 offset;
    if(!prepare_memory_map()) return 0;
    if(bootstrap_cursor) while(bootstrap_cursor<bootstrap_end) { u64 p=bootstrap_cursor; bootstrap_cursor+=PAGE_SIZE; if(!bootstrap_reserved(p)) return p; }
    for(offset=0;offset<memory_map->size-16;offset+=memory_map->entry_size) { const struct mb2_mmap_entry *e=(const struct mb2_mmap_entry *)((const u8 *)memory_map+16+offset); u64 p,end; if(e->type!=1 || e->addr+e->len<e->addr) continue; p=align_up_page(e->addr); end=align_down_page(e->addr+e->len); while(p && p<end) { if(!bootstrap_reserved(p)) { bootstrap_cursor=p+PAGE_SIZE; bootstrap_end=end; return p; } p+=PAGE_SIZE; } }
    return 0;
}
static void zero_page(u64 p) { u32 i; volatile u32 *w=(volatile u32 *)(unsigned long)(u32)p; for(i=0;i<1024;i++) w[i]=0; }
static int table_page_ok(u64 p) { return p>=LOW_MEMORY_END && p<IDENTITY_MAP_END && !(p&(PAGE_SIZE-1)); }
static u32 setup_long_mode_tables(void)
{
    volatile u64 *pml4,*pdpt,*pd,*hpdpt,*hpd; u32 i,j;
    long_mode_handoff.pml4=bootstrap_alloc_page(); long_mode_handoff.pdpt=bootstrap_alloc_page(); long_mode_handoff.pd=bootstrap_alloc_page();
    for(i=0;i<PAGE_TABLES_PER_ALIAS;i++)long_mode_handoff.pt[i]=bootstrap_alloc_page();
    long_mode_handoff.high_pdpt=bootstrap_alloc_page(); long_mode_handoff.high_pd=bootstrap_alloc_page();
    for(i=0;i<PAGE_TABLES_PER_ALIAS;i++)long_mode_handoff.high_pt[i]=bootstrap_alloc_page();
    if(!table_page_ok(long_mode_handoff.pml4)||!table_page_ok(long_mode_handoff.pdpt)||!table_page_ok(long_mode_handoff.pd)||!table_page_ok(long_mode_handoff.high_pdpt)||!table_page_ok(long_mode_handoff.high_pd))return 0;
    for(i=0;i<PAGE_TABLES_PER_ALIAS;i++)if(!table_page_ok(long_mode_handoff.pt[i])||!table_page_ok(long_mode_handoff.high_pt[i]))return 0;
    zero_page(long_mode_handoff.pml4); zero_page(long_mode_handoff.pdpt); zero_page(long_mode_handoff.pd); zero_page(long_mode_handoff.high_pdpt); zero_page(long_mode_handoff.high_pd);
    for(i=0;i<PAGE_TABLES_PER_ALIAS;i++){zero_page(long_mode_handoff.pt[i]);zero_page(long_mode_handoff.high_pt[i]);}
    pml4=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pml4; pdpt=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pdpt; pd=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pd; hpdpt=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.high_pdpt; hpd=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.high_pd;
    pml4[0]=long_mode_handoff.pdpt|PTE_PRESENT_WRITABLE; pdpt[0]=long_mode_handoff.pd|PTE_PRESENT_WRITABLE;
    pml4[511]=long_mode_handoff.high_pdpt|PTE_PRESENT_WRITABLE; hpdpt[510]=long_mode_handoff.high_pd|PTE_PRESENT_WRITABLE;
    for(i=0;i<PAGE_TABLES_PER_ALIAS;i++){volatile u64 *pt=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pt[i],*hpt=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.high_pt[i];pd[i]=long_mode_handoff.pt[i]|PTE_PRESENT_WRITABLE;hpd[i]=long_mode_handoff.high_pt[i]|PTE_PRESENT_WRITABLE;for(j=0;j<PAGE_ENTRIES;j++){u64 p=((u64)i*PAGE_ENTRIES+j)*PAGE_SIZE;pt[j]=p|PTE_PRESENT_WRITABLE;hpt[j]=p|PTE_PRESENT_WRITABLE;}}
    for(j=VM_REGION_FIRST_PTE;j<PAGE_ENTRIES;j++){
        ((volatile u64 *)(unsigned long)(u32)long_mode_handoff.pt[PAGE_TABLES_PER_ALIAS-1])[j]=0;
        ((volatile u64 *)(unsigned long)(u32)long_mode_handoff.high_pt[PAGE_TABLES_PER_ALIAS-1])[j]=0;
    }
    return (u32)long_mode_handoff.pml4;
}
u32 kernel_main32(u32 magic,u32 mbi_address)
{
    multiboot_magic=magic; multiboot_address=mbi_address;
    long_mode_handoff.kernel_start=(u64)(u32)(unsigned long)_kernel_start; long_mode_handoff.kernel_end=(u64)(u32)(unsigned long)_kernel_end; long_mode_handoff.stack_start=(u64)(u32)(unsigned long)stack_bottom; long_mode_handoff.stack_end=(u64)(u32)(unsigned long)stack_top; long_mode_handoff.idt_address=(u64)(u32)(unsigned long)idt_backing_store;
    long_mode_handoff.kernel_vma_base=KERNEL_VMA_BASE; long_mode_handoff.kernel_phys_base=0x00100000ULL;
    if(!prepare_memory_map()) return 0;
    long_mode_handoff.mbi_address=multiboot_address; long_mode_handoff.mbi_size=multiboot_total_size;
    return setup_long_mode_tables();
}
