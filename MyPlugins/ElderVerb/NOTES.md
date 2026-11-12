# Elder Verb — 作業メモ（Claude 用）

本人向けの説明は README.md。ここは中身の仕組み・ID・版ごとの変更の記録です。
（2026-11-06 に CLAUDE.md の「作ったもの」から移した。版を上げたら、ここに 1 行足す）

Elder Verb（ElderVerb）… 70〜80 年代のデジタルリバーブ機材ふうのリバーブ。この Mac の市販ビンテージリバーブを AU で鳴らして測り、合わせ込んだオリジナル。
22 モード（Concert Hall〜Vintage Hall、順番は本物の ReverbMode = k/24 の k−1）、COLOR 70s / 80s / NOW、つまみ 15 個は本物と同じ 0..1 の意味なので、
この Mac の .vpreset をプリセットメニューからそのまま読める。エンジンは ElderEngine.h（JUCE なし）: 入口の拡散 4 段 → 粒のかたまり（ATTACK で形と始まり）→ 4 枝の輪。
モードごとの性格は Source/Profiles.h（tools/measure の fit.py / fitmod.py → genprof.py で自動生成。作業データは build/ElderVerbFit、AU を鳴らす auhost.swift）。
70s は出口に 12 kHz の 12 次楕円（VintageFilter.h）＋ループ内 12.7 kHz の 1 次。比べ方は tools/measure/compare.py au
- 1.1.0（2026-11-09）: GAIN MATCH を追加（Shared/GainMatch.h、ID gainMatch、既定 OFF、状態に gainMatchDb を保存）。ボタンはプリセット欄の左端。MIX 0.995 以上は効かない（センド使い）。下の欄を 2 段（footerH 72）に
- 1.1.1（2026-11-12）: HIGH CUT / LOW CUT のジジッを直した（健康診断の click で +28 / +22）。周波数は log で 16 サンプルごとに 30 ms で追い、着いたら元の値ちょうど。ON ⇄ OFF は混ぜる量（hcOn / lcOn）を 20 ms で（フィルターはいつも通す）。止まっている時の差は −128 dB（丸めだけ）
  DSP 検査係: 追う式を「丸めでもう進まないなら目標ちょうど」に（192 kHz で目標の直前で止まり、係数を作り直し続ける所があった）
