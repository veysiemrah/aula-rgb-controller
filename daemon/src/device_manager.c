#include "device_manager.h"
#include <string.h>
#include <stdio.h>

int f87d_devmgr_init(f87d_device_manager_t *mgr)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->ctx = f87_init();
    if (!mgr->ctx)
        return -1;
    return 0;
}

void f87d_devmgr_destroy(f87d_device_manager_t *mgr)
{
    if (mgr->dev) {
        f87_close(mgr->dev);
        mgr->dev = NULL;
    }
    if (mgr->dev_list) {
        f87_free_device_list(mgr->dev_list);
        mgr->dev_list = NULL;
    }
    if (mgr->ctx) {
        f87_exit(mgr->ctx);
        mgr->ctx = NULL;
    }
    mgr->connected = false;
}

int f87d_devmgr_scan(f87d_device_manager_t *mgr,
                      const f87d_device_callbacks_t *cbs)
{
    if (mgr->dev) {
        f87_close(mgr->dev);
        mgr->dev = NULL;
        mgr->connected = false;
    }
    if (mgr->dev_list) {
        f87_free_device_list(mgr->dev_list);
        mgr->dev_list = NULL;
    }

    int rc = f87_find_devices(mgr->ctx, &mgr->dev_list, &mgr->dev_count);
    if (rc < 0 || mgr->dev_count == 0)
        return -1;

    mgr->dev = f87_open(mgr->ctx, &mgr->dev_list[0]);
    if (!mgr->dev)
        return -1;

    mgr->connected = true;
    memcpy(&mgr->connected_info, &mgr->dev_list[0], sizeof(f87_device_info));

    if (cbs && cbs->on_connected)
        cbs->on_connected(&mgr->connected_info, cbs->userdata);

    return 0;
}

bool f87d_devmgr_check_alive(f87d_device_manager_t *mgr)
{
    if (!mgr->dev || !mgr->connected)
        return false;

    f87_effect cur;
    int rc = f87_get_current_effect(mgr->dev, &cur);
    if (rc < 0) {
        mgr->connected = false;
        return false;
    }
    return true;
}

int f87d_devmgr_poll(f87d_device_manager_t *mgr,
                      const f87d_device_callbacks_t *cbs)
{
    if (mgr->connected) {
        if (!f87d_devmgr_check_alive(mgr)) {
            /* Stop animations BEFORE closing device —
             * animation thread holds a reference to dev */
            mgr->connected = false;
            if (cbs && cbs->on_disconnected)
                cbs->on_disconnected(cbs->userdata);
            f87_close(mgr->dev);
            mgr->dev = NULL;
        }
        return 0;
    }

    return f87d_devmgr_scan(mgr, cbs);
}

f87_device *f87d_devmgr_get_device(f87d_device_manager_t *mgr)
{
    return mgr->connected ? mgr->dev : NULL;
}
