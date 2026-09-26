# かな入力モジュール。behavior は keymap を処理する側（central）でだけビルドする。
if(CONFIG_ZMK_BEHAVIOR_KANA)
  target_include_directories(app PRIVATE include)

  if((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    target_sources(app PRIVATE src/behavior_kana.c)
  endif()
endif()
