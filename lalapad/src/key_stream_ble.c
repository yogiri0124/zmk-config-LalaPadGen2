/*
 * キー位置の送信（key stream）の Bluetooth の送り道。
 *
 * ZMK のキーボードの HID のサービス（hog.c）とは別に、ベンダー定義の HID のサービス（HID over GATT）を
 * もう1つ増やす。PC（Windows）からは、USB のときと同じ raw HID として見える。
 * 入力レポート（キーボード → PC）は通知で送り、出力レポート（PC → キーボード、命令）は書き込みで届く。
 * 権限は ZMK のキーボードの HID（hog.c）に合わせる: レポートの記述・入力レポート・出力レポートの読み書きと
 * 通知の設定は、暗号化した接続でだけ許す（HID Information の読み取りと Control Point の書き込みは、暗号化なしで許す）。
 */

#include <errno.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "key_stream.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

enum {
    HIDS_REMOTE_WAKE = BIT(0),
    HIDS_NORMALLY_CONNECTABLE = BIT(1),
};

struct hids_info {
    uint16_t version; /* 元になる USB HID の仕様の版 */
    uint8_t code;     /* 国コード */
    uint8_t flags;
} __packed;

struct hids_report {
    uint8_t id;   /* レポート ID（使わないので 0） */
    uint8_t type; /* 1 = 入力、2 = 出力 */
} __packed;

enum {
    HIDS_INPUT = 0x01,
    HIDS_OUTPUT = 0x02,
};

static const struct hids_info info = {
    .version = 0x0000,
    .code = 0x00,
    .flags = HIDS_NORMALLY_CONNECTABLE,
};

static const struct hids_report input_ref = {.id = 0x00, .type = HIDS_INPUT};
static const struct hids_report output_ref = {.id = 0x00, .type = HIDS_OUTPUT};

static ssize_t read_info(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                         uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data, sizeof(info));
}

static ssize_t read_report_ref(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                               uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_report));
}

static ssize_t read_report_map(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                               uint16_t len, uint16_t offset) {
    return bt_gatt_attr_read(conn, attr, buf, len, offset, key_stream_report_desc,
                             key_stream_report_desc_size);
}

/* 入力レポートを読まれたとき（通知で送るので、中身は空で返す） */
static ssize_t read_input_report(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
                                 uint16_t len, uint16_t offset) {
    static const uint8_t empty[KEY_STREAM_REPORT_SIZE];
    return bt_gatt_attr_read(conn, attr, buf, len, offset, empty, sizeof(empty));
}

static ssize_t write_output_report(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                   const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset != 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    key_stream_received(KEY_STREAM_TRANSPORT_BLE, conn, buf, len);
    return len;
}

/* Control Point（PC のサスペンドの知らせ）は受け取るだけで使わない（HID のサービスとして必要な属性） */
static ssize_t write_ctrl_point(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset, uint8_t flags) {
    if (offset + len > 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    return len;
}

/* 属性の並び: [0] サービス [1][2] HID Information [3][4] Report Map
 *            [5][6] 入力レポート [7] その CCC [8] その Report Reference
 *            [9][10] 出力レポート [11] その Report Reference [12][13] Control Point */
BT_GATT_SERVICE_DEFINE(
    key_stream_svc, BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ, BT_GATT_PERM_READ, read_info, NULL,
                           (void *)&info),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ, BT_GATT_PERM_READ_ENCRYPT,
                           read_report_map, NULL, NULL),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT, read_input_report, NULL, NULL),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_report_ref, NULL,
                       (void *)&input_ref),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT, read_input_report,
                           write_output_report, NULL),
    BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ_ENCRYPT, read_report_ref, NULL,
                       (void *)&output_ref),
    BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, write_ctrl_point, NULL));

#define INPUT_REPORT_ATTR (&key_stream_svc.attrs[5])

static void notify_sent_cb(struct bt_conn *conn, void *user_data) {
    ARG_UNUSED(conn);
    key_stream_sent((uint32_t)(uintptr_t)user_data);
}

int key_stream_ble_send(struct bt_conn *conn, const uint8_t *report, uint32_t id) {
    if (conn == NULL) {
        return -ENOTCONN;
    }
    if (!bt_gatt_is_subscribed(conn, INPUT_REPORT_ATTR, BT_GATT_CCC_NOTIFY)) {
        return -ENOTCONN;
    }
    struct bt_gatt_notify_params params = {
        .attr = INPUT_REPORT_ATTR,
        .data = report,
        .len = KEY_STREAM_REPORT_SIZE,
        .func = notify_sent_cb,
        .user_data = (void *)(uintptr_t)id, // 「送り終わった」で、どの送信かを知らせる
    };
    // 送るバッファの空きがなければ待つことがある（専用のワークキューで呼ぶので、ほかの処理は止めない）
    const int err = bt_gatt_notify_cb(conn, &params);
    if (err == -EPERM) {
        bt_conn_set_security(conn, BT_SECURITY_L2);
        return -EAGAIN;
    }
    if (err == -ENOMEM || err == -ENOBUFS) {
        return -EAGAIN;
    }
    return err;
}

static void disconnected(struct bt_conn *conn, uint8_t reason) {
    ARG_UNUSED(reason);
    key_stream_transport_lost(KEY_STREAM_TRANSPORT_BLE, conn);
}

BT_CONN_CB_DEFINE(key_stream_conn_callbacks) = {
    .disconnected = disconnected,
};
