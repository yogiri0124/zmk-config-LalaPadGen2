# このリポジトリの自作モジュール（かな入力、マウスボタンの押しっぱなし切り替え、カーソル低速）。
# behavior は keymap を処理する側（central）でだけビルドする。
if(CONFIG_ZMK_BEHAVIOR_KANA OR CONFIG_ZMK_CURSOR_SLOW)
  target_include_directories(app PRIVATE include)
endif()

if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  if(CONFIG_ZMK_BEHAVIOR_KANA)
    target_sources(app PRIVATE
      src/kana_output.c
      src/behavior_kana.c
      src/behavior_kana_mode.c
      src/behavior_kana_sync.c
    )
  endif()
  target_sources_ifdef(CONFIG_ZMK_BEHAVIOR_MOUSE_BUTTON_TOGGLE app PRIVATE
    src/behavior_mouse_button_toggle.c
  )
  target_sources_ifdef(CONFIG_ZMK_CURSOR_SLOW app PRIVATE
    src/behavior_cursor_slow.c
    src/input_processor_cursor_slow.c
  )
endif()
