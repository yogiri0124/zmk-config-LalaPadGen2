/*
 * キー位置の送信（key stream）の共通部分。
 * key_stream.c（本体）と、送り道（key_stream_usb.c・key_stream_ble.c）の間の取り決め。
 *
 * PC とは、ベンダー定義の HID（raw HID）で 32 バイトのレポートをやり取りする。
 * USB では HID の機器をもう1つ、Bluetooth では HID のサービスをもう1つ増やして、そこを通す
 * （ふだんのキーボードの HID とは別。どちらの接続でも、PC からは同じ raw HID として WebHID で読める）。
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* レポートの大きさ（バイト）。PC との取り決めなので変えない */
#define KEY_STREAM_REPORT_SIZE 32

/* raw HID のレポートの記述（USB と Bluetooth で共通） */
extern const uint8_t key_stream_report_desc[];
extern const size_t key_stream_report_desc_size;

/* 送り道 */
enum key_stream_transport {
    KEY_STREAM_TRANSPORT_NONE,
    KEY_STREAM_TRANSPORT_USB,
    KEY_STREAM_TRANSPORT_BLE,
};

struct bt_conn;

/* ---- 送り道 → 本体 ---- */

/* PC から届いたレポート（命令）。conn は Bluetooth のときの接続（USB では NULL）。
 * どのスレッドから呼んでもよい（中身を写して、本体の処理用のスレッドで処理する） */
void key_stream_received(enum key_stream_transport transport, struct bt_conn *conn,
                         const uint8_t *data, size_t len);

/* 送ったレポートが送り終わった（次を送ってよい）。id は送るときに渡された番号。どのスレッドから呼んでもよい */
void key_stream_sent(uint32_t id);

/* 送り道が切れた（USB を抜いた、Bluetooth が切れた）。どのスレッドから呼んでもよい */
void key_stream_transport_lost(enum key_stream_transport transport, struct bt_conn *conn);

/* ---- 本体 → 送り道 ---- */

/* 32 バイトのレポートを1つ送り始める。送り終わったら key_stream_sent(id) を呼ぶ。
 * USB は、前に送ったものがまだ送り終わっていなければ、送らずに -EAGAIN を返す（バッファを書き換えないため）。
 * 戻り値: 0 = 送り始めた / -EAGAIN = 今は送れない（少し後でもう一度試す）/ そのほか = 送れない（送るのをやめる） */
int key_stream_usb_send(const uint8_t *report, uint32_t id);
/* USB の転送が終わらないまま待つのをやめる（次の key_stream_usb_send を受け付けるようにする） */
void key_stream_usb_abort(void);
int key_stream_ble_send(struct bt_conn *conn, const uint8_t *report, uint32_t id);
