/**
 * \file
 * \brief pmap management
 */

/*
 * Copyright (c) 2010,2015, ETH Zurich.
 * Copyright (c) 2015, Hewlett Packard Enterprise Development LP.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

/*
 * There was some minor difficulty here with mapping the cpus native
 * page table arrangement onto Barrelfish. The problem lies with
 * resource bootstrapping. The bootstrap ram allocator allocates pages.
 *
 *
 * The natural division of bits is 12/10/12, corresponding to 4K
 * L1 entries in the L1 table and 256 L2 entries per L2
 * table. Unfortunately 256 entries consumes 1KB rather than a
 * page (4KB) so we pretend here and in the kernel caps page
 * code that the L1 has 1024 entries and L2 tables are 4KB in
 * size. The 4KB constraint comes from ram_alloc_fixed
 * allocating single pages and the difficulty in bootstrapping
 * cap slots (alloc_node takes a single slot.
 *
 * For now this suffices, but might need to be revisited in future.
 *
 * An earlier cut at this, used the first 1KB from each
 * allocation made from ram_alloc_fixed and wasted the remaining
 * space. Aside from the space wasted it entailed a couple of minor
 * platform ifdefs to work around the discrepency.
 *
 * Alternative fixes discussed include:
 *
 * 1. avoid the need to create vnodes before connecting to a
 *    real allocator (probably not plausible).
 *
 * 2. somehow make ram_alloc_fixed handle sub-page allocations
 *    (it's clunky, but perhaps we can give each domain a separate
 *     cnode full of 1k- sized RAM caps?)
 *
 * 3. handle the problem at the level of vnode_create (can't see how to
 *    do this)
 *
 * 4. waste the space -- doing this cleanly will require a new parameter
 * to retype to prevent all 4 caps being created
 *
 * 5. introduce a new arm-specific version of vnode_create that creates
 * 4 1k vnodes, and is only called from the ARM VM code.
 *
 */

#include <barrelfish/barrelfish.h>
#include <barrelfish/caddr.h>
#include <barrelfish/invocations_arch.h>
#include <stdio.h>

// Location of VSpace managed by this system.
#define VSPACE_BEGIN   ((lvaddr_t)(512*512*512*BASE_PAGE_SIZE * (disp_get_core_id() + 1)))

// Amount of virtual address space reserved for mapping frames
// backing refill_slabs. Need way more with pmap_array.
#define META_DATA_RESERVED_SPACE (BASE_PAGE_SIZE * 256000)
// increased above value from 128 for pandaboard port

static inline uintptr_t
vregion_flags_to_kpi_paging_flags(vregion_flags_t flags)
{
    STATIC_ASSERT(0x1ff == VREGION_FLAGS_MASK, "");
    STATIC_ASSERT(0x0f == KPI_PAGING_FLAGS_MASK, "");
    STATIC_ASSERT(VREGION_FLAGS_READ    == KPI_PAGING_FLAGS_READ,    "");
    STATIC_ASSERT(VREGION_FLAGS_WRITE   == KPI_PAGING_FLAGS_WRITE,   "");
    STATIC_ASSERT(VREGION_FLAGS_EXECUTE == KPI_PAGING_FLAGS_EXECUTE, "");
    STATIC_ASSERT(VREGION_FLAGS_NOCACHE == KPI_PAGING_FLAGS_NOCACHE, "");
    if ((flags & VREGION_FLAGS_MPB) != 0) {
        // XXX: ignore MPB flag on ARM,
        //      otherwise the assert below fires -AB
        flags &= ~VREGION_FLAGS_MPB;
    }
    // XXX: Ignore VTD Snoop flag on AArch64 - this stuff really isn't
    // portable -DC
    flags &= ~VREGION_FLAGS_VTD_SNOOP;
    if ((flags & VREGION_FLAGS_GUARD) != 0) {
        flags = 0;
    }
    assert(0 == (~KPI_PAGING_FLAGS_MASK & (uintptr_t)flags));
    return (uintptr_t)flags;
}

static void
set_mapping_cap(struct vnode *vnode, struct vnode *root, uint16_t entry)
{
    assert(root->is_vnode);
    assert(entry < PTABLE_ENTRIES);
    vnode->mapping.cnode = root->u.vnode.mcnode[entry / L2_CNODE_SLOTS];
    vnode->mapping.slot  = entry % L2_CNODE_SLOTS;
    assert(!cnoderef_is_null(vnode->mapping.cnode));
    assert(!capref_is_null(vnode->mapping));
}

static bool has_vnode(struct vnode *root, uint16_t entry, size_t len)
{
    assert(root != NULL);
    assert(root->is_vnode);
    struct vnode *n;

    uint32_t end_entry = entry + len;

#if defined(PMAP_LL)
    for (n = root->u.vnode.children; n; n = n->next) {
#elif defined(PMAP_ARRAY)
    for (int i = 0; i < VMSAv8_64_PTABLE_NUM_ENTRIES; i++) {
        n = root->u.vnode.children[i];
        if (!n) { continue; }
#else
#error invalid pmap data structure
#endif
        if (n->is_vnode && n->entry == entry) {
            return true;
        }
        // n is frame
        uint32_t end = n->entry + n->u.frame.pte_count;
        if (n->entry < entry && end > end_entry) {
            return true;
        }
        if (n->entry >= entry && n->entry < end_entry) {
            return true;
        }
    }

    return false;
}

/**
 * \brief Allocates a new VNode, adding it to the page table and our metadata
 */
static errval_t alloc_vnode(struct pmap_aarch64 *pmap_aarch64, struct vnode *root,
                            enum objtype type, uint32_t entry,
                            struct vnode **retvnode)
{
    assert(root->is_vnode);
    errval_t err;

    if (!retvnode) {
        debug_printf("%s called without retvnode from %p, expect badness!\n", __FUNCTION__, __builtin_return_address(0));
        // XXX: should probably return error.
    }
    assert(retvnode);

    struct vnode *newvnode = slab_alloc(&pmap_aarch64->p.m->slab);
    if (newvnode == NULL) {
        return LIB_ERR_SLAB_ALLOC_FAIL;
    }
    newvnode->is_vnode = true;

    // The VNode capability
    err = pmap_aarch64->p.slot_alloc->alloc(pmap_aarch64->p.slot_alloc, &newvnode->u.vnode.cap);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_SLOT_ALLOC);
    }

    assert(!capref_is_null(newvnode->u.vnode.cap));

    err = vnode_create(newvnode->u.vnode.cap, type);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_VNODE_CREATE);
    }

    assert(!capref_is_null(newvnode->u.vnode.cap));

    // XXX: need to make sure that vnode cap that we will invoke is in our cspace!
    if (get_croot_addr(newvnode->u.vnode.cap) != CPTR_ROOTCN) {
        // debug_printf("%s: creating vnode for another domain in that domain's cspace; need to copy vnode cap to our cspace to make it invokable\n", __FUNCTION__);
        assert(!capref_is_null(newvnode->u.vnode.cap));
        err = slot_alloc(&newvnode->u.vnode.invokable);
        assert(!capref_is_null(newvnode->u.vnode.cap));
        assert(err_is_ok(err));
        assert(!capref_is_null(newvnode->u.vnode.cap));
        err = cap_copy(newvnode->u.vnode.invokable, newvnode->u.vnode.cap);
        assert(err_is_ok(err));
        assert(!capref_is_null(newvnode->u.vnode.invokable));
        assert(!capref_is_null(newvnode->u.vnode.cap));

        assert(!capref_is_null(newvnode->u.vnode.cap));
    } else {
        // debug_printf("vnode in our cspace: copying capref to invokable\n");
        assert(!capref_is_null(newvnode->u.vnode.cap));
        newvnode->u.vnode.invokable = newvnode->u.vnode.cap;
        assert(!capref_is_null(newvnode->u.vnode.cap));
    }
    assert(!capref_is_null(newvnode->u.vnode.cap));
    assert(!capref_is_null(newvnode->u.vnode.invokable));

    // set mapping cap to correct slot in mapping cnodes.
    set_mapping_cap(newvnode, root, entry);

    // Map it
    err = vnode_map(root->u.vnode.invokable, newvnode->u.vnode.cap, entry,
                    KPI_PAGING_FLAGS_READ | KPI_PAGING_FLAGS_WRITE, 0, 1, newvnode->mapping);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_VNODE_MAP);
    }

    // The VNode meta data
    newvnode->is_vnode  = true;
    newvnode->entry     = entry;
    pmap_vnode_init(&pmap_aarch64->p, newvnode);
    pmap_vnode_insert_child(root, newvnode);

    /* allocate mapping cnodes */
    for (int i = 0; i < MCN_COUNT; i++) {
        err = cnode_create_l2(&newvnode->u.vnode.mcn[i], &newvnode->u.vnode.mcnode[i]);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_PMAP_ALLOC_CNODE);
        }
    }

    *retvnode = newvnode;
    return SYS_ERR_OK;
}

/**
 * \brief Returns the vnode for the pagetable mapping a given vspace address
 */
static errval_t get_ptable(struct pmap_aarch64  *pmap,
                           genvaddr_t        vaddr,
                           struct vnode    **ptable)
{
    errval_t err;
    struct vnode *root = &pmap->root;
    struct vnode *pl1, *pl2, *pl3;
    assert(root != NULL);

    // L0 mapping
    if ((pl1 = pmap_find_vnode(root, VMSAv8_64_L0_BASE(vaddr))) == NULL) {
        err = alloc_vnode(pmap, root, ObjType_VNode_AARCH64_l1,
                            VMSAv8_64_L0_BASE(vaddr), &pl1);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_PMAP_ALLOC_VNODE);
        }
    }

    // L1 mapping
    if ((pl2 = pmap_find_vnode(pl1, VMSAv8_64_L1_BASE(vaddr))) == NULL) {
        err = alloc_vnode(pmap, pl1, ObjType_VNode_AARCH64_l2,
                            VMSAv8_64_L1_BASE(vaddr), &pl2);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_PMAP_ALLOC_VNODE);
        }
    }

    // L2 mapping
    if ((pl3 = pmap_find_vnode(pl2, VMSAv8_64_L2_BASE(vaddr))) == NULL) {
        err = alloc_vnode(pmap, pl2, ObjType_VNode_AARCH64_l3,
                            VMSAv8_64_L2_BASE(vaddr), &pl3);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_PMAP_ALLOC_VNODE);
        }
    }

	assert(pl3 != NULL);
	*ptable = pl3;
    return SYS_ERR_OK;
}

static struct vnode *find_ptable(struct pmap_aarch64  *pmap,
                                 genvaddr_t vaddr)
{
    struct vnode *root = &pmap->root;
    struct vnode *pl1, *pl2;
    assert(root != NULL);

    // L0 mapping
    if((pl1 = pmap_find_vnode(root, VMSAv8_64_L0_BASE(vaddr))) == NULL) {
        return NULL;
    }

    // L1 mapping
    if((pl2 = pmap_find_vnode(pl1, VMSAv8_64_L1_BASE(vaddr))) == NULL) {
        return NULL;
    }

    // L2 mapping
    return pmap_find_vnode(pl2, VMSAv8_64_L2_BASE(vaddr));
}

static errval_t do_single_map(struct pmap_aarch64 *pmap, genvaddr_t vaddr, genvaddr_t vend,
                              struct capref frame, size_t offset, size_t pte_count,
                              vregion_flags_t flags)
{
    // Get the page table
    struct vnode *ptable= NULL;
    errval_t err = get_ptable(pmap, vaddr, &ptable);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_PMAP_GET_PTABLE);
    }
    uintptr_t pmap_flags = vregion_flags_to_kpi_paging_flags(flags);

    uintptr_t idx = VMSAv8_64_L3_BASE(vaddr);

    // Create user level datastructure for the mapping
    bool has_page = has_vnode(ptable, idx, pte_count);
    assert(!has_page);

    struct vnode *page = slab_alloc(&pmap->p.m->slab);
    assert(page);

    page->is_vnode = false;
    page->entry = idx;
    pmap_vnode_insert_child(ptable, page);
    page->u.frame.cap = frame;
    page->u.frame.offset = offset;
    page->u.frame.flags = flags;
    page->u.frame.pte_count = pte_count;

    set_mapping_cap(page, ptable, idx);

    // Map entry into the page table
    assert(!capref_is_null(ptable->u.vnode.invokable));
    err = vnode_map(ptable->u.vnode.invokable, frame, idx,
                    pmap_flags, offset, pte_count, page->mapping);

    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_VNODE_MAP);
    }

    return SYS_ERR_OK;
}

static errval_t do_map(struct pmap_aarch64 *pmap, genvaddr_t vaddr,
                       struct capref frame, size_t offset, size_t size,
                       vregion_flags_t flags, size_t *retoff, size_t *retsize)
{
    errval_t err;

    size = ROUND_UP(size, BASE_PAGE_SIZE);
    size_t pte_count = DIVIDE_ROUND_UP(size, BASE_PAGE_SIZE);
    genvaddr_t vend = vaddr + size;

    if (VMSAv8_64_L012_BASE(vaddr) == VMSAv8_64_L012_BASE(vend - 1)) {
        // fast path
        err = do_single_map(pmap, vaddr, vend, frame, offset, pte_count, flags);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "[do_map] in fast path");
            return err_push(err, LIB_ERR_PMAP_DO_MAP);
        }
    } else { // multiple leaf page tables
        // first leaf
        uint32_t c = VMSAv8_64_PTABLE_NUM_ENTRIES - VMSAv8_64_L3_BASE(vaddr);
        genvaddr_t temp_end = vaddr + c * BASE_PAGE_SIZE;
        err = do_single_map(pmap, vaddr, temp_end, frame, offset, c, flags);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_PMAP_DO_MAP);
        }

        // map full leaves
        while (VMSAv8_64_L012_BASE(temp_end) < VMSAv8_64_L012_BASE(vend)) { // update vars
            vaddr = temp_end;
            temp_end = vaddr + VMSAv8_64_PTABLE_NUM_ENTRIES * BASE_PAGE_SIZE;
            offset += c * BASE_PAGE_SIZE;
            c = VMSAv8_64_PTABLE_NUM_ENTRIES;

            // do mapping
            err = do_single_map(pmap, vaddr, temp_end, frame, offset,
                    VMSAv8_64_PTABLE_NUM_ENTRIES, flags);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_PMAP_DO_MAP);
            }
        }

        // map remaining part
        offset += c * BASE_PAGE_SIZE;
        c = VMSAv8_64_L3_BASE(vend) - VMSAv8_64_L3_BASE(temp_end);
        if (c) {
            // do mapping
            err = do_single_map(pmap, temp_end, vend, frame, offset, c, flags);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_PMAP_DO_MAP);
            }
        }
    }
    if (retoff) {
        *retoff = offset;
    }
    if (retsize) {
        *retsize = size;
    }
    //has_vnode_debug = false;
    return SYS_ERR_OK;
#if 0
    errval_t err;
    uintptr_t pmap_flags = vregion_flags_to_kpi_paging_flags(flags);

    for (size_t i = offset; i < offset + size; i += BASE_PAGE_SIZE) {

        vaddr += BASE_PAGE_SIZE;
    }

    if (retoff) {
        *retoff = offset;
    }
    if (retsize) {
        *retsize = size;
    }
    return SYS_ERR_OK;
#endif
}

static size_t
max_slabs_required(size_t bytes)
{
    // Perform a slab allocation for every page (do_map -> slab_alloc)
    size_t pages     = DIVIDE_ROUND_UP(bytes, BASE_PAGE_SIZE);

    // Perform a slab allocation for every L2 (get_ptable -> find_vnode)
    size_t l3entries = DIVIDE_ROUND_UP(pages, 512);

    // Perform a slab allocation for every L2 (get_ptable -> find_vnode)
    size_t l2entries = DIVIDE_ROUND_UP(l3entries, 512);

    // Perform a slab allocation for every L1 (do_map -> find_vnode)
    size_t l1entries = DIVIDE_ROUND_UP(l2entries, 512);

    // Perform a slab allocation for every L0 (do_map -> find_vnode)
    size_t l0entries = DIVIDE_ROUND_UP(l1entries, 512);

    return pages + l3entries + l2entries + l1entries + l0entries;
}

/**
 * \brief Refill slabs using pages from fixed allocator, used if we need to
 * refill the slab allocator before we've established a connection to
 * the memory server.
 *
 * \arg pmap the pmap whose slab allocator we want to refill
 * \arg bytes the refill buffer size in bytes
 */
static errval_t refill_slabs_fixed_allocator(struct pmap_aarch64 *pmap,
                                             struct slab_allocator *slab, size_t bytes)
{
    size_t pages = DIVIDE_ROUND_UP(bytes, BASE_PAGE_SIZE);

    genvaddr_t vbase = pmap->vregion_offset;

    // Allocate and map buffer using base pages
    for (int i = 0; i < pages; i++) {
        struct capref cap;
        size_t retbytes;
        // Get page
        errval_t err = frame_alloc(&cap, BASE_PAGE_SIZE, &retbytes);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_FRAME_ALLOC);
        }
        assert(retbytes == BASE_PAGE_SIZE);

        // Map page
        genvaddr_t genvaddr = pmap->vregion_offset;
        pmap->vregion_offset += (genvaddr_t)BASE_PAGE_SIZE;
        assert(pmap->vregion_offset < vregion_get_base_addr(&pmap->vregion) +
                vregion_get_size(&pmap->vregion));

        err = do_map(pmap, genvaddr, cap, 0, BASE_PAGE_SIZE,
                VREGION_FLAGS_READ_WRITE, NULL, NULL);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_PMAP_DO_MAP);
        }
    }

    /* Grow the slab */
    lvaddr_t buf = vspace_genvaddr_to_lvaddr(vbase);
    //debug_printf("%s: Calling slab_grow with %#zx bytes\n", __FUNCTION__, bytes);
    slab_grow(slab, (void*)buf, bytes);

    return SYS_ERR_OK;
}

/**
 * \brief Refill slabs used for metadata
 *
 * \param pmap     The pmap to refill in
 * \param request  The number of slabs the allocator must have
 * when the function returns
 *
 * When the current pmap is initialized,
 * it reserves some virtual address space for metadata.
 * This reserved address space is used here
 *
 * Can only be called for the current pmap
 * Will recursively call into itself till it has enough slabs
 */
bool debug_refill = false;
static errval_t refill_slabs(struct pmap_aarch64 *pmap, struct slab_allocator *slab, size_t request)
{
    errval_t err;

    /* Keep looping till we have #request slabs */
    while (slab_freecount(slab) < request) {
        // Amount of bytes required for #request
        size_t slabs_req = request - slab_freecount(slab);
        size_t bytes = SLAB_STATIC_SIZE(slabs_req,
                                        slab->blocksize);
        bytes = ROUND_UP(bytes, BASE_PAGE_SIZE);

        if (debug_refill) {
        debug_printf("%s: req=%zu, bytes=%zu, slab->blocksize=%zu, slab->freecount=%zu\n",
                __FUNCTION__, slabs_req, bytes, slab->blocksize, slab_freecount(slab));
        }

        /* Get a frame of that size */
        struct capref cap;
        size_t retbytes = 0;
        err = frame_alloc(&cap, bytes, &retbytes);
        if (err_is_fail(err)) {
            if (err_no(err) == LIB_ERR_RAM_ALLOC_MS_CONSTRAINTS &&
                err_no(err_pop(err)) == LIB_ERR_RAM_ALLOC_WRONG_SIZE) {
                /* only retry with fixed allocator if we get
                 * LIB_ERR_RAM_ALLOC_WRONG_SIZE.
                 */
                return refill_slabs_fixed_allocator(pmap, slab, bytes);
            }
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_FRAME_ALLOC);
            }
        }
        bytes = retbytes;

        /* If we do not have enough slabs to map the frame in, recurse */
        size_t required_slabs_for_frame = max_slabs_required(bytes);
        // Here we need to check that we have enough vnode slabs, not whatever
        // slabs we're refilling
        if (slab_freecount(&pmap->p.m->slab) < required_slabs_for_frame) {
            debug_printf("%s: called from %p\n", __FUNCTION__,
                    __builtin_return_address(0));
            // If we recurse, we require more slabs than to map a single page
            assert(required_slabs_for_frame > max_slabs_required(BASE_PAGE_SIZE));
            if (required_slabs_for_frame <= max_slabs_required(BASE_PAGE_SIZE)) {
                USER_PANIC(
                    "%s: cannot handle this recursion: required slabs = %zu > max slabs for a mapping (%zu)\n",
                    __FUNCTION__, required_slabs_for_frame, max_slabs_required(BASE_PAGE_SIZE));
            }

            err = refill_slabs(pmap, slab, required_slabs_for_frame);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_SLAB_REFILL);
            }
        }

        /* Perform mapping */
        genvaddr_t genvaddr = pmap->vregion_offset;
        pmap->vregion_offset += (genvaddr_t)bytes;
        assert(pmap->vregion_offset < vregion_get_base_addr(&pmap->vregion) +
               vregion_get_size(&pmap->vregion));

        err = do_map(pmap, genvaddr, cap, 0, bytes,
                     VREGION_FLAGS_READ_WRITE, NULL, NULL);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_PMAP_DO_MAP);
        }

        /* Grow the slab */
        lvaddr_t buf = vspace_genvaddr_to_lvaddr(genvaddr);
        slab_grow(slab, (void*)buf, bytes);
    }

    return SYS_ERR_OK;
}

static errval_t refill_vnode_slabs(struct pmap *pmap, size_t count)
{
    struct pmap_aarch64 *pmapx = (struct pmap_aarch64 *)pmap;
    return refill_slabs(pmapx, &pmap->m->slab, count);
}

#ifdef PMAP_ARRAY
static errval_t refill_pt_slabs(struct pmap *pmap, size_t count)
{
    struct pmap_aarch64 *pmapx = (struct pmap_aarch64 *)pmap;
    return refill_slabs(pmapx, &pmap->m->ptslab, count);
}
#endif

/**
 * \brief Create page mappings
 *
 * \param pmap     The pmap object
 * \param vaddr    The virtual address to create the mapping for
 * \param frame    The frame cap to map in
 * \param offset   Offset into the frame cap
 * \param size     Size of the mapping
 * \param flags    Flags for the mapping
 * \param retoff   If non-NULL, filled in with adjusted offset of mapped region
 * \param retsize  If non-NULL, filled in with adjusted size of mapped region
 */
static errval_t
map(struct pmap     *pmap,
    genvaddr_t       vaddr,
    struct capref    frame,
    size_t           offset,
    size_t           size,
    vregion_flags_t  flags,
    size_t          *retoff,
    size_t          *retsize)
{
    struct pmap_aarch64 *pmap_aarch64 = (struct pmap_aarch64 *)pmap;

    size   += BASE_PAGE_OFFSET(offset);
    size    = ROUND_UP(size, BASE_PAGE_SIZE);
    offset -= BASE_PAGE_OFFSET(offset);

    const size_t slabs_reserve = 3; // == max_slabs_required(1)
    uint64_t  slabs_free       = slab_freecount(&pmap_aarch64->p.m->slab);
    size_t    slabs_required   = max_slabs_required(size) + slabs_reserve;

    if (slabs_required > slabs_free) {
        if (get_current_pmap() == pmap) {
            errval_t err = refill_vnode_slabs(pmap, slabs_required);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_SLAB_REFILL);
            }
        }
        else {
            size_t bytes = SLAB_STATIC_SIZE(slabs_required - slabs_free,
                                            sizeof(struct vnode));
            void *buf = malloc(bytes);
            if (!buf) {
                return LIB_ERR_MALLOC_FAIL;
            }
            slab_grow(&pmap->m->slab, buf, bytes);
        }
    }
#ifdef PMAP_ARRAY
    // Refill child array slabs, if necessary
    size_t ptslabs_free = slab_freecount(&pmap->m->ptslab);
    size_t max_ptslabs = slabs_required; // XXX Is this a strict overestimation??

    if (ptslabs_free < max_ptslabs) {
        struct pmap *mypmap = get_current_pmap();
        if (pmap == mypmap) {
            errval_t err;
            err = refill_pt_slabs(pmap, max_ptslabs);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_SLAB_REFILL);
            }
        } else {
            size_t bytes = SLAB_STATIC_SIZE(max_ptslabs - ptslabs_free,
                                            sizeof(struct vnode *)*PTABLE_ENTRIES);
            void *buf = malloc(bytes);
            if (!buf) {
                return LIB_ERR_MALLOC_FAIL;
            }
            slab_grow(&pmap_aarch64->p.m->ptslab, buf, bytes);
        }
    }
#endif

    return do_map(pmap_aarch64, vaddr, frame, offset, size, flags,
                  retoff, retsize);
}

static errval_t do_single_unmap(struct pmap_aarch64 *pmap, genvaddr_t vaddr,
                                size_t pte_count)
{
    errval_t err;
    struct vnode *pt = find_ptable(pmap, vaddr);
    if (pt) {
        struct vnode *page = pmap_find_vnode(pt, VMSAv8_64_L3_BASE(vaddr));
        if (page && page->u.frame.pte_count == pte_count) {
            err = vnode_unmap(pt->u.vnode.cap, page->mapping);
            if (err_is_fail(err)) {
                DEBUG_ERR(err, "vnode_unmap");
                return err_push(err, LIB_ERR_VNODE_UNMAP);
            }

            err = cap_delete(page->mapping);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_CAP_DELETE);
            }
            pmap_remove_vnode(pt, page);
            slab_free(&pmap->p.m->slab, page);
        }
        else {
            return LIB_ERR_PMAP_FIND_VNODE;
        }
    }

    return SYS_ERR_OK;
}

/**
 * \brief Remove page mappings
 *
 * \param pmap     The pmap object
 * \param vaddr    The start of the virtual addres to remove
 * \param size     The size of virtual address to remove
 * \param retsize  If non-NULL, filled in with the actual size removed
 */
static errval_t
unmap(struct pmap *pmap,
      genvaddr_t   vaddr,
      size_t       size,
      size_t      *retsize)
{
    errval_t err, ret = SYS_ERR_OK;
    struct pmap_aarch64 *pmap_aarch64 = (struct pmap_aarch64*)pmap;
    size = ROUND_UP(size, BASE_PAGE_SIZE);
    size_t pte_count = size / BASE_PAGE_SIZE;
    genvaddr_t vend = vaddr + size;

    if (VMSAv8_64_L012_BASE(vaddr) == VMSAv8_64_L012_BASE(vend - 1)) {
        // fast path
        err = do_single_unmap(pmap_aarch64, vaddr, pte_count);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_PMAP_UNMAP);
        }
    } else { // slow path
        // unmap first leaf
        uint32_t c = VMSAv8_64_PTABLE_NUM_ENTRIES - VMSAv8_64_L3_BASE(vaddr);
        err = do_single_unmap(pmap_aarch64, vaddr, c);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_PMAP_UNMAP);
        }

        // unmap full leaves
        vaddr += c * BASE_PAGE_SIZE;
        while (VMSAv8_64_L012_BASE(vaddr) < VMSAv8_64_L012_BASE(vend)) {
            c = VMSAv8_64_PTABLE_NUM_ENTRIES;
            err = do_single_unmap(pmap_aarch64, vaddr, c);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_PMAP_UNMAP);
            }
            vaddr += c * BASE_PAGE_SIZE;
        }

        // unmap remaining part
        c = VMSAv8_64_L3_BASE(vend) - VMSAv8_64_L3_BASE(vaddr);
        if (c) {
            err = do_single_unmap(pmap_aarch64, vaddr, c);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_PMAP_UNMAP);
            }
        }
    }

    if (retsize) {
        *retsize = size;
    }

    return ret;
}

/**
 * \brief Determine a suitable address for a given memory object
 *
 * \param pmap    The pmap object
 * \param memobj  The memory object to determine the address for
 * \param alignment Minimum alignment
 * \param vaddr   Pointer to return the determined address
 *
 * Relies on vspace.c code maintaining an ordered list of vregions
 */
static errval_t
determine_addr(struct pmap   *pmap,
               struct memobj *memobj,
               size_t        alignment,
               genvaddr_t    *retvaddr)
{
    assert(pmap->vspace->head);
    struct pmap_aarch64* pmap_aarch64 = (struct pmap_aarch64*)pmap;
    genvaddr_t vaddr;

    if (alignment == 0) {
        alignment = BASE_PAGE_SIZE;
    } else {
        alignment = ROUND_UP(alignment, BASE_PAGE_SIZE);
    }
    size_t size = ROUND_UP(memobj->size, alignment);

    struct vregion *walk = pmap->vspace->head;
    while (walk->next) { // Try to insert between existing mappings
        genvaddr_t walk_base = vregion_get_base_addr(walk);
        genvaddr_t walk_size = ROUND_UP(vregion_get_size(walk), BASE_PAGE_SIZE);
        genvaddr_t walk_end  = ROUND_UP(walk_base + walk_size, alignment);
        genvaddr_t next_base = vregion_get_base_addr(walk->next);

        // sanity-check for page alignment
        assert(walk_base % BASE_PAGE_SIZE == 0);
        assert(next_base % BASE_PAGE_SIZE == 0);

        if (next_base > walk_end + size && walk_end > VSPACE_BEGIN) {
            vaddr = walk_end;
            goto out;
        }

        walk = walk->next;
    }

    // place beyond last mapping with alignment
    vaddr = ROUND_UP((vregion_get_base_addr(walk)
                + ROUND_UP(vregion_get_size(walk), BASE_PAGE_SIZE)),
                alignment);



out:
    // ensure that we haven't run out of the valid part of the address space
    if (vaddr + memobj->size > pmap_aarch64->max_mappable_va) {
        return LIB_ERR_OUT_OF_VIRTUAL_ADDR;
    }
    assert(retvaddr != NULL);
    *retvaddr = vaddr;

    return SYS_ERR_OK;
}

static errval_t do_single_modify_flags(struct pmap_aarch64 *pmap, genvaddr_t vaddr,
                                       size_t pages, vregion_flags_t flags)
{
    errval_t err = SYS_ERR_OK;
    struct vnode *ptable = find_ptable(pmap, vaddr);
    uint16_t ptentry = VMSAv8_64_L3_BASE(vaddr);
    if (ptable) {
        struct vnode *page = pmap_find_vnode(ptable, ptentry);
        if (page) {
            if (pmap_inside_region(ptable, ptentry, pages)) {
                // we're modifying part of a valid mapped region
                // arguments to invocation: invoke frame cap, first affected
                // page (as offset from first page in mapping), #affected
                // pages, new flags. Invocation should check compatibility of
                // new set of flags with cap permissions.
                size_t off = ptentry - page->entry;
                uintptr_t pmap_flags = vregion_flags_to_kpi_paging_flags(flags);
                // VA hinting NYI on ARMv8, always passing 0
                err = invoke_mapping_modify_flags(page->mapping, off, pages, pmap_flags, 0);
                printf("invoke_frame_modify_flags returned error: %s (%"PRIuERRV")\n",
                        err_getstring(err), err);
                return err;
            } else {
                // overlaps some region border
                return LIB_ERR_PMAP_EXISTING_MAPPING;
            }
        }
    }
    return SYS_ERR_OK;
}

/**
 * \brief Modify page mapping
 *
 * \param pmap     The pmap object
 * \param vaddr    The virtual address to unmap
 * \param flags    New flags for the mapping
 * \param retsize  If non-NULL, filled in with the actual size modified
 */
static errval_t
modify_flags(struct pmap     *pmap,
             genvaddr_t       vaddr,
             size_t           size,
             vregion_flags_t  flags,
             size_t          *retsize)
{
    errval_t err, ret = SYS_ERR_OK;
    struct pmap_aarch64 *pmap_aarch64 = (struct pmap_aarch64*)pmap;
    size = ROUND_UP(size, BASE_PAGE_SIZE);
    size_t pte_count = size / BASE_PAGE_SIZE;
    genvaddr_t vend = vaddr + size;

    if (VMSAv8_64_L012_BASE(vaddr) == VMSAv8_64_L012_BASE(vend - 1)) {
        // fast path
        err = do_single_modify_flags(pmap_aarch64, vaddr, pte_count, false);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_PMAP_UNMAP);
        }
    } else { // slow path
        // unmap first leaf
        uint32_t c = VMSAv8_64_PTABLE_NUM_ENTRIES - VMSAv8_64_L3_BASE(vaddr);
        err = do_single_modify_flags(pmap_aarch64, vaddr, c, false);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_PMAP_UNMAP);
        }

        // unmap full leaves
        vaddr += c * BASE_PAGE_SIZE;
        while (VMSAv8_64_L012_BASE(vaddr) < VMSAv8_64_L012_BASE(vend)) {
            c = VMSAv8_64_PTABLE_NUM_ENTRIES;
            err = do_single_modify_flags(pmap_aarch64, vaddr, c, true);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_PMAP_UNMAP);
            }
            vaddr += c * BASE_PAGE_SIZE;
        }

        // unmap remaining part
        c = VMSAv8_64_L3_BASE(vend) - VMSAv8_64_L3_BASE(vaddr);
        if (c) {
            err = do_single_modify_flags(pmap_aarch64, vaddr, c, true);
            if (err_is_fail(err)) {
                return err_push(err, LIB_ERR_PMAP_UNMAP);
            }
        }
    }

    if (retsize) {
        *retsize = size;
    }

    return ret;
}

/**
 * \brief Query existing page mapping
 *
 * \param pmap     The pmap object
 * \param vaddr    The virtual address to query
 * \param retvaddr Returns the base virtual address of the mapping
 * \param retsize  Returns the actual size of the mapping
 * \param retcap   Returns the cap mapped at this address
 * \param retoffset Returns the offset within the cap that is mapped
 * \param retflags Returns the flags for this mapping
 *
 * All of the ret parameters are optional.
 */
static errval_t lookup(struct pmap *pmap, genvaddr_t vaddr,
                       struct pmap_mapping_info *info)
{
    USER_PANIC("NYI");
    return 0;
}


static errval_t
serialise(struct pmap *pmap, void *buf, size_t buflen)
{
    // Unimplemented: ignored
    return SYS_ERR_OK;
}

static errval_t
deserialise(struct pmap *pmap, void *buf, size_t buflen)
{
    // Unimplemented: we start with an empty pmap, and avoid the bottom of the A/S
    return SYS_ERR_OK;
}

static struct pmap_funcs pmap_funcs = {
    .determine_addr = determine_addr,
    .map = map,
    .unmap = unmap,
    .modify_flags = modify_flags,
    .lookup = lookup,
    .serialise = serialise,
    .deserialise = deserialise,
};

/**
 * \brief Initialize the pmap object
 */
errval_t
pmap_init(struct pmap   *pmap,
          struct vspace *vspace,
          struct capref  vnode,
          struct slot_allocator *opt_slot_alloc)
{
    struct pmap_aarch64* pmap_aarch64 = (struct pmap_aarch64*)pmap;

    /* Generic portion */
    pmap->f = pmap_funcs;
    pmap->vspace = vspace;

    if (opt_slot_alloc != NULL) {
        pmap->slot_alloc = opt_slot_alloc;
    } else { /* use default allocator for this dispatcher */
        pmap->slot_alloc = get_default_slot_allocator();
    }

    pmap_vnode_mgmt_init(pmap);
    pmap->m->refill_slabs = refill_vnode_slabs;
#if defined(PMAP_ARRAY)
    pmap->m->refill_ptslab = refill_pt_slabs;
#endif

    pmap_aarch64->root.is_vnode         = true;
    pmap_aarch64->root.u.vnode.cap      = vnode;
    pmap_aarch64->root.u.vnode.invokable = vnode;

    if (get_croot_addr(vnode) != CPTR_ROOTCN) {
        errval_t err = slot_alloc(&pmap_aarch64->root.u.vnode.invokable);
        assert(err_is_ok(err));
        err = cap_copy(pmap_aarch64->root.u.vnode.invokable, vnode);
        assert(err_is_ok(err));
    }
    assert(!capref_is_null(pmap_aarch64->root.u.vnode.cap));
    assert(!capref_is_null(pmap_aarch64->root.u.vnode.invokable));
    pmap_vnode_init(pmap, &pmap_aarch64->root);

    /*
     * Initialize root vnode mapping cnode
     */
    if (pmap == get_current_pmap()) {
        /*
         * for now, for our own pmap, we use the left over slot allocator cnode to
         * provide the mapping cnode for the first half of the root page table as
         * we cannot allocate CNodes before establishing a connection to the
         * memory server!
         */
        pmap_aarch64->root.u.vnode.mcn[0].cnode = cnode_root;
        pmap_aarch64->root.u.vnode.mcn[0].slot = ROOTCN_SLOT_ROOT_MAPPING;
        pmap_aarch64->root.u.vnode.mcnode[0].croot = CPTR_ROOTCN;
        pmap_aarch64->root.u.vnode.mcnode[0].cnode = ROOTCN_SLOT_ADDR(ROOTCN_SLOT_ROOT_MAPPING);
        pmap_aarch64->root.u.vnode.mcnode[0].level = CNODE_TYPE_OTHER;
    } else {
        errval_t err;
        err = cnode_create_l2(&pmap_aarch64->root.u.vnode.mcn[0], &pmap_aarch64->root.u.vnode.mcnode[0]);
        if (err_is_fail(err)) {
            return err_push(err, LIB_ERR_PMAP_ALLOC_CNODE);
        }
    }

    // choose a minimum mappable VA for most domains; enough to catch NULL
    // pointer derefs with suitably large offsets
    pmap_aarch64->min_mappable_va = 64 * 1024;

    // maximum mappable VA is derived from X86_64_MEMORY_OFFSET in kernel
    pmap_aarch64->max_mappable_va = (genvaddr_t)0xffffff8000000000;

    return SYS_ERR_OK;
}

errval_t pmap_current_init(bool init_domain)
{
    struct pmap_aarch64 *pmap_aarch64 = (struct pmap_aarch64*)get_current_pmap();

    // To reserve a block of virtual address space,
    // a vregion representing the address space is required.
    // We construct a superficial one here and add it to the vregion list.
    struct vregion *vregion = &pmap_aarch64->vregion;
    assert((void*)vregion > (void*)pmap_aarch64);
    assert((void*)vregion < (void*)(pmap_aarch64 + 1));
    vregion->vspace = NULL;
    vregion->memobj = NULL;
    vregion->base   = VSPACE_BEGIN;
    vregion->offset = 0;
    vregion->size   = META_DATA_RESERVED_SPACE;
    vregion->flags  = 0;
    vregion->next = NULL;

    struct vspace *vspace = pmap_aarch64->p.vspace;
    assert(!vspace->head);
    vspace->head = vregion;

    pmap_aarch64->vregion_offset = pmap_aarch64->vregion.base;

    return SYS_ERR_OK;
}
