/**
 * \file
 * \brief Morecore implementation for malloc
 */

/*
 * Copyright (c) 2007, 2008, 2009, 2010, 2011, ETH Zurich.
 * Copyright (c) 2014, HP Labs.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <barrelfish/barrelfish.h>
#include <barrelfish/core_state.h>
#include <barrelfish/morecore.h>
#include <stdio.h>
#include <stdint.h>

/// Amount of virtual space for malloc depending on 32/64 bits architectures
#if (UINTPTR_MAX == UINT64_MAX)
#       define HEAP_REGION (128UL * 1024UL * 1024 * 1024) /* 128GB */
#else
#       define HEAP_REGION (512UL * 1024 * 1024) /* 512MB */
#endif

typedef void *(*morecore_alloc_func_t)(size_t bytes, size_t *retbytes);
extern morecore_alloc_func_t sys_morecore_alloc;

typedef void (*morecore_free_func_t)(void *base, size_t bytes);
extern morecore_free_func_t sys_morecore_free;

/**
 * \brief Allocate some memory for malloc to use
 *
 * This function will keep trying with smaller and smaller frames till
 * it finds a set of frames that satisfy the requirement. retbytes can
 * be smaller than bytes if we were able to allocate a smaller memory
 * region than requested for.
 */
static void *morecore_alloc(size_t bytes, size_t *retbytes)
{
    errval_t err;
    struct morecore_state *state = get_morecore_state();

    struct ram_alloc_state *ram_alloc_state = get_ram_alloc_state();
    if(ram_alloc_state->ram_alloc_func != ram_alloc_fixed) {
        if (bytes < LARGE_PAGE_SIZE) {
            bytes = LARGE_PAGE_SIZE;
        }

        bytes = ROUND_UP(bytes, LARGE_PAGE_SIZE);
    }

    void *buf = NULL;
    size_t mapped = 0;
    size_t step = bytes;
    while (mapped < bytes) {
        void *mid_buf = NULL;
        err = vspace_mmu_aware_map(&state->mmu_state, step, &mid_buf, &step);
        if (err_is_ok(err)) {
            if (buf == NULL) {
                buf = mid_buf;
            }
            mapped += step;
        } else {
            /*
              vspace_mmu_aware_map failed probably because we asked
              for a very large frame, will try asking for smaller one.
             */
            if (err_no(err) == LIB_ERR_FRAME_CREATE_MS_CONSTRAINTS) {
                if (step < BASE_PAGE_SIZE) {
                    // Return whatever we have allocated until now
                    break;
                }
                step /= 2;
                continue;
            } else {
                debug_err(__FILE__, __func__, __LINE__, err,
                          "vspace_mmu_aware_map fail");
                return NULL;
            }
        }
    }

    *retbytes = mapped;
    return buf;
}

static void morecore_free(void *base, size_t bytes)
{
    struct morecore_state *state = get_morecore_state();
    errval_t err = vspace_mmu_aware_unmap(&state->mmu_state,
                                          (lvaddr_t)base, bytes);
    if(err_is_fail(err)) {
        USER_PANIC_ERR(err, "vspace_mmu_aware_unmap");
    }
}

Header *get_malloc_freep(void);
Header *get_malloc_freep(void)
{
    return get_morecore_state()->header_freep;
}

errval_t morecore_init(size_t pagesize)
{
    errval_t err;
    struct morecore_state *state = get_morecore_state();

    thread_mutex_init(&state->mutex);

    // setup flags that match the pagesize
    vregion_flags_t morecore_flags = VREGION_FLAGS_READ_WRITE;
#if __x86_64__
    morecore_flags |= (pagesize == HUGE_PAGE_SIZE ? VREGION_FLAGS_HUGE : 0);
#endif
    morecore_flags |= (pagesize == LARGE_PAGE_SIZE ? VREGION_FLAGS_LARGE : 0);

    // Always align heap to 4 gigabyte boundary
    const size_t heap_alignment = 4UL * 1024 * 1024 * 1024;
    err = vspace_mmu_aware_init_aligned(&state->mmu_state, NULL, HEAP_REGION,
                                        heap_alignment, morecore_flags);
    if (err_is_fail(err)) {
        return err_push(err, LIB_ERR_VSPACE_MMU_AWARE_INIT);
    }

    /* overwrite alignment field in vspace_mmu_aware state */
    state->mmu_state.alignment = heap_alignment;

    sys_morecore_alloc = morecore_alloc;
    sys_morecore_free = morecore_free;

    return SYS_ERR_OK;
}

errval_t morecore_reinit(void)
{
    errval_t err;
    struct morecore_state *state = get_morecore_state();
    if ((vregion_get_flags(&state->mmu_state.vregion)
            & (VREGION_FLAGS_HUGE|VREGION_FLAGS_LARGE)) == 0)
    {
        // No need to do anything if the heap is using base pages anyway
        return SYS_ERR_OK;
    }

    if ((vregion_get_flags(&state->mmu_state.vregion) &
         (VREGION_FLAGS_LARGE|VREGION_FLAGS_HUGE)) == 0)
    {
        return SYS_ERR_OK;
    }

    size_t mapoffset = state->mmu_state.mapoffset;
    size_t remapsize = ROUND_UP(mapoffset, state->mmu_state.alignment);
    if (remapsize <= mapoffset) {
        // don't need to do anything if we only recreate the exact same
        // mapping
        // XXX: do we need/want to recreate existing mappings with a larger
        // page size here? If so, what is the implication on early boot
        // domains that don't have access to mem_serv? -SG, 2015-04-30.
        return SYS_ERR_OK;
    }
    struct capref frame;
    size_t retsize;
    err = frame_alloc(&frame, remapsize, &retsize);
    if (err_is_fail(err)) {
        return err;
    }
    return vspace_mmu_aware_reset(&state->mmu_state, frame, remapsize);
}
