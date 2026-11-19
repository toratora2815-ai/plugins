#pragma once
// 全プラグイン共通: GAIN MATCH（入れても・つまみをいじっても音量が変わらない）
//
// 仕組み（2026-11-09、ミックスエンジニア係の案 → DSP 検査係の指摘で直した）:
//   - 入口と出口の音量を K 特性（耳の感じ方に合わせた重み付け、BS.1770 と同じ）で測る
//   - 100 ms 区切りで、入口が −60 dB より大きい区切りと、その後 3 秒（響き・エコーの残り）を数える
//     （残りも入口・出口の両方で数えるので、間のある歌でもエコーを持ち上げすぎない。それより後の無音は数えない）
//   - 測り直すのは「ON にした時」「プラグインの画面でつまみを触った時（PresetBar が見ている・つまみのジェスチャー）」
//     「プリセット・取り消しを使った時」「内蔵プリセットを読んだ時（requestRelearn）」「別のトラックへコピーされた時（トラック名が変わった）」だけ。
//     オートメーションの再生では測り直さない（バウンスの結果が再生位置で変わらない）。
//     注意: Logic 側の操作（Smart Controls・コントロール表示・MIDI コントローラー）では AU にジェスチャーが来ないので測り直さない
//   - 測り始めてから、入口が鳴っている所が 0.3 秒たまったら仮の補正、その後は通しの平均で確かにして、10 秒たまったら固定する
//     （短い区間で追いかけないので、コンプのように音量が揺れない。演奏の強弱は残る）
//   - 補正は L/R 同じ量、下げは −24 dB・上げは +12 dB まで、0.4 秒かけてなめらかに移す
//   - amount（0〜1）: MIX 90〜100% のように「比べる元が薄い」所では補正を少しずつ 0 へ。0 = 効かない（覚えた値は捨てない）
//     切り替わり（BYPASS・DELTA など 0 ⇄ 1）は 10 ms でさっと移す
//   - 状態に「補正量・固定したか・トラック名」を保存。開き直したら固定した値のまま（入口の音量では測り直さない。
//     歪み系は入口の大きさで補正が変わるので、イントロから再生しただけでミックスが変わってしまうため）。
//     トラック名が保存したものと違う（別のトラックへコピーした）ときだけ測り直す
//   - プラグインの遅れ（レイテンシー）の分だけ、入口の測り値を遅らせて比べる
//   - NaN・無限大に強く（2026-11-19。前は測っている途中に入口へ NaN が 1 サンプル来ると補正が NaN になり、出口がずっと NaN、
//     Reset・OFF・開き直しでも戻らず、状態に "nan" が保存された。GAIN MATCH と関係なく、NaN 1 回で音の処理が止まったままになるプラグインも 9 個あった）:
//     measureInput が入口の NaN・無限大・ありえない大きさ（+120 dBFS 以上）を 0 に直す（普通の音は 1 ビットも変えない。Twin Take が自分でしていたのと同じ線）。
//     測る側はもっと手前の +40 dBFS で線を引き、入口・出口にそれ以上・NaN が混じった 100 ms の区切りは数えず、K 特性のフィルターには 0 を入れる
//     （+40〜+119 dBFS の 1 サンプルで、頭打ちするプラグインの補正が最大 +18 dB 狂ったまま固定されていた。DSP 検査係）。
//     補正・保存値はいつも有限。保存値が "nan" などの曲は 0 dB にして、ON なら測り直す
//
// 使い方（各プラグイン）:
//   レイアウト:   params.push_back (myp::GainMatch::makeParameter());
//   プロセッサー: myp::GainMatch gainMatch { *this, myPresets };   // apvts・myPresets より後に書く
//   prepareToPlay: gainMatch.prepare (sampleRate, getTotalNumOutputChannels(), samplesPerBlock);
//   processBlock の最初: gainMatch.measureInput (buffer, numCh);   // 入口の NaN・無限大もここで 0 に直す（buffer を書き換える）
//   processBlock の最後: gainMatch.process (buffer, numCh, getLatencySamples(), amount);   // amount = 1 で普通、0 で効かない
//   getStateInformation:  auto st = apvts.copyState(); gainMatch.saveTo (st);
//   setStateInformation:  apvts.replaceState (...); gainMatch.loadFrom (apvts.state);
//   内蔵プリセットを読む関数の最後: gainMatch.requestRelearn();
//   updateTrackProperties:  gainMatch.setTrackName (*tp.name);
//   エディター:   presetBar.showGainMatch (myp::GainMatch::paramId, [this] { return processor.gainMatch.readout(); }, ...);
#include <JuceHeader.h>
#include "PresetBar.h"

namespace myp
{
    class GainMatch : private juce::AudioProcessorParameter::Listener
    {
    public:
        static constexpr const char* paramId = "gainMatch";
        static constexpr float maxCutDb = 24.0f, maxBoostDb = 12.0f;
        static constexpr double lockSeconds = 10.0;     // 入口が鳴っている所がこれだけたまったら固定
        static constexpr double tailSeconds = 3.0;      // 入口が止まってから、これだけは残り（響き・エコー）も数える
        static constexpr float maxSample = 1.0e6f;      // これ以上（+120 dBFS）・NaN・無限大は壊れた値として扱う（Twin Take と同じ線）

        static constexpr float maxMeasure = 100.0f;     // 測るときだけの線: +40 dBFS 以上が混じった区切りは数えない（音は変えない）

        // 使える値か（NaN はどの比べ方でも false になるので、NaN・±無限大・+120 dBFS 以上が false）
        static bool usable (float x) noexcept { return std::abs (x) < maxSample; }
        // 測りに数えてよい値か（+40 dBFS 以上は前のプラグインの不具合。1 サンプルでも、頭打ちするプラグインの補正が最大 +18 dB 狂って固定されるため）
        static bool measurable (float x) noexcept { return std::abs (x) < maxMeasure; }

        // 版のヒントは 10（今ある全プラグインの最大 9 より大きく）。小さいと Logic のパラメーター番号がずれて、
        // 今ある曲のオートメーションが別のつまみを動かしてしまう
        static std::unique_ptr<juce::AudioParameterBool> makeParameter()
        {
            return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { paramId, 10 }, "Gain Match", false);
        }

        // ignoreIds: 動いても測り直さないパラメーター（聴くだけのスイッチ、BYPASS など）
        GainMatch (juce::AudioProcessor& p, Presets& presets, juce::StringArray ignoreIds = {})
        {
            ignoreIds.addArray ({ paramId, "linkGroup", "linkMatch" });
            for (auto* prm : p.getParameters())
            {
                auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
                if (r != nullptr && r->getParameterID() == paramId) enabledParam = r;
                if (r != nullptr && ignoreIds.contains (r->getParameterID())) continue;
                if (r != nullptr) watchedIds.add (r->getParameterID());
                prm->addListener (this);
                listened.push_back (prm);
            }
            jassert (enabledParam != nullptr);   // makeParameter() をレイアウトに足し忘れていないか
            // 画面で触った・プリセット・取り消しで変わったパラメーターを教えてもらう
            presets.onUserChange = [this] (const juce::StringArray& ids)
            {
                for (auto& id : ids) if (watchedIds.contains (id)) { requestRelearn(); return; }
            };
        }

        ~GainMatch() override
        {
            for (auto* prm : listened) prm->removeListener (this);
        }

        // 測り直してほしいとき（内蔵プリセットを読んだ後など）。どのスレッドからでもよい
        void requestRelearn() { relearnRequest.store (true); }

        // トラック名（updateTrackProperties から）。開き直した直後に、保存したトラック名と違えば（= 別のトラックへコピー）測り直す。
        // 開いている間の名前の変更では測り直さない
        void setTrackName (const juce::String& name)
        {
            const juce::ScopedLock sl (nameLock);
            if (justLoaded && savedTrack.isNotEmpty() && name.isNotEmpty() && name != savedTrack) requestRelearn();
            if (name.isNotEmpty()) justLoaded = false;
            currentTrack = name;
        }

        // 上げる補正の上限（毎ブロック。最後にリミッターがあるときは 0 にする）
        void limitBoost (float maxUpDb) { boostLimitDb = std::isfinite (maxUpDb) ? maxUpDb : 0.0f; }

        void prepare (double sampleRate, int numChannels, int samplesPerBlock = 0)
        {
            if ((int) inPow.size() < samplesPerBlock) inPow.assign ((size_t) samplesPerBlock, 0.0f);
            sr = juce::jmax (8000.0, sampleRate);
            channels = juce::jlimit (1, maxCh, numChannels);
            designKWeighting();
            for (auto& f : inF)  f.reset();
            for (auto& f : outF) f.reset();
            std::fill (ring.begin(), ring.end(), 0.0f);
            ringPos = 0;
            windowLen = juce::jmax (1, (int) std::round (sr * 0.1));
            winIn = winOut = 0.0; winN = 0; winBad = false;
            glide = (float) (1.0 - std::exp (-1.0 / (0.4 * sr)));
            glideFast = (float) (1.0 - std::exp (-1.0 / (0.01 * sr)));
            fastLen = (int) (0.06 * sr);
            blocksSincePrepare = 0;
        }

        // processBlock の最初（音をいじる前）に呼ぶ。
        // 入口の NaN・無限大・ありえない大きさは、測ったあとでここで 0 に直す（前のプラグインの不具合 1 回で、
        // このプラグインのフィルター・リバーブや GAIN MATCH の測り値が NaN のまま戻らなくならないように）
        void measureInput (juce::AudioBuffer<float>& buf, int numCh)
        {
            const int n = buf.getNumSamples(), nb = buf.getNumChannels();
            const int ch = juce::jlimit (1, channels, juce::jmin (numCh, nb));
            inValid = 0;
            // 先に全部のチャンネルをまとめて調べる（途中で抜けないのでベクトル化される）。普通の音ならどちらも true
            bool allMeasurable = true, allUsable = true;
            for (int c = 0; c < nb; ++c)
            {
                const float* x = buf.getReadPointer (c);
                bool m = true, u = true;
                for (int i = 0; i < n; ++i) { const float a = std::abs (x[i]); m &= a < maxMeasure; u &= a < maxSample; }
                allUsable = allUsable && u;
                if (c < ch) allMeasurable = allMeasurable && m;
            }
            if ((int) inPow.size() >= n && nb > 0)   // 想定より大きいブロック: その分は測らない
            {
                std::fill (inPow.begin(), inPow.begin() + n, 0.0f);
                for (int c = 0; c < ch; ++c)
                {
                    const float* x = buf.getReadPointer (c);
                    auto& f = inF[(size_t) c];
                    if (allMeasurable)
                        for (int i = 0; i < n; ++i) { const float y = f.process (x[i]); inPow[(size_t) i] += y * y; }   // 普通の音（直す前と同じ式）
                    else
                        for (int i = 0; i < n; ++i)
                        {
                            if (measurable (x[i])) { const float y = f.process (x[i]); inPow[(size_t) i] += y * y; }
                            else { f.process (0.0f); inPow[(size_t) i] = badMark; }   // 負の印 = この瞬間は測れない（その区切りは数えない）
                        }
                }
                const float k = 1.0f / (float) ch;
                for (int i = 0; i < n; ++i) inPow[(size_t) i] *= k;
                inValid = n;
            }
            // 入口を直す（測ったあとで）。全部のチャンネル（モノの入口を左右に写してから呼ぶプラグインもある）。普通の音なら書き込まない
            if (! allUsable)
                for (int c = 0; c < nb; ++c)
                {
                    float* w = buf.getWritePointer (c);
                    for (int i = 0; i < n; ++i) if (! usable (w[i])) w[i] = 0.0f;
                }
        }

        // processBlock の最後に呼ぶ。latency = プラグインの遅れ（サンプル）。amount = 効かせる割合（0〜1）
        void process (juce::AudioBuffer<float>& buf, int numCh, int latency, float amount = 1.0f)
        {
            const int n = buf.getNumSamples();
            const int ch = juce::jmin (numCh, buf.getNumChannels());
            amount = juce::jlimit (0.0f, 1.0f, std::isfinite (amount) ? amount : 0.0f);
            isAvailable.store (amount > 0.0f);
            const bool userOn = enabledParam != nullptr && enabledParam->getValue() > 0.5f;

            // 開き直した状態を受け取る（loadFrom はメッセージスレッドから）
            int restoreKind = 0;
            if (pending.exchange (false))
            {
                targetDb = pendingDb.load();
                restoreKind = pendingLocked.load() ? 2 : 1;
                lockedDb.store (targetDb); isLocked.store (false);
                wasOn = false;          // 下で「ON になった」として扱う
                fastLeft = fastLen;     // 鳴らしている途中なら、今の補正からさっと移す
                if (blocksSincePrepare == 0 && enabledParam != nullptr && enabledParam->getValue() > 0.5f)
                    curDb = juce::jmin (targetDb, boostLimitDb) * amount;   // 曲を開いた直後（まだ鳴らしていない）は、いきなりその値から
            }
            if (blocksSincePrepare < 1000000) ++blocksSincePrepare;

            if (userOn != wasOn)
            {
                wasOn = userOn;
                const bool relearn = relearnRequest.exchange (false);   // 別のトラックへコピーされた、など
                if (userOn) { if (restoreKind == 2 && ! relearn) restoreLocked(); else startLearning(); }
                else
                {
                    mode = Mode::idle; targetDb = 0.0f;
                    lockedDb.store (0.0f); isLocked.store (false); hasEstimate.store (false); learningShown.store (false);
                }
            }
            else if (relearnRequest.exchange (false) && userOn)
                startLearning();

            // 測る（ON で、効かせている間だけ）
            if (userOn && amount > 0.0f && inValid == n && ch > 0)
            {
                const int lat = juce::jlimit (0, ringSize - 1, latency);
                const int chM = juce::jlimit (1, channels, ch);
                const float k = 1.0f / (float) chM;
                for (int i = 0; i < n; ++i)
                {
                    // 入口の測り値を遅れの分だけ遅らせる
                    ring[(size_t) ringPos] = inPow[(size_t) i];
                    float pin = ring[(size_t) ((ringPos - lat + ringSize) & (ringSize - 1))];
                    ringPos = (ringPos + 1) & (ringSize - 1);
                    if (pin < 0.0f) { pin = 0.0f; winBad = true; }   // 入口に NaN・無限大が来た瞬間（measureInput の印）
                    float pout = 0.0f;
                    for (int c = 0; c < chM; ++c)
                    {
                        const float v = buf.getSample (c, i);
                        if (measurable (v)) { const float y = outF[(size_t) c].process (v); pout += y * y; }
                        else { outF[(size_t) c].process (0.0f); winBad = true; }   // プラグイン自身が NaN・ありえない大きさを出した瞬間
                    }
                    winIn += pin; winOut += pout * k; ++winN;
                    if (winN >= windowLen) endWindow();
                }
            }
            inValid = 0;

            // 念のため: 補正が有限でなくなっていたら 0 dB に戻す（上の測り方で NaN は入らないはず）
            if (! std::isfinite (targetDb)) targetDb = 0.0f;
            if (! std::isfinite (curDb)) curDb = 0.0f;

            // 補正をかける（L/R 同じ量）
            if (std::abs (amount - lastAmount) > 0.05f) fastLeft = fastLen;
            lastAmount = amount;
            const float aim = userOn ? juce::jmin (targetDb, boostLimitDb) * amount : 0.0f;
            boostLimited.store (userOn && amount > 0.0f && targetDb > boostLimitDb + 0.1f);
            if (aim == 0.0f && std::abs (curDb) < 1.0e-4f) { curDb = 0.0f; fastLeft = 0; shownDb.store (0.0f); return; }
            if (std::abs (aim - curDb) < 1.0e-4f)
            {
                curDb = aim; fastLeft = 0;
                const float g = juce::Decibels::decibelsToGain (curDb, -200.0f);
                for (int c = 0; c < ch; ++c) juce::FloatVectorOperations::multiply (buf.getWritePointer (c), g, n);
            }
            else
            {
                float* const* x = buf.getArrayOfWritePointers();
                for (int i = 0; i < n; ++i)
                {
                    curDb += (aim - curDb) * (fastLeft > 0 ? glideFast : glide);
                    if (fastLeft > 0) --fastLeft;
                    const float g = std::exp (curDb * 0.11512925465f);   // dB → 倍率
                    for (int c = 0; c < ch; ++c) x[c][i] *= g;
                }
            }
            shownDb.store (curDb);
        }

        // ---- 状態の保存（補正量・固定したか・そのときの入口の音量） ----
        void saveTo (juce::ValueTree& st) const
        {
            const float db = lockedDb.load();
            const bool ok = std::isfinite (db);   // 念のため: NaN を "nan" で保存しない
            st.setProperty ("gainMatchDb", ok ? db : 0.0f, nullptr);
            st.setProperty ("gainMatchLocked", ok && isLocked.load(), nullptr);
            const juce::ScopedLock sl (nameLock);
            st.setProperty ("gainMatchTrack", currentTrack, nullptr);
        }
        void loadFrom (const juce::ValueTree& st)
        {
            // 2026-11-19 より前の版は、壊れた補正を "nan" で保存していることがある（JUCE は "nan"・"inf" を NaN・無限大として読む）
            // → 0 dB・固定していない扱いにする（ON なら次に鳴らした所から測り直す）
            const double db = (double) st.getProperty ("gainMatchDb", 0.0);
            const bool ok = std::isfinite (db);
            pendingDb.store (ok ? juce::jlimit (-maxCutDb, maxBoostDb, (float) db) : 0.0f);
            pendingLocked.store (ok && (bool) st.getProperty ("gainMatchLocked", false));
            pending.store (true);
            const juce::ScopedLock sl (nameLock);
            savedTrack = st.getProperty ("gainMatchTrack", "").toString();
            if (currentTrack.isNotEmpty() && savedTrack.isNotEmpty() && currentTrack != savedTrack) requestRelearn();   // 名前が先に来ていた
            justLoaded = currentTrack.isEmpty();   // 名前がまだなら、後で来たときに比べる
        }

        // ---- 画面用 ----
        float correctionDb() const { return shownDb.load(); }
        bool isLearning() const { return learningShown.load(); }
        bool locked() const { return isLocked.load(); }
        bool available() const { return isAvailable.load(); }
        juce::String readout() const
        {
            if (enabledParam == nullptr || enabledParam->getValue() < 0.5f || ! available()) return "GAIN MATCH";
            if (isLearning() && ! hasEstimate.load()) return "GAIN MATCH ...";
            const float d = correctionDb();
            return "GAIN MATCH " + juce::String (d > 0.05f ? "+" : "") + juce::String (d, 1) + (isBoostLimited() ? " LIM" : "");
        }
        // 上げたいのに limitBoost で止めている（Forge Strip のリミッター ON）
        bool isBoostLimited() const { return boostLimited.load(); }

    private:
        static constexpr int maxCh = 2;
        static constexpr int ringSize = 1 << 17;   // 遅れ 2.7 秒（48 kHz）まで
        static constexpr int maxBlock = 8192;      // prepare で samplesPerBlock がもっと大きければ広げる

        enum class Mode { idle, learning, locked };

        struct Biquad
        {
            double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
            float process (float x)
            {
                const double y = b0 * x + z1;
                z1 = b1 * x - a1 * y + z2;
                z2 = b2 * x - a2 * y;
                return (float) y;
            }
            void reset() { z1 = z2 = 0; }
        };
        struct KFilter
        {
            Biquad shelf, hp;
            float process (float x) { return hp.process (shelf.process (x)); }
            void reset() { shelf.reset(); hp.reset(); }
        };

        // Logic の画面・操作面でつまみを触り終えたら測り直す（オートメーションの再生ではジェスチャーは来ない）
        void parameterValueChanged (int, float) override {}
        void parameterGestureChanged (int, bool gestureIsStarting) override
        {
            if (! gestureIsStarting) requestRelearn();
        }

        void designKWeighting()
        {
            // BS.1770 の K 特性（どのサンプルレートでも同じ形）
            Biquad s, h;
            {
                const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
                const double K = std::tan (juce::MathConstants<double>::pi * f0 / sr);
                const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
                const double a0 = 1.0 + K / Q + K * K;
                s.b0 = (Vh + Vb * K / Q + K * K) / a0; s.b1 = 2.0 * (K * K - Vh) / a0; s.b2 = (Vh - Vb * K / Q + K * K) / a0;
                s.a1 = 2.0 * (K * K - 1.0) / a0;       s.a2 = (1.0 - K / Q + K * K) / a0;
            }
            {
                const double f0 = 38.13547087602444, Q = 0.5003270373238773;
                const double K = std::tan (juce::MathConstants<double>::pi * f0 / sr);
                const double a0 = 1.0 + K / Q + K * K;
                h.b0 = 1.0; h.b1 = -2.0; h.b2 = 1.0;
                h.a1 = 2.0 * (K * K - 1.0) / a0; h.a2 = (1.0 - K / Q + K * K) / a0;
            }
            for (auto* bank : { &inF, &outF })
                for (auto& f : *bank) { f.shelf = s; f.hp = h; }
        }

        void resetAccumulators()
        {
            accIn = accOut = 0.0; accActive = 0.0;
            winIn = winOut = 0.0; winN = 0; winBad = false;   // 区切りの途中から数え直す
        }
        void startLearning()
        {
            mode = Mode::learning;
            resetAccumulators();
            isLocked.store (false);
            hasEstimate.store (false);
            learningShown.store (true);
        }
        void restoreLocked()
        {
            mode = Mode::locked;
            resetAccumulators();
            isLocked.store (true);
            hasEstimate.store (true);   // 補正は開き直した値のまま
            learningShown.store (false);
        }

        void endWindow()
        {
            const double inMs = winIn / winN, outMs = winOut / winN;
            const double dur = winN / sr;
            const bool bad = winBad;
            winIn = winOut = 0.0; winN = 0; winBad = false;
            if (bad) { sinceActive += dur; return; }   // NaN・無限大が混じった区切りは数えない（時計だけ進める）
            const bool active = inMs >= 1.0e-6;   // 入口が −60 dB 以上
            sinceActive = active ? 0.0 : sinceActive + dur;
            if (mode != Mode::learning) return;
            if (! active && sinceActive > tailSeconds) return;   // 入口が止まって 3 秒より後は数えない
            if (! active && accActive <= 0.0) return;             // 鳴り始める前の残りは数えない
            accIn += inMs * dur; accOut += outMs * dur;
            if (active) accActive += dur;

            if (accActive >= 0.3 && accOut > 1.0e-12)
            {
                const double ratioDb = 10.0 * std::log10 (accIn / accOut);
                if (! std::isnan (ratioDb))   // 念のため（±無限大は下の範囲で止まる）
                {
                    targetDb = juce::jlimit (-maxCutDb, maxBoostDb, (float) ratioDb);
                    lockedDb.store (targetDb);
                    hasEstimate.store (true);
                }
            }
            if (accActive >= lockSeconds && hasEstimate.load())
                lock();
        }

        void lock()
        {
            mode = Mode::locked;
            lockedDb.store (targetDb);
            isLocked.store (true);
            learningShown.store (false);
        }

        juce::RangedAudioParameter* enabledParam = nullptr;
        juce::StringArray watchedIds;
        std::vector<juce::AudioProcessorParameter*> listened;

        double sr = 48000.0;
        int channels = 2;
        std::array<KFilter, maxCh> inF, outF;
        static constexpr float badMark = -1.0e30f;   // inPow・ring の「この瞬間は測れない」印（測り値は 0 以上なので負なら印）
        std::vector<float> inPow = std::vector<float> (maxBlock, 0.0f);
        int inValid = 0;
        std::vector<float> ring = std::vector<float> (ringSize, 0.0f);
        int ringPos = 0;

        int windowLen = 4800, winN = 0;
        double winIn = 0.0, winOut = 0.0;
        bool winBad = false;   // 今の 100 ms の区切りに NaN・無限大が混じった
        double accIn = 0.0, accOut = 0.0, accActive = 0.0, sinceActive = 1.0e9;
        Mode mode = Mode::idle;
        bool wasOn = false;
        float targetDb = 0.0f, curDb = 0.0f, lastAmount = 1.0f, boostLimitDb = maxBoostDb;
        float glide = 1.0e-4f, glideFast = 1.0e-2f;
        int fastLeft = 0, fastLen = 2880, blocksSincePrepare = 0;

        std::atomic<bool> relearnRequest { false }, pending { false }, pendingLocked { false };
        std::atomic<float> pendingDb { 0.0f };
        std::atomic<float> lockedDb { 0.0f }, shownDb { 0.0f };
        std::atomic<bool> learningShown { false }, hasEstimate { false }, isAvailable { true }, isLocked { false }, boostLimited { false };

        // トラック名（メッセージスレッドかホストのスレッド。音のスレッドでは触らない）
        mutable juce::CriticalSection nameLock;
        juce::String currentTrack, savedTrack;
        bool justLoaded = false;
    };
}
