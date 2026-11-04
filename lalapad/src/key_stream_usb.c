/*
 * キー位置の送信（key stream）の USB の送り道。
 *
 * ふだんのキーボードの HID（HID_0）とは別に、ベンダー定義の HID の機器（HID_1）を1つ増やす。
 * CONFIG_USB_HID_DEVICE_COUNT を 2 にしておく必要がある。
 * PC からのレポート（命令）は、出力レポート（SET_REPORT）で届く。
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
// 割り込み OUT エンドポイントは使わない。有効にすると HID_0（ZMK のキーボード）にも OUT エンドポイントが付き、
// ZMK はその受け取り（int_out_ready）を登録していないので、キーボードの出力レポート（LED など）が届かなくなる
BUILD_ASSERT(!IS_ENABLED(CONFIG_ENABLE_HID_INT_OUT_EP),
             "key stream over USB does not support CONFIG_ENABLE_HID_INT_OUT_EP");

#define HID_GET_REPORT_TYPE_MASK 0xff00
#define HID_REPORT_TYPE_OUTPUT 0x200

static const struct device *hid_dev;

/* USB は送り終わるまでバッファから直接読む（DMA。エンドポイントの大きさが 16 バイトなら 32 バイトは 2 回に
 * 分けて送るので、途中で書き換えると前半と後半が別のレポートになる）ので、スタックではなく静的に持つ。
 * 本体は、終わらない転送を見切って次を送ってくることがあるので、バッファを2つ交互に使い、最後に受け付けられた
 * 転送のバッファ（tx_last）は書き換えない。USB の側は前の転送が終わるまで次を受け付けない（-EAGAIN）ので、
 * 次が受け付けられた時点で、もう一方のバッファは使われていない。
 * tx_bufs と tx_last は、本体の送る処理（1つのスレッド）からだけ触る */
static uint8_t tx_bufs[2][KEY_STREAM_REPORT_SIZE];
static uint8_t tx_last;
/* 最後に送ろうとした転送の番号。送り終わったの知らせでは、どの転送が終わったかが分からないので、これを渡す
 * （見切った前の転送が後から終わると、今の番号で知らせてしまうことがある。そのときは本体が次を少し早く送り、
 *  USB の側に断られて送り直すだけで、バッファは書き換えない） */
static atomic_t tx_id;

static void in_ready_cb(const struct device *dev) {
    ARG_UNUSED(dev);
    key_stream_sent((uint32_t)atomic_get(&tx_id));
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

/* GET_REPORT は受け付けない（登録しなければ Zephyr が -ENOTSUP で断る） */
static const struct hid_ops ops = {
    .set_report = set_report_cb,
    .int_in_ready = in_ready_cb,
};

int key_stream_usb_send(const uint8_t *report, uint32_t id) {
    if (hid_dev == NULL || !zmk_usb_is_hid_ready()) {
        return -ENOTCONN;
    }
    if (zmk_usb_get_status() == USB_DC_SUSPEND) {
        // PC がサスペンドしている間は送れない（Zephyr が断るたびに警告を出すので、ここで断って間を空ける）
        return -EBUSY;
    }
    const uint8_t next = tx_last ^ 1;
    memcpy(tx_bufs[next], report, KEY_STREAM_REPORT_SIZE);
    atomic_set(&tx_id, (atomic_val_t)id);
    const int err = hid_int_ep_write(hid_dev, tx_bufs[next], KEY_STREAM_REPORT_SIZE, NULL);
    if (err == 0) {
        tx_last = next;
        return 0;
    }
    if (err == -EAGAIN || err == -EBUSY) {
        // 前の転送がまだ終わっていない（見切った転送が残っているときなど）。本体は送り終わったの知らせを待ってから
        // 次を送るので、ふだんはここに来ない（nRF の USB は、知らせの前に次を受け付けられる状態にしている）。
        // Zephyr は断るたびに、その中で何度も試し直して警告を出すので、短い間隔（-EAGAIN）で試し直させると
        // 警告が続けて出る。間を空けて試し直してもらう（-EBUSY）
        return -EBUSY;
    }
    return err;
}

/* USB を抜いた（PC から使えなくなった）ら、USB で送っていた分をやめる */
static int key_stream_usb_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *ev = as_zmk_usb_conn_state_changed(eh);
    if (ev != NULL && ev->conn_state != ZMK_USB_CONN_HID) {
        // 抜いたときは送っていた転送も取り消される（送り終わったの知らせは来ない）
        key_stream_transport_lost(KEY_STREAM_TRANSPORT_USB, NULL);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(key_stream_usb, key_stream_usb_listener);
ZMK_SUBSCRIPTION(key_stream_usb, zmk_usb_conn_state_changed);

static int key_stream_usb_init(void) {
    // HID_0 はキーボード（ZMK）が使うので、2つ目の HID_1 を使う
    hid_dev = device_get_binding("HID_1");
    if (hid_dev == NULL) {
        LOG_ERR("Key stream: USB HID device HID_1 not found");
        return -ENODEV;
    }
    usb_hid_register_device(hid_dev, key_stream_report_desc, key_stream_report_desc_size, &ops);
    return usb_hid_init(hid_dev);
}

// ZMK のキーボードの HID（HID_0）と同じ時点で登録する
SYS_INIT(key_stream_usb_init, APPLICATION, CONFIG_ZMK_USB_HID_INIT_PRIORITY);
