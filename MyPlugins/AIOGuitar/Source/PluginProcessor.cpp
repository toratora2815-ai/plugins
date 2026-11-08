#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets.h"

namespace
{
    using APF = juce::AudioParameterFloat;
    using APB = juce::AudioParameterBool;
    using APC = juce::AudioParameterChoice;
    using Attr = juce::AudioParameterFloatAttributes;

    Attr fmt (juce::String suffix, int decimals)
    {
        return Attr().withStringFromValueFunction ([suffix, decimals] (float v, int)
        {
            return juce::String (v, decimals) + suffix;
        });
    }

    std::unique_ptr<APF> knob (const char* id, const char* name, float def)
    {
        return std::make_unique<APF> (juce::ParameterID { id, 1 }, name,
                                      juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), def, fmt ("", 1));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout AIOGuitarProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;

    // ---- MY GUITAR (playing)
    l.add (std::make_unique<APF> (juce::ParameterID { "gtMute", 1 }, "Palm Mute",
                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f, fmt ("", 2)));
    l.add (std::make_unique<APF> (juce::ParameterID { "gtRelease", 1 }, "Release",
                                  juce::NormalisableRange<float> (0.02f, 1.5f, 0.001f, 0.5f), 0.12f, fmt (" s", 2)));
    l.add (std::make_unique<APF> (juce::ParameterID { "gtVolume", 1 }, "Guitar Volume",
                                  juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, fmt (" dB", 1)));
    l.add (std::make_unique<APF> (juce::ParameterID { "kitHuman", 1 }, "Humanize",
                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, fmt ("", 2)));
    l.add (std::make_unique<APC> (juce::ParameterID { "stringMode", 1 }, "String Choice",
                                  juce::StringArray { "Auto", "MIDI Ch 1-6 = String" }, 0));

    // ---- Gate
    l.add (std::make_unique<APF> (juce::ParameterID { "gateThresh", 1 }, "Gate Threshold",
                                  juce::NormalisableRange<float> (-80.0f, -20.0f, 0.1f), -80.0f,
                                  Attr().withStringFromValueFunction ([] (float v, int)
                                  { return v <= -79.5f ? juce::String ("Off") : juce::String (v, 1) + " dB"; })));

    // ---- Compressor
    l.add (std::make_unique<APB> (juce::ParameterID { "compOn", 1 }, "Comp On", false));
    l.add (std::make_unique<APF> (juce::ParameterID { "compThresh", 1 }, "Comp Threshold",
                                  juce::NormalisableRange<float> (-50.0f, 0.0f, 0.1f), -20.0f, fmt (" dB", 1)));
    l.add (std::make_unique<APF> (juce::ParameterID { "compRatio", 1 }, "Comp Ratio",
                                  juce::NormalisableRange<float> (1.0f, 20.0f, 0.01f, 0.4f), 4.0f, fmt (":1", 1)));
    l.add (std::make_unique<APF> (juce::ParameterID { "compAttack", 1 }, "Comp Attack",
                                  juce::NormalisableRange<float> (0.1f, 100.0f, 0.01f, 0.35f), 5.0f, fmt (" ms", 1)));
    l.add (std::make_unique<APF> (juce::ParameterID { "compRelease", 1 }, "Comp Release",
                                  juce::NormalisableRange<float> (10.0f, 1000.0f, 0.1f, 0.4f), 120.0f, fmt (" ms", 0)));
    l.add (std::make_unique<APF> (juce::ParameterID { "compMakeup", 1 }, "Comp Makeup",
                                  juce::NormalisableRange<float> (0.0f, 24.0f, 0.1f), 4.0f, fmt (" dB", 1)));

    // ---- Drive pedal
    l.add (std::make_unique<APB> (juce::ParameterID { "drvOn", 1 }, "Drive On", false));
    l.add (std::make_unique<APC> (juce::ParameterID { "drvType", 1 }, "Drive Type",
                                  juce::StringArray { "Overdrive", "Distortion", "Fuzz" }, 0));
    l.add (knob ("drvDrive", "Drive", 5.0f));
    l.add (knob ("drvTone",  "Drive Tone", 5.0f));
    l.add (knob ("drvLevel", "Drive Level", 5.0f));

    // ---- Amp
    l.add (std::make_unique<APB> (juce::ParameterID { "ampOn", 1 }, "Amp On", true));
    l.add (std::make_unique<APC> (juce::ParameterID { "ampChannel", 1 }, "Amp Channel",
                                  juce::StringArray { "Clean", "Crunch", "Lead" }, 1));
    l.add (knob ("ampGain",     "Amp Gain", 5.0f));
    l.add (knob ("ampBass",     "Bass", 5.0f));
    l.add (knob ("ampMid",      "Middle", 5.0f));
    l.add (knob ("ampTreble",   "Treble", 5.0f));
    l.add (knob ("ampPresence", "Presence", 5.0f));
    l.add (knob ("ampMaster",   "Master", 5.0f));

    // ---- Cabinet
    l.add (std::make_unique<APB> (juce::ParameterID { "cabOn", 1 }, "Cab On", true));
    l.add (std::make_unique<APC> (juce::ParameterID { "cabType", 1 }, "Cab Type",
                                  juce::StringArray { "4x12 Closed", "2x12 Combo", "1x12 Open" }, 0));
    l.add (knob ("cabMic", "Mic Tone", 5.0f));

    // ---- Pitch wheel
    l.add (std::make_unique<APF> (juce::ParameterID { "bendUp", 1 }, "Wheel Bend Up",
                                  juce::NormalisableRange<float> (0.0f, 12.0f, 0.5f), 2.0f, fmt (" st", 1)));
    l.add (std::make_unique<APF> (juce::ParameterID { "bendDown", 1 }, "Wheel Bend Down",
                                  juce::NormalisableRange<float> (0.0f, 12.0f, 0.5f), 2.0f, fmt (" st", 1)));

    // ---- Articulations (keyswitch / Logic articulation set)
    l.add (std::make_unique<APF> (juce::ParameterID { "artBendStart", 1 }, "Bend Start",
                                  juce::NormalisableRange<float> (0.0f, 1000.0f, 1.0f, 0.5f), 80.0f, fmt (" ms", 0)));
    l.add (std::make_unique<APF> (juce::ParameterID { "artBendTime", 1 }, "Bend Time",
                                  juce::NormalisableRange<float> (20.0f, 1500.0f, 1.0f, 0.5f), 150.0f, fmt (" ms", 0)));
    l.add (std::make_unique<APF> (juce::ParameterID { "artHold", 1 }, "Bend Hold",
                                  juce::NormalisableRange<float> (0.0f, 2000.0f, 1.0f, 0.5f), 300.0f, fmt (" ms", 0)));
    l.add (std::make_unique<APF> (juce::ParameterID { "slideRange", 1 }, "Slide Range",
                                  juce::NormalisableRange<float> (1.0f, 12.0f, 1.0f), 3.0f, fmt (" st", 0)));
    l.add (std::make_unique<APF> (juce::ParameterID { "vibRate", 1 }, "Vibrato Rate",
                                  juce::NormalisableRange<float> (1.0f, 10.0f, 0.01f), 5.5f, fmt (" Hz", 1)));
    l.add (std::make_unique<APF> (juce::ParameterID { "vibDelay", 1 }, "Vibrato Delay",
                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.2f, fmt (" s", 2)));

    // ---- MY GUITAR (recorded samples)
    l.add (std::make_unique<APB> (juce::ParameterID { "kitTune", 1 }, "Pitch Correction", true));
    l.add (std::make_unique<APB> (juce::ParameterID { "kitLevel", 1 }, "Level Match", true));
    l.add (std::make_unique<APF> (juce::ParameterID { "kitVelSens", 1 }, "Velocity Sensitivity",
                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.7f, fmt ("", 2)));

    // ---- Reverb / Output
    l.add (std::make_unique<APF> (juce::ParameterID { "revMix", 1 }, "Reverb Mix",
                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.12f, fmt ("", 2)));
    l.add (std::make_unique<APF> (juce::ParameterID { "revSize", 1 }, "Reverb Size",
                                  juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f, fmt ("", 2)));
    l.add (std::make_unique<APF> (juce::ParameterID { "outGain", 1 }, "Output",
                                  juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f, fmt (" dB", 1)));

    // ---- SCORE: follow Logic's MIDI, or play the plugin's own score in sync with Logic
    l.add (std::make_unique<APC> (juce::ParameterID { "scoreMode", 1 }, "Score Mode",
                                  juce::StringArray { "Follow Logic", "Play in Sync" }, 0));

    // ---- Plugin Link（LINK の番号）
    l.add (plink::makeGroupParameter (1));
    return l;
}

//==============================================================================
AIOGuitarProcessor::AIOGuitarProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            params.push_back ({ rp->getParameterID(), apvts.getRawParameterValue (rp->getParameterID()) });

    for (int i = 0; i < 48; ++i)   // plenty, so voice stealing (an instant cut) practically never happens
        synth.addVoice (new KitVoice (voiceParams));
    synth.addSound (new KitSound (voiceParams));
    synth.isBrushNote = [] (int note) { return note == Kit::kBrushDownNote || note == Kit::kBrushUpNote; };
    synth.onNoteOn = [this] (int ch, int note, float vel, int autoString, int& art, int& forced)
    {
        if (ch > 10) return;   // auditions from the UI
        const double ppq = hookPpq0 + (double) (synth.getClock() - hookClock0) * hookPpqPerSample;
        if (hookCapture)
        {
            Score::CaptureEvent e;
            e.type = Score::CaptureEvent::noteOn; e.ppq = ppq; e.note = note;
            e.velocity = juce::jlimit (1, 127, juce::roundToInt (vel * 127.0f)); e.channel = ch;
            e.logicArt = art; e.autoString = autoString;
            pushCapture (e);
        }
        if (auto* t = audioTable.get())
            if (auto* en = t->find (Score::toTick (ppq), note))
            {
                if (hookApplyAll) { art = en->effArt; if (en->effString >= 1) forced = en->effString; }
                else if (hookCapture) { if (en->ovArt >= 0) art = en->ovArt; if (en->ovString >= 1) forced = en->ovString; }
            }
    };
    synth.onNoteOff = [this] (int ch, int note)
    {
        if (! hookCapture || ch > 10) return;
        Score::CaptureEvent e;
        e.type = Score::CaptureEvent::noteOff;
        e.ppq = hookPpq0 + (double) (synth.getClock() - hookClock0) * hookPpqPerSample;
        e.note = note; e.channel = ch;
        pushCapture (e);
    };
    synth.chokeString = [this] (int s)
    {
        for (int i = 0; i < synth.getNumVoices(); ++i)
            if (auto* v = dynamic_cast<KitVoice*> (synth.getVoice (i)))
                if (v->getString() == s) v->choke();
    };
    apvts.addParameterListener ("kitLevel", this);
    publishTable();
    startTimerHz (30);
}

AIOGuitarProcessor::~AIOGuitarProcessor()
{
    alive->store (false);
    stopTimer();
    apvts.removeParameterListener ("kitLevel", this);
    loader.removeAllJobs (true, 10000);
}

bool AIOGuitarProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void AIOGuitarProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
    volume.reset (sampleRate, 0.03);
    sortedMidi.ensureSize (16384);
    scoreMidi.ensureSize (16384);
    previewRunning = capturing = syncWasPlaying = false;
    expectedPpq = -1.0;
    volume.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (get ("gtVolume")));
    chain.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (chain.getLatencySamples());

    synth.resetArticulation();
    keyboardState.reset();
}

void AIOGuitarProcessor::readParams()
{
    voiceParams.muteKnob  = get ("gtMute");
    voiceParams.release   = get ("gtRelease");
    voiceParams.humanize  = get ("kitHuman");
    voiceParams.stringMode = (int) get ("stringMode");
    voiceParams.bendUp    = get ("bendUp");
    voiceParams.bendDown  = get ("bendDown");
    voiceParams.artBendStart = get ("artBendStart") * 0.001f;
    voiceParams.artBendTime  = get ("artBendTime") * 0.001f;
    voiceParams.artHold      = get ("artHold") * 0.001f;
    voiceParams.slideRange   = get ("slideRange");
    voiceParams.vibRate   = get ("vibRate");
    voiceParams.vibDelay  = get ("vibDelay");
    voiceParams.kitTune     = get ("kitTune") > 0.5f;
    voiceParams.kitVelSens  = get ("kitVelSens");

    fx.gateThresh  = get ("gateThresh");
    fx.compOn      = get ("compOn") > 0.5f;
    fx.compThresh  = get ("compThresh");
    fx.compRatio   = get ("compRatio");
    fx.compAttack  = get ("compAttack");
    fx.compRelease = get ("compRelease");
    fx.compMakeup  = get ("compMakeup");
    fx.drvOn       = get ("drvOn") > 0.5f;
    fx.drvType     = (int) get ("drvType");
    fx.drvDrive    = get ("drvDrive");
    fx.drvTone     = get ("drvTone");
    fx.drvLevel    = get ("drvLevel");
    fx.ampOn       = get ("ampOn") > 0.5f;
    fx.ampChannel  = (int) get ("ampChannel");
    fx.ampGain     = get ("ampGain");
    fx.ampBass     = get ("ampBass");
    fx.ampMid      = get ("ampMid");
    fx.ampTreble   = get ("ampTreble");
    fx.ampPresence = get ("ampPresence");
    fx.ampMaster   = get ("ampMaster");
    fx.cabOn       = get ("cabOn") > 0.5f;
    fx.cabType     = (int) get ("cabType");
    fx.cabMic      = get ("cabMic");
    fx.revMix      = get ("revMix");
    fx.revSize     = get ("revSize");
    fx.outGain     = get ("outGain");
}

void AIOGuitarProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();

    keyboardState.processNextMidiBuffer (midi, 0, n, true);

    // keyswitches must act before notes that share the same timestamp
    {
        bool hasKs = false;
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOnOrOff() && GuitarSynth::isKeyswitch (meta.getMessage().getNoteNumber())) { hasKs = true; break; }
        if (hasKs)
        {
            auto& sorted = sortedMidi;
            sorted.clear();
            for (const auto meta : midi)
                if (meta.getMessage().isNoteOnOrOff() && GuitarSynth::isKeyswitch (meta.getMessage().getNoteNumber()))
                    sorted.addEvent (meta.getMessage(), meta.samplePosition);
            for (const auto meta : midi)
                if (! (meta.getMessage().isNoteOnOrOff() && GuitarSynth::isKeyswitch (meta.getMessage().getNoteNumber())))
                    sorted.addEvent (meta.getMessage(), meta.samplePosition);
            midi.swapWith (sorted);
        }
    }
    const int preview = voiceParams.previewArticulation.exchange (-1);
    if (preview >= 0) synth.select (preview);

    // mod wheel -> palm mute
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isController() && msg.getControllerNumber() == 1)
            voiceParams.modWheel.store (msg.getControllerValue() / 127.0f);
        else if (msg.isChannelPressure())
            voiceParams.aftertouch.store (msg.getChannelPressureValue() / 127.0f);
    }

    // pick up a newly loaded kit (never blocks the audio thread). Notes of the old kit are
    // faded out first (12 ms choke), then the kits are swapped once every voice has stopped.
    {
        const juce::SpinLock::ScopedTryLockType tl (kitLock);
        if (tl.isLocked() && kitChanged && retiredKit == nullptr)
        {
            bool anyActive = false;
            for (int i = 0; i < synth.getNumVoices(); ++i)
                if (auto* v = dynamic_cast<KitVoice*> (synth.getVoice (i)))
                    if (v->isVoiceActive()) { anyActive = true; v->choke(); }
            if (! kitSwapWaiting) { kitSwapWaiting = true; kitSwapWait = 0; }
            kitSwapWait += n;
            if (! anyActive || kitSwapWait > (int) (sr * 0.1))
            {
                if (anyActive) synth.allNotesOff (0, false);   // safety net only
                retiredKit = std::move (audioKit);
                audioKit = std::move (pendingKit);
                kitChanged = false;
                kitSwapWaiting = false;
            }
        }
    }
    voiceParams.kit = kitSwapWaiting ? nullptr : audioKit.get();   // no new notes on a kit that is being swapped out

    readParams();

    // ---- SCORE: transport, capture from Logic, or play the score ourselves
    {
        const juce::SpinLock::ScopedTryLockType tl (tableLock);
        if (tl.isLocked() && tableChanged && retiredTable == nullptr)
        {
            retiredTable = std::move (audioTable);
            audioTable = std::move (pendingTable);
            tableChanged = false;
        }
    }
    double ppq = 0.0, bpm = 120.0;
    bool playing = false, havePpq = false;
    juce::int64 timeInSamples = -1;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto ts = pos->getTimeInSamples()) timeInSamples = *ts;
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 400.0, *b);
            if (auto q = pos->getPpqPosition()) { ppq = *q; havePpq = true; }
            if (auto ts = pos->getTimeSignature()) { hostNum.store (ts->numerator); hostDen.store (ts->denominator); }
        }
    hostBpm.store (bpm);
    const double ppqPerSample = bpm / 60.0 / sr;
    const int req = previewRequest.exchange (0);
    if (req == 1) { previewRunning = true; previewPpq = previewFrom.load(); synth.allNotesOff (0, true); }
    if (req == 2 && previewRunning) { previewRunning = false; synth.allNotesOff (0, true); }

    const bool syncPlay = ! previewRunning && get ("scoreMode") > 0.5f && playing && havePpq && audioTable != nullptr && ! audioTable->entries.empty();
    // a jump (cycle, click in the ruler): judged on the sample position when the host gives it,
    // so a tempo change inside a block is not mistaken for one
    bool jumped = false;
    if (timeInSamples >= 0 && expectedSample >= 0) jumped = std::abs (timeInSamples - expectedSample) > 2;
    if (havePpq && expectedPpq >= 0.0 && ppq < expectedPpq - 0.01) jumped = true;   // went backwards (cycle), whatever the sample clock says
    else if (havePpq && expectedPpq >= 0.0)
        jumped = std::abs (ppq - expectedPpq) > 0.01 + n * std::abs (bpm - lastBpm) / 60.0 / sr;
    if (syncWasPlaying && (! syncPlay || jumped)) synth.allNotesOff (0, true);   // stop / cycle jump: let held score notes ring out
    if (syncPlay && ! syncWasPlaying) synth.allNotesOff (0, true);              // host notes held from before must not hang
    // playback starts / jumps: articulation back to Normal, so a keyswitch latched at the end
    // of the last pass does not leak into the first notes (same result every time you press play)
    if (playing && havePpq && ! previewRunning && (expectedPpq < 0.0 || jumped)) synth.select (artNormal);
    syncWasPlaying = syncPlay;

    // capture passes (FOLLOW LOGIC): a pass = one continuous stretch of playback
    const bool cap = ! previewRunning && ! syncPlay && get ("scoreMode") < 0.5f && playing && havePpq;
    if (capturing && (! cap || jumped))
    {
        Score::CaptureEvent e; e.type = Score::CaptureEvent::passEnd; e.ppq = captureEnd;
        e.velocity = captureLost ? 1 : 0;   // lost events: the pass must not delete notes it "did not see"
        pushCapture (e);
        capturing = false;
    }
    if (cap && ! capturing)
    {
        Score::CaptureEvent e; e.type = Score::CaptureEvent::passStart; e.ppq = ppq;
        captureLost = false;
        pushCapture (e);
        capturing = true;
    }
    expectedPpq = playing && havePpq ? ppq + n * ppqPerSample : -1.0;
    expectedSample = playing && timeInSamples >= 0 ? timeInSamples + n : -1;
    lastBpm = bpm;
    if (capturing) captureEnd = expectedPpq;

    hookClock0 = synth.getClock();
    hookPpqPerSample = ppqPerSample;
    hookCapture = cap;
    hookApplyAll = previewRunning || syncPlay;
    if (previewRunning || syncPlay)
    {
        hookPpq0 = previewRunning ? previewPpq : ppq;
        renderScoreNotes (midi, hookPpq0, n, ppqPerSample);
        if (previewRunning)
        {
            previewPpq += n * ppqPerSample;
            if (audioTable == nullptr || previewPpq * Score::kTicks > (double) audioTable->lastEnd + Score::kTicks) { previewRunning = false; synth.allNotesOff (0, true); }
        }
    }
    else hookPpq0 = ppq;
    uiPpq.store (previewRunning ? previewPpq : ppq);
    uiHostPlaying.store (playing);
    uiPreview.store (previewRunning);

    synth.renderNextBlock (buffer, midi, 0, n);

    // guitar volume (smoothed)
    volume.setTargetValue (juce::Decibels::decibelsToGain (get ("gtVolume")));
    if (volume.isSmoothing())
        for (int i = 0; i < n; ++i)
        {
            const float g = volume.getNextValue();
            for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.getWritePointer (c)[i] *= g;
        }
    else
        buffer.applyGain (volume.getTargetValue());

    chain.process (buffer, n, fx);
}

//==============================================================================
// Score notes inside this block replace the notes coming from the host (keyswitches, CCs and
// UI auditions on channels 11-16 are kept).
void AIOGuitarProcessor::renderScoreNotes (juce::MidiBuffer& midi, double startPpq, int n, double ppqPerSample)
{
    scoreMidi.clear();
    for (const auto meta : midi)
    {
        const auto& m = meta.getMessage();
        if (m.isNoteOnOrOff() && m.getChannel() <= 10 && ! GuitarSynth::isKeyswitch (m.getNoteNumber())) continue;
        scoreMidi.addEvent (m, meta.samplePosition);
    }
    if (audioTable != nullptr && ppqPerSample > 0.0)
    {
        const double a = startPpq * Score::kTicks, b = (startPpq + n * ppqPerSample) * Score::kTicks;
        auto toSample = [&] (double tick) { return juce::jlimit (0, n - 1, (int) ((tick / Score::kTicks - startPpq) / ppqPerSample)); };
        const auto& entries = audioTable->entries;
        // notes that can still end in this block start no earlier than a - the longest note
        auto it = std::lower_bound (entries.begin(), entries.end(), (juce::int64) std::floor (a) - audioTable->maxLength - 1,
                                    [] (const Score::ApplyTable::Entry& e, juce::int64 t) { return e.tick < t; });
        for (; it != entries.end(); ++it)
        {
            const auto& e = *it;
            if ((double) e.tick >= b) break;
            const double off = (double) (e.tick + e.length);
            if (off >= a && off < b) scoreMidi.addEvent (juce::MidiMessage::noteOff (1, e.note), toSample (off));
            if ((double) e.tick >= a) scoreMidi.addEvent (juce::MidiMessage::noteOn (1, e.note, (juce::uint8) juce::jlimit (1, 127, e.velocity)), toSample ((double) e.tick));
        }
    }
    midi.swapWith (scoreMidi);
}

void AIOGuitarProcessor::timerCallback()
{
    const int stringMode = (int) get ("stringMode");
    captureFifo.popAll ([&] (const Score::CaptureEvent& e)
    {
        if (e.type == Score::CaptureEvent::passStart)
        {
            score.bpm = hostBpm.load(); score.numerator = hostNum.load(); score.denominator = hostDen.load();
        }
        score.handle (e, stringMode);
    });
    if (score.changed) { score.changed = false; publishTable(); ++scoreVersion; }
}

void AIOGuitarProcessor::publishTable()
{
    std::shared_ptr<const Score::ApplyTable> t = score.buildTable(), toFree;
    const juce::SpinLock::ScopedLockType sl (tableLock);
    toFree = std::move (retiredTable);
    retiredTable = nullptr;
    pendingTable = t;
    tableChanged = true;
}

void AIOGuitarProcessor::scoreEdited()
{
    score.orphaned = score.countOrphans();
    publishTable();
    ++scoreVersion;
}

void AIOGuitarProcessor::pushUndo()
{
    undoStack.push_back (score.overrides);
    if (undoStack.size() > 100) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

bool AIOGuitarProcessor::undo()
{
    if (undoStack.empty()) return false;
    redoStack.push_back (score.overrides);
    score.overrides = undoStack.back();
    undoStack.pop_back();
    scoreEdited();
    return true;
}

bool AIOGuitarProcessor::redo()
{
    if (redoStack.empty()) return false;
    undoStack.push_back (score.overrides);
    score.overrides = redoStack.back();
    redoStack.pop_back();
    scoreEdited();
    return true;
}

//==============================================================================
static bool readAudioFile (const juce::File& file, juce::AudioBuffer<float>& buf, double& sr)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (file));
    if (rd == nullptr) return false;
    const int n = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 600.0));
    buf.setSize ((int) std::max (1u, rd->numChannels), n);
    rd->read (&buf, 0, n, 0, true, true);
    sr = rd->sampleRate;
    return true;
}

void AIOGuitarProcessor::loadRecording (int slot, const juce::File& file, std::function<void (juce::String)> onDone)
{
    auto flag = alive;
    loader.addJob ([this, flag, slot, file, onDone]
    {
        juce::AudioBuffer<float> buf;
        double fileSr = 44100.0;
        Kit::SliceResult res;
        if (! readAudioFile (file, buf, fileSr)) res.message = "Can't read this file.";
        else res = Kit::slice (buf, fileSr, slot);

        juce::MessageManager::callAsync ([this, flag, slot, res = std::move (res), onDone]() mutable
        {
            if (! flag->load()) return;
            const auto msg = res.message;
            applySlice (slot, std::move (res));
            if (onDone) onDone (msg);
        });
    });
}

juce::String AIOGuitarProcessor::loadRecordingNow (int slot, const juce::File& file)
{
    juce::AudioBuffer<float> buf;
    double fileSr = 44100.0;
    if (! readAudioFile (file, buf, fileSr)) return "Can't read this file.";
    auto res = Kit::slice (buf, fileSr, slot);
    const auto msg = res.message;
    applySlice (slot, std::move (res));
    return msg;
}

void AIOGuitarProcessor::applySlice (int slot, Kit::SliceResult res)
{
    slotMessage[slot] = res.message;
    if (res.samples.empty()) return;   // keep what was there
    auto kit = editKit ? std::make_shared<Kit::GuitarKit> (*editKit) : std::make_shared<Kit::GuitarKit>();
    kit->samples.erase (std::remove_if (kit->samples.begin(), kit->samples.end(),
                                        [slot] (const Kit::Sample& s) { return s.slot == slot; }), kit->samples.end());
    for (auto& s : res.samples) kit->samples.push_back (std::move (s));
    publish (kit);
}

void AIOGuitarProcessor::clearSlot (int slot)
{
    if (! editKit) return;
    auto kit = std::make_shared<Kit::GuitarKit> (*editKit);
    kit->samples.erase (std::remove_if (kit->samples.begin(), kit->samples.end(),
                                        [slot] (const Kit::Sample& s) { return s.slot == slot; }), kit->samples.end());
    slotMessage[slot] = {};
    publish (kit->samples.empty() ? nullptr : kit);
}

void AIOGuitarProcessor::setKitName (const juce::String& name)
{
    // the name does not change the sound: rename in place (no re-encoding, no swap)
    if (! editKit || editKit->name == name) return;
    editKit->name = name;
    if (editKit->encoded.isValid()) editKit->encoded.setProperty ("name", name, nullptr);
    ++kitVersion;
}

bool AIOGuitarProcessor::saveKit (const juce::File& file)
{
    return editKit != nullptr && Kit::saveToFile (*editKit, file);
}

juce::String AIOGuitarProcessor::loadKit (const juce::File& file)
{
    auto kit = Kit::loadFromFile (file, get ("kitLevel") > 0.5f);
    if (kit == nullptr) return "Can't read this guitar file.";
    for (auto& m : slotMessage) m = {};
    publish (kit);
    return "Loaded " + kit->name;
}

void AIOGuitarProcessor::publish (std::shared_ptr<Kit::GuitarKit> kit)
{
    if (kit != nullptr)
    {
        Kit::normalise (*kit, get ("kitLevel") > 0.5f);
        if (! kit->encoded.isValid() || kit->encoded.getNumChildren() != (int) kit->samples.size()
            || kit->encoded.getProperty ("name").toString() != kit->name)
            kit->encoded = Kit::encode (*kit);
    }
    editKit = kit;
    ++kitVersion;

    std::shared_ptr<const Kit::GuitarKit> toFree;
    {
        const juce::SpinLock::ScopedLockType sl (kitLock);
        toFree = std::move (retiredKit);   // freed here, on the message thread
        retiredKit = nullptr;
        pendingKit = kit;
        kitChanged = true;
    }
}

void AIOGuitarProcessor::parameterChanged (const juce::String&, float) { triggerAsyncUpdate(); }

void AIOGuitarProcessor::handleAsyncUpdate()
{
    if (editKit) publish (std::make_shared<Kit::GuitarKit> (*editKit));   // re-apply level matching
}

//==============================================================================
int AIOGuitarProcessor::getNumPrograms() { return (int) getFactoryPresets().size(); }

const juce::String AIOGuitarProcessor::getProgramName (int index)
{
    const auto& p = getFactoryPresets();
    return juce::isPositiveAndBelow (index, (int) p.size()) ? juce::String (p[(size_t) index].name) : juce::String();
}

void AIOGuitarProcessor::setCurrentProgram (int index)
{
    const auto& presets = getFactoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size())) return;
    currentProgram = index;

    // Performance settings (play board) and the "My Guitar" switch are not part of the sound presets.
    static const juce::StringArray keep { "kitTune", "kitLevel", "kitVelSens", "kitHuman", "stringMode", "bendUp", "bendDown",
                                          "artBendStart", "artBendTime", "artHold", "slideRange",
                                          "vibRate", "vibDelay", "gtVolume", "gtRelease", "gtMute", "linkGroup", "scoreMode" };

    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            if (keep.contains (id)) continue;

            float norm = rp->getDefaultValue();
            for (auto& [pid, value] : presets[(size_t) index].values)
                if (id == pid)
                    norm = rp->convertTo0to1 (value);

            rp->setValueNotifyingHost (norm);
        }
}

// State = parameters + the MY GUITAR recordings (FLAC), as binary ValueTrees.
void AIOGuitarProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.removeChild (state.getChildWithName ("KIT"), nullptr);
    juce::MemoryOutputStream mos (destData, false);
    mos.writeInt (0x47494f41);   // "AIOG"
    mos.writeInt (3);
    state.writeToStream (mos);
    if (editKit != nullptr) editKit->encoded.writeToStream (mos);
    else juce::ValueTree ("NOKIT").writeToStream (mos);
    score.toTree().writeToStream (mos);
}

void AIOGuitarProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ValueTree tree, kitTree, scoreTree;
    juce::MemoryInputStream mis (data, (size_t) sizeInBytes, false);
    if (sizeInBytes > 8 && mis.readInt() == 0x47494f41)
    {
        const int version = mis.readInt();
        tree = juce::ValueTree::readFromStream (mis);
        kitTree = juce::ValueTree::readFromStream (mis);
        if (version >= 3 && ! mis.isExhausted()) scoreTree = juce::ValueTree::readFromStream (mis);
    }
    else if (auto xml = getXmlFromBinary (data, sizeInBytes))
        tree = juce::ValueTree::fromXml (*xml);

    if (! tree.hasType (apvts.state.getType())) return;
    currentProgram = tree.getProperty ("program", 0);
    apvts.replaceState (tree);

    // make sure every parameter object (incl. booleans) holds exactly the restored value
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const float norm = rp->convertTo0to1 (apvts.getRawParameterValue (rp->getParameterID())->load());
            if (std::abs (rp->getValue() - norm) > 1.0e-6f)
                rp->setValueNotifyingHost (norm);
        }

    for (auto& m : slotMessage) m = {};
    publish (kitTree.hasType ("KIT") ? Kit::decode (kitTree, get ("kitLevel") > 0.5f) : nullptr);
    score.fromTree (scoreTree);
    undoStack.clear(); redoStack.clear();
    score.orphaned = score.countOrphans();
    publishTable();
    ++scoreVersion;
}

void AIOGuitarProcessor::previewNote (int note, int string, bool on)
{
    // channels 11..16 = "play this on string 1..6" (UI audition only)
    const int ch = string >= 1 && string <= 6 ? 10 + string : 1;
    if (on) keyboardState.noteOn (ch, note, 0.6f);
    else    keyboardState.noteOff (ch, note, 0.0f);
}

juce::AudioProcessorEditor* AIOGuitarProcessor::createEditor() { return new AIOGuitarEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AIOGuitarProcessor(); }
