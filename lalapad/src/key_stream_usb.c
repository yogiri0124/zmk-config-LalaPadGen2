/*
 * キー位置の送信（key stream）の USB の送り道。
 *
 * ふだんのキーボードの HID（HID_0）とは別に、ベンダー定義の HID の機器（CONFIG_ZMK_KEY_STREAM_USB_DEVICE、
 * 標準は HID_1）を1つ増やす。CONFIG_USB_HID_DEVICE_COUNT を 2 にしておく必要がある。
 * PC からのレポート（命令）は、出力レポート（SET_REPORT。割り込み OUT エンドポイントがあればそちら）で届く。
 */

#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>

#include "key_stream.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// HID_0 はキーボード（ZMK）が使うので、キー位置の送信用にもう1つ要る
BUILD_ASSERT(CONFIG_USB_HID_DEVICE_COUNT >= 2, "key stream over USB needs CONFIG_USB_HID_DEVICE_COUNT=2");

#define HID_GET_REPORT_TYPE_MASK 0xff00
#define HID_REPORT_TYPE_OUTPUT 0x200

static const struct device *hid_dev;

/* USB は送り終わるまでこのバッファから直接読む（DMA）ので、スタックではなく静的に持つ。
 * 送り終わるまで（tx_busy の間）は書き換えない。本体が送る先を Bluetooth に変えて戻ってきた場合など、
 * 前の転送が終わっていなければ、次は受け付けない */
static uint8_t tx_buf[KEY_STREAM_REPORT_SIZE];
static atomic_t tx_busy;
/* 送っている転送の番号（tx_busy の間だけ有効） */
static uint32_t tx_id;

static void in_ready_cb(const struct device *dev) {
    ARG_UNUSED(dev);
    const uint32_t id = tx_id;
    atomic_clear(&tx_busy);
    key_stream_sent(id);
}

static int set_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {
    ARG_UNUSED(dev);
    if ((setup->wValue & HID_GET_REPORT_TYPE_MASK) != HID_REPORT_TYPE_OUTPUT) {
        return -ENOTSUP;
    }
    key_stream_received(KEY_STREAM_TRANSPORT_USB, NULL, *data, *len);
    return 0;
}

static int get_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {
    ARG_UNUSED(dev);
    ARG_UNUSED(setup);
    ARG_UNUSED(len);
    ARG_UNUSED(data);
    return -ENOTSUP;
}

#if IS_ENABLED(CONFIG_ENABLE_HID_INT_OUT_EP)
static void out_ready_cb(const struct device *dev) {
    uint8_t buf[KEY_STREAM_REPORT_SIZE];
    uint32_t read = 0;
    if (hid_int_ep_read(dev, buf, sizeof(buf), &read) == 0 && read > 0) {
        key_stream_received(KEY_STREAM_TRANSPORT_USB, NULL, buf, read);
    }
}
#endif

static const struct hid_ops ops = {
    .get_report = get_report_cb,
    .set_report = set_report_cb,
    .int_in_ready = in_ready_cb,
#if IS_ENABLED(CONFIG_ENABLE_HID_INT_OUT_EP)
    .int_out_ready = out_ready_cb,
#endif
};

int key_stream_usb_send(const uint8_t *report, uint32_t id) {
    if (hid_dev == NULL || !zmk_usb_is_hid_ready()) {
        return -ENOTCONN;
    }
    if (!atomic_cas(&tx_busy, 0, 1)) {
        return -EAGAIN; // 前の転送がまだ終わっていない
    }
    tx_id = id;
    memcpy(tx_buf, report, sizeof(tx_buf));
    const int err = hid_int_ep_write(hid_dev, tx_buf, sizeof(tx_buf), NULL);
    if (err != 0) {
        atomic_clear(&tx_busy);
    }
    if (err == -EAGAIN || err == -EBUSY) {
        return -EAGAIN;
    }
    return err;
}

/* USB を抜いた（PC から使えなくなった）ら、USB で送っていた分をやめる */
static int key_stream_usb_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *ev = as_zmk_usb_conn_state_changed(eh);
    if (ev != NULL && ev->conn_state != ZMK_USB_CONN_HID) {
        // 抜いたときは送っていた転送も取り消される（送り終わったの知らせは来ない）
        atomic_clear(&tx_busy);
        key_stream_transport_lost(KEY_STREAM_TRANSPORT_USB, NULL);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(key_stream_usb, key_stream_usb_listener);
ZMK_SUBSCRIPTION(key_stream_usb, zmk_usb_conn_state_changed);

static int key_stream_usb_init(void) {
    // HID_0 はキーボード（ZMK）が使う。指定すると、キーボードの HID の記述を上書きしてしまう
    if (strcmp(CONFIG_ZMK_KEY_STREAM_USB_DEVICE, "HID_0") == 0) {
        LOG_ERR("Key stream: HID_0 is used by the keyboard, set CONFIG_ZMK_KEY_STREAM_USB_DEVICE to HID_1");
        return -EINVAL;
    }
    hid_dev = device_get_binding(CONFIG_ZMK_KEY_STREAM_USB_DEVICE);
    if (hid_dev == NULL) {
        LOG_ERR("Key stream: USB HID device %s not found", CONFIG_ZMK_KEY_STREAM_USB_DEVICE);
        return -ENODEV;
    }
    usb_hid_register_device(hid_dev, key_stream_report_desc, key_stream_report_desc_size, &ops);
    return usb_hid_init(hid_dev);
}

// ZMK のキーボードの HID（HID_0）と同じ時点で登録する
SYS_INIT(key_stream_usb_init, APPLICATION, CONFIG_ZMK_USB_HID_INIT_PRIORITY);
