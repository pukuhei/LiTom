#include <zephyr/kernel.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/zmk/hid_usage_pages.h>

#include <zmk/endpoints.h>
#include <zmk/endpoints_types.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#include <zmk/usb_hid.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static void prefer_usb_endpoint_work_handler(struct k_work *work) {
    if (!zmk_usb_is_hid_ready()) {
        return;
    }

    struct zmk_endpoint_instance selected = zmk_endpoints_selected();
    if (selected.transport == ZMK_TRANSPORT_USB) {
        return;
    }

    LOG_INF("USB HID ready; selecting USB endpoint");

    if (zmk_endpoints_get_preferred_transport() == ZMK_TRANSPORT_USB) {
        zmk_endpoints_select_transport(ZMK_TRANSPORT_BLE);
    }

    zmk_endpoints_select_transport(ZMK_TRANSPORT_USB);
}

K_WORK_DELAYABLE_DEFINE(prefer_usb_endpoint_work, prefer_usb_endpoint_work_handler);

static atomic_t pending_usb_mouse_report;

#define PENDING_USB_MOUSE_REPORT BIT(0)

static bool usb_hid_transport_available(void) {
    bool ready = zmk_usb_is_hid_ready();
    LOG_DBG("LiTom USB fallback HID ready: %d", ready);
    return ready;
}

static void send_usb_keyboard_report_fallback(void) {
    if (!usb_hid_transport_available()) {
        return;
    }

    int err = zmk_usb_hid_send_keyboard_report();
    if (err < 0) {
        LOG_WRN("USB fallback keyboard report failed: %d", err);
    } else {
        LOG_DBG("USB fallback keyboard report sent");
    }
}

static void send_usb_consumer_report_fallback(void) {
    if (!usb_hid_transport_available()) {
        return;
    }

    int err = zmk_usb_hid_send_consumer_report();
    if (err < 0) {
        LOG_WRN("USB fallback consumer report failed: %d", err);
    } else {
        LOG_DBG("USB fallback consumer report sent");
    }
}

#if IS_ENABLED(CONFIG_ZMK_POINTING)
static void usb_mouse_report_fallback_work_handler(struct k_work *work) {
    if (!usb_hid_transport_available()) {
        return;
    }

    if ((atomic_set(&pending_usb_mouse_report, 0) & PENDING_USB_MOUSE_REPORT) == 0) {
        return;
    }

    int err = zmk_usb_hid_send_mouse_report();
    if (err < 0) {
        LOG_WRN("USB fallback mouse report failed: %d", err);
    } else {
        LOG_DBG("USB fallback mouse report sent");
    }
}

K_WORK_DELAYABLE_DEFINE(usb_mouse_report_fallback_work, usb_mouse_report_fallback_work_handler);

static void schedule_usb_mouse_report_fallback(void) {
    if (!usb_hid_transport_available()) {
        return;
    }

    atomic_or(&pending_usb_mouse_report, PENDING_USB_MOUSE_REPORT);
    k_work_schedule(&usb_mouse_report_fallback_work, K_MSEC(1));
}
#endif

static int prefer_usb_endpoint_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *ev = as_zmk_usb_conn_state_changed(eh);
    if (ev->conn_state == ZMK_USB_CONN_HID) {
        k_work_reschedule(&prefer_usb_endpoint_work, K_MSEC(100));
    }

    return 0;
}

ZMK_LISTENER(prefer_usb_endpoint_listener, prefer_usb_endpoint_listener);
ZMK_SUBSCRIPTION(prefer_usb_endpoint_listener, zmk_usb_conn_state_changed);

static int usb_keycode_report_fallback_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL) {
        return 0;
    }

    switch (ev->usage_page) {
    case HID_USAGE_KEY:
        LOG_DBG("USB fallback keyboard event: keycode 0x%02X state %d", ev->keycode, ev->state);
        send_usb_keyboard_report_fallback();
        break;
    case HID_USAGE_CONSUMER:
        LOG_DBG("USB fallback consumer event: keycode 0x%02X state %d", ev->keycode, ev->state);
        send_usb_consumer_report_fallback();
        break;
    }

    return 0;
}

ZMK_LISTENER(usb_keycode_report_fallback_listener, usb_keycode_report_fallback_listener);
ZMK_SUBSCRIPTION(usb_keycode_report_fallback_listener, zmk_keycode_state_changed);

#if IS_ENABLED(CONFIG_ZMK_POINTING)
static void usb_mouse_report_fallback_listener(struct input_event *ev) {
    if (ev->sync && zmk_endpoints_selected().transport != ZMK_TRANSPORT_USB) {
        schedule_usb_mouse_report_fallback();
    }
}

INPUT_CALLBACK_DEFINE(NULL, usb_mouse_report_fallback_listener);
#endif

static int prefer_usb_endpoint_init(void) {
    LOG_INF("LiTom USB endpoint fallback initialized");
    k_work_reschedule(&prefer_usb_endpoint_work, K_SECONDS(1));
    return 0;
}

SYS_INIT(prefer_usb_endpoint_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
