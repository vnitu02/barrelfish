/**
 * \file
 * \brief Pmap definition common for the x86 archs
 */

/*
 * Copyright (c) 2009, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Haldeneggsteig 4, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef TARGET_X86_BARRELFISH_PMAP_H
#define TARGET_X86_BARRELFISH_PMAP_H

#include <barrelfish/pmap.h>
#include <barrelfish_kpi/capabilities.h>
#include <barrelfish_kpi/paging_arch.h> // for PTABLE_ENTRIES
#include <barrelfish/pmap_ds.h>

#define MCN_COUNT DIVIDE_ROUND_UP(PTABLE_ENTRIES, L2_CNODE_SLOTS)

/// Node in the meta-data, corresponds to an actual VNode object
struct vnode { // NB: misnomer :)
    uint16_t      entry;       ///< Page table entry of this VNode
    bool          is_vnode;    ///< Is this a vnode, or a (leaf) page mapping
    bool          is_cloned;   ///< For copy-on-write: indicate whether we
                               //   still have to clone this vnode
    bool          is_pinned;   ///< is this a pinned vnode (do not reclaim automatically)
    enum objtype  type;        ///< Type of cap in the vnode
    struct vnode  *next;       ///< Next entry in list of siblings
    struct capref mapping;     ///< mapping cap associated with this node (stored in parent's mapping cnode)
    union {
        struct {
            lvaddr_t base;             ///< Virtual address start of page (upper level bits)
            struct capref cap;         ///< VNode cap
            struct capref invokable;    ///< Copy of VNode cap that is invokable
            struct capref mcn[MCN_COUNT]; ///< CNodes to store mappings (caprefs)
            struct cnoderef mcnode[MCN_COUNT]; ///< CNodeRefs of mapping cnodes
            pmap_ds_child_t *children; ///< Children of this VNode
            lvaddr_t virt_base;        ///< vaddr of mapped RO page table in user-space
            struct capref page_table_frame;
        } vnode; // for non-leaf node (maps another vnode)
        struct {
            struct capref cap;         ///< Frame cap
            genvaddr_t    offset;      ///< Offset within mapped frame cap
            vregion_flags_t flags;     ///< Flags for mapping
            size_t        pte_count;   ///< number of mapped PTEs in this mapping
            lvaddr_t vaddr;            ///< The virtual address this frame has
            uint16_t cloned_count;     ///< counter for #times a page of this range was cloned
        } frame; // for leaf node (maps an actual page)
    } u;
};

struct pmap_x86 {
    struct pmap p;
    struct vnode root;          ///< Root of the vnode tree
    genvaddr_t min_mappable_va; ///< Minimum mappable virtual address
    genvaddr_t max_mappable_va; ///< Maximum mappable virtual address
    size_t used_cap_slots;      ///< Current count of capability slots allocated by pmap code
};

#endif // TARGET_X86_BARRELFISH_PMAP_H
