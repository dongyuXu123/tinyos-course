/* 第八课 32-bit setup：验证 Multiboot2 map，并构造 x86_64 long-mode 页表。 */
#define MB2_BOOT_MAGIC 0x36d76289U
#define MB2_TAG_END 0
#define MB2_TAG_MMAP 6
#define PAGE_SIZE 0x1000ULL
#define LOW_MEMORY_END 0x00100000ULL
#define PAGE_ENTRIES 512
#define PTE_PRESENT_WRITABLE 0x003ULL
#define IDENTITY_MAP_END 0x00400000ULL
#define ALLOCATION_HISTORY_MAX 64

typedef unsigned int u32;
typedef unsigned long long u64;
struct mb2_tag { u32 type; u32 size; } __attribute__((packed));
struct mb2_mmap_tag { u32 type; u32 size; u32 entry_size; u32 entry_version; } __attribute__((packed));
struct mb2_mmap_entry { u64 addr; u64 len; u32 type; u32 reserved; } __attribute__((packed));
/* 此块由 .code64 通过 RDI 读取和更新；所有地址在本课 identity window 内。 */
struct long_mode_handoff {
    u64 pml4, pdpt, pd, pt0, pt1;
    u64 allocation_cursor, allocation_end, allocation_history[ALLOCATION_HISTORY_MAX];
    u64 kernel_start, kernel_end, stack_start, stack_end;
    u32 mbi_address, mbi_size, allocated_pages;
};
extern char _kernel_start[], _kernel_end[], stack_bottom[], stack_top[];
struct long_mode_handoff long_mode_handoff;
static u32 multiboot_magic, multiboot_address, multiboot_total_size;
static const struct mb2_mmap_tag *memory_map;
static int memory_map_ready;

static int ranges_overlap(u64 a, u64 b, u64 c, u64 d) { return a < d && c < b; }
static u64 align_up_page(u64 v) { if (v > ~0ULL - (PAGE_SIZE - 1)) return 0; return (v + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1); }
static u64 align_down_page(u64 v) { return v & ~(PAGE_SIZE - 1); }
static int page_was_allocated(u64 page) { u32 i; for (i=0;i<long_mode_handoff.allocated_pages;i++) if (long_mode_handoff.allocation_history[i] == page) return 1; return 0; }
static int page_is_reserved(u64 page)
{
    u64 end = page + PAGE_SIZE;
    if (end < page || ranges_overlap(page,end,0,LOW_MEMORY_END)) return 1;
    if (ranges_overlap(page,end,long_mode_handoff.kernel_start,long_mode_handoff.kernel_end)) return 1;
    if (ranges_overlap(page,end,long_mode_handoff.stack_start,long_mode_handoff.stack_end)) return 1;
    if (ranges_overlap(page,end,multiboot_address,(u64)multiboot_address + multiboot_total_size)) return 1;
    return page_was_allocated(page);
}
static int prepare_memory_map(void)
{
    u32 pos,end;
    if (memory_map_ready) return 1;
    if (multiboot_magic != MB2_BOOT_MAGIC || (multiboot_address & 7) != 0) return 0;
    multiboot_total_size=*(const u32 *)(unsigned long)multiboot_address;
    if (multiboot_total_size < 16 || multiboot_total_size > 0x100000 || multiboot_address + multiboot_total_size < multiboot_address) return 0;
    pos=multiboot_address+8; end=multiboot_address+multiboot_total_size;
    while (pos < end) { const struct mb2_tag *tag; u32 rounded;
        if (end-pos < 8) return 0;
        tag=(const struct mb2_tag *)(unsigned long)pos;
        if (tag->size < 8 || tag->size > end-pos) return 0;
        if (tag->type == MB2_TAG_END) { if (tag->size != 8 || !memory_map) return 0; memory_map_ready=1; return 1; }
        if (tag->type == MB2_TAG_MMAP && !memory_map) { const struct mb2_mmap_tag *map=(const struct mb2_mmap_tag *)tag; if (tag->size < 16 || map->entry_version || map->entry_size < 24 || (map->entry_size & 7) || ((tag->size-16)%map->entry_size)) return 0; memory_map=map; }
        rounded=(tag->size+7)&~7U; if (rounded < tag->size || rounded > end-pos) return 0; pos+=rounded;
    }
    return 0;
}
static u64 phys_alloc_page(void)
{
    u32 offset;
    if (!prepare_memory_map() || long_mode_handoff.allocated_pages == ALLOCATION_HISTORY_MAX) return 0;
    if (long_mode_handoff.allocation_cursor) while (long_mode_handoff.allocation_cursor < long_mode_handoff.allocation_end) { u64 p=long_mode_handoff.allocation_cursor; long_mode_handoff.allocation_cursor+=PAGE_SIZE; if (!page_is_reserved(p)) { long_mode_handoff.allocation_history[long_mode_handoff.allocated_pages++]=p; return p; } }
    for (offset=0;offset<memory_map->size-16;offset+=memory_map->entry_size) { const struct mb2_mmap_entry *e=(const struct mb2_mmap_entry *)((const unsigned char *)memory_map+16+offset); u64 p,end; if(e->type!=1 || e->addr+e->len<e->addr) continue; p=align_up_page(e->addr); end=align_down_page(e->addr+e->len); while(p && p<end) { if(!page_is_reserved(p)) { long_mode_handoff.allocation_cursor=p+PAGE_SIZE; long_mode_handoff.allocation_end=end; long_mode_handoff.allocation_history[long_mode_handoff.allocated_pages++]=p; return p; } p+=PAGE_SIZE; } }
    return 0;
}
static void zero_page(u64 p) { u32 i; volatile u32 *w=(volatile u32 *)(unsigned long)(u32)p; for(i=0;i<1024;i++) w[i]=0; }
static int table_page_ok(u64 p) { return p >= LOW_MEMORY_END && p < IDENTITY_MAP_END && !(p & (PAGE_SIZE-1)); }
static u32 setup_long_mode_tables(void)
{
    volatile u64 *pml4,*pdpt,*pd,*pt0,*pt1; u32 i;
    long_mode_handoff.pml4=phys_alloc_page(); long_mode_handoff.pdpt=phys_alloc_page(); long_mode_handoff.pd=phys_alloc_page(); long_mode_handoff.pt0=phys_alloc_page(); long_mode_handoff.pt1=phys_alloc_page();
    if(!table_page_ok(long_mode_handoff.pml4)||!table_page_ok(long_mode_handoff.pdpt)||!table_page_ok(long_mode_handoff.pd)||!table_page_ok(long_mode_handoff.pt0)||!table_page_ok(long_mode_handoff.pt1)) return 0;
    zero_page(long_mode_handoff.pml4); zero_page(long_mode_handoff.pdpt); zero_page(long_mode_handoff.pd); zero_page(long_mode_handoff.pt0); zero_page(long_mode_handoff.pt1);
    pml4=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pml4; pdpt=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pdpt; pd=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pd; pt0=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pt0; pt1=(volatile u64 *)(unsigned long)(u32)long_mode_handoff.pt1;
    pml4[0]=long_mode_handoff.pdpt|PTE_PRESENT_WRITABLE; pdpt[0]=long_mode_handoff.pd|PTE_PRESENT_WRITABLE; pd[0]=long_mode_handoff.pt0|PTE_PRESENT_WRITABLE; pd[1]=long_mode_handoff.pt1|PTE_PRESENT_WRITABLE;
    for(i=0;i<PAGE_ENTRIES;i++) { pt0[i]=((u64)i*PAGE_SIZE)|PTE_PRESENT_WRITABLE; pt1[i]=((u64)(i+PAGE_ENTRIES)*PAGE_SIZE)|PTE_PRESENT_WRITABLE; }
    return (u32)long_mode_handoff.pml4;
}
u32 kernel_main32(u32 magic,u32 mbi_address)
{
    multiboot_magic=magic; multiboot_address=mbi_address;
    long_mode_handoff.kernel_start=(u64)(u32)(unsigned long)_kernel_start; long_mode_handoff.kernel_end=(u64)(u32)(unsigned long)_kernel_end; long_mode_handoff.stack_start=(u64)(u32)(unsigned long)stack_bottom; long_mode_handoff.stack_end=(u64)(u32)(unsigned long)stack_top;
    if(!prepare_memory_map()) return 0;
    long_mode_handoff.mbi_address=multiboot_address;
    long_mode_handoff.mbi_size=multiboot_total_size;
    return setup_long_mode_tables();
}
