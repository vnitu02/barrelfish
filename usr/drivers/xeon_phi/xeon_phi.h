/*
 * Copyright (c) 2014 ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef XEON_PHI_H_
#define XEON_PHI_H_

#include <xeon_phi/xeon_phi.h>

#include "debug.h"

/*
 * Common setting values
 */

/// the name of the Xeon Phi bootloader image
#define XEON_PHI_BOOTLOADER "weever"

/// the name of the Xeon Phi multiboot image containint the modules
#define XEON_PHI_MULTIBOOT "xeon_phi_multiboot"

/// if we use MSIX interrupts or legacy interrupts
#define XEON_PHI_MSIX_ENABLED 1

/// the number of MSIX interrupts we use
#define XEON_PHI_MSIX_NUM     1

#define XEON_PHI_APERTURE_INIT_SIZE (1024*1024*1024)

/*
 * This defines are used to reference the MMIO registers on the host side.
 *
 * The Mackerel Specifications use the SBOX or DBOX baseaddress as their
 * register base. however the SBOX or DBOX have a certain offset into the
 * MMIO range.
 */
#define XEON_PHI_HOST_DBOX_OFFSET      0x00000000
#define XEON_PHI_HOST_SBOX_OFFSET      0x00010000

#define XEON_PHI_MMIO_TO_SBOX(phi) \
    ((mackerel_addr_t)((lvaddr_t)(phi->mmio.vbase)+XEON_PHI_HOST_SBOX_OFFSET))
#define XEON_PHI_MMIO_TO_DBOX(phi) \
    ((mackerel_addr_t)((lvaddr_t)(phi->mmio.vbase)+XEON_PHI_HOST_DBOX_OFFSET))

/*
 * --------------------------------------------------------------------------
 * Xeon Phi Management structure
 */

/// represents the state of the Xeon Phi
typedef enum xeon_phi_state
{
    XEON_PHI_STATE_NULL,    ///< The card has not yet been initialized
    XEON_PHI_STATE_PCI_OK,  ///< The card has been registered with PCI
    XEON_PHI_STATE_RESET,   ///< The card has been reset
    XEON_PHI_STATE_READY,   ///< The card is ready to load the os
    XEON_PHI_STATE_BOOTING,  ///< The card is in the booting state
    XEON_PHI_STATE_ONLINE   ///< the card has booted and is online
} xeon_phi_state_t;

typedef enum xnode_state {
    XNODE_STATE_NONE,
    XNODE_STATE_REGISTERING,
    XNODE_STATE_READY,
    XNODE_STATE_FAILURE
} xnode_state_t;

/// represents the memory ranges occupied by the Xeon Phi card
struct mbar
{
    lvaddr_t vbase;     ///< virtual address of the mbar if mapped
    lpaddr_t pbase;     ///< physical address of the mbar
    size_t length;      ///< length of the mapped area
    struct capref cap;  ///< capability of the mbar
    uint8_t bits;       ///< size of the capability in bits
};

struct xnode
{
    struct xeon_phi_binding *binding;
    iref_t                   iref;
    xnode_state_t state;
    uint8_t         id;
    struct xeon_phi *local;
};

struct xeon_phi
{
    xeon_phi_state_t state;
    struct mbar mmio;       ///< pointer to the MMIO address range
    struct mbar apt;        ///< pointer to the aperture address range

    lvaddr_t os_offset;     ///< offset of the OS image into the aperture
    uint32_t os_size;       ///< the size of the OS image
    char *cmdline;          ///< pointer to the bootloader cmdline
    uint32_t cmdlen;     ///< the length of the cmd line

    uint8_t id;             ///< card id for identifying the card
    iref_t iref;
    uint32_t apicid;        ///< APIC id used for sending the boot interrupt

    uint8_t      connected;
    struct xnode topology[XEON_PHI_NUM_MAX];

    uint8_t is_client;

    struct smpt_info *smpt;  ///< pointer to the SMPT information struct
    struct irq_info *irq;  ///< pointer to the IRQ information struct
    struct dma_info *dma;  ///< pointer to the DMA information struct
    struct msg_info *msg;
};

/**
 * \brief starts the serial receive thread
 *
 * \param phi   pointer to the card information
 */
errval_t xeon_phi_serial_start_recv_thread(struct xeon_phi *phi);

/**
 * \brief initializes the serial receiver
 *
 * \param phi   pointer to the card information
 */
errval_t xeon_phi_serial_init(struct xeon_phi *phi);

/**
 * \brief checks if there is a message waiting on the serial and reads
 *        it into the buffer.
 *
 * \return 0: There was no message
 *         1: There was a message waiting and it porocessed.
 */
uint32_t xeon_phi_serial_handle_recv(void);

/**
 * \brief boots the card with the given loader and multiboot image
 *
 * \param phi           pointer to the card information
 * \param xloader_img   pointer to the card bootloader image
 * \param multiboot_img pointer to the card multiboot image
 */
errval_t xeon_phi_boot(struct xeon_phi *phi,
                       char *xloader_img,
                       char *multiboot_img);

/**
 * \brief performs a soft reset of the card
 *
 * \param phi   pointer to the card information
 */
errval_t xeon_phi_reset(struct xeon_phi *phi);

/**
 * \brief initializes the coprocessor card
 *
 * \param phi pointer to the information structure
 */
errval_t xeon_phi_init(struct xeon_phi *phi, uint32_t bus, uint32_t dev, uint32_t fun);

/**
 * \brief Bootstraps the host driver to get the multiboot images of the
 *        xeon phi loader and the xeon phi multiboot image
 */
errval_t host_bootstrap(void);


/**
 * \brief maps the aperture memory range of the Xeon Phi into the drivers
 *        vspace to be able to load the coprocessor OS onto the card
 *
 * \param phi   pointer to the Xeon Phi structure holding aperture information
 * \param range how much bytes to map
 *
 * \returns SYS_ERR_OK on success
 */
errval_t xeon_phi_map_aperture(struct xeon_phi *phi,
                               size_t range);

/**
 * \brief unmaps the previously mapped aperture range when the programming
 *        completes.
 *
 * \param phi pointer to the Xeon Phi structure holiding mapping information
 *
 * \return SYS_ERR_OK on success
 */
errval_t xeon_phi_unmap_aperture(struct xeon_phi *phi);


#endif /* XEON_PHI_H_ */
