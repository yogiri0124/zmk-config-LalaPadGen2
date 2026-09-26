# ZMK キーコード一覧（ZMK v0.3.0）

`&kp` / `&sk` / `&kt` / `&mt` の引数に使える名前の一覧。**このリポジトリが固定している ZMK v0.3.0** の
[`app/include/dt-bindings/zmk/keys.h`](https://github.com/zmkfirmware/zmk/blob/v0.3.0/app/include/dt-bindings/zmk/keys.h)（MIT License）から機械的に生成した。
説明は同ファイルのコメント（英語、USB HID の用語）をそのまま使っている。OS ごとの対応状況は公式の
[List of Keycodes](https://zmk.dev/docs/keymaps/list-of-keycodes) を参照。

- 同じ行の名前はすべて同じキー（別名）。ファイル内の既存の書き方に合わせる。
- `(非推奨)` が付いた名前は keys.h で `DEPRECATED (DO NOT USE)` とされているもの。v0.3.0 ではまだ使えるが、新しく書くときは使わない。
- ここにない名前は存在しない（ビルドエラーになる）。v0.3.0 より新しい版で追加された名前は使えない。

## 修飾キー関数

キーコードを修飾キー付きにする：`LC()` 左Ctrl、`LS()` 左Shift、`LA()` 左Alt、`LG()` 左GUI(Win/Cmd)、
`RC()` `RS()` `RA()` `RG()` は右側。入れ子可：`&kp LC(LS(T))` ＝ Ctrl+Shift+T。

## 日本語環境（JIS 配列ホスト）での注意

ZMK は **キーの位置（HID usage）** を送り、どの文字になるかはホスト OS のキーボード配列で決まる。
JIS 配列として認識されている PC では、US 配列の名前と出る文字が一致しないことがある。

| 名前 | JIS 配列ホストでの意味（Windows の例） |
| --- | --- |
| `LANGUAGE_1` (`LANG1`) | かな |
| `LANGUAGE_2` (`LANG2`) | 英数 |
| `INTERNATIONAL_1` (`INT1`) | ろ（`\` と `_`） |
| `INTERNATIONAL_2` (`INT2`) | カタカナ/ひらがな |
| `INTERNATIONAL_3` (`INT3`) | `¥` と縦棒 |
| `INTERNATIONAL_4` (`INT4`) | 変換 |
| `INTERNATIONAL_5` (`INT5`) | 無変換 |
| `GRAVE` | 半角/全角 |

- 例：`&kp AT`（US の Shift+2）は、JIS 配列ホストでは `"` になる。記号キーを変更する依頼では、
  ホストが US / JIS のどちらで認識しているかを確認事項として報告する。
- 上表は一般的な Windows/JIS の挙動。OS・IME 設定で変わるため、実機で確認すること。

## 一覧

| 説明 (keys.h のコメント) | 名前（別名） |
| --- | --- |
| System Power Down | `SYSTEM_POWER` `SYS_PWR` |
| System Sleep | `SYSTEM_SLEEP` `SYS_SLEEP` |
| System Wake Up | `SYSTEM_WAKE_UP` `SYS_WAKE` |
| Keyboard a and A | `A` |
| Keyboard b and B | `B` |
| Keyboard c and C | `C` |
| Keyboard d and D | `D` |
| Keyboard e and E | `E` |
| Keyboard f and F | `F` |
| Keyboard g and G | `G` |
| Keyboard h and H | `H` |
| Keyboard i and I | `I` |
| Keyboard j and J | `J` |
| Keyboard k and K | `K` |
| Keyboard l and L | `L` |
| Keyboard m and M | `M` |
| Keyboard n and N | `N` |
| Keyboard o and O | `O` |
| Keyboard p and P | `P` |
| Keyboard q and Q | `Q` |
| Keyboard r and R | `R` |
| Keyboard s and S | `S` |
| Keyboard t and T | `T` |
| Keyboard u and U | `U` |
| Keyboard v and V | `V` |
| Keyboard w and W | `W` |
| Keyboard x and X | `X` |
| Keyboard y and Y | `Y` |
| Keyboard z and Z | `Z` |
| Keyboard 1 and ! (Exclamation) | `NUMBER_1` `N1` `NUM_1` (非推奨) |
| Keyboard ! (Exclamation) | `EXCLAMATION` `EXCL` `BANG` (非推奨) |
| Keyboard 2 and @ (At sign) | `NUMBER_2` `N2` `NUM_2` (非推奨) |
| Keyboard @ (At sign) | `AT_SIGN` `AT` `ATSN` (非推奨) |
| Keyboard 3 and # (Hash/Number) | `NUMBER_3` `N3` `NUM_3` (非推奨) |
| Keyboard # (Hash/Number) | `HASH` `POUND` |
| Keyboard 4 and $ (Dollar) | `NUMBER_4` `N4` `NUM_4` (非推奨) |
| Keyboard $ (Dollar) | `DOLLAR` `DLLR` |
| Keyboard 5 and % (Percent) | `NUMBER_5` `N5` `NUM_5` (非推奨) |
| Keyboard % (Percent) | `PERCENT` `PRCNT` `PRCT` (非推奨) |
| Keyboard 6 and ^ (Caret) | `NUMBER_6` `N6` `NUM_6` (非推奨) |
| Keyboard ^ (Caret) | `CARET` `CRRT` (非推奨) |
| Keyboard 7 and & (Ampersand) | `NUMBER_7` `N7` `NUM_7` (非推奨) |
| Keyboard & (Ampersand) | `AMPERSAND` `AMPS` |
| Keyboard 8 and * (Asterisk) | `NUMBER_8` `N8` `NUM_8` (非推奨) |
| Keyboard * (Asterisk) | `ASTERISK` `ASTRK` `STAR` |
| Keyboard 9 and ( (Left Parenthesis) | `NUMBER_9` `N9` `NUM_9` (非推奨) |
| Keyboard ( (Left Parenthesis) | `LEFT_PARENTHESIS` `LPAR` `LPRN` (非推奨) |
| Keyboard 0 and ) (Right Parenthesis) | `NUMBER_0` `N0` `NUM_0` (非推奨) |
| Keyboard ) (Right Parenthesis) | `RIGHT_PARENTHESIS` `RPAR` `RPRN` (非推奨) |
| Keyboard Return (Enter) | `RETURN` `ENTER` `RET` |
| Keyboard Escape | `ESCAPE` `ESC` |
| Keyboard Backspace | `BACKSPACE` `BSPC` `BKSP` (非推奨) |
| Keyboard Tab | `TAB` |
| Keyboard Space | `SPACE` `SPC` (非推奨) |
| Keyboard - and _ (Minus and Underscore) | `MINUS` |
| Keyboard _ (Underscore) | `UNDERSCORE` `UNDER` |
| Keyboard = and + (Equal and Plus) | `EQUAL` `EQL` (非推奨) |
| Keyboard + (Plus) | `PLUS` |
| Keyboard [ and { (Left Bracket and Left Brace) | `LEFT_BRACKET` `LBKT` |
| Keyboard { (Left Brace) | `LEFT_BRACE` `LBRC` `LCUR` (非推奨) |
| Keyboard ] and } (Right Bracket and Right Brace) | `RIGHT_BRACKET` `RBKT` |
| Keyboard } (Right Brace) | `RIGHT_BRACE` `RBRC` `RCUR` (非推奨) |
| Keyboard \ and \| (Backslash and Pipe) | `BACKSLASH` `BSLH` |
| Keyboard \| (Pipe) | `PIPE` |
| Keyboard Non-US # and ~ (Non-US Hash/Number and Tilde) | `NON_US_HASH` `NUHS` |
| Keyboard ~ (Tilde) | `TILDE2` |
| Keyboard ; and : (Semicolon and Colon) | `SEMICOLON` `SEMI` `SCLN` (非推奨) |
| Keyboard : (Colon) | `COLON` `COLN` (非推奨) |
| Keyboard ' and " (Apostrophe and Quote) | `SINGLE_QUOTE` `SQT` `APOSTROPHE` `APOS` `QUOT` (非推奨) |
| Keyboard " (Quote) | `DOUBLE_QUOTES` `DQT` |
| Keyboard \` and ~ (Grave Accent and Tilde) | `GRAVE` `GRAV` (非推奨) |
| Keyboard ~ (Tilde) | `TILDE` `TILD` (非推奨) |
| Keyboard , and < (Comma and Less Than) | `COMMA` `CMMA` (非推奨) |
| Keyboard < (Less Than) | `LESS_THAN` `LT` `LABT` (非推奨) |
| Keyboard . and > (Period and Greater Than) | `PERIOD` `DOT` |
| Keyboard > (Greater Than) | `GREATER_THAN` `GT` `RABT` (非推奨) |
| Keyboard / and ? (Forward Slash and Question) | `SLASH` `FSLH` |
| Keyboard ? (Question) | `QUESTION` `QMARK` |
| Keyboard Caps Lock | `CAPSLOCK` `CAPS` `CLCK` |
| Keyboard F1 | `F1` |
| Keyboard F2 | `F2` |
| Keyboard F3 | `F3` |
| Keyboard F4 | `F4` |
| Keyboard F5 | `F5` |
| Keyboard F6 | `F6` |
| Keyboard F7 | `F7` |
| Keyboard F8 | `F8` |
| Keyboard F9 | `F9` |
| Keyboard F10 | `F10` |
| Keyboard F11 | `F11` |
| Keyboard F12 | `F12` |
| Keyboard Print Screen | `PRINTSCREEN` `PSCRN` `PRSC` (非推奨) |
| Keyboard Scroll Lock | `SCROLLLOCK` `SLCK` `SCLK` (非推奨) |
| Keyboard Pause/Break | `PAUSE_BREAK` `PAUS` (非推奨) |
| Keyboard Insert | `INSERT` `INS` |
| Keyboard Home | `HOME` |
| Keyboard Page Up | `PAGE_UP` `PG_UP` `PGUP` (非推奨) |
| Keyboard Delete | `DELETE` `DEL` |
| Keyboard End | `END` |
| Keyboard Page Down | `PAGE_DOWN` `PG_DN` `PGDN` (非推奨) |
| Keyboard Right Arrow | `RIGHT_ARROW` `RIGHT` `RARW` (非推奨) |
| Keyboard Left Arrow | `LEFT_ARROW` `LEFT` `LARW` (非推奨) |
| Keyboard Down Arrow | `DOWN_ARROW` `DOWN` `DARW` (非推奨) |
| Keyboard Up Arrow | `UP_ARROW` `UP` `UARW` (非推奨) |
| Keypad Numlock and Clear | `KP_NUMLOCK` `KP_NUM` `KP_NLCK` |
| Keypad Clear | `CLEAR2` |
| Keypad / (Slash/Divide) | `KP_DIVIDE` `KP_SLASH` `KDIV` (非推奨) |
| Keypad * (Multiply) | `KP_MULTIPLY` `KP_ASTERISK` `KMLT` (非推奨) |
| Keypad - (Minus) | `KP_MINUS` `KP_SUBTRACT` `KMIN` (非推奨) |
| Keypad + (Plus) | `KP_PLUS` `KPLS` (非推奨) |
| Keypad Enter | `KP_ENTER` |
| Keypad 1 | `KP_NUMBER_1` `KP_N1` |
| Keypad 2 | `KP_NUMBER_2` `KP_N2` |
| Keypad 3 | `KP_NUMBER_3` `KP_N3` |
| Keypad 4 | `KP_NUMBER_4` `KP_N4` |
| Keypad 5 | `KP_NUMBER_5` `KP_N5` |
| Keypad 6 | `KP_NUMBER_6` `KP_N6` |
| Keypad 7 | `KP_NUMBER_7` `KP_N7` |
| Keypad 8 | `KP_NUMBER_8` `KP_N8` |
| Keypad 9 | `KP_NUMBER_9` `KP_N9` |
| Keypad 0 | `KP_NUMBER_0` `KP_N0` |
| Keypad . (Dot) | `KP_DOT` |
| Keyboard Non-US \ and \| (Non-us Backslash and Pipe) | `NON_US_BACKSLASH` `NON_US_BSLH` `NUBS` |
| Keyboard Pipe | `PIPE2` |
| Keyboard Application (Context Menu) | `K_APPLICATION` `K_APP` `K_CONTEXT_MENU` `K_CMENU` `GUI` (非推奨) |
| Keyboard Power | `K_POWER` `K_PWR` |
| Keypad = (Equal) | `KP_EQUAL` |
| Keyboard F13 | `F13` |
| Keyboard F14 | `F14` |
| Keyboard F15 | `F15` |
| Keyboard F16 | `F16` |
| Keyboard F17 | `F17` |
| Keyboard F18 | `F18` |
| Keyboard F19 | `F19` |
| Keyboard F20 | `F20` |
| Keyboard F21 | `F21` |
| Keyboard F22 | `F22` |
| Keyboard F23 | `F23` |
| Keyboard F24 | `F24` |
| Keyboard Execute | `K_EXECUTE` `K_EXEC` |
| Keyboard Help | `K_HELP` |
| Keyboard Menu | `K_MENU` |
| Keyboard Select | `K_SELECT` |
| Keyboard Stop | `K_STOP` |
| Keyboard Again | `K_AGAIN` `K_REDO` |
| Keyboard Undo | `K_UNDO` `UNDO` (非推奨) |
| Keyboard Cut | `K_CUT` `CUT` (非推奨) |
| Keyboard Copy | `K_COPY` `COPY` (非推奨) |
| Keyboard Paste | `K_PASTE` `PSTE` (非推奨) |
| Keyboard Find | `K_FIND` |
| Keyboard Mute | `K_MUTE` |
| Keyboard Volume Up | `K_VOLUME_UP` `K_VOL_UP` `VOLU` (非推奨) |
| Keyboard Volume Down | `K_VOLUME_DOWN` `K_VOL_DN` `VOLD` (非推奨) |
| Keyboard Locking Caps Lock | `LOCKING_CAPS` `LCAPS` |
| Keyboard Locking Num Lock | `LOCKING_NUM` `LNLCK` |
| Keyboard Locking Scroll Lock | `LOCKING_SCROLL` `LSLCK` |
| Keypad , (Comma) | `KP_COMMA` |
| Keypad = (Equal) AS/400 | `KP_EQUAL_AS400` |
| Keyboard International 1 | `INTERNATIONAL_1` `INT1` `INT_RO` |
| Keyboard International 2 | `INTERNATIONAL_2` `INT2` `INT_KATAKANAHIRAGANA` `INT_KANA` |
| Keyboard International 3 | `INTERNATIONAL_3` `INT3` `INT_YEN` |
| Keyboard International 4 | `INTERNATIONAL_4` `INT4` `INT_HENKAN` |
| Keyboard International 5 | `INTERNATIONAL_5` `INT5` `INT_MUHENKAN` |
| Keyboard International 6 | `INTERNATIONAL_6` `INT6` `INT_KPJPCOMMA` |
| Keyboard International 7 | `INTERNATIONAL_7` `INT7` |
| Keyboard International 8 | `INTERNATIONAL_8` `INT8` |
| Keyboard International 9 | `INTERNATIONAL_9` `INT9` |
| Keyboard Language 1 | `LANGUAGE_1` `LANG1` `LANG_HANGEUL` |
| Keyboard Language 2 | `LANGUAGE_2` `LANG2` `LANG_HANJA` |
| Keyboard Language 3 | `LANGUAGE_3` `LANG3` `LANG_KATAKANA` |
| Keyboard Language 4 | `LANGUAGE_4` `LANG4` `LANG_HIRAGANA` |
| Keyboard Language 5 | `LANGUAGE_5` `LANG5` `LANG_ZENKAKUHANKAKU` |
| Keyboard Language 6 | `LANGUAGE_6` `LANG6` |
| Keyboard Language 7 | `LANGUAGE_7` `LANG7` |
| Keyboard Language 8 | `LANGUAGE_8` `LANG8` |
| Keyboard Language 9 | `LANGUAGE_9` `LANG9` |
| Keyboard Alternate Erase | `ALT_ERASE` |
| Keyboard SysReq/Attention | `SYSREQ` `ATTENTION` |
| Keyboard Cancel | `K_CANCEL` |
| Keyboard Clear | `CLEAR` |
| Keyboard Prior | `PRIOR` |
| Keyboard Return | `RETURN2` `RET2` |
| Keyboard Separator | `SEPARATOR` |
| Keyboard Out | `OUT` |
| Keyboard Oper | `OPER` |
| Keyboard Clear/Again | `CLEAR_AGAIN` |
| Keyboard CrSel/Props | `CRSEL` |
| Keyboard ExSel | `EXSEL` |
| Keyboard Currency Unit | `CURU` |
| Keypad ( (Left Parenthesis) | `KP_LEFT_PARENTHESIS` `KP_LPAR` |
| Keypad ) (Right Parenthesis) | `KP_RIGHT_PARENTHESIS` `KP_RPAR` |
| Keypad Space | `KSPC` |
| Keypad Clear | `KP_CLEAR` |
| Keyboard Left Control | `LEFT_CONTROL` `LCTRL` `LCTL` (非推奨) |
| Keyboard Left Shift | `LEFT_SHIFT` `LSHIFT` `LSHFT` `LSFT` (非推奨) |
| Keyboard Left Alt | `LEFT_ALT` `LALT` |
| Keyboard Left GUI (Windows / Command / Meta) | `LEFT_GUI` `LGUI` `LEFT_WIN` `LWIN` `LEFT_COMMAND` `LCMD` `LEFT_META` `LMETA` |
| Keyboard Right Control | `RIGHT_CONTROL` `RCTRL` `RCTL` (非推奨) |
| Keyboard Right Shift | `RIGHT_SHIFT` `RSHIFT` `RSHFT` `RSFT` (非推奨) |
| Keyboard Right Alt | `RIGHT_ALT` `RALT` |
| Keyboard Right GUI (Windows / Command / Meta) | `RIGHT_GUI` `RGUI` `RIGHT_WIN` `RWIN` `RIGHT_COMMAND` `RCMD` `RIGHT_META` `RMETA` |
| Keyboard Play/Pause | `K_PLAY_PAUSE` `K_PP` |
| Keyboard Stop | `K_STOP2` |
| Keyboard Previous | `K_PREVIOUS` `K_PREV` |
| Keyboard Next | `K_NEXT` |
| Keyboard Eject | `K_EJECT` |
| Keyboard Volume Up | `K_VOLUME_UP2` `K_VOL_UP2` |
| Keyboard Volume Down | `K_VOLUME_DOWN2` `K_VOL_DN2` |
| Keyboard Mute | `K_MUTE2` |
| Keyboard WWW | `K_WWW` |
| Keyboard Back | `K_BACK` |
| Keyboard Forward | `K_FORWARD` |
| Keyboard Stop | `K_STOP3` |
| Keyboard Find | `K_FIND2` |
| Keyboard Scroll Up | `K_SCROLL_UP` |
| Keyboard Scroll Down | `K_SCROLL_DOWN` |
| Keyboard Edit | `K_EDIT` |
| Keyboard Sleep | `K_SLEEP` |
| Keyboard Lock | `K_LOCK` `K_SCREENSAVER` `K_COFFEE` |
| Keyboard Refresh | `K_REFRESH` |
| Keyboard Calculator | `K_CALCULATOR` `K_CALC` |
| Consumer Power | `C_POWER` `C_PWR` |
| Consumer Reset | `C_RESET` |
| Consumer Sleep | `C_SLEEP` |
| Consumer Sleep Mode | `C_SLEEP_MODE` |
| Consumer Menu | `C_MENU` |
| Consumer Menu Pick | `C_MENU_PICK` `C_MENU_SELECT` |
| Consumer Menu Up | `C_MENU_UP` |
| Consumer Menu Down | `C_MENU_DOWN` |
| Consumer Menu Left | `C_MENU_LEFT` |
| Consumer Menu Right | `C_MENU_RIGHT` |
| Consumer Menu Escape | `C_MENU_ESCAPE` `C_MENU_ESC` |
| Consumer Menu Value Increase | `C_MENU_INCREASE` `C_MENU_INC` |
| Consumer Menu Value Decrease | `C_MENU_DECREASE` `C_MENU_DEC` |
| Consumer Data On Screen | `C_DATA_ON_SCREEN` |
| Consumer Closed Caption | `C_CAPTIONS` `C_SUBTITLES` |
| Consumer Snapshot | `C_SNAPSHOT` |
| Consumer Picture-in-Picture Toggle | `C_PIP` |
| Consumer Red Menu Button | `C_RED_BUTTON` `C_RED` |
| Consumer Green Menu Button | `C_GREEN_BUTTON` `C_GREEN` |
| Consumer Blue Menu Button | `C_BLUE_BUTTON` `C_BLUE` |
| Consumer Yellow Menu Button | `C_YELLOW_BUTTON` `C_YELLOW` |
| Consumer Aspect | `C_ASPECT` |
| Consumer Display Brightness Increment | `C_BRIGHTNESS_INC` `C_BRI_INC` `C_BRI_UP` |
| Consumer Display Brightness Decrement | `C_BRIGHTNESS_DEC` `C_BRI_DEC` `C_BRI_DN` |
| Consumer Display Backlight Toggle | `C_BACKLIGHT_TOGGLE` `C_BKLT_TOG` |
| Consumer Display Set Brightness to Minimum | `C_BRIGHTNESS_MINIMUM` `C_BRI_MIN` |
| Consumer Display Set Brightness to Maximum | `C_BRIGHTNESS_MAXIMUM` `C_BRI_MAX` |
| Consumer Display Set Auto Brightness | `C_BRIGHTNESS_AUTO` `C_BRI_AUTO` |
| Consumer Mode Step | `C_MEDIA_STEP` `C_MODE_STEP` |
| Consumer Recall Last | `C_RECALL_LAST` `C_CHAN_LAST` |
| Consumer Media Select Computer | `C_MEDIA_COMPUTER` |
| Consumer Media Select TV | `C_MEDIA_TV` |
| Consumer Media Select WWW | `C_MEDIA_WWW` |
| Consumer Media Select DVD | `C_MEDIA_DVD` |
| Consumer Media Select Telephone | `C_MEDIA_PHONE` |
| Consumer Media Select Program Guide | `C_MEDIA_GUIDE` |
| Consumer Media Select Video Phone | `C_MEDIA_VIDEOPHONE` |
| Consumer Media Select Games | `C_MEDIA_GAMES` |
| Consumer Media Select Messages | `C_MEDIA_MESSAGES` |
| Consumer Media Select CD | `C_MEDIA_CD` |
| Consumer Media Select VCR | `C_MEDIA_VCR` |
| Consumer Media Select Tuner | `C_MEDIA_TUNER` |
| Consumer Quit | `C_QUIT` |
| Consumer Help | `C_HELP` |
| Consumer Media Select Tape | `C_MEDIA_TAPE` |
| Consumer Media Select Cable | `C_MEDIA_CABLE` |
| Consumer Media Select Satellite | `C_MEDIA_SATELLITE` |
| Consumer Media Select Home | `C_MEDIA_HOME` |
| Consumer Channel Increment | `C_CHANNEL_INC` `C_CHAN_INC` |
| Consumer Channel Decrement | `C_CHANNEL_DEC` `C_CHAN_DEC` |
| Consumer VCR Plus | `C_MEDIA_VCR_PLUS` |
| Consumer Play | `C_PLAY` |
| Consumer Pause | `C_PAUSE` |
| Consumer Record | `C_RECORD` `C_REC` |
| Consumer Fast Forward | `C_FAST_FORWARD` `C_FF` |
| Consumer Rewind | `C_REWIND` `C_RW` |
| Consumer Scan Next Track | `C_NEXT` `M_NEXT` (非推奨) |
| Consumer Scan Previous Track | `C_PREVIOUS` `C_PREV` `M_PREV` (非推奨) |
| Consumer Stop | `C_STOP` `M_STOP` (非推奨) |
| Consumer Eject | `C_EJECT` `M_EJCT` (非推奨) |
| Consumer Random Play | `C_RANDOM_PLAY` `C_SHUFFLE` |
| Consumer Repeat | `C_REPEAT` |
| Consumer Slow Tracking | `C_SLOW_TRACKING` `C_SLOW2` |
| Consumer Stop/Eject | `C_STOP_EJECT` |
| Consumer Play/Pause | `C_PLAY_PAUSE` `C_PP` `M_PLAY` (非推奨) |
| Consumer Voice Command | `C_VOICE_COMMAND` |
| Consumer Mute | `C_MUTE` `M_MUTE` (非推奨) |
| Consumer Bass Boost | `C_BASS_BOOST` |
| Consumer Volume Increment | `C_VOLUME_UP` `C_VOL_UP` `M_VOLU` (非推奨) |
| Consumer Volume Decrement | `C_VOLUME_DOWN` `C_VOL_DN` `M_VOLD` (非推奨) |
| Consumer Slow | `C_SLOW` |
| Consumer Alternate Audio Increment | `C_ALTERNATE_AUDIO_INCREMENT` `C_ALT_AUDIO_INC` |
| Consumer AL Consumer Control Configuration | `C_AL_CCC` |
| Consumer AL Word Processor | `C_AL_WORD` |
| Consumer AL Text Editor | `C_AL_TEXT_EDITOR` |
| Consumer AL Spreadsheet | `C_AL_SPREADSHEET` `C_AL_SHEET` |
| Consumer AL Graphics Editor | `C_AL_GRAPHICS_EDITOR` |
| Consumer AL Presentation App | `C_AL_PRESENTATION` |
| Consumer AL Database App | `C_AL_DATABASE` `C_AL_DB` |
| Consumer AL Email Reader | `C_AL_EMAIL` `C_AL_MAIL` |
| Consumer AL Newsreader | `C_AL_NEWS` |
| Consumer AL Voicemail | `C_AL_VOICEMAIL` |
| Consumer AL Contacts/Address Book | `C_AL_CONTACTS` `C_AL_ADDRESS_BOOK` |
| Consumer AL Calendar/Schedule | `C_AL_CALENDAR` `C_AL_CAL` |
| Consumer AL Task/Project Manager | `C_AL_TASK_MANAGER` |
| Consumer AL Log/Journal/Timecard | `C_AL_JOURNAL` |
| Consumer AL Checkbook/Finance | `C_AL_FINANCE` |
| Consumer AL Calculator | `C_AL_CALCULATOR` `C_AL_CALC` |
| Consumer AL A/V Capture/Playback | `C_AL_AV_CAPTURE_PLAYBACK` |
| Consumer AL Local Machine Browser | `C_AL_MY_COMPUTER` |
| Consumer AL Internet Browser | `C_AL_WWW` |
| Consumer AL Network Chat | `C_AL_NETWORK_CHAT` `C_AL_CHAT` |
| Consumer AL Logoff | `C_AL_LOGOFF` |
| Consumer AL Terminal Lock/Screensaver | `C_AL_LOCK` `C_AL_SCREENSAVER` `C_AL_COFFEE` |
| Consumer AL Control Panel | `C_AL_CONTROL_PANEL` |
| Consumer AL Select Task/Application | `C_AL_SELECT_TASK` |
| Consumer AL Next Task/Application | `C_AL_NEXT_TASK` |
| Consumer AL Previous Task/Application | `C_AL_PREVIOUS_TASK` `C_AL_PREV_TASK` |
| Consumer AL Integrated Help Center | `C_AL_HELP` |
| Consumer AL Documents | `C_AL_DOCUMENTS` `C_AL_DOCS` |
| Consumer AL Spell Check | `C_AL_SPELLCHECK` `C_AL_SPELL` |
| Consumer AL Keyboard Layout | `C_AL_KEYBOARD_LAYOUT` |
| Consumer AL Screen Saver | `C_AL_SCREEN_SAVER` |
| Consumer AL File Browser | `C_AL_FILE_BROWSER` `C_AL_FILES` |
| Consumer AL Image Browser | `C_AL_IMAGE_BROWSER` `C_AL_IMAGES` |
| Consumer AL Audio Browser | `C_AL_AUDIO_BROWSER` `C_AL_AUDIO` `C_AL_MUSIC` |
| Consumer AL Movie Browser | `C_AL_MOVIE_BROWSER` `C_AL_MOVIES` |
| Consumer AL Instant Messaging | `C_AL_INSTANT_MESSAGING` `C_AL_IM` |
| Consumer AL OEM Features/Tips/Tutorial Browser | `C_AL_OEM_FEATURES` `C_AL_TIPS` `C_AL_TUTORIAL` |
| Consumer AC New | `C_AC_NEW` |
| Consumer AC Open | `C_AC_OPEN` |
| Consumer AC Close | `C_AC_CLOSE` |
| Consumer AC Exit | `C_AC_EXIT` |
| Consumer AC Save | `C_AC_SAVE` |
| Consumer AC Print | `C_AC_PRINT` |
| Consumer AC Properties | `C_AC_PROPERTIES` `C_AC_PROPS` |
| Consumer AC Undo | `C_AC_UNDO` |
| Consumer AC Copy | `C_AC_COPY` |
| Consumer AC Cut | `C_AC_CUT` |
| Consumer AC Paste | `C_AC_PASTE` |
| Consumer AC Find | `C_AC_FIND` |
| Consumer AC Search | `C_AC_SEARCH` |
| Consumer AC Go To | `C_AC_GOTO` |
| Consumer AC Home | `C_AC_HOME` |
| Consumer AC Back | `C_AC_BACK` |
| Consumer AC Forward | `C_AC_FORWARD` |
| Consumer AC Stop | `C_AC_STOP` |
| Consumer AC Refresh | `C_AC_REFRESH` |
| Consumer AC Bookmarks | `C_AC_BOOKMARKS` `C_AC_FAVORITES` `C_AC_FAVOURITES` |
| Consumer AC Zoom In | `C_AC_ZOOM_IN` |
| Consumer AC Zoom Out | `C_AC_ZOOM_OUT` |
| Consumer AC Zoom | `C_AC_ZOOM` |
| Consumer AC View Toggle | `C_AC_VIEW_TOGGLE` |
| Consumer AC Scroll Up | `C_AC_SCROLL_UP` |
| Consumer AC Scroll Down | `C_AC_SCROLL_DOWN` |
| Consumer AC Edit | `C_AC_EDIT` |
| Consumer AC Cancel | `C_AC_CANCEL` |
| Consumer AC Insert Mode | `C_AC_INSERT` `C_AC_INS` |
| Consumer AC Delete | `C_AC_DEL` |
| Consumer AC Redo/Repeat | `C_AC_REDO` |
| Consumer AC Reply | `C_AC_REPLY` |
| Consumer AC Forward Msg | `C_AC_FORWARD_MAIL` |
| Consumer AC Send | `C_AC_SEND` |
| Consumer AC Desktop Show All Windows | `C_AC_DESKTOP_SHOW_ALL_WINDOWS` |
| Consumer AC Desktop Show All Applications | `C_AC_DESKTOP_SHOW_ALL_APPLICATIONS` |
| Consumer Keyboard Input Assist Previous | `C_KEYBOARD_INPUT_ASSIST_PREVIOUS` `C_KBIA_PREV` |
| Consumer Keyboard Input Assist Next | `C_KEYBOARD_INPUT_ASSIST_NEXT` `C_KBIA_NEXT` |
| Consumer Keyboard Input Assist Previous Group | `C_KEYBOARD_INPUT_ASSIST_PREVIOUS_GROUP` `C_KBIA_PREV_GRP` |
| Consumer Keyboard Input Assist Next Group | `C_KEYBOARD_INPUT_ASSIST_NEXT_GROUP` `C_KBIA_NEXT_GRP` |
| Consumer Keyboard Input Assist Accept | `C_KEYBOARD_INPUT_ASSIST_ACCEPT` `C_KBIA_ACCEPT` |
| Consumer Keyboard Input Assist Cancel | `C_KEYBOARD_INPUT_ASSIST_CANCEL` `C_KBIA_CANCEL` |
| Apple Globe key | `C_AC_NEXT_KEYBOARD_LAYOUT_SELECT` `GLOBE` |
