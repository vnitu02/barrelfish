/**
 * \file
 * \brief Serial port driver.
 */

/*
 * Copyright (c) 2007, 2008, 2010, 2011, 2012, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, CAB F.78, Universitaetstrasse 6, CH-8092 Zurich,
 * Attn: Systems Group.
 */

#include <barrelfish/barrelfish.h>
#include <int_route/int_route_client.h>
#include <pci/pci.h>
#include "serial.h"
#include "serial_debug.h"
#include "pci/pci.h"
#include <dev/lpuart_dev.h>
#include <driverkit/driverkit.h>

#define DEFAULT_PORTBASE            0x3f8   //< COM1 port
#define DEFAULT_IRQ                 4       //< COM1 IRQ

struct serial_lpuart {
    struct serial_common m;
    struct lpuart_t uart;

};
__attribute__((__unused__))
// CALL THIS FUNCTION IN INIT
static void serial_poll(struct serial_lpuart *spc)
{   while(lpuart_stat_rdrf_rdf(&spc->uart)){
        char c = lpuart_data_buf_rdf(&spc->uart);
        debug_printf("read %c from uart\n", c);
        serial_input(&spc->m, &c, 1);
     }
     
}
// __attribute__((__unused__))
// static void serial_interrupt(void *m)
// {
//     // Figure out which interrupt happened
//     // -> we have data to read -> call serial_poll
//     // 
// }

static void hw_init(struct serial_lpuart *spc)
{   // Disable transceiver
    lpuart_ctrl_t ctrl = lpuart_ctrl_rawrd(&spc->uart);
    ctrl = lpuart_ctrl_te_insert(ctrl, 0);
    ctrl = lpuart_ctrl_re_insert(ctrl, 0);
    lpuart_ctrl_rawwr(&spc->uart, ctrl);
    // Set baudrate
    // baudrate = clock rate / (over sampling rate * SBR)
    // TODO: Currently we assume UART clock is set to 8MHz
    lpuart_baud_t baud = lpuart_baud_default;
    baud = lpuart_baud_osr_insert(baud, lpuart_ratio5);
    // OSR of 5 needs bothedge set
    baud = lpuart_baud_bothedge_insert(baud, 1);
    baud = lpuart_baud_sbr_insert(baud, 139);
    lpuart_baud_rawwr(&spc->uart, baud);
    //enable FIFOs
    lpuart_fifo_t fcr = lpuart_fifo_default;
    fcr = lpuart_fifo_txfe_insert(fcr, 1);
    fcr = lpuart_fifo_rxfe_insert(fcr, 1);
    
    // Transmit FIFO/Buffer depth is 256 datawords
    fcr = lpuart_fifo_txfifosize_insert(fcr, 0x3);
    lpuart_fifo_wr(&spc->uart, fcr);
    fcr = lpuart_fifo_rxfifosize_insert(fcr, 0x3);
    lpuart_fifo_wr(&spc->uart, fcr);
   // lpuart_fifo_t lcr = lpuart_fifo_default;
    // lcr = pc16550d_lcr_wls_insert(lcr, pc16550d_bits8); // 8 data bits
    // lcr = pc16550d_lcr_stb_insert(lcr, 1); // 1 stop bit
    // lcr = pc16550d_lcr_pen_insert(lcr, 0); // no parity
    // lpuart_fifo_wr(&spc->uart, lcr);

    // Enable transceiver
    ctrl = lpuart_ctrl_default;
    ctrl = lpuart_ctrl_te_insert(ctrl, 1);
    ctrl = lpuart_ctrl_re_insert(ctrl, 1);
    lpuart_ctrl_rawwr(&spc->uart, ctrl);

    // ENABLE INTERRUPTS
    /*
    //transmit interrupt enable
     lpuart_ctrl_tie_wrf(&spc->uart,1);
    //Transmission Complete Interrupt Enable
    lpuart_ctrl_tcie_wrf(&spc->uart,1);
    //Receiver Interrupt Enable
    lpuart_ctrl_rie_wrf(&spc->uart,1);
    //Overrun Interrupt Enable
     lpuart_ctrl_orie_wrf(&spc->uart,1);
     //noise error interrupt enable
     lpuart_ctrl_neie_wrf(&spc->uart,1);
     //framing error interrupt enable
     lpuart_ctrl_feie_wrf(&spc->uart,1);
     //Parity Error Interrupt Enable
     lpuart_ctrl_peie_wrf(&spc->uart,1);
      */
}

__attribute__((__unused__))
static void serial_putc(struct serial_lpuart *spc,char c)
{
    // // Wait until FIFO can hold more characters
    // while(!pc16550d_lsr_thre_rdf(&spc->uart));
    // // Write character
    // pc16550d_thr_wr(&spc->uart, c);
   
    lpuart_t *u = &spc->uart;
    assert(u->base != 0);

    while(lpuart_stat_tdre_rdf(u) == 0);
    lpuart_data_buf_wrf(u, c);
   

}

static void serial_write(void * m, const char *c, size_t len)
{
    struct serial_lpuart * spc = m;
    for (int i = 0; i < len; i++) {
        serial_putc(spc, c[i]);
    }
}

// #define MAX_NUM_UARTS 1
// lpaddr_t platform_uart_base[MAX_NUM_UARTS] =
// {
//         0x5A090000
// };

// errval_t serial_early_init(unsigned n)
// {unsigned serial_num_physical_ports = 1;
//     assert(!paging_mmu_enabled());
//     assert(n < serial_num_physical_ports);

//     lpuart_initialize(&uarts[n],
//         (mackerel_addr_t)local_phys_to_mem(platform_uart_base[n]));

//     // Make sure that the UART is enabled and transmitting - not all platforms
//     // do this for us.serial_putc
//    hw_init(&uarts[n]);serial_putc

//     return SYS_ERR_OK;serial_putc
// }

/**
 * \brief Configure the serial interface, from a caller that knows
 * that this i__attribute__((__unused__))s a bunch of PL011s, and furthermore where they are in
 * the physical address space.
 */
// errval_t serial_early_init_mmu_enabled(unsigned n)
// {
//     assert(paging_mmu_enabled());serial_putc
//     assert(n < serial_num_physicaserial_putcorts);

//     lpuart_initialize(&uarts[n],
//         (mackerel_addr_t)local_phys_to_mem(platform_uart_base[n]));

//     // Make sure that the UART is enabled and transmitting - not all platforms
//     // do this for us.
//     lpuart_hw_init(&uarts[n]);

//     return SYS_ERR_OK;
// }

static errval_t
serial_lpuart_init(struct serial_lpuart *spc, struct capref irq_src)
{   debug_printf("Hello world from serial lpuart driver\n");
    
    //errval_t err;

    // if(capref_is_null(irq_src))
    //     USER_PANIC("serial_kernel requires an irq cap");

    spc->m.output = serial_write;
    spc->m.output_arg = spc;

    //Register interrupt handler
    //err = int_route_client_route_and_connect(irq_src, 0,
    //        get_default_waitset(), serial_interrupt, spc);
    //if (err_is_fail(err)) {
    //    USER_PANIC_ERR(err, "interrupt setup failed.");
    // }

    hw_init(spc);

    // offer service now we're up
    start_service(&spc->m);
    return SYS_ERR_OK;
}
// static errval_t serial_lpuart_init(unsigned port, lvaddr_t base, bool hwinit)
// {   errval_t err;
//     assert(paging_mmu_enabled());
//     assert(port < serial_num_physical_ports);

//     lpuart_t *u = &uarts[port];

//     // [Re]initialize the Mackerel state for the UART
//     lpuart_initialize(u, (mackerel_addr_t) base);

//     if (hwinit) {
//         lpuart_hw_init(u);
//     }
//     return SYS_ERR_OK;
// }



static errval_t
init(struct bfdriver_instance* bfi, uint64_t flags, iref_t *dev)
{
    errval_t err;
    struct serial_lpuart *spc = malloc(sizeof(struct serial_lpuart));
    init_serial_common(&spc->m);
    //copy
    lvaddr_t vbase;
    struct capref devframe_cap = {
        .slot = DRIVERKIT_ARGCN_SLOT_BAR0,
        .cnode = bfi->argcn
    };
    err = map_device_cap(devframe_cap, &vbase);
    debug_printf("vbase = %p\n", vbase);

    lpuart_initialize (&spc->uart,(mackerel_addr_t) vbase);
   //paste

    /*  struct capref irq_src;
    irq_src.cnode = bfi->argcn;
    irq_src.slot = PCIARG_SLOT_INT; */
    err = serial_lpuart_init(spc, NULL_CAP);
    assert(err_is_ok(err));
    bfi->dstate = spc;

    SERIAL_DEBUG("lpuart Serial driver initialized.\n");
    debug_printf("reading from uart\n");
    while(1)
    {
    serial_poll(spc);
    }
    return SYS_ERR_OK;
}

static errval_t attach(struct bfdriver_instance* bfi) {
    return SYS_ERR_OK;
}

static errval_t detach(struct bfdriver_instance* bfi) {
    return SYS_ERR_OK;
}

static errval_t set_sleep_level(struct bfdriver_instance* bfi, uint32_t level) {
    return SYS_ERR_OK;
}

static errval_t destroy(struct bfdriver_instance* bfi) {
    struct lpuart_t * spc = bfi->dstate;
    free(spc);
    bfi->dstate = NULL;
    // XXX: Tear-down the service
    bfi->device = 0x0;
    return SYS_ERR_OK;
}

static errval_t get_ep(struct bfdriver_instance* bfi, bool lmp, struct capref* ret_cap)
{   
    USER_PANIC("NIY \n");
    return SYS_ERR_OK;
}

DEFINE_MODULE(serial_lpuart, init, attach, detach, set_sleep_level, destroy, get_ep);
