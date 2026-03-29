#ifndef F87D_DEVICE_MANAGER_H
#define F87D_DEVICE_MANAGER_H

#include <f87/f87.h>
#include <stdbool.h>

typedef struct {
    f87_ctx *ctx;
    f87_device *dev;
    f87_device_info *dev_list;
    int dev_count;
    f87_device_info connected_info;
    bool connected;
} f87d_device_manager_t;

typedef void (*f87d_device_connected_cb)(const f87_device_info *info, void *userdata);
typedef void (*f87d_device_disconnected_cb)(void *userdata);

typedef struct {
    f87d_device_connected_cb on_connected;
    f87d_device_disconnected_cb on_disconnected;
    void *userdata;
} f87d_device_callbacks_t;

int  f87d_devmgr_init(f87d_device_manager_t *mgr);
void f87d_devmgr_destroy(f87d_device_manager_t *mgr);

int  f87d_devmgr_scan(f87d_device_manager_t *mgr,
                       const f87d_device_callbacks_t *cbs);
bool f87d_devmgr_check_alive(f87d_device_manager_t *mgr);
int  f87d_devmgr_poll(f87d_device_manager_t *mgr,
                       const f87d_device_callbacks_t *cbs);

f87_device *f87d_devmgr_get_device(f87d_device_manager_t *mgr);

#endif
