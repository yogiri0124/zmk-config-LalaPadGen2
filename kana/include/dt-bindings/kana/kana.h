/*
 * かな入力モジュール (&kana_key) の文字 ID。
 * keymap では `&kana_key KN_KA` のように使う。ID から送るローマ字は src/behavior_kana.c の表で決まる。
 * 0 は未定義（何も送らない）。
 */

#pragma once

/* 清音 */
#define KN_A 1
#define KN_I 2
#define KN_U 3
#define KN_E 4
#define KN_O 5
#define KN_KA 6
#define KN_KI 7
#define KN_KU 8
#define KN_KE 9
#define KN_KO 10
#define KN_SA 11
#define KN_SI 12
#define KN_SU 13
#define KN_SE 14
#define KN_SO 15
#define KN_TA 16
#define KN_TI 17
#define KN_TU 18
#define KN_TE 19
#define KN_TO 20
#define KN_NA 21
#define KN_NI 22
#define KN_NU 23
#define KN_NE 24
#define KN_NO 25
#define KN_HA 26
#define KN_HI 27
#define KN_HU 28
#define KN_HE 29
#define KN_HO 30
#define KN_MA 31
#define KN_MI 32
#define KN_MU 33
#define KN_ME 34
#define KN_MO 35
#define KN_YA 36
#define KN_YU 37
#define KN_YO 38
#define KN_RA 39
#define KN_RI 40
#define KN_RU 41
#define KN_RE 42
#define KN_RO 43
#define KN_WA 44
#define KN_WO 45
#define KN_NN 46

/* 濁音 */
#define KN_GA 47
#define KN_GI 48
#define KN_GU 49
#define KN_GE 50
#define KN_GO 51
#define KN_ZA 52
#define KN_ZI 53
#define KN_ZU 54
#define KN_ZE 55
#define KN_ZO 56
#define KN_DA 57
#define KN_DI 58
#define KN_DU 59
#define KN_DE 60
#define KN_DO 61
#define KN_BA 62
#define KN_BI 63
#define KN_BU 64
#define KN_BE 65
#define KN_BO 66

/* 半濁音 */
#define KN_PA 67
#define KN_PI 68
#define KN_PU 69
#define KN_PE 70
#define KN_PO 71

/* 拗音・小書き */
#define KN_XYA 72
#define KN_XYU 73
#define KN_XYO 74
#define KN_XA 75
#define KN_XI 76
#define KN_XU 77
#define KN_XE 78
#define KN_XO 79
#define KN_XTU 80
#define KN_VU 81

/* 記号 */
#define KN_TOUTEN 82   /* 、 */
#define KN_KUTEN 83    /* 。 */
#define KN_CHOUON 84   /* ー */
#define KN_SANTEN 85   /* … */
#define KN_EXCL 86     /* ！ */
#define KN_QUES 87     /* ？ */
#define KN_KAGI 88     /* 「」 */
#define KN_NIJUKAGI 89 /* 『』 */
#define KN_PAREN 90    /* （） */

/* 編集用のキー（かなと同じ送信キューで順番どおりに送る。IME オンの再送はしない） */
#define KN_SPACE 91
#define KN_ENTER 92
#define KN_BSPC 93

#define KN_MAX_ID 93

/* &kana_sw のパラメータ */
#define KANA_SW_ON 1
#define KANA_SW_OFF 2
