# LumenRooms 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。

- 1.5.0（2026-11-09）: GAIN MATCH を追加（Shared/GainMatch.h、ID gainMatch、既定 OFF、状態に gainMatchDb を保存）。ボタンはプリセット欄の左端。MIX 99.5% 以上は効かない（センド使い）。setCurrentProgram と .vpreset 読み込みの初期化から gainMatch を外した
- 1.5.1（2026-11-12）: オートメーションのジジッを直した（健康診断の click で 9 個、+30〜+63）。RoomEngine.h: 混ぜる量・TRIM・OUTPUT・幅は 20 ms、反射の揺らぎの深さは 50 ms、REVERB ROLL-OFF は log で 16 サンプルごと（着いたら元の値ちょうど）、Tail の LOW BOOST は 20 ms（0 へ下がりきるまで処理し、再開時は中を空に）。止まっている時の音は前と同じ。初回の健康診断の「ときどき重くなる（73%）」は Mac が混んでいたため（空いている時は平均 5%・いちばん重いブロックでも 14%）
  DSP 検査係: 追う式を「丸めでもう進まないなら目標ちょうど」に（192 kHz で目標の直前で止まり、係数を作り直し続ける所があった）
