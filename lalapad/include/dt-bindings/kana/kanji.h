/*
 * 漢字入力 (&kanji_key) の漢字の番号。
 * このファイルは docs/tools/gen_kanji.py が docs/google-ime/kanji.tsv から作る。直接書き換えないこと。
 * keymap では `&kanji_key KJ_…` のように使う。どの番号がどの漢字になるかは、PC 側のローマ字テーブル
 * （docs/google-ime/romantable.txt）で決まる（キーボードは番号の符号「;」＋英字 2 文字を送るだけ）。
 */

#pragma once

/* 番号の上限（符号が英字 2 文字なので 26 × 26 - 1。0 は使わない） */
#define KJ_MAX_ID 675

#define KJ_AU 1 /* 会（;ab） 単打面 あう */
#define KJ_BU 2 /* 部（;ac） 単打面 ぶ */
#define KJ_BUN 3 /* 分（;ad） 単打面 ぶん */
#define KJ_DERU 4 /* 出（;ae） 単打面 でる */
#define KJ_HAIRU 5 /* 入（;af） 単打面 はいる */
#define KJ_HANASU 6 /* 話（;ag） 単打面 はなす */
#define KJ_HITO 7 /* 人（;ah） 単打面 ひと */
#define KJ_HOU 8 /* 方（;ai） 単打面 ほう */
#define KJ_I 9 /* 意（;aj） 単打面 い */
#define KJ_IU 10 /* 言（;ak） 単打面 いう */
#define KJ_JI 11 /* 字（;al） 単打面 じ */
#define KJ_KAKU 12 /* 書（;am） 単打面 かく */
#define KJ_KAN 13 /* 漢（;an） 単打面 かん */
#define KJ_KANGAERU 14 /* 考（;ao） 単打面 かんがえる */
#define KJ_KANJIRU 15 /* 感（;ap） 単打面 かんじる */
#define KJ_KAZE 16 /* 風（;aq） 単打面 かぜ */
#define KJ_KI 17 /* 気（;ar） 単打面 き */
#define KJ_KURU 18 /* 来（;as） 単打面 くる */
#define KJ_MIRU 19 /* 見（;at） 単打面 みる */
#define KJ_MIZUKARA 20 /* 自（;au） 単打面 みずから */
#define KJ_OBOERU 21 /* 覚（;av） 単打面 おぼえる */
#define KJ_OMOU 22 /* 思（;aw） 単打面 おもう */
#define KJ_SHIRU 23 /* 知（;ax） 単打面 しる */
#define KJ_TEKI 24 /* 的（;ay） 単打面 てき */
#define KJ_TORU 25 /* 取（;az） 単打面 とる */
#define KJ_UTSU 26 /* 打（;ba） 単打面 うつ */
#define KJ_YOI 27 /* 良（;bb） 単打面 よい */
#define KJ_ZEN 28 /* 全（;bc） 単打面 ぜん */
#define KJ_AIDA 101 /* 間（;dx） シフト面 あいだ */
#define KJ_AJI 102 /* 味（;dy） シフト面 あじ */
#define KJ_AKA 103 /* 赤（;dz） シフト面 あか */
#define KJ_AKARUI 104 /* 明（;ea） シフト面 あかるい */
#define KJ_AO 105 /* 青（;eb） シフト面 あお */
#define KJ_BA 106 /* 場（;ec） シフト面 ば */
#define KJ_DAI 107 /* 大（;ed） シフト面 だい */
#define KJ_EGAKU 108 /* 描（;ee） シフト面 えがく */
#define KJ_FUKAI 109 /* 深（;ef） シフト面 ふかい */
#define KJ_GIN 110 /* 銀（;eg） シフト面 ぎん */
#define KJ_GOU 111 /* 合（;eh） シフト面 ごう */
#define KJ_HAJIME 112 /* 初（;ei） シフト面 はじめ */
#define KJ_HAJIMERU 113 /* 始（;ej） シフト面 はじめる */
#define KJ_HAKARU 114 /* 計（;ek） シフト面 はかる */
#define KJ_HANARERU 115 /* 離（;el） シフト面 はなれる */
#define KJ_HATSU 116 /* 発（;em） シフト面 はつ */
#define KJ_HAYAI 117 /* 早（;en） シフト面 はやい */
#define KJ_HEN 118 /* 変（;eo） シフト面 へん */
#define KJ_HON 119 /* 本（;ep） シフト面 ほん */
#define KJ_IKU 120 /* 行（;eq） シフト面 いく */
#define KJ_IRO 121 /* 色（;er） シフト面 いろ */
#define KJ_KAI 122 /* 回（;es） シフト面 かい */
#define KJ_KARADA 123 /* 体（;et） シフト面 からだ */
#define KJ_KEI 124 /* 系（;eu） シフト面 けい */
#define KJ_KIKU 125 /* 聞（;ev） シフト面 きく */
#define KJ_KIMERU 126 /* 決（;ew） シフト面 きめる */
#define KJ_KIN 127 /* 金（;ex） シフト面 きん */
#define KJ_KO 128 /* 個（;ey） シフト面 こ */
#define KJ_KOTO 129 /* 事（;ez） シフト面 こと */
#define KJ_KOU 130 /* 効（;fa） シフト面 こう */
#define KJ_KURAI 131 /* 暗（;fb） シフト面 くらい */
#define KJ_KURO 132 /* 黒（;fc） シフト面 くろ */
#define KJ_MEN 133 /* 面（;fd） シフト面 めん */
#define KJ_NAN 134 /* 難（;fe） シフト面 なん */
#define KJ_NANI 135 /* 何（;ff） シフト面 なに */
#define KJ_NARERU 136 /* 慣（;fg） シフト面 なれる */
#define KJ_NOMA 137 /* 々（;fh） シフト面 のま（踊り字） */
#define KJ_OKIRU 138 /* 起（;fi） シフト面 おきる */
#define KJ_OOI 139 /* 多（;fj） シフト面 おおい */
#define KJ_SADAMERU 140 /* 定（;fk） シフト面 さだめる */
#define KJ_SATSU 141 /* 撮（;fl） シフト面 さつ */
#define KJ_SENBUN 142 /* 線（;fm） シフト面 せん */
#define KJ_SHA 143 /* 者（;fn） シフト面 しゃ */
#define KJ_SHIRO 144 /* 白（;fo） シフト面 しろ */
#define KJ_SOKU 145 /* 速（;fp） シフト面 そく */
#define KJ_TAI 146 /* 対（;fq） シフト面 たい */
#define KJ_TE 147 /* 手（;fr） シフト面 て */
#define KJ_TEN 148 /* 点（;fs） シフト面 てん */
#define KJ_TOKI 149 /* 時（;ft） シフト面 とき */
#define KJ_TOKORO 150 /* 所（;fu） シフト面 ところ */
#define KJ_TOKU 151 /* 解（;fv） シフト面 とく */
#define KJ_TSUKAU 152 /* 使（;fw） シフト面 つかう */
#define KJ_TSUMERU 153 /* 詰（;fx） シフト面 つめる */
#define KJ_UGOKU 154 /* 動（;fy） シフト面 うごく */
#define KJ_UN 155 /* 運（;fz） シフト面 うん */
#define KJ_WARAU 156 /* 笑（;ga） シフト面 わらう */
#define KJ_WARUI 157 /* 悪（;gb） シフト面 わるい */
#define KJ_YOU 158 /* 用（;gc） シフト面 よう */
#define KJ_SOTO 201 /* 外（;ht） 機能の面 そと */
#define KJ_NOBORU 202 /* 昇（;hu） 機能の面 のぼる */
#define KJ_UE 203 /* 上（;hv） 機能の面 うえ */
#define KJ_MAE 204 /* 前（;hw） 機能の面 まえ */
#define KJ_UCHI 205 /* 内（;hx） 機能の面 うち */
#define KJ_KAZU 206 /* 数（;hy） 機能の面 かず */
#define KJ_NEN 207 /* 年（;hz） 機能の面 ねん */
#define KJ_OWARU 208 /* 終（;ia） 機能の面 おわる */
#define KJ_HIDARI 209 /* 左（;ib） 機能の面 ひだり */
#define KJ_MOTTE 210 /* 以（;ic） 機能の面 もって */
#define KJ_MIGI 211 /* 右（;id） 機能の面 みぎ */
#define KJ_IMA 212 /* 今（;ie） 機能の面 いま */
#define KJ_SAI 213 /* 歳（;if） 機能の面 さい */
#define KJ_TSUKI 214 /* 月（;ig） 機能の面 つき */
#define KJ_TSUGI 215 /* 次（;ih） 機能の面 つぎ */
#define KJ_ORIRU 216 /* 降（;ii） 機能の面 おりる */
#define KJ_SHITA 217 /* 下（;ij） 機能の面 した */
#define KJ_ATO 218 /* 後（;ik） 機能の面 あと */
#define KJ_SAKI 219 /* 先（;il） 機能の面 さき */
#define KJ_HI 220 /* 日（;im） 機能の面 ひ */
#define KJ_NUM0 221 /* ０（;in） 機能の面 ぜろ（全角） */
#define KJ_NUM1 222 /* １（;io） 機能の面 いち（全角） */
#define KJ_NUM2 223 /* ２（;ip） 機能の面 に（全角） */
#define KJ_NUM3 224 /* ３（;iq） 機能の面 さん（全角） */
#define KJ_NUM4 225 /* ４（;ir） 機能の面 よん（全角） */
#define KJ_NUM5 226 /* ５（;is） 機能の面 ご（全角） */
#define KJ_NUM6 227 /* ６（;it） 機能の面 ろく（全角） */
#define KJ_NUM7 228 /* ７（;iu） 機能の面 なな（全角） */
#define KJ_NUM8 229 /* ８（;iv） 機能の面 はち（全角） */
#define KJ_NUM9 230 /* ９（;iw） 機能の面 きゅう（全角） */
#define KJ_SUSUMU 231 /* 進（;ix） 機能の面 すすむ */
#define KJ_TOOI 232 /* 遠（;iy） 機能の面 とおい */
#define KJ_TOMARU 233 /* 止（;iz） 機能の面 とまる */
#define KJ_ONAJI 234 /* 同（;ja） 機能の面 おなじ */
#define KJ_TAKAI 235 /* 高（;jb） 機能の面 たかい */
#define KJ_HIKUI 236 /* 低（;jc） 機能の面 ひくい */
#define KJ_TEI 237 /* 停（;jd） 機能の面 てい */
#define KJ_CHIISAI 238 /* 小（;je） 機能の面 ちいさい */
#define KJ_NAKA 239 /* 中（;jf） 機能の面 なか */
#define KJ_MOTTOMO 240 /* 最（;jg） 機能の面 もっとも */
#define KJ_KOTONARU 241 /* 異（;jh） 機能の面 ことなる */
#define KJ_SHIRIZOKU 242 /* 退（;ji） 機能の面 しりぞく */
#define KJ_CHIKAI 243 /* 近（;jj） 機能の面 ちかい */
#define KJ_KOKORO 244 /* 心（;jk） 機能の面 こころ */
#define KJ_CHIGAU 245 /* 違（;jl） 機能の面 ちがう */
#define KJ_REI 246 /* 〇（;jm） 機能の面 れい */
#define KJ_ICHI 247 /* 一（;jn） 機能の面 いち */
#define KJ_NI 248 /* 二（;jo） 機能の面 に */
#define KJ_SAN 249 /* 三（;jp） 機能の面 さん */
#define KJ_YON 250 /* 四（;jq） 機能の面 よん */
#define KJ_GO 251 /* 五（;jr） 機能の面 ご */
#define KJ_ROKU 252 /* 六（;js） 機能の面 ろく */
#define KJ_NANA 253 /* 七（;jt） 機能の面 なな */
#define KJ_HACHI 254 /* 八（;ju） 機能の面 はち */
#define KJ_KYUU 255 /* 九（;jv） 機能の面 きゅう */
#define KJ_JUU 256 /* 十（;jw） 機能の面 じゅう */
#define KJ_HYAKU 257 /* 百（;jx） 機能の面 ひゃく */
#define KJ_SEN 258 /* 千（;jy） 機能の面 せん */
#define KJ_MAN 259 /* 万（;jz） 機能の面 まん */
#define KJ_OKU 260 /* 億（;ka） 機能の面 おく */

/* 表にある漢字の数と、名前・出力の一覧（behavior_kanji.c が ZMK Studio の一覧を作るのに使う。Noxrix も読む）。
 * 出力を変えただけなら、ファームウェアを書き換えなくても入る漢字は変わるが、Studio に出る文字は書き換えるまで前のまま */
#define KJ_COUNT 146
#define KANJI_LIST(ENTRY) \
    ENTRY(KJ_AU, "会") \
    ENTRY(KJ_BU, "部") \
    ENTRY(KJ_BUN, "分") \
    ENTRY(KJ_DERU, "出") \
    ENTRY(KJ_HAIRU, "入") \
    ENTRY(KJ_HANASU, "話") \
    ENTRY(KJ_HITO, "人") \
    ENTRY(KJ_HOU, "方") \
    ENTRY(KJ_I, "意") \
    ENTRY(KJ_IU, "言") \
    ENTRY(KJ_JI, "字") \
    ENTRY(KJ_KAKU, "書") \
    ENTRY(KJ_KAN, "漢") \
    ENTRY(KJ_KANGAERU, "考") \
    ENTRY(KJ_KANJIRU, "感") \
    ENTRY(KJ_KAZE, "風") \
    ENTRY(KJ_KI, "気") \
    ENTRY(KJ_KURU, "来") \
    ENTRY(KJ_MIRU, "見") \
    ENTRY(KJ_MIZUKARA, "自") \
    ENTRY(KJ_OBOERU, "覚") \
    ENTRY(KJ_OMOU, "思") \
    ENTRY(KJ_SHIRU, "知") \
    ENTRY(KJ_TEKI, "的") \
    ENTRY(KJ_TORU, "取") \
    ENTRY(KJ_UTSU, "打") \
    ENTRY(KJ_YOI, "良") \
    ENTRY(KJ_ZEN, "全") \
    ENTRY(KJ_AIDA, "間") \
    ENTRY(KJ_AJI, "味") \
    ENTRY(KJ_AKA, "赤") \
    ENTRY(KJ_AKARUI, "明") \
    ENTRY(KJ_AO, "青") \
    ENTRY(KJ_BA, "場") \
    ENTRY(KJ_DAI, "大") \
    ENTRY(KJ_EGAKU, "描") \
    ENTRY(KJ_FUKAI, "深") \
    ENTRY(KJ_GIN, "銀") \
    ENTRY(KJ_GOU, "合") \
    ENTRY(KJ_HAJIME, "初") \
    ENTRY(KJ_HAJIMERU, "始") \
    ENTRY(KJ_HAKARU, "計") \
    ENTRY(KJ_HANARERU, "離") \
    ENTRY(KJ_HATSU, "発") \
    ENTRY(KJ_HAYAI, "早") \
    ENTRY(KJ_HEN, "変") \
    ENTRY(KJ_HON, "本") \
    ENTRY(KJ_IKU, "行") \
    ENTRY(KJ_IRO, "色") \
    ENTRY(KJ_KAI, "回") \
    ENTRY(KJ_KARADA, "体") \
    ENTRY(KJ_KEI, "系") \
    ENTRY(KJ_KIKU, "聞") \
    ENTRY(KJ_KIMERU, "決") \
    ENTRY(KJ_KIN, "金") \
    ENTRY(KJ_KO, "個") \
    ENTRY(KJ_KOTO, "事") \
    ENTRY(KJ_KOU, "効") \
    ENTRY(KJ_KURAI, "暗") \
    ENTRY(KJ_KURO, "黒") \
    ENTRY(KJ_MEN, "面") \
    ENTRY(KJ_NAN, "難") \
    ENTRY(KJ_NANI, "何") \
    ENTRY(KJ_NARERU, "慣") \
    ENTRY(KJ_NOMA, "々") \
    ENTRY(KJ_OKIRU, "起") \
    ENTRY(KJ_OOI, "多") \
    ENTRY(KJ_SADAMERU, "定") \
    ENTRY(KJ_SATSU, "撮") \
    ENTRY(KJ_SENBUN, "線") \
    ENTRY(KJ_SHA, "者") \
    ENTRY(KJ_SHIRO, "白") \
    ENTRY(KJ_SOKU, "速") \
    ENTRY(KJ_TAI, "対") \
    ENTRY(KJ_TE, "手") \
    ENTRY(KJ_TEN, "点") \
    ENTRY(KJ_TOKI, "時") \
    ENTRY(KJ_TOKORO, "所") \
    ENTRY(KJ_TOKU, "解") \
    ENTRY(KJ_TSUKAU, "使") \
    ENTRY(KJ_TSUMERU, "詰") \
    ENTRY(KJ_UGOKU, "動") \
    ENTRY(KJ_UN, "運") \
    ENTRY(KJ_WARAU, "笑") \
    ENTRY(KJ_WARUI, "悪") \
    ENTRY(KJ_YOU, "用") \
    ENTRY(KJ_SOTO, "外") \
    ENTRY(KJ_NOBORU, "昇") \
    ENTRY(KJ_UE, "上") \
    ENTRY(KJ_MAE, "前") \
    ENTRY(KJ_UCHI, "内") \
    ENTRY(KJ_KAZU, "数") \
    ENTRY(KJ_NEN, "年") \
    ENTRY(KJ_OWARU, "終") \
    ENTRY(KJ_HIDARI, "左") \
    ENTRY(KJ_MOTTE, "以") \
    ENTRY(KJ_MIGI, "右") \
    ENTRY(KJ_IMA, "今") \
    ENTRY(KJ_SAI, "歳") \
    ENTRY(KJ_TSUKI, "月") \
    ENTRY(KJ_TSUGI, "次") \
    ENTRY(KJ_ORIRU, "降") \
    ENTRY(KJ_SHITA, "下") \
    ENTRY(KJ_ATO, "後") \
    ENTRY(KJ_SAKI, "先") \
    ENTRY(KJ_HI, "日") \
    ENTRY(KJ_NUM0, "０") \
    ENTRY(KJ_NUM1, "１") \
    ENTRY(KJ_NUM2, "２") \
    ENTRY(KJ_NUM3, "３") \
    ENTRY(KJ_NUM4, "４") \
    ENTRY(KJ_NUM5, "５") \
    ENTRY(KJ_NUM6, "６") \
    ENTRY(KJ_NUM7, "７") \
    ENTRY(KJ_NUM8, "８") \
    ENTRY(KJ_NUM9, "９") \
    ENTRY(KJ_SUSUMU, "進") \
    ENTRY(KJ_TOOI, "遠") \
    ENTRY(KJ_TOMARU, "止") \
    ENTRY(KJ_ONAJI, "同") \
    ENTRY(KJ_TAKAI, "高") \
    ENTRY(KJ_HIKUI, "低") \
    ENTRY(KJ_TEI, "停") \
    ENTRY(KJ_CHIISAI, "小") \
    ENTRY(KJ_NAKA, "中") \
    ENTRY(KJ_MOTTOMO, "最") \
    ENTRY(KJ_KOTONARU, "異") \
    ENTRY(KJ_SHIRIZOKU, "退") \
    ENTRY(KJ_CHIKAI, "近") \
    ENTRY(KJ_KOKORO, "心") \
    ENTRY(KJ_CHIGAU, "違") \
    ENTRY(KJ_REI, "〇") \
    ENTRY(KJ_ICHI, "一") \
    ENTRY(KJ_NI, "二") \
    ENTRY(KJ_SAN, "三") \
    ENTRY(KJ_YON, "四") \
    ENTRY(KJ_GO, "五") \
    ENTRY(KJ_ROKU, "六") \
    ENTRY(KJ_NANA, "七") \
    ENTRY(KJ_HACHI, "八") \
    ENTRY(KJ_KYUU, "九") \
    ENTRY(KJ_JUU, "十") \
    ENTRY(KJ_HYAKU, "百") \
    ENTRY(KJ_SEN, "千") \
    ENTRY(KJ_MAN, "万") \
    ENTRY(KJ_OKU, "億")
