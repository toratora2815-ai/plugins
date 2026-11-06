# Aside — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Aside（Aside）… ひとりごと秘書。SFSpeechRecognizer（ja-JP、requiresOnDeviceRecognition。できない Mac では聞かない）で約 0.9 秒の無音ごとに確定、
TaskParser.h（JUCE なし）がきっかけの言葉（あとで・〜しなきゃ・忘れないように・メモして等）のある文だけ「やること」と日時に。EventKit でリマインダーのリスト「ひとりごと」へ、
打ち合わせ等で時刻があればカレンダー。右上の HUD で取り消し。tools/test_parser.cpp（17 例）
v1.1.0: 標準は「押したときだけ聞く」（設定 always=false）。Num Deck の「一言メモ」か ⌃⌥M でマイクを開き、一言確定（またはしゃべらず 12 秒）で閉じる。
分散通知 com.myplugins.aside.command（"memo" / "undo" / "show"）。「いつも聞く」ON のときだけ常に聞く（Logic が前なら休む）
