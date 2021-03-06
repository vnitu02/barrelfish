/*
 *Copyright (c) 2007-2011, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#ifndef SFN5122F_CHANNEL_H_
#define SFN5122F_CHANNEL_H_

#include <string.h>
#include <stdlib.h>

#include <net_interfaces/net_interfaces.h>
#include <devif/queue_interface.h>
#include <dev/sfn5122f_q_dev.h>
#include <dev/sfn5122f_dev.h>
#include "helper.h"

//#define BENCH_QUEUE 1

#ifdef BENCH_QUEUE
#define BENCH_SIZE 100000
#include <bench/bench.h>
#endif

#define MTU_MAX 2048

struct sfn5122f_devif_binding;
struct sfn5122f_devif_rpc_client;
struct sfn5122f_queue;
struct peridoc_event;

struct sfn5122f_queue_ops {
    errval_t (*update_txtail)(struct sfn5122f_queue*, size_t);
    errval_t (*update_rxtail)(struct sfn5122f_queue*, size_t);
};

struct region_entry {
    uint32_t rid;
    lpaddr_t phys;
    size_t size;
    uint64_t buftbl_idx;
    struct capref cap;
    struct region_entry* next;
};

struct sfn5122f_queue {
    struct devq q;


#ifdef BENCH_QUEUE
    struct bench_ctl en_tx;
    struct bench_ctl en_rx;
    struct bench_ctl deq_rx;
    struct bench_ctl deq_tx;
#endif

    union {
        sfn5122f_q_tx_user_desc_array_t* user;
        sfn5122f_q_tx_ker_desc_array_t* ker;
    } tx_ring;
    struct devq_buf*                tx_bufs;
    uint16_t                        tx_head;
    uint16_t                        tx_tail;
    size_t                          tx_size;

    union {
        sfn5122f_q_rx_user_desc_array_t* user;
        sfn5122f_q_rx_ker_desc_array_t* ker;
    } rx_ring;
    struct devq_buf*                rx_bufs;
    uint16_t                        rx_head;
    uint16_t                        rx_tail;
    uint16_t                        rx_size;
    uint8_t                         rx_batch_size;


    sfn5122f_q_event_entry_array_t* ev_ring;
    uint32_t                        ev_head;
    uint32_t                        ev_tail;
    size_t                          ev_size;

    struct sfn5122f_queue_ops       ops;
    void*                           opaque;
    bool                            userspace;

    struct capref filter_ep;
    // For batchin of TX events, maximum of 32
    // entries since there can be a maximum of 
    // TX_CACHE descriptors per event
    struct devq_buf bufs[32];
    uint8_t last_deq; // last deq from buffer
    uint8_t num_left;

    // state for devif interface
    struct sfn5122f_devif_binding* b;
    struct sfn5122f_devif_rpc_client* rpc;
    volatile bool bound;
    bool use_interrupts;

    // interrupts
    uint8_t core;
    uint8_t vector;

    // callback 
    sfn5122f_event_cb_t cb;

    // Direct interface fields
    uint16_t id;
    uint64_t mac;
    struct capref frame;
    sfn5122f_t *device;
    void* device_va;
    struct region_entry* regions;


};

typedef struct sfn5122f_queue sfn5122f_queue_t;

static inline sfn5122f_queue_t* sfn5122f_queue_init(void* tx, 
                                                    size_t tx_size,
                                                    void* rx, 
                                                    size_t rx_size, 
                                                    void* ev, 
                                                    size_t ev_size,
                                                    struct sfn5122f_queue_ops* ops, 
                                                    bool userspace)
{

    sfn5122f_queue_t* q = malloc(sizeof(*q));

    if (userspace) {
        q->tx_ring.user = tx;
    } else {
        q->tx_ring.ker = tx;
    }
    q->tx_bufs = malloc(sizeof(struct devq_buf) * tx_size);
    q->tx_head = 0;
    q->tx_tail = 0;
    q->tx_size = tx_size;

    if (userspace) {
        q->rx_ring.user = rx;
    } else {
        q->rx_ring.ker = rx;
    }
    q->rx_bufs = malloc(sizeof(struct devq_buf) * rx_size);
    q->rx_head = 0;
    q->rx_tail = 0;
    q->rx_batch_size = 0;
    q->rx_size = rx_size;
  
    q->ev_ring = ev;
    q->ev_head = 0;
    q->ev_tail = 0;
    q->ev_size = ev_size;
    q->userspace = userspace; 
    q->num_left = 0;
    q->last_deq = 0;

    q -> ops = *ops;

    // Initialize ring memory with 0xff
    if(!userspace){
       memset(tx, 0xff, tx_size * sfn5122f_q_tx_ker_desc_size);
       memset(rx, 0xff, rx_size * sfn5122f_q_rx_ker_desc_size);
    }else{
       memset(tx, 0xff, tx_size * sfn5122f_q_tx_user_desc_size);
       memset(rx, 0xff, rx_size * sfn5122f_q_rx_user_desc_size);
    }
    /* all 0 is potential valid event */
    memset(ev, 0xff, ev_size * sfn5122f_q_event_entry_size);

#ifdef BENCH_QUEUE
    q->en_tx.mode = BENCH_MODE_FIXEDRUNS;
    q->en_tx.result_dimensions = 1;
    q->en_tx.min_runs = BENCH_SIZE;
    q->en_tx.data = calloc(q->en_tx.min_runs * q->en_tx.result_dimensions,
                       sizeof(*q->en_tx.data));
    assert(q->en_tx.data != NULL);

    q->en_rx.mode = BENCH_MODE_FIXEDRUNS;
    q->en_rx.result_dimensions = 1;
    q->en_rx.min_runs = BENCH_SIZE;
    q->en_rx.data = calloc(q->en_rx.min_runs * q->en_rx.result_dimensions,
                       sizeof(*q->en_rx.data));
    assert(q->en_rx.data != NULL);

    q->deq_rx.mode = BENCH_MODE_FIXEDRUNS;
    q->deq_rx.result_dimensions = 1;
    q->deq_rx.min_runs = BENCH_SIZE;
    q->deq_rx.data = calloc(q->deq_rx.min_runs * q->deq_rx.result_dimensions,
                       sizeof(*q->deq_rx.data));
    assert(q->deq_rx.data != NULL);

    q->deq_tx.mode = BENCH_MODE_FIXEDRUNS;
    q->deq_tx.result_dimensions = 1;
    q->deq_tx.min_runs = BENCH_SIZE;
    q->deq_tx.data = calloc(q->deq_tx.min_runs * q->deq_tx.result_dimensions,
                       sizeof(*q->deq_tx.data));

    assert(q->deq_tx.data != NULL);
#endif

    return q;
}


static inline errval_t sfn5122f_queue_free(struct sfn5122f_queue* q)
{
    errval_t err;

    // only one cap that is mapped (TX)
    if (q->userspace) {
        err = vspace_unmap(q->tx_ring.user);  
    } else {
        err = vspace_unmap(q->tx_ring.ker);  
    }
    if (err_is_fail(err)) {
        return err;
    }   
    free(q->rx_bufs);
    free(q->tx_bufs);
    free(q);

    return SYS_ERR_OK;
}

static inline void sfn5122f_queue_bump_evhead(sfn5122f_queue_t* q)
{
     q->ev_head = (q->ev_head +1) & (q->ev_size - 1);
}

static inline void sfn5122f_queue_bump_rxhead(sfn5122f_queue_t* q)
{
    q->rx_head = (q->rx_head +1) & (q->rx_size - 1);
}

static inline void sfn5122f_queue_bump_rxtail(sfn5122f_queue_t* q)
{
    q->rx_tail = (q->rx_tail +1) & (q->rx_size - 1);
}

static inline void sfn5122f_queue_bump_txhead(sfn5122f_queue_t* q)
{
    q->tx_head = (q->tx_head +1) & (q->tx_size - 1);
}

static inline void sfn5122f_queue_bump_txtail(sfn5122f_queue_t* q)
{
    q->tx_tail = (q->tx_tail +1) & (q->tx_size - 1);
}

static inline void sfn5122f_queue_clear_ev(void *ev)
{
    STATIC_ASSERT_SIZEOF(sfn5122f_q_tx_ev_array_t, sizeof(uint64_t));
    STATIC_ASSERT_SIZEOF(sfn5122f_q_rx_ev_array_t, sizeof(uint64_t));
    //memset(ev, 0xff, sfn5122f_q_event_entry_size);

    *((uint64_t *)ev) = 0xffffffffffffffffUL;
}

static inline void sfn5122f_queue_clear_kdesc(void *kdesc)
{
    STATIC_ASSERT_SIZEOF(sfn5122f_q_tx_user_desc_size, sizeof(uint64_t));
    STATIC_ASSERT_SIZEOF(sfn5122f_q_tx_ker_desc_size, sizeof(uint64_t));
    *((uint64_t *)kdesc) = 0UL;
}

static inline uint8_t sfn5122f_get_event_code(sfn5122f_queue_t* queue)
{             
       sfn5122f_q_event_entry_t ev;
       ev = queue->ev_ring[queue->ev_head];
       return sfn5122f_q_event_entry_ev_code_extract(ev);
}


static inline errval_t sfn5122f_queue_update_txtail(sfn5122f_queue_t* q)
{
    return q->ops.update_txtail(q, q->tx_tail);
}


static inline errval_t sfn5122f_queue_update_rxtail(sfn5122f_queue_t* q)
{
    return q->ops.update_rxtail(q, q->rx_tail);
}


static inline errval_t sfn5122f_handle_drv_ev(sfn5122f_queue_t* q, uint16_t n)
{   
    size_t ev_head = q->ev_head;

    sfn5122f_q_event_entry_t code;
    code = q->ev_ring[ev_head]; 

    if (sfn5122f_q_driver_ev_driver_ev_subcode_extract(code) == 2) {
        printf("Event queue init done %d \n", n);
    }

    if (sfn5122f_q_driver_ev_driver_ev_subcode_extract(code) == 9) {
        printf("Packet neither TCP nor UPD %d \n", n);
    }
    
    if (sfn5122f_q_driver_ev_driver_ev_subcode_extract(code) == 14) {
        printf("RX error %d \n", n);
        return NIC_ERR_RX_PKT;
    }

    if (sfn5122f_q_driver_ev_driver_ev_subcode_extract(code) == 15) {
        printf("TX error %d \n", n);
        return NIC_ERR_TX_PKT;
    }

    sfn5122f_queue_clear_ev(code);
    return SYS_ERR_OK;

}



static inline errval_t sfn5122f_queue_handle_mcdi_event(sfn5122f_queue_t* q)
{
    // TODO handle different events    
    size_t ev_head = q->ev_head;
    sfn5122f_q_event_entry_t ev;
    uint64_t reg;
    ev = q->ev_ring[ev_head]; 
    reg = sfn5122f_q_event_entry_ev_data_extract(ev);

    sfn5122f_queue_clear_ev(ev);
    return SYS_ERR_OK;

}

/*    RX      */
static inline int sfn5122f_queue_add_user_rxbuf_devif(sfn5122f_queue_t* q, 
                                                      uint32_t buf_id,
                                                      uint16_t b_off,
                                                      regionid_t rid,
                                                      genoffset_t offset,
                                                      genoffset_t length,
                                                      genoffset_t valid_data,
                                                      genoffset_t valid_length,
                                                      uint64_t flags)
{
    struct devq_buf* buf;
    sfn5122f_q_rx_user_desc_t d;
    size_t tail = q->rx_tail;

    d = q->rx_ring.user[tail];
    buf = &q->rx_bufs[tail];

    buf->rid = rid;
    buf->offset = offset;
    buf->length = length;
    buf->valid_data = valid_data;
    buf->valid_length = valid_length;
    buf->flags = flags;
    sfn5122f_q_rx_user_desc_rx_user_buf_id_insert(d, buf_id);
    sfn5122f_q_rx_user_desc_rx_user_2byte_offset_insert(d, b_off >> 1);

    sfn5122f_queue_bump_rxtail(q);
    return 0;
}

static inline int sfn5122f_queue_add_rxbuf_devif(sfn5122f_queue_t* q, 
                                                 lpaddr_t addr,
                                                 regionid_t rid,
                                                 genoffset_t offset,
                                                 genoffset_t length,
                                                 genoffset_t valid_data,
                                                 genoffset_t valid_length,
                                                 uint64_t flags)
{


    struct devq_buf* buf;
    size_t tail = q->rx_tail;
    buf = &q->rx_bufs[tail];

    buf->rid = rid;
    buf->offset = offset;
    buf->length = length;
    buf->valid_data = valid_data;
    buf->valid_length = valid_length;
    buf->flags = flags;

#ifdef BENCH_QUEUE
    uint64_t start, end;
    start = rdtscp();
#endif

    sfn5122f_q_rx_ker_desc_t d;
    d = q->rx_ring.ker[tail];

    sfn5122f_q_rx_ker_desc_rx_ker_buf_addr_insert(d, addr);
    sfn5122f_q_rx_ker_desc_rx_ker_buf_region_insert(d, 0);
    // TODO: Check size
    sfn5122f_q_rx_ker_desc_rx_ker_buf_size_insert(d, length);
    sfn5122f_queue_bump_rxtail(q);

#ifdef BENCH_QUEUE
    end = rdtscp();
    uint64_t res = end - start;
    bench_ctl_add_run(&q->en_rx, &res);
#endif

    return 0;
}



static inline errval_t sfn5122f_queue_handle_rx_ev_devif(sfn5122f_queue_t* q, 
                                                         regionid_t* rid,
                                                         genoffset_t* offset,
                                                         genoffset_t* length,
                                                         genoffset_t* valid_data,
                                                         genoffset_t* valid_length,
                                                         uint64_t* flags)
{
   
#ifdef BENCH_QUEUE
    uint64_t start, end;
    start = rdtscp();
#endif
    errval_t err = SYS_ERR_OK;
    /*  Only one event is generated even if there is more than one
        descriptor per packet  */
    struct devq_buf* buf;
    size_t rx_head;

    sfn5122f_q_rx_ev_t ev;
    //sfn5122f_q_rx_user_desc_t d_user= 0;
    //sfn5122f_q_rx_ker_desc_t d = 0;

    ev = q->ev_ring[q->ev_head];
    rx_head = sfn5122f_q_rx_ev_rx_ev_desc_ptr_extract(ev);
    assert(rx_head < q->rx_size);
    buf = &q->rx_bufs[rx_head];

    *rid = buf->rid;
    *offset = buf->offset;
    *length = buf->length;
    *valid_data = buf->valid_data;
    *flags = buf->flags;

    if(!sfn5122f_q_rx_ev_rx_ev_pkt_ok_extract(ev)) {   
        if (sfn5122f_q_rx_ev_rx_ev_tobe_disc_extract(ev)) {
            // packet discared by software -> ok
            printf("Discard \n");
            err = NIC_ERR_RX_DISCARD;
        } else if (sfn5122f_q_rx_ev_rx_ev_buf_owner_id_extract(ev)) {
            printf("Wrong owner \n");
            err = NIC_ERR_RX_PKT;
        } else if (sfn5122f_q_rx_ev_rx_ev_frm_trunc_extract(ev)) {
            printf("Frame truncated \n");
            err = NIC_ERR_RX_PKT;
        } else if (sfn5122f_q_rx_ev_rx_ev_tcp_udp_chksum_err_extract(ev)) {
            printf("UDP/TCP checksum error \n");
            err = NIC_ERR_RX_PKT;
        } else if (sfn5122f_q_rx_ev_rx_ev_pkt_not_parsed_extract(ev)) {
            printf("Card error while parsing \n");
            err = NIC_ERR_RX_PKT;
        } else if (sfn5122f_q_rx_ev_rx_ev_ip_frag_err_extract(ev)) {
            /*
             * Seems like this should not really be handeled as an error even
             * if the event itself seems to suggest so. Not even handeled in linux. 
             * (pkt_ok()) is not even read out ...
             */ 
            err = SYS_ERR_OK;
            *valid_length = sfn5122f_q_rx_ev_rx_ev_byte_ctn_extract(ev);
        } else if (sfn5122f_q_rx_ev_rx_ev_ip_hdr_chksum_err_extract(ev)) {
            printf("IP header ckecksum error \n");
            err = NIC_ERR_RX_PKT;
        } else {
            printf("Unknown error \n");
            err = NIC_ERR_RX_PKT;
        }
    } else {
        *valid_length = sfn5122f_q_rx_ev_rx_ev_byte_ctn_extract(ev);
        /* Length of 0 is treated as 16384 bytes */
        if (*valid_length == 0) {
            *valid_length = 16384;
        }
    }
    /*
    if (q->userspace){
        d_user = q->tx_ring.user[q->tx_head];  
        d_user = 0;
    } else {
        d = q->tx_ring.ker[q->tx_head];  
        d = 0;
    }
    */
    /* only have to reset event entry */
    sfn5122f_queue_clear_ev(ev);
    sfn5122f_queue_bump_rxhead(q);

#ifdef BENCH_QUEUE
    end = rdtscp();
    uint64_t res = end - start;
    bench_ctl_add_run(&q->deq_rx, &res);
#endif
    return err;
}


static inline size_t sfn5122f_queue_free_rxslots(sfn5122f_queue_t* q)
{
    size_t head = q->rx_head;
    size_t tail = q->rx_tail;
    size_t size = q->rx_size;

    if (tail >= head) {
        return size - (tail - head) -1; 
    } else {
        return size - (tail + size - head) -1; 
    }
}


/*   TX       */
static inline size_t sfn5122f_queue_free_txslots(sfn5122f_queue_t* q)
{
    size_t head = q->tx_head;
    size_t tail = q->tx_tail;
    size_t size = q->tx_size;

    if (tail >= head) {
        return size - (tail - head) - 1; 
    } else {
        return size - (tail + size - head) - 1; 
    }

}

static inline bool is_batched(size_t size, uint16_t tx_head, uint16_t q_tx_head)
{
    if (tx_head >= q_tx_head) {
        return (tx_head - q_tx_head > 0);
    } else {
        return (((tx_head + size) - q_tx_head) > 0);
    }
}

static inline errval_t sfn5122f_queue_handle_tx_ev_devif(sfn5122f_queue_t* q, 
                                                         regionid_t* rid,
                                                         genoffset_t* offset,
                                                         genoffset_t* length,
                                                         genoffset_t* valid_data,
                                                         genoffset_t* valid_length,
                                                         uint64_t* flags)
{
#ifdef BENCH_QUEUE
    uint64_t start, end;
    start = rdtscp();
#endif
    errval_t err = SYS_ERR_OK;

    /*  Only one event is generated even if there is more than one
        descriptor per packet  */
    uint16_t ev_head = q->ev_head;
    uint16_t tx_head;
    struct devq_buf* buf;
    sfn5122f_q_tx_ev_t ev;
    sfn5122f_q_tx_user_desc_t d_user= 0;
    sfn5122f_q_tx_ker_desc_t d = 0;
   
    ev = q->ev_ring[ev_head];
    tx_head = sfn5122f_q_tx_ev_tx_ev_desc_ptr_extract(ev);
    
    assert(q->tx_head < q->tx_size);
    buf = &q->tx_bufs[q->tx_head];

    //printf("Tx_head %d q->tx_head %d size %ld q->tx_tail %d\n", 
    //        tx_head, q->tx_head, q->tx_size, q->tx_tail);

    *rid = buf->rid;
    *offset = buf->offset;
    *length = buf->length;
    *valid_data = buf->valid_data;
    *valid_length = buf->valid_length;
    *flags = buf->flags;

    sfn5122f_queue_bump_txhead(q);

    if (sfn5122f_q_tx_ev_tx_ev_pkt_err_extract(ev)){     
        debug_printf("ERROR \n");
        err = NIC_ERR_TX_PKT;
        goto out;
    }

    if (sfn5122f_q_tx_ev_tx_ev_comp_extract(ev) == 1){  
        // TX Event is a batch
        if (is_batched(q->tx_size, tx_head, q->tx_head)) {
            uint8_t index = 0;
            q->num_left = 0;

            if (q->userspace) {
                d_user = q->tx_ring.user[q->tx_head];  
            } else {
                d = q->tx_ring.ker[q->tx_head];  
            }

            tx_head = (tx_head +1) & (q->tx_size - 1);
            while (q->tx_head != tx_head ) {
                assert(q->tx_head < q->tx_size);
                buf = &q->tx_bufs[q->tx_head];
                q->bufs[index].rid = buf->rid;
                q->bufs[index].offset = buf->offset;
                q->bufs[index].valid_data = buf->valid_data;
                q->bufs[index].valid_length = buf->valid_length;
                q->bufs[index].flags = buf->flags;
                q->bufs[index].length = buf->length;
                //d_user = q->tx_ring.user[tx_head];  
                index++;
                sfn5122f_queue_bump_txhead(q);
                q->num_left++;
            }
          
            q->last_deq = 0;

            // set descriptor to 0 
            if (q->userspace){
                memset(d_user, 0 , sfn5122f_q_tx_user_desc_size*q->num_left);
            } else {
                memset(d, 0 , sfn5122f_q_tx_ker_desc_size*q->num_left);
            }
        } else { // Singe descriptor
            if (q->userspace){
                d_user = q->tx_ring.user[q->tx_head];  
                sfn5122f_queue_clear_kdesc(d_user);

            } else {
                d = q->tx_ring.ker[q->tx_head];  
                sfn5122f_queue_clear_kdesc(d);
            }
        }
    }

    out:

    // reset entry event in queue
    sfn5122f_queue_clear_ev(ev);

#ifdef BENCH_QUEUE
    end = rdtscp();
    uint64_t res = end - start;
    bench_ctl_add_run(&q->deq_tx, &res);
#endif

    return err;
}

static inline int sfn5122f_queue_add_txbuf_devif(sfn5122f_queue_t* q, 
                                                 lpaddr_t addr,
                                                 regionid_t rid,
                                                 genoffset_t offset,
                                                 genoffset_t length,
                                                 genoffset_t valid_data,
                                                 genoffset_t valid_length,
                                                 uint64_t flags)
{

    struct devq_buf* buf;
    size_t tail = q->tx_tail;


    assert(tail < q->tx_size);

    buf = &q->tx_bufs[tail];
   
    bool last = flags & NETIF_TXFLAG_LAST;    
    buf->rid = rid;
    buf->offset = offset;
    buf->length = length;
    buf->valid_data = valid_data;
    buf->valid_length = valid_length;
    buf->flags = flags;

#ifdef BENCH_QUEUE
    uint64_t start, end;
    start = rdtscp();
#endif

    sfn5122f_q_tx_ker_desc_t d;
    d = q->tx_ring.ker[tail];

    sfn5122f_q_tx_ker_desc_tx_ker_buf_addr_insert(d, addr + valid_data);
    sfn5122f_q_tx_ker_desc_tx_ker_byte_count_insert(d, valid_length);
    sfn5122f_q_tx_ker_desc_tx_ker_cont_insert(d, !last);
    sfn5122f_q_tx_ker_desc_tx_ker_buf_region_insert(d, 0);

    sfn5122f_queue_bump_txtail(q);

    __sync_synchronize();

#ifdef BENCH_QUEUE
    end = rdtscp();
    uint64_t res = end - start;
    bench_ctl_add_run(&q->en_tx, &res);
#endif

    return 0;
}


static inline int sfn5122f_queue_add_user_txbuf_devif(sfn5122f_queue_t* q, 
                                                      uint64_t buftbl_idx, 
                                                      uint64_t b_off,
                                                      regionid_t rid,
                                                      genoffset_t offset,
                                                      genoffset_t length,
                                                      genoffset_t valid_data,
                                                      genoffset_t valid_length,
                                                      uint64_t flags)
{
    
    //printf("Add tx_buf %lx \n", base);
    sfn5122f_q_tx_user_desc_t d;
    struct devq_buf* buf;
    size_t tail = q->tx_tail;

    d = q->tx_ring.ker[tail];

    assert(tail < q->tx_size);
    buf = &q->tx_bufs[tail];
   
    bool last = flags & NETIF_TXFLAG_LAST;    
    buf->rid = rid;
    buf->offset = offset;
    buf->length = length;
    buf->valid_data = valid_data;
    buf->valid_length = valid_length;
    buf->flags = flags;

    sfn5122f_q_tx_user_desc_tx_user_sw_ev_en_insert(d, 0);
    sfn5122f_q_tx_user_desc_tx_user_cont_insert(d, !last);
    sfn5122f_q_tx_user_desc_tx_user_byte_cnt_insert(d, valid_length);
    sfn5122f_q_tx_user_desc_tx_user_buf_id_insert(d, buftbl_idx);
    sfn5122f_q_tx_user_desc_tx_user_byte_ofs_insert(d, b_off);

    __sync_synchronize();
 
    sfn5122f_queue_bump_txtail(q);
    return 0;
}

static inline struct bench_ctl* sfn5122f_queue_get_benchmark_data(sfn5122f_queue_t* q, uint8_t type)
{
#ifdef BENCH_QUEUE
    switch (type) {
        case 0:
            return &q->en_rx;
        case 1:
            return &q->en_tx;
        case 2:
            return &q->deq_rx;
        case 3:
            return &q->deq_tx;
        default:
            return NULL;
    }
#endif
    return NULL;
}

#endif //ndef SFN5122F_CHANNEL_H_

