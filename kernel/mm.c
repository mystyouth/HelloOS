#include "./include/mm.h"
#include "./include/console.h"
#include "./include/debug.h"
#include "../common/include/strings.h"
extern uint8_t g_kern_start[];
extern uint8_t g_kern_end[];
uint32_t g_seg_cnt = 0; // 管理的物理段的数量
uint32_t g_def_cr3 = 0; // 默认内核使用的页目录表
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

    g_def_cr3 = get_cr3();
    ASSERT(g_def_cr3 < KERNEL_LIMIT)
}

//  添加用于内存管理的段
void add_memseg(uint32_t addr, uint32_t size)
{
    g_mm_manager.free_area[g_seg_cnt].start = addr;
    g_mm_manager.free_area[g_seg_cnt].size = size;
    g_seg_cnt++;
}

// 分配未使用的物理内存空间 分配4k对齐的内存，成功返回内存地址，失败返回0
void *alloc_phy_4k(uint32_t start_addr, uint32_t size)
{
    merge();
    if (0 == size)
    {
#ifdef DEBUG
        kprintf("alloc_4k failed(size=0)\n");
#endif
        return 0;
    }
    for (int i = MAX_FREE_BLOCK_NUM - 1; i > 0; i--)
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
                continue;
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
                PANIC("alloc_4k failed(no space)");
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
                PANIC("alloc_4k failed(no space2)");
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
                continue;
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
                PANIC("alloc_4k failed");
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
                PANIC("alloc_4k failed");
            }
            return (void *)alloc_addr;
        }
    }
#ifdef DEBUG
    kprintf("alloc_4k failed\n");
#endif
    return 0;
}
void *alloc_phy(uint32_t start_addr, uint32_t size)
{
    merge();
    if (0 == size)
    {
#ifdef DEBUG
        kprintf("alloc failed(size=0)\n");
#endif
        return 0;
    }
    for (int i = MAX_FREE_BLOCK_NUM - 1; i > 0; i--)
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
                PANIC("alloc failed");
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
                PANIC("alloc failed");
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
bool free_phy(uint32_t ptr)
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
    PANIC("No free memory block\n");
}
// 返回当前可用的内存大小
uint32_t get_phy_total_free()
{
    uint32_t total = 0;
    for (int i = 0; i < MAX_FREE_BLOCK_NUM; i++)
    {
        total += g_mm_manager.free_area[i].size;
    }
    return total;
}

// 返回当前已分配的内存大小
uint32_t get_phy_total_alloc()
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

// 为虚拟地址addr分配对应的物理页
bool alloc_phy_for_vma(uint32_t vm_addr, uint32_t cr3)
{
    ASSERT(0 == (cr3 & 0x3ff));
    ASSERT(cr3 < KERNEL_LIMIT)

    uint32_t pde_idx = vm_addr >> 22;
    uint32_t pte_idx = ((vm_addr) >> 12) & 0x3ff;
    uint32_t page_offset = vm_addr & 0xfff;
    ASSERT(pde_idx < 0x300)
    struct pde_t *pde = (struct pde_t *)TOHM(((cr3)));
    if (0 == pde[pde_idx].present)
    {
        uint32_t m_pte = (uint32_t)alloc_phy_4k(0, 1024 * 4);
        if (0 == m_pte)
        {
#ifdef DEBUG
            kprintf("alloc_phy_for_vma failed");
#endif
            return FALSE;
        }
        memset((void *)TOHM(m_pte), 0, 1024 * 4);
        pde[pde_idx].present = 1;
        pde[pde_idx].base = m_pte >> 12;
    }

    uint32_t pte_ptr = ((pde[pde_idx].base) << 12);
    ASSERT(pte_ptr < KERNEL_LIMIT);

    struct pte_t *pte = (struct pte_t *)TOHM(pte_ptr);
    if (0 == pte[pte_idx].present)
    {
        uint32_t m_page = (uint32_t)alloc_phy_4k(0, 1024 * 4);
        if (0 == m_page)
        {
#ifdef DEBUG
            kprintf("alloc_phy_for_vma failed");
#endif
            return FALSE;
        }
        memset((void *)TOHM(m_page), 0, 1024 * 4);
        pte[pte_idx].present = 1;
        pte[pte_idx].base = m_page >> 12;
    }
    return TRUE;
}

// 释放虚拟地址addr对应的物理页
bool free_phy_for_vma(uint32_t vm_addr, uint32_t cr3)
{
    ASSERT(0 == (cr3 & 0x3ff));
    ASSERT(cr3 < KERNEL_LIMIT)

    uint32_t pde_idx = vm_addr >> 22;
    uint32_t pte_idx = ((vm_addr) >> 12) & 0x3ff;
    uint32_t page_offset = vm_addr & 0xfff;
    ASSERT(pde_idx < 0x300)
    struct pde_t *pde = (struct pde_t *)TOHM(((cr3)));
    if (0 == pde[pde_idx].present)
    {
#ifdef DEBUG
        kprintf("free_phy_for_vma failed");
#endif
        return FALSE;
    }
    uint32_t pte_ptr = ((pde[pde_idx].base) << 12);
    ASSERT(pte_ptr < KERNEL_LIMIT);

    struct pte_t *pte = (struct pte_t *)TOHM(pte_ptr);
    if (0 == pte[pte_idx].present)
    {
#ifdef DEBUG
        kprintf("free_phy_for_vma failed");
#endif
        return FALSE;
    }
    pte[pte_idx].present = 0;
    uint32_t page_ptr = ((pte[pte_idx].base) << 12);
    return free_phy(page_ptr);
}

// 创建一个页目录表，高1G依然设置为内核空间，成功返回物理地址，失败0
uint32_t create_cr3()
{
    uint32_t cr3 = (uint32_t)alloc_phy_4k(0, 4 * 1024);
    if (0 == cr3)
    {
#ifdef DEBUG
        kprintf("create_cr3 failed\n");
#endif
        return 0;
    }
    memset((void *)TOHM(cr3), 0, 1024 * 4);

    uint32_t *def_pde = (uint32_t *)TOHM(g_def_cr3);
    uint32_t *cur_pde = (uint32_t *)TOHM(cr3);

    cur_pde[0x300] = def_pde[0x300];
    return cr3;
}

void switch_cr3(uint32_t cr3)
{
    // 加载cr3
    asm volatile("mov %0, %%cr3" ::"r"(cr3));
}

// 清除占用的物理页 返回释放的物理内存字节数
uint32_t clean_cr3(uint32_t cr3)
{
    uint32_t total = 0;
    ASSERT(0 == (cr3 & 0x3ff));
    ASSERT(cr3 < KERNEL_LIMIT)

    uint32_t *pde = (uint32_t *)TOHM(cr3);
    for (int i = 0; i < 0x300; i++)
    {
        if (pde[i] & 0x1)
        {
            uint32_t *pte = (uint32_t *)TOHM((pde[i] & 0xfffff000));
            for (int j = 0; j < 1024; j++)
            {
                if (pte[j] & 0x1)
                {
                    total += 0x1000;
                    ASSERT(free_phy((uint32_t)(pte[j] & 0xfffff000)))
                }
            }
            total += 0x1000;
            ASSERT(free_phy((uint32_t)(pde[i] & 0xfffff000)))
        }
    }
    total += 0x1000;
    ASSERT(free_phy((uint32_t)(cr3)))

    return total;
}

// 初始化虚拟内存Bitmap
void init_vm(uint32_t cr3)
{
    ASSERT((0 != VM_START_ADDR) && (0 == (VM_START_ADDR & 0x3ff)))
    uint32_t cnt = VM_START_ADDR / 0x1000;
    uint32_t s = 0;
    for (int i = 0; i < cnt; i++)
    {
        ASSERT(alloc_phy_for_vma(s, cr3))
        s += 0x1000;
    }
    // 虚拟内存[0,VM_START_ADDR)是bitmap用来管理虚拟内存的
    switch_cr3(cr3);
    memset((void *)0, 0, VM_START_ADDR);
    switch_cr3(g_def_cr3);
}

//
void reverse_bitmap(uint32_t start_group_idx, uint32_t start_bit_idx, uint32_t end_group_idx, uint32_t end_bit_idx)
{
    ASSERT(start_group_idx <= end_group_idx)
    ASSERT(start_bit_idx < 8)
    ASSERT(end_bit_idx < 8)

    if (start_group_idx == end_group_idx)
    {
        uint8_t *p = (uint8_t *)(start_group_idx);

        for (int i = start_bit_idx; i <= end_bit_idx; i++)
        {
            *p ^= (1 << i);
        }
        return;
    }
    for (int k = start_group_idx; k <= end_group_idx; k++)
    {
        // 用的虚拟内存访问
        uint8_t *p = (uint8_t *)(k);
        if (k < end_group_idx)
        {
            *p ^= (0xff << start_bit_idx);
            continue;
        }
        else
        {
            for (int i = 0; i <= end_bit_idx; i++)
            {
                *p ^= (1 << i);
            }
        }
    }
    return;
}

// 分配虚拟内存，分配实际的物理页 返回4k对齐的虚拟地址
void *alloc_vm(uint32_t cr3, uint32_t size)
{
    ASSERT(0 == (cr3 & 0x3ff));
    ASSERT(cr3 < KERNEL_LIMIT);

    // 计算实际需要的内存大小和页数
    uint32_t need_size = size + sizeof(struct vm_controll_t);
    uint32_t need_page_cnt = need_size / (VM_BITMAP_PAGE_SIZE);
    // 页数不够，需要多分配一页
    if (0 != (need_size % (VM_BITMAP_PAGE_SIZE)))
    {
        need_page_cnt++;
    }
    // 记录连续的0的个数
    uint32_t continue_free_bit_cnt = 0;
    int32_t start_bit_offset = -1;
    switch_cr3(cr3);

    // [0,VM_START_ADDR)虚拟内存空间是bitmap用来管理虚拟内存的
    for (int i = 0; i < VM_START_ADDR; i++)
    {
        uint8_t *p = (uint8_t *)i;
        if ((*p) != 0xff)
        {
            for (int j = 0; j < 8; j++)
            {
                if (((*p) & (1 << j)) == 0)
                {
                    if (start_bit_offset == -1)
                    {
                        start_bit_offset = i * 8 + j;
                        continue_free_bit_cnt = 1;
                    }
                    else
                    {
                        continue_free_bit_cnt++;
                    }
                    // 找到了足够的连续空闲页
                    if (continue_free_bit_cnt == need_page_cnt)
                    {
                        uint32_t start_group_idx = start_bit_offset / 8;
                        uint32_t start_bit_idx = start_bit_offset % 8;

                        uint32_t end_group_idx = i;
                        uint32_t end_bit_idx = j;

                        // 标记这段内存为已使用
                        reverse_bitmap(start_group_idx, start_bit_idx, end_group_idx, end_bit_idx);

                        //
                        uint32_t target_addr = VM_START_ADDR + start_bit_offset * VM_BITMAP_PAGE_SIZE;

                        // 写入控制块
                        struct vm_controll_t *vmc = (struct vm_controll_t *)target_addr;

                        // 分配实际的物理页
                        alloc_phy_for_vma_range((uint32_t)vmc, need_size, cr3);

                        vmc->size = need_size;
                        vmc->start_addr = ((uint32_t)vmc) + sizeof(struct vm_controll_t);
                        vmc->start_bit_offset = start_bit_offset;
                        vmc->end_bit_offset = end_group_idx * 8 + end_bit_idx;
                        ASSERT((((uint32_t)vmc) & 0x3ff) == 0);

                        uint32_t ret = vmc->start_addr;
                        // 切换回默认内核的页表
                        switch_cr3(g_def_cr3);
                        return (void *)ret;
                    }
                }
                else
                {
                    start_bit_offset = -1;
                    continue_free_bit_cnt = 0;
                }
            }
        }
    }
    switch_cr3(g_def_cr3);
#ifdef DEBUG
    kprintf("alloc_vm failed");
#endif
    return 0;
}
// 释放虚拟内存
bool free_vm(uint32_t vm_addr, uint32_t cr3)
{
    ASSERT(0 == (cr3 & 0x3ff));
    ASSERT(cr3 < KERNEL_LIMIT);
    switch_cr3(cr3);
    struct vm_controll_t *vmc = (struct vm_controll_t *)(vm_addr - sizeof(struct vm_controll_t));
    ASSERT(0 == (((uint32_t)vmc) & 0x3ff));
    uint32_t start_group_idx = vmc->start_bit_offset / 8;
    uint32_t start_bit_idx = vmc->start_bit_offset % 8;
    uint32_t end_group_idx = vmc->end_bit_offset / 8;
    uint32_t end_bit_idx = vmc->end_bit_offset % 8;

    // 标记为未使用
    reverse_bitmap(start_group_idx, start_bit_idx, end_group_idx, end_bit_idx);

    // 释放占用的物理页
    free_phy_for_vma_range((uint32_t)vmc, vmc->size, cr3);
    switch_cr3(g_def_cr3);
}
// 为一片虚拟内存分配实际的物理页
void alloc_phy_for_vma_range(uint32_t vm_addr, uint32_t size, uint32_t cr3)
{
    // 所在的起始页
    uint32_t start = vm_addr & 0xfffff000;
    uint32_t end = (vm_addr + size - 1) & 0xfffff000;

    for (uint32_t i = start; i <= end; i += 0x1000)
    {
        ASSERT((((uint32_t)i) & 0xfff) == 0);
        ASSERT(alloc_phy_for_vma(i, cr3))
    }
}
// 释放掉这片虚拟内存占用的物理页
void free_phy_for_vma_range(uint32_t vm_addr, uint32_t size, uint32_t cr3)
{
    ASSERT(0 == (cr3 & 0x3ff));
    ASSERT(cr3 < KERNEL_LIMIT);
    ASSERT((((uint32_t)vm_addr) & 0xfff) == 0);

    // 所在的起始页
    uint32_t start = vm_addr & 0xfffff000;
    uint32_t end = (vm_addr + size - 1) & 0xfffff000;

    for (uint32_t i = start; i <= end; i += 0x1000)
    {
        ASSERT((((uint32_t)i) & 0xfff) == 0);
        free_phy_for_vma(i, cr3);
    }
}

// 返回分配的虚拟内存大小
uint32_t get_vm_total_alloc(uint32_t cr3)
{
    ASSERT(0 == (cr3 & 0x3ff));
    ASSERT(cr3 < KERNEL_LIMIT);
    switch_cr3(cr3);
    uint32_t ret = 0;
    for (int i = 0; i < VM_START_ADDR; i++)
    {
        uint8_t *p = (uint8_t *)(i);
        if (*p == 0xff)
        {
            ret += VM_BITMAP_PAGE_SIZE * 8;
        }
        else
        {
            for (int j = 0; j < 8; j++)
            {
                if (((*p) & (1 << j)) == 1)
                {
                    ret += VM_BITMAP_PAGE_SIZE;
                }
            }
        }
    }
    switch_cr3(g_def_cr3);
    return ret;
}
#ifdef DEBUG
void test_mm()
{
    {
        // uint32_t aa = get_phy_total_free();
        // uint32_t bb = get_phy_total_alloc();

        // uint32_t cr3 = create_cr3();
        // init_vm(cr3);
        // uint32_t a = get_vm_total_alloc(cr3);

        // uint32_t *p = alloc_vm(cr3, 30500);
        // uint32_t b = get_vm_total_alloc(cr3);
        // uint32_t *p2 = alloc_vm(cr3, 70591);
        // b = get_vm_total_alloc(cr3);
        // uint32_t *p3 = alloc_vm(cr3, 8105);
        // b = get_vm_total_alloc(cr3);
        // free_vm((uint32_t)p3, cr3);
        // free_vm((uint32_t)p, cr3);
        // free_vm((uint32_t)p2, cr3);

        // b = get_vm_total_alloc(cr3);
        // ASSERT(a == b)

        // clean_cr3(cr3);
        // uint32_t aaa = get_phy_total_free();
        // uint32_t bbb = get_phy_total_alloc();
        // ASSERT(aaa == aa);
        // ASSERT(bbb == bb);
    }
    // {
    //     uint32_t cr3 = create_cr3();
    //     init_vm(cr3);
    //     uint32_t a = get_phy_total_free();
    //     uint32_t b = get_phy_total_alloc();

    //     uint32_t *p = alloc_vm(cr3, 105);
    //     uint32_t *p2 = alloc_vm(cr3, 105);
    //     uint32_t *p3 = alloc_vm(cr3, 105);
    //     free_vm((uint32_t)p3, cr3);
    //     free_vm((uint32_t)p, cr3);
    //     free_vm((uint32_t)p2, cr3);
    //     const int test_cnt = 10;
    //     int ptrs[test_cnt];
    //     for (int i = 0; i < test_cnt; i++)
    //     {
    //         ptrs[i] = alloc_vm(cr3, (i + 1) * 0x1000);
    //     }
    //     for (int i = 0; i < test_cnt; i++)
    //     {
    //         free_vm(ptrs[i], cr3);
    //     }

    //     uint32_t aa = get_phy_total_free();
    //     uint32_t bb = get_phy_total_alloc();
    //     ASSERT(aa == a);
    //     ASSERT(bb == b);
    // }
    // {
    //     uint32_t a = get_phy_total_free();
    //     uint32_t b = get_phy_total_alloc();
    //     uint32_t cr3 = create_cr3();
    //     init_vm(cr3);
    //     clean_cr3(cr3);
    //     uint32_t aa = get_phy_total_free();
    //     uint32_t bb = get_phy_total_alloc();
    //     ASSERT(aa == a);
    //     ASSERT(bb == b);
    // }
    // {
    //     uint32_t *ptr = (uint32_t *)alloc_phy(0, 4);
    //     ptr = (uint32_t *)(TOHM((uint32_t)(ptr)));
    //     *ptr = 1;
    //     set_vm_attr(((uint32_t)ptr), get_cr3(), PAGE_USER_ACCESS | PAGE_WRITE);

    //     bool vma = alloc_phy_for_vma(0x3f000000, get_cr3());
    //     uint32_t *x = (uint32_t *)0x3f000000;
    //     *x = 111;
    //     kprintf("%x %x!!!\n", ptr, *x);
    // }
    for (int i = 0; i < 1; i++)
    {
        uint32_t pre_a, pre_b;
        pre_a = get_phy_total_free();
        pre_b = get_phy_total_alloc();

        uint32_t t = (uint32_t)alloc_phy(0, 1 + i * 3);

        free_phy(t);

        t = (uint32_t)alloc_phy_4k(0, 1 + i * 3);
        ASSERT((t & 0x3ff) == 0)
        free_phy(t);

        t = (uint32_t)alloc_phy(i * 0x10000, 1 + i * 3);
        ASSERT(t >= (i * 0x10000));
        free_phy(t);

        t = (uint32_t)alloc_phy_4k(i * 0x10000, 1 + i * 3);
        ASSERT(t >= (i * 0x10000));
        ASSERT((t & 0x3ff) == 0)
        free_phy(t);

        uint32_t aft_a, afte_b;
        aft_a = get_phy_total_free();
        afte_b = get_phy_total_alloc();
        ASSERT(pre_a == aft_a);
        ASSERT(pre_b == afte_b);
    }
    {
        uint32_t pre_a, pre_b;
        pre_a = get_phy_total_free();
        pre_b = get_phy_total_alloc();

        int ptrs[1024];
        for (int i = 0; i < 1024; i++)
        {
            if (i == 1023)
            {
                kprintf("1");
            }
            ptrs[i] = alloc_phy(i, 1 + i * 3);
        }
        for (int i = 1024 - 1; i >= 0; i--)
        {
            kprintf("%d %d", i, ptrs[i]);
            free_phy(ptrs[i]);
        }
        uint32_t aft_a, afte_b;
        aft_a = get_phy_total_free();
        afte_b = get_phy_total_alloc();
        ASSERT(pre_a == aft_a);
        ASSERT(pre_b == afte_b);
    }
    {
        uint32_t a = get_phy_total_free();
        uint32_t b = get_phy_total_alloc();
        kprintf("%x - %x \n", get_phy_total_free(), get_phy_total_alloc());
        uint32_t cr3 = create_cr3();
        alloc_phy_for_vma(0x3f000000, cr3);
        alloc_phy_for_vma(0xf000000, cr3);
        alloc_phy_for_vma(0x5f000000, cr3);

        alloc_phy_for_vma(0x3f000000, cr3);
        alloc_phy_for_vma(0xf000000, cr3);
        alloc_phy_for_vma(0x5f000000, cr3);

        alloc_phy_for_vma(0x4f000000, cr3);

        alloc_phy_for_vma(0x5f00, cr3);
        alloc_phy_for_vma(0xaf000000, cr3);
        for (int i = 0; i < 80; i++)
        {
            alloc_phy_for_vma(0x1000000 + i * 0x111f1a, cr3);
        }
        kprintf("%x - %x \n", get_phy_total_free(), get_phy_total_alloc());
        clean_cr3(cr3);
        uint32_t aa = get_phy_total_free();
        uint32_t bb = get_phy_total_alloc();
        kprintf("%x - %x \n", get_phy_total_free(), get_phy_total_alloc());
        ASSERT(aa == a);
        ASSERT(bb == b);
    }
}
#endif