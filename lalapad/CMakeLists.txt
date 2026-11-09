# このリポジトリの自作モジュール（かな入力・記号キー・漢字入力、マウスボタン・キーの押しっぱなし切り替え、カーソル低速、
# オートマウスレイヤー、レイヤーの固定、Num Word、Alt コード入力、ウィンドウの切り替え、スクロールスナップの切り替え、
# トラックパッドをキー操作に変えるモード、キー位置の送信、
# トラックパッドのキー位置の出来事の中継）。
# behavior と入力処理は keymap を処理する側（central）でだけビルドする（Kconfig の LALAPAD_CENTRAL で、
# 右＝子機では各シンボルが n になる）。
target_include_directories(app PRIVATE include)

# 右（子機）とのつながりの状態（オートマウス・tp_keys・キー位置の中継が使う）
target_sources_ifdef(CONFIG_LALAPAD_CENTRAL app PRIVATE src/split_link.c)

# 最初に足す: 入力スレッドから出たキー位置の出来事を預かり、ほかのモジュール（と ZMK 本体）の処理を
# システムワークキューで行わせるため（listener はリンクの順に呼ばれる）
target_sources_ifdef(CONFIG_ZMK_POSITION_RELAY app PRIVATE src/position_relay.c)
if(CONFIG_ZMK_BEHAVIOR_KANA)
  target_sources(app PRIVATE
    src/kana_output.c
    src/behavior_kana.c
    src/behavior_kana_seq.c
    src/behavior_kana_mode.c
    src/behavior_kana_sync.c
    src/behavior_kanji.c
    src/kanji_chord.c
  )
endif()
target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_MOUSE_BUTTON_TOGGLE app PRIVATE
  src/behavior_mouse_button_toggle.c
)
target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_KEY_LOCK app PRIVATE
  src/behavior_key_lock.c
)
# ウィンドウの切り替え・Num Word は、Alt コードより前に足す: Alt コードは送っている間のキー入力を預かり、送り終えて
# から自分の listener の次から通し直すので、後ろにあると、預けられた入力を（自分が送ったものも含めて）後から見て、
# 届いた時点と違う状態で判定してしまう。前にあれば、どの入力も届いた時点で見る（Alt コードが自分で送るキーは、
# ZMK 本体の hold-tap・sticky key より後から流すので、自作モジュールの listener には届かない。どちらもないビルドでは
# alt_code_is_own_event で見分ける）
target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_ALT_TAB app PRIVATE
  src/behavior_alt_tab.c
)
target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_WORD_LAYER app PRIVATE
  src/behavior_word_layer.c
)
target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_ALT_CODE app PRIVATE
  src/behavior_alt_code.c
)
target_sources_ifdef(CONFIG_ZMK_AUTO_MOUSE_LAYER app PRIVATE
  src/input_processor_auto_mouse_layer.c
)
target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_LAYER_LOCK app PRIVATE
  src/behavior_layer_lock.c
)
target_sources_ifdef(CONFIG_ZMK_SCROLL_SNAP_GATE app PRIVATE
  src/behavior_scroll_snap_hold.c
  src/input_processor_scroll_snap_gate.c
)
target_sources_ifdef(CONFIG_ZMK_CURSOR_SLOW app PRIVATE
  src/behavior_cursor_slow.c
  src/input_processor_cursor_slow.c
)
target_sources_ifdef(CONFIG_ZMK_TP_KEYS app PRIVATE
  src/tp_keys.c
  src/behavior_tp_keys.c
  src/behavior_tp_stroke.c
)
target_sources_ifdef(CONFIG_ZMK_KEY_STREAM app PRIVATE src/key_stream.c)
target_sources_ifdef(CONFIG_ZMK_KEY_STREAM_USB app PRIVATE src/key_stream_usb.c)
target_sources_ifdef(CONFIG_ZMK_KEY_STREAM_BLE app PRIVATE src/key_stream_ble.c)
