# このリポジトリの自作モジュール（かな入力・記号キー、マウスボタン・キーの押しっぱなし切り替え、カーソル低速、
# オートマウスレイヤー、Alt コード入力、スクロールスナップの切り替え、キー位置の送信、右が切れたときのトラックパッドの解放）。
# behavior と入力処理は keymap を処理する側（central）でだけビルドする。
target_include_directories(app PRIVATE include)

if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  if(CONFIG_ZMK_BEHAVIOR_KANA)
    target_sources(app PRIVATE
      src/kana_output.c
      src/behavior_kana.c
      src/behavior_kana_seq.c
      src/behavior_kana_mode.c
      src/behavior_kana_sync.c
    )
  endif()
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_MOUSE_BUTTON_TOGGLE app PRIVATE
    src/behavior_mouse_button_toggle.c
  )
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_KEY_LOCK app PRIVATE
    src/behavior_key_lock.c
  )
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_ALT_CODE app PRIVATE
    src/behavior_alt_code.c
  )
  target_sources_ifdef(CONFIG_ZMK_AUTO_MOUSE_LAYER app PRIVATE
    src/input_processor_auto_mouse_layer.c
  )
  target_sources_ifdef(CONFIG_ZMK_SCROLL_SNAP_GATE app PRIVATE
    src/behavior_scroll_snap_hold.c
    src/input_processor_scroll_snap_gate.c
  )
  target_sources_ifdef(CONFIG_ZMK_CURSOR_SLOW app PRIVATE
    src/behavior_cursor_slow.c
    src/input_processor_cursor_slow.c
  )
  target_sources_ifdef(CONFIG_ZMK_SPLIT_TRACKPAD_RELEASE app PRIVATE src/split_trackpad_release.c)
  target_sources_ifdef(CONFIG_ZMK_KEY_STREAM app PRIVATE src/key_stream.c)
  target_sources_ifdef(CONFIG_ZMK_KEY_STREAM_USB app PRIVATE src/key_stream_usb.c)
  target_sources_ifdef(CONFIG_ZMK_KEY_STREAM_BLE app PRIVATE src/key_stream_ble.c)
endif()
