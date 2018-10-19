/**
 * \file
 * \brief Pmap definition common for the AARCH64 archs
 */

/*
 * Copyright (c) 2015, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstr. 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef TARGET_AARCH64_BARRELFISH_PMAP_H
#define TARGET_AARCH64_BARRELFISH_PMAP_H

#include <barrelfish/pmap.h>
#include <barrelfish_kpi/paging_arch.h>

/*
 * On x86_64 we define MCN_COUNT with PTABLE_SIZE which is the number of
 * entries in a page table. However on ARMv8 PTABLE_SIZE is the size of a page
 * table in bytes rather than entries, so we're using the additional define
 * VMSAv8_64_PTABLE_NUM_ENTRIES here.
 *
 * Note: this would also need to be handled carefully if we wanted to unify
 * some of the mostly identical parts of the pmap implementations.
 *
 * -SG, 2018-10-19.
 */
#define MCN_COUNT DIVIDE_ROUND_UP(VMSAv8_64_PTABLE_NUM_ENTRIES, L2_CNODE_SLOTS)

/// Node in the meta-data, corresponds to an actual VNode object
struct vnode {
    uint16_t      entry;       ///< Page table entry of this VNode
    bool          is_vnode;    ///< Is this a page table or a page mapping
    struct vnode  *next;       ///< Next entry in list of siblings
    struct capref mapping;     ///< the mapping for this vnode
    union {
        struct {
            struct capref cap;         ///< Capability of this VNode
            struct capref invokable;    ///< Copy of VNode cap that is invokable
            struct capref mcn[MCN_COUNT]; ///< CNodes to store mappings (caprefs)
            struct cnoderef mcnode[MCN_COUNT]; ///< CNodeRefs of mapping cnodes
            struct vnode  *children;   ///< Children of this VNode
        } vnode; // for non-leaf node
        struct {
            struct capref cap;         ///< Capability of this VNode
            genvaddr_t    offset;      ///< Offset within mapped frame cap
            vregion_flags_t flags;     ///< Flags for mapping
            size_t        pte_count;   ///< number of mapped PTEs in this mapping
        } frame; // for leaf node (maps page(s))
    } u;
};

#define INIT_SLAB_BUFFER_BYTES SLAB_STATIC_SIZE(32, sizeof(struct vnode))
struct pmap_aarch64 {
    struct pmap p;
    struct vregion vregion;     ///< Vregion used to reserve virtual address for metadata
    genvaddr_t vregion_offset;  ///< Offset into amount of reserved virtual address used
    struct vnode root;          ///< Root of the vnode tree
    errval_t (*refill_slabs)(struct pmap_aarch64 *); ///< Function to refill slabs
    struct slab_allocator slab;     ///< Slab allocator for the vnode lists
    genvaddr_t min_mappable_va; ///< Minimum mappable virtual address
    genvaddr_t max_mappable_va; ///< Maximum mappable virtual address
    uint8_t slab_buffer[INIT_SLAB_BUFFER_BYTES];   ///< Initial buffer to back the allocator
};

#endif // TARGET_AARCH64_BARRELFISH_PMAP_H
