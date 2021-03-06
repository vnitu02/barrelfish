/*
 * Copyright (c) 2016, ETH Zurich.
 * All rights reserved.
 *
 * This file is distributed under the terms in the attached LICENSE file.
 * If you do not find this file, copies can be found by writing to:
 * ETH Zurich D-INFK, Universitaetsstrasse 6, CH-8092 Zurich. Attn: Systems Group.
 */

#include <stdio.h>
#include <string.h>

#include <barrelfish/barrelfish.h>
#include <barrelfish/spawn_client.h>
#include <driverkit/driverkit.h>
#include <driverkit/control.h>
#include <collections/list.h>

#include <pci/pci.h> // for pci_address

#ifdef __arm__
#include <if/monitor_blocking_defs.h>
#endif

#ifdef __aarch64__
#include <if/monitor_blocking_defs.h>
#endif

#include "kaluga.h"

//#if defined(__x86__) || defined(__ARM_ARCH_8A__)
#if 1
// Add an argument to argc/argv pair. argv must be mallocd!
static void argv_push(int * argc, char *** argv, char * new_arg){
    int new_size = *argc + 1;
    //char ** argv_old = *argv;
    //*argv = malloc((new_size+1) * sizeof(char*));
    //memcpy(*argv, argv_old, sizeof(char*) * (*argc + 1));
    *argv = realloc(*argv, (new_size+1) * sizeof(char*)); // +1 for last NULL entry.
    if(*argv == NULL){
        USER_PANIC("Could not allocate argv memory");
    }
    *argc = new_size;
    (*argv)[new_size-1] = new_arg;
    (*argv)[new_size] = NULL;
}

#ifndef __ARM_ARCH_7A__

errval_t default_start_function(coreid_t where,
                                struct module_info* mi,
                                char* record, struct driver_argument * arg)
{
    assert(mi != NULL);
    errval_t err = SYS_ERR_OK;
    coreid_t core;

    /*
     *  XXX: there may be more device using this driver, so starting it a second time
     *       may be needed.
     */
    if (!can_start(mi)) {
        return KALUGA_ERR_DRIVER_ALREADY_STARTED;
    }

    core = where + get_core_id_offset(mi);

    if (!is_auto_driver(mi)) {
        return KALUGA_ERR_DRIVER_NOT_AUTO;
    }

    // Construct additional command line arguments containing pci-id.
    // We need one extra entry for the new argument.
    char **argv = NULL;
    int argc = mi->argc;
    argv = calloc((argc+1) * sizeof(char *), 1); // +1 for trailing NULL
    assert(argv != NULL);
    memcpy(argv, mi->argv, (argc+1) * sizeof(char *));
    //assert(argv[argc] == NULL);

    uint64_t vendor_id, device_id, bus, dev, fun;
    err = oct_read(record, "_ { bus: %d, device: %d, function: %d, vendor: %d, device_id: %d }",
                    &bus, &dev, &fun, &vendor_id, &device_id);

    char * int_arg_str = NULL;
    if(arg != NULL && arg->int_arg.model != 0){
        // This mallocs int_arg_str
        int_startup_argument_to_string(&(arg->int_arg), &int_arg_str);
        KALUGA_DEBUG("Adding int_arg_str: %s\n", int_arg_str);
        argv_push(&argc, &argv, int_arg_str);
    }

    char * pci_arg_str = NULL;
    if (err_is_ok(err)) {
        // We assume that we're starting a device if the query above succeeds
        // and therefore append the pci vendor and device id to the argument
        // list.
        pci_arg_str = malloc(26);
        // Make sure pci vendor and device id fit into our argument
        assert(vendor_id < 0xFFFF && device_id < 0xFFFF);
        snprintf(pci_arg_str, 26, "%04"PRIx64":%04"PRIx64":%04"PRIx64":%04"
                        PRIx64":%04"PRIx64, vendor_id, device_id, bus, dev, fun);

        argv_push(&argc, &argv, pci_arg_str);
    }


    //err = spawn_program(core, mi->path, argv,
    //                environ, 0, get_did_ptr(mi));

    struct capref arg_cap = NULL_CAP;
    if(arg != NULL){
       arg_cap = arg->arg_caps;
    }
    err = spawn_program_with_caps(core, mi->path, argv,
                    environ, NULL_CAP, arg_cap, 0, get_did_ptr(mi));

    if (err_is_fail(err)) {
        DEBUG_ERR(err, "Spawning %s failed.", mi->path);
    }

    free(argv);
    free(pci_arg_str);
    free(int_arg_str);

    return err;
}
#endif

/**
 * \brief Startup function for drivers. Makes no assumptions about started 
 * device.
 */
errval_t
default_start_function_pure(coreid_t where, struct module_info* mi, char* record,
                           struct driver_argument* arg) {

    assert(mi != NULL);
    struct domain_instance* inst;
    errval_t err;

    if(mi->driverinstance == NULL){
        KALUGA_DEBUG("Creating new driver domain for %s\n", mi->binary);
        inst = instantiate_driver_domain(mi->binary, where);
        if (inst == NULL) {
            return DRIVERKIT_ERR_DRIVER_INIT;
        }
        
        while (inst->b == NULL) {
            event_dispatch(get_default_waitset());
        }   
        mi->driverinstance = inst;
    }

    inst = mi->driverinstance;
    char* oct_id;
    err = oct_read(record, "%s {}", &oct_id);
    assert(err_is_ok(err));

    struct driver_instance* drv =
        ddomain_create_driver_instance(arg->module_name, oct_id);

    drv->flags = arg->flags;
    drv->args = (char*[]){NULL,NULL,NULL,NULL};
    drv->argcn_cap = arg->arg_caps;
    return ddomain_instantiate_driver(inst, drv);
}

/**
 * \brief Startup function for new-style PCI drivers.
 *
 * Launches the driver instance in a driver domain instead.
 */
errval_t
default_start_function_new(coreid_t where, struct module_info* mi, char* record,
                           struct driver_argument* arg)
{
    assert(mi != NULL);
    errval_t err;

    // Construct additional command line arguments containing pci-id.
    // We need one extra entry for the new argument.
    char **argv = NULL;
    int argc = mi->argc;
    argv = malloc((argc+1) * sizeof(char *)); // +1 for trailing NULL
    assert(argv != NULL);
    memcpy(argv, mi->argv, (argc+1) * sizeof(char *));
    assert(argv[argc] == NULL);

    struct pci_addr addr;
    struct pci_id id;
    struct pci_class cls = {0, 0, 0};
    static struct domain_instance* inst;

    if (!is_auto_driver(mi)) {
        return KALUGA_ERR_DRIVER_NOT_AUTO;
    }

    char *oct_id;

    // TODO: Determine cls here as well
    {
        int64_t vendor_id, device_id, bus, dev, fun;
        err = oct_read(record, "%s { bus: %d, device: %d, function: %d, vendor: %d, device_id: %d }",
                        &oct_id, &bus, &dev, &fun,
                        &vendor_id, &device_id);

        if(err_is_fail(err)){
            DEBUG_ERR(err, "oct_read");
            return err;
        }
        addr.bus = bus;
        addr.device = dev;
        addr.function = fun;
        id.device = device_id;
        id.vendor = vendor_id;
    }

    // If driver instance not yet started, start. 
    // In case of multi instance, we start a new domain every time
    if (mi->driverinstance == NULL) {
        KALUGA_DEBUG("Creating new driver domain for %s\n", mi->binary);
        inst = instantiate_driver_domain(mi->binary, where);
        if (inst == NULL) {
            return DRIVERKIT_ERR_DRIVER_INIT;
        }

        mi->driverinstance = inst;
        
        while (inst->b == NULL) {
            event_dispatch(get_default_waitset());
        }   

    } else {
        inst = mi->driverinstance;
        KALUGA_DEBUG("Reusing existing driver domain %s\n", mi->binary);
    }

    struct driver_instance* drv = ddomain_create_driver_instance(arg->module_name, oct_id);

    // Build interrupt argument
    char * int_arg_str = NULL;
    if(arg != NULL && arg->int_arg.model != 0){
        // This mallocs int_arg_str
        int_startup_argument_to_string(&(arg->int_arg), &int_arg_str);
        KALUGA_DEBUG("Adding int_arg_str: %s\n", int_arg_str);
        argv_push(&argc, &argv, int_arg_str);
    }

    // Build PCI address argument
    char * pci_arg_str = malloc(strlen("pci=") + PCI_OCTET_LEN);
    assert(pci_arg_str);
    strcpy(pci_arg_str, "pci=");
    pci_serialize_octet(addr, id, cls, pci_arg_str + strlen("pci="));
    argv_push(&argc, &argv, pci_arg_str);

    drv->args = argv;
    drv->argcn_cap = arg->arg_caps;
    drv->arg_idx = argc;

    drv->caps[0] = arg->arg_caps; // Interrupt cap

    if (strncmp(mi->binary, "mlx4", 4) != 0) {
        ddomain_instantiate_driver(inst, drv);
    } else {
        KALUGA_DEBUG("Not starting module for mlx4 \n");
    }

    free(int_arg_str);
    return SYS_ERR_OK;
}

static int32_t predicate(void* data, void* arg)
{
    struct driver_instance* drv = (struct driver_instance*) data;
    if (strncmp(drv->inst_name, arg, strlen(drv->inst_name)) == 0) {
        return 1;
    }
    return 0;
}

static errval_t get_driver_ep(coreid_t where, struct module_info* driver,
                              char* oct_id, struct capref* ret_ep)
{
    errval_t err;
    struct driver_instance* drv = (struct driver_instance*) 
                                  collections_list_find_if(driver->driverinstance->spawned, 
                                                           predicate, oct_id);

    if (drv == NULL) {
        return DRIVERKIT_ERR_NO_DRIVER_FOUND;
    }

    err = driverkit_get_driver_ep_cap(drv, ret_ep, (where == driver->core));
    if (err_is_fail(err)) {
        return err;
    }

    return SYS_ERR_OK;
}

static struct driver_instance* get_driver_instance(struct module_info* driver, 
                                                   char* oct_id)
{
     return collections_list_find_if(driver->driverinstance->spawned, 
                                     predicate, oct_id);

}

/**
 * \brief Startup function for new-style x86 drivers for NICs.
 *
 * Launches the driver instance in a driver domain instead.
 */

errval_t start_networking_new(coreid_t where,
                              struct module_info* driver,
                              char* record, struct driver_argument * int_arg)
{
    assert(driver != NULL);
    errval_t err = SYS_ERR_OK;

    if (is_started(driver)) {
        if (strstr(int_arg->module_name, "_vf_") == NULL) {
            return KALUGA_ERR_DRIVER_ALREADY_STARTED;
        }
    }

    if (!is_auto_driver(driver)) {
        printf("Not auto %s\n", driver->binary);
        return KALUGA_ERR_DRIVER_NOT_AUTO;
    }

    err = default_start_function_new(where, driver, record, int_arg);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "Spawning %s failed.", driver->path);
        return err;
    }

    // TODO: Determine cls here as well
    struct pci_class cls = {0,0,0};
    int64_t vendor_id, device_id, bus, dev, fun;
    char* oct_id;
    err = oct_read(record, "%s { bus: %d, device: %d, function: %d, vendor: %d, device_id: %d }",
                    &oct_id, &bus, &dev, &fun,
                    &vendor_id, &device_id);
    if (err_is_fail(err)) {
        DEBUG_ERR(err, "Parsing record %s failed.", record);
        return err;
    }

    // add way to request endpoint through service
    struct driver_instance* drv = get_driver_instance(driver, oct_id);
    if (drv != NULL) {
        char qf_name[256];
        sprintf(qf_name, "%s:%"PRIu64":%"PRIu64":%"PRIu64"", driver->binary, bus, dev, fun); 

        err = queue_service_add_ep_factory(qs, qf_name, where, drv);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "Could not add EP factory (%s) to queue service.", qf_name);
            return err;
        }
    }

    if (!is_started(driver)) {
        // netsocket server in seperate process TODO might put into same process
        static struct domain_instance* inst;
        struct module_info* mi;
        if (strncmp(driver->binary, "mlx4", 4) != 0) {
            mi = find_module("net_sockets_server");
            if (mi == NULL) {
                KALUGA_DEBUG("Net_socket_server not found\n");
                return err;
            }

            KALUGA_DEBUG("Creating new driver domain for net_sockets_server\n");
            // TODO for now alway start on core 0
            inst = instantiate_driver_domain("net_sockets_server", where);
            if (inst == NULL) {
                return DRIVERKIT_ERR_DRIVER_INIT;
            }
        } else {
            inst = driver->driverinstance;
            mi = driver;
        }

        while (inst->b == NULL) {
            event_dispatch(get_default_waitset());
        }   

        char netss_name[256];
        sprintf(netss_name, "%s:%s:%"PRIu64":%"PRIu64":%"PRIu64"", mi->binary, 
                driver->binary, bus, dev, fun); 
        
        struct driver_instance* drv2;
        drv2 = ddomain_create_driver_instance("net_sockets_server_module", 
                                              netss_name);
     
        debug_printf("Starting net socket server (%s) \n", netss_name);
        // Build PCI address argument
        struct pci_id id;
        struct pci_addr addr;

        addr.bus = bus;
        addr.device = dev;
        addr.function = fun;
        id.device = device_id;
        id.vendor = vendor_id;

        // TODO free this
        char * pci_arg_str = malloc(PCI_OCTET_LEN);
        assert(pci_arg_str);
        pci_serialize_octet(addr, id, cls, pci_arg_str);

        // Spawn net_sockets_server
        err = ddomain_driver_add_arg(drv2, "net_sockets_server");
        assert(err_is_ok(err));
        err = ddomain_driver_add_arg(drv2, "auto");
        assert(err_is_ok(err));
        err = ddomain_driver_add_arg(drv2, driver->binary);
        assert(err_is_ok(err));
        err = ddomain_driver_add_arg(drv2, pci_arg_str);
        assert(err_is_ok(err));

        if (strncmp(driver->binary, "mlx4", 4) != 0) {
            struct capref cap;
            
            err = get_driver_ep(where, driver, oct_id, &cap);
            if (err_is_fail(err)) {     
                debug_printf("Failed getting EP to driver %s \n", oct_id);
                free(pci_arg_str);
                return err; 
            }

            err = ddomain_driver_add_cap(drv2, cap);        
            assert(err_is_ok(err));
        } else {
            struct capref cap = {
                .cnode = int_arg->argnode_ref,
                .slot = PCIARG_SLOT_INT
            };

            err = ddomain_driver_add_cap(drv2, cap);
            //drv2->argcn_cap = int_arg->arg_caps;

            KALUGA_DEBUG("Not adding mlx4 enpdoint %zu \n", drv2->cap_idx);
        }

        ddomain_instantiate_driver(inst, drv2);


        KALUGA_DEBUG("Adding %s to EP factories \n", netss_name);
        err = queue_service_add_ep_factory(qs, netss_name, where, drv2);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "Could not add EP factory (%s) to queue service.", 
                      netss_name);
            return err;
        }
    }

    return err;
}
#endif

errval_t start_networking(coreid_t core,
                          struct module_info* driver,
                          char* record, struct driver_argument * arg)
{
    assert(driver != NULL);
    errval_t err = SYS_ERR_OK;


    /* check if we are using the supplied pci address of eth0 */
    /*
    if (eth0.bus != 0xff || eth0.device != 0xff || eth0.function != 0xff) {
        err = oct_read(record, "_ { bus: %d, device: %d, function: %d, vendor: %d, device_id: %d }",
                            &bus, &dev, &fun, &vendor_id, &device_id);
        assert(err_is_ok(err));

        if ((eth0.bus != (uint8_t)bus)
             | (eth0.device != (uint8_t)dev)
             | (eth0.function != (uint8_t)fun)) {
            printf("start_networking: skipping card %" PRIu64 ":%" PRIu64 ":%"
                    PRIu64"\n", bus, dev, fun);
            printf("eth0 %" PRIu8 ":%" PRIu8 ":%"
                    PRIu8"\n", eth0.bus, eth0.device, eth0.function);
            return KALUGA_ERR_DRIVER_NOT_AUTO;
        }
    }
    */


    if (is_started(driver)) {
        printf("Already started %s\n", driver->binary);
        return KALUGA_ERR_DRIVER_ALREADY_STARTED;
    }

    if (!is_auto_driver(driver)) {
        printf("Not auto %s\n", driver->binary);
        return KALUGA_ERR_DRIVER_NOT_AUTO;
    }


    debug_printf("Start networking %s\n ", driver->binary);
    if (!(strcmp(driver->binary, "netss") == 0)) {
        
        driver->allow_multi = 1;
        uint64_t vendor_id, device_id, bus, dev, fun;
        err = oct_read(record, "_ { bus: %d, device: %d, function: %d, vendor: %d, device_id: %d }",
                       &bus, &dev, &fun, &vendor_id, &device_id);

        char* pci_arg_str = malloc(26);
        snprintf(pci_arg_str, 26, "%04"PRIx64":%04"PRIx64":%04"PRIx64":%04"
                        PRIx64":%04"PRIx64, vendor_id, device_id, bus, dev, fun);

        err = default_start_function(core, driver, record, arg);
        if (err_is_fail(err)) {
            DEBUG_ERR(err, "Spawning %s failed.", driver->path);
            return err;
        }

        // cards with driver in seperate process
        struct module_info* net_sockets = find_module("net_sockets_server");
        if (net_sockets == NULL) {
            printf("Net sockets server not found\n");
            return KALUGA_ERR_DRIVER_NOT_AUTO;
        }

        // Spawn net_sockets_server
        net_sockets->argv[0] = "net_sockets_server";
        net_sockets->argv[1] = "auto";
        net_sockets->argv[2] = driver->binary;
        net_sockets->argv[3] = pci_arg_str;

        err = spawn_program(core, net_sockets->path, net_sockets->argv, environ, 0,
                            get_did_ptr(net_sockets));
        free (pci_arg_str);
    } else {
        debug_printf("Starting MLX4 net socket server \n ");
        //driver->allow_multi = 1;
        // TODO currently only for mxl4, might be other cards that 
        // start the driver by creating a queuea
        /*
        if (!(driver->argc > 2)) {
            driver->argv[driver->argc] = "mlx4";        
            driver->argc++;
            driver->argv[driver->argc] = NULL;
        }
        */

        uint64_t vendor_id, device_id, bus, dev, fun;
        err = oct_read(record, "_ { bus: %d, device: %d, function: %d, vendor: %d, device_id: %d }",
                       &bus, &dev, &fun, &vendor_id, &device_id);

        // Build PCI address argument
        struct pci_id id;
        struct pci_addr addr;
        struct pci_class cls = {0, 0, 0};

        addr.bus = bus;
        addr.device = dev;
        addr.function = fun;
        id.device = device_id;
        id.vendor = vendor_id;

        // TODO free this
        char * pci_arg_str = malloc(PCI_OCTET_LEN);
        assert(pci_arg_str);
        pci_serialize_octet(addr, id, cls, pci_arg_str);

        // Spawn net_sockets_server
        driver->argv[0] = "netss";
        driver->argv[1] = "auto";
        driver->argv[2] = "mlx4";
        driver->argv[3] = pci_arg_str;
        driver->argc = 4;
        debug_printf("ARGC %d \n", driver->argc);

        err = spawn_program(core, driver->path, driver->argv, environ, 0,
                            get_did_ptr(driver));
        free (pci_arg_str);
    }

    return err;
}
