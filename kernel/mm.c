#include "./include/mm.h"
#include "./include/console.h"
#include "./include/debug.h"
extern uint8_t g_kern_start[];
extern uint8_t g_kern_end[];
uint32_t g_seg_cnt = 0;
struct mm_manager_t g_mm_manager;
void merge();
void init_mm(struct multiboot_t *m)
{
    uint32_t mmap_addr = TOHM(m->mmap_addr);
    uint32_t mmap_length = m->mmap_length;
    mmap_entry_t *mmap = (mmap_entry_t *)(mmap_addr);
    for (mmap = (mmap_entry_t *)mmap_addr; (uint32_t)mmap < mmap_addr + mmap_length; mmap++)
    {
        uint64_t start = ((uint64_t)mmap->base_addr_high << 32) | mmap->base_addr_low;
        uint64_t size = ((uint64_t)mmap->length_high << 32) | mmap->length_low;
        //只管理1M以上的内存
        if (start >= 0x100000L && mmap->type == 1)
        {
            //超过4G的内存不处理，因为寻址只有4GB
            if (start + size > 0x100000000L)
            {
                continue;
            }
            if (start == 0x100000L)
            {
                // 管理的内存不包括内核占用的内存
                uint32_t kenerl_size = ((int)g_kern_end - (int)g_kern_start);
                add_memseg((uint32_t)(start + kenerl_size), (uint32_t)(size - kenerl_size));
            }
            else
            {
                add_memseg((uint32_t)start, (uint32_t)size);
            }
        }
#ifdef DEBUG
        kprintf("base_addr = %X, length = %X type:%d \n", start, size, mmap->type);
#endif
    }
}

//  添加用于内存管理的段
void add_memseg(uint32_t addr, uint32_t size)
{
    g_mm_manager.free_area[g_seg_cnt].start = addr;
    g_mm_manager.free_area[g_seg_cnt].size = size;
    g_seg_cnt++;
}

// 分配4k对齐的内存，成功返回内存地址，失败返回0
void *alloc_4k(uint32_t start_addr, uint32_t size)
{
    merge();
    if (0 == size)
    {
#ifdef DEBUG
        panic("alloc_4k failed(size=0)\n");
#endif
        return 0;
    }
    for (int i = 0; i < MAX_FREE_BLOCK_NUM; i++)
    {
        uint32_t range_start = g_mm_manager.free_area[i].start;
        uint32_t range_end = g_mm_manager.free_area[i].start + g_mm_manager.free_area[i].size; //闭区间
        uint32_t range_size = g_mm_manager.free_area[i].size;
        //从中间分开
        if (range_start <= start_addr && start_addr + size < range_end)
        {
            uint32_t alloc_addr = 0;
            bool not_found = TRUE;
            for (uint32_t k = range_start & 0xfffff000; (k) < (range_end); k += 4096)
            {
                // k是这个区间上4k对齐的地址
                if ((k >= start_addr) && (k + size < range_end))
                {
                    alloc_addr = k;
                    not_found = FALSE;
                    break;
                }
            }
            if (not_found)
            {
                panic("alloc_4k failed\n");
            }
            bool no_space = TRUE, no_space2 = TRUE;
            //记录下这块分配的内存块
            for (int j = 0; j < MAX_ALLOC_BLOCK_NUM; j++)
            {
                if (g_mm_manager.alloc_area[j].size == 0)
                {
                    g_mm_manager.alloc_area[j].start = alloc_addr;
                    g_mm_manager.alloc_area[j].size = size;
                    no_space = FALSE;
                    break;
                }
            }
            if (no_space)
            {
                panic("alloc_4k failed(no space)\n");
            }
            //剩下的内存
            uint32_t free_size = alloc_addr - range_start;
            g_mm_manager.free_area[i].start += (free_size + size);
            g_mm_manager.free_area[i].size -= (free_size + size);
            //左边部分放回到待分配的列表里
            for (int j = 0; j < MAX_FREE_BLOCK_NUM; j++)
            {
                if (g_mm_manager.free_area[j].size == 0)
                {
                    g_mm_manager.free_area[j].start = range_start;
                    g_mm_manager.free_area[j].size = free_size;
                    no_space2 = FALSE;
                    break;
                }
            }
            if (no_space2)
            {
                panic("alloc_4k failed(no space2)\n");
            }
            return (void *)alloc_addr;
        }
        else if (range_start > start_addr && range_size > size)
        {
            uint32_t alloc_addr = 0;
            bool not_found = TRUE;
            for (uint32_t k = range_start & 0xfffff000;
                 (k) < (range_end); k += 4096)
            {
                // k是这个区间上4k对齐的地址
                if ((k >= range_start) && (k + size < range_end))
                {
                    alloc_addr = k;
                    not_found = FALSE;
                    break;
                }
            }
            if (not_found)
            {
                panic("alloc_4k failed\n");
            }

            //剩下的内存
            uint32_t free_size = alloc_addr - range_start;
            g_mm_manager.free_area[i].start += (size + free_size);
            g_mm_manager.free_area[i].size -= (size + free_size);

            not_found = TRUE;
            for (int j = 0; j < MAX_ALLOC_BLOCK_NUM; j++)
            {
                if (g_mm_manager.alloc_area[j].size == 0)
                {
                    g_mm_manager.alloc_area[j].start = alloc_addr;
                    g_mm_manager.alloc_area[j].size = size;
                    not_found = FALSE;
                    break;
                }
            }
            if (not_found)
            {
                panic("alloc_4k failed\n");
            }
            not_found = TRUE;
            for (int j = 0; j < MAX_FREE_BLOCK_NUM; j++)
            {
                if (g_mm_manager.free_area[j].size == 0)
                {
                    g_mm_manager.free_area[j].start = range_start;
                    g_mm_manager.free_area[j].size = free_size;
                    not_found = FALSE;
                    break;
                }
            }
            if (not_found)
            {
                panic("alloc_4k failed\n");
            }
            return (void *)alloc_addr;
        }
    }
#ifdef DEBUG
    kprintf("alloc_4k failed\n");
#endif
    return 0;
}
void *alloc(uint32_t start_addr, uint32_t size)
{
    merge();
    if (0 == size)
    {
#ifdef DEBUG
        kprintf("alloc failed(size=0)\n");
#endif
        return 0;
    }
    for (int i = 0; i < MAX_FREE_BLOCK_NUM; i++)
    {
        uint32_t range_start = g_mm_manager.free_area[i].start;
        uint32_t range_end = g_mm_manager.free_area[i].start + g_mm_manager.free_area[i].size; //闭区间
        uint32_t range_size = g_mm_manager.free_area[i].size;
        //从中间分开
        if (range_start <= start_addr && start_addr + size < range_end)
        {
            //剩下的内存
            uint32_t free_size = start_addr - range_start;
            g_mm_manager.free_area[i].start += (free_size + size);
            g_mm_manager.free_area[i].size -= (free_size + size);
            bool no_space = TRUE, no_space2 = TRUE;
            //记录下这块分配的内存块
            for (int j = 0; j < MAX_ALLOC_BLOCK_NUM; j++)
            {
                if (g_mm_manager.alloc_area[j].size == 0)
                {
                    g_mm_manager.alloc_area[j].start = start_addr;
                    g_mm_manager.alloc_area[j].size = size;
                    no_space = FALSE;
                    break;
                }
            }
            //同时由于是在这大块中间切开的，左边的不需要的放回到待分配列表
            for (int j = 0; j < MAX_FREE_BLOCK_NUM; j++)
            {
                if (g_mm_manager.free_area[j].size == 0)
                {
                    g_mm_manager.free_area[j].size = free_size;
                    g_mm_manager.free_area[j].start = range_start;
                    no_space2 = FALSE;
                    break;
                }
            }
            if (no_space || no_space2)
            {
                panic("alloc failed\n");
            }
            return (void *)start_addr;
        }
        //不用从中间分开，区间起始地址就是待分配的内存的地址
        else if (range_start >= start_addr && range_size > size)
        {
            g_mm_manager.free_area[i].start += size;
            g_mm_manager.free_area[i].size -= size;
            bool no_space = TRUE;
            for (int i = 0; i < MAX_ALLOC_BLOCK_NUM; i++)
            {
                if (g_mm_manager.alloc_area[i].size == 0)
                {
                    g_mm_manager.alloc_area[i].start = range_start;
                    g_mm_manager.alloc_area[i].size = size;
                    no_space = FALSE;
                    break;
                }
            }
            if (no_space)
            {
                panic("alloc failed\n");
            }
            return (void *)range_start;
        }
    }
#ifdef DEBUG
    kprintf("alloc failed\n");
#endif
    //没有足够的内存分配了
    return 0;
}
// 合并分散的碎片块
void merge()
{
    // 按照起始地址排序
    for (int i = 0; i < MAX_FREE_BLOCK_NUM; i++)
    {
        for (int j = 0; j < MAX_FREE_BLOCK_NUM - 1; j++)
        {
            if (g_mm_manager.free_area[j].start > g_mm_manager.free_area[j + 1].start)
            {
                struct mm_block_t temp = g_mm_manager.free_area[j];
                g_mm_manager.free_area[j] = g_mm_manager.free_area[j + 1];
                g_mm_manager.free_area[j + 1] = temp;
            }
        }
    }
    // 合并成一大块
    for (int i = 0; i < MAX_FREE_BLOCK_NUM; i++)
    {
        if (g_mm_manager.free_area[i].size == 0)
        {
            continue;
        }
        for (int j = i + 1; j < MAX_FREE_BLOCK_NUM; j++)
        {
            if (g_mm_manager.free_area[j].size == 0)
            {
                continue;
            }
            if (g_mm_manager.free_area[i].start + g_mm_manager.free_area[i].size == g_mm_manager.free_area[j].start)
            {
                g_mm_manager.free_area[i].size += g_mm_manager.free_area[j].size;
                g_mm_manager.free_area[j].size = 0;
            }
        }
    }
}
bool free(uint32_t ptr)
{
    uint32_t addr = (uint32_t)ptr;
    uint32_t size = 0;
    ASSERT(addr > 0x100000 && addr != 0);
    //获取到这个内存块的大小
    for (int i = 0; i < MAX_ALLOC_BLOCK_NUM; i++)
    {
        if (g_mm_manager.alloc_area[i].start == addr)
        {
            size = g_mm_manager.alloc_area[i].size;
            g_mm_manager.alloc_area[i].start = 0;
            g_mm_manager.alloc_area[i].size = 0;
            break;
        }
    }
    ASSERT(size != 0);
    //合并到大块里
    for (int i = 0; i < MAX_FREE_BLOCK_NUM; i++)
    {
        if (g_mm_manager.free_area[i].start == addr + size)
        {
            g_mm_manager.free_area[i].start -= size;
            g_mm_manager.free_area[i].size += size;
            return TRUE;
        }
    }
    //否则独立成一小块
    for (int i = 0; i < MAX_FREE_BLOCK_NUM; i++)
    {
        if (g_mm_manager.free_area[i].size == 0)
        {
            g_mm_manager.free_area[i].start = addr;
            g_mm_manager.free_area[i].size = size;
            return TRUE;
        }
    }
    panic("No free memory block");
}
// 返回当前可用的内存大小
uint32_t get_total_free()
{
    uint32_t total = 0;
    for (int i = 0; i < MAX_FREE_BLOCK_NUM; i++)
    {
        total += g_mm_manager.free_area[i].size;
    }
    return total;
}

// 返回当前已分配的内存大小
uint32_t get_total_alloc()
{
    uint32_t total = 0;
    for (int i = 0; i < MAX_ALLOC_BLOCK_NUM; i++)
    {
        total += g_mm_manager.alloc_area[i].size;
    }
    return total;
}

// 返回当前cr3寄存器的值
uint32_t get_cr3()
{
    uint32_t cr3;
    // 获取cr3
    asm volatile("movl %%cr3, %0"
                 : "=r"(cr3));
    return cr3;
}
// 设置虚拟地址vm_addr所在的物理页的属性 (cr3寄存器存的都是物理地址)
void set_vm_attr(uint32_t vm_addr, uint32_t cr3, uint32_t attr)
{
    // pde pte 所在的物理地址必须小于KERNEL_LIMIT
    ASSERT(0 == (cr3 & 0x3ff));
    ASSERT(cr3 < KERNEL_LIMIT)

    uint32_t pde_idx = vm_addr >> 22;
    uint32_t pte_idx = ((vm_addr) >> 12) & 0x3ff;
    struct pde_t *pde = (struct pde_t *)TOHM(((cr3)));
    uint32_t pte_ptr = ((pde[pde_idx].base) << 12);
    ASSERT(pte_ptr < KERNEL_LIMIT);
    struct pte_t *pte = (struct pte_t *)TOHM(pte_ptr);
    uint32_t *old_attr_ptr = (((uint32_t *)(&pte[pte_idx])));
    *old_attr_ptr = (*old_attr_ptr & 0xfffff000) | attr;
}

#ifdef DEBUG
void test()
{
    for (int i = 0; i < 1024; i++)
    {
        uint32_t pre_a, pre_b;
        pre_a = get_total_free();
        pre_b = get_total_alloc();

        uint32_t t = (uint32_t)alloc(0, 1 + i * 3);

        free(t);

        t = (uint32_t)alloc_4k(0, 1 + i * 3);
        ASSERT((t & 0x3ff) == 0)
        free(t);

        t = (uint32_t)alloc(i * 0x10000, 1 + i * 3);
        ASSERT(t >= (i * 0x10000));
        free(t);

        t = (uint32_t)alloc_4k(i * 0x10000, 1 + i * 3);
        ASSERT(t >= (i * 0x10000));
        ASSERT((t & 0x3ff) == 0)
        free(t);

        uint32_t aft_a, afte_b;
        aft_a = get_total_free();
        afte_b = get_total_alloc();
        ASSERT(pre_a == aft_a);
        ASSERT(pre_b == afte_b);
    }
}
#endif