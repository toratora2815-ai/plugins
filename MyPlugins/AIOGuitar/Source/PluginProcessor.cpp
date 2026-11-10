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
    // STRUM: chords are played string by string (direction AUTO from the beat, or set in SCORE / D1 D#1)
    l.add (std::make_unique<APB> (juce::ParameterID { "strumOn", 1 }, "Strum", true));
    l.add (std::make_unique<APF> (juce::ParameterID { "strumSpeed", 1 }, "Strum Speed",
                                  juce::NormalisableRange<float> (0.0f, 30.0f, 0.1f, 0.6f), 7.0f, fmt (" ms", 1)));
    l.add (std::make_unique<APC> (juce::ParameterID { "upStrings", 1 }, "Up-stroke Strings",
                                  juce::StringArray { "Top 3", "Top 4", "All" }, 1));

    // ---- EFFECTS: Porch Drive / Wool Fuzz (same ranges and defaults as the stand-alone plugins)
    l.add (std::make_unique<APB> (juce::ParameterID { "ccOn", 1 }, "Comp On", false));
    l.add (std::make_unique<APF> (juce::ParameterID { "ccComp", 1 }, "Comp Amount", juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f, fmt ("", 1)));
    l.add (std::make_unique<APF> (juce::ParameterID { "ccLevel", 1 }, "Comp Level", juce::NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, fmt (" dB", 1)));
    l.add (std::make_unique<APF> (juce::ParameterID { "ccBlend", 1 }, "Comp Blend", juce::NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, fmt (" %", 0)));
    l.add (std::make_unique<APB> (juce::ParameterID { "pdOn", 1 }, "Drive On", false));
    l.add (std::make_unique<APF> (juce::ParameterID { "pdGain", 1 }, "Drive Gain", juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 4.0f, fmt ("", 1)));
    l.add (std::make_unique<APF> (juce::ParameterID { "pdTone", 1 }, "Drive Tone", juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f, fmt ("", 1)));
    l.add (std::make_unique<APF> (juce::ParameterID { "pdLevel", 1 }, "Drive Level", juce::NormalisableRange<float> (-30.0f, 12.0f, 0.1f), -6.0f, fmt (" dB", 1)));
    l.add (std::make_unique<APB> (juce::ParameterID { "wfOn", 1 }, "Fuzz On", false));
    l.add (std::make_unique<APF> (juce::ParameterID { "wfSustain", 1 }, "Fuzz Sustain", juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 6.0f, fmt ("", 1)));
    l.add (std::make_unique<APF> (juce::ParameterID { "wfTone", 1 }, "Fuzz Tone", juce::NormalisableRange<float> (0.0f, 10.0f, 0.01f), 5.0f, fmt ("", 1)));
    l.add (std::make_unique<APF> (juce::ParameterID { "wfVolume", 1 }, "Fuzz Volume", juce::NormalisableRange<float> (-36.0f, 6.0f, 0.1f), -12.0f, fmt (" dB", 1)));
    // the order of COMP / DRIVE / FUZZ / AMP (EFFECTS page; CAB and OUTPUT always last). Not automatable:
    // reordering in the middle of a song would only make level jumps
    l.add (std::make_unique<APC> (juce::ParameterID { "fxOrder", 2 }, "Effects Order", rig::orderNames(), rig::kDefault,
                                  juce::AudioParameterChoiceAttributes().withAutomatable (false)));

    // ---- Amp
    l.add (std::make_unique<APB> (juce::ParameterID { "ampOn", 1 }, "Amp On", true));
    l.add (std::make_unique<APC> (juce::ParameterID { "ampModel", 1 }, "Amp Model", ampsim::modelNames(), 3));
    l.add (knob ("ampGain",     "Amp Gain", 4.0f));
    l.add (knob ("ampBass",     "Bass", 5.0f));
    l.add (knob ("ampMid",      "Middle", 5.0f));
    l.add (knob ("ampTreble",   "Treble", 5.0f));
    l.add (knob ("ampPresence", "Presence", 5.0f));
    l.add (knob ("ampMaster",   "Master", 5.0f));

    // ---- Cabinet
    l.add (std::make_unique<APB> (juce::ParameterID { "cabOn", 1 }, "Cab On", true));
    l.add (std::make_unique<APC> (juce::ParameterID { "cabType", 1 }, "Cab Type", ampsim::cabNames(), 2));
    l.add (std::make_unique<APC> (juce::ParameterID { "cabMicType", 1 }, "Mic", ampsim::micNames(), 0));
    l.add (knob ("cabPos",  "Mic Position", 3.0f));
    l.add (knob ("cabDist", "Mic Distance", 2.0f));

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

    // ---- Output (no reverb inside since 0.3.0: reverbs go after AIO Guitar)
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
juce::AudioProcessor::BusesProperties AIOGuitarProcessor::makeBuses()
{
    auto b = BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
    // the stand-alone app listens to the guitar for the RECORD GUIDE (the AU stays an instrument without inputs)
    if (AIOGuitarProcessor::isStandaloneApp()) b = b.withInput ("Guitar In", juce::AudioChannelSet::stereo(), true);
    return b;
}

AIOGuitarProcessor::AIOGuitarProcessor()
    : AudioProcessor (makeBuses()),
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
        double ppq = hookPpq0 + (double) (synth.getClock() - hookClock0) * hookPpqPerSample;
        if (const double orig = strum.origPpqFor (note); orig >= 0.0) { ppq = orig; strum.clearOrig (note); }   // strummed: where Logic put it
        if (hookCapture)
        {
            Score::CaptureEvent e;
            e.type = Score::CaptureEvent::noteOn; e.ppq = ppq; e.note = note;
            e.velocity = juce::jlimit (1, 127, juce::roundToInt (vel * 127.0f)); e.channel = ch;
            e.logicArt = art; e.autoString = autoString;
            e.logicStrum = strum.logicStrumFor (note);
            pushCapture (e);
        }
        if (testNoteLog) testNoteLog (note, autoString, synth.strumUpForNote[note], synth.getClock());
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
    strum.onDroppedNoteOn = [this] (int ch, int note, int vel, double ppq, int string, int keyDir)
    {
        if (! hookCapture || ppq < 0.0) return;
        Score::CaptureEvent e;
        e.type = Score::CaptureEvent::noteOn; e.ppq = ppq; e.note = note; e.velocity = vel; e.channel = ch;
        e.logicArt = voiceParams.articulation; e.autoString = string; e.logicStrum = keyDir;
        pushCapture (e);
    };
    strum.onDroppedNoteOff = [this] (int ch, int note, double ppq)
    {
        if (! hookCapture || ppq < 0.0) return;
        Score::CaptureEvent e;
        e.type = Score::CaptureEvent::noteOff; e.ppq = ppq; e.note = note; e.channel = ch;
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
    if (isStandaloneApp()) inputTap.allocate();   // the RECORD GUIDE's buffer (8 MB), not in the AU
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
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    if (layouts.inputBuses.isEmpty()) return true;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void AIOGuitarProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
    volume.reset (sampleRate, 0.03);
    sortedMidi.ensureSize (16384);
    strum.prepare (sampleRate);
    scoreMidi.ensureSize (16384);
    previewRunning = capturing = syncWasPlaying = false;
    expectedPpq = -1.0;
    volume.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (get ("gtVolume")));
    readParams();
    rig.prepare (sampleRate, samplesPerBlock, (int) get ("fxOrder"), fx);
    rig.comp.snapParams (get ("ccComp"), get ("ccLevel"), get ("ccBlend"));
    rig.drive.snapParams (get ("pdGain"), get ("pdTone"), get ("pdLevel"));
    rig.fuzz.snapParams (get ("wfSustain"), get ("wfTone"), get ("wfVolume"));
    setLatencySamples (rig.latency());   // the same for every order
    quietSamples = 0;
    lastOutPeak = 1.0f;

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
    strumSet.on = get ("strumOn") > 0.5f;
    strumSet.msPerString = get ("strumSpeed");
    strumSet.upStrings = (int) get ("upStrings") >= 2 ? 6 : (int) get ("upStrings") + 3;   // 3, 4, all (6)
    strumSet.humanize = voiceParams.humanize;

    fx.ampOn       = get ("ampOn") > 0.5f;
    fx.ampModel    = (int) get ("ampModel");
    fx.ampGain     = get ("ampGain");
    fx.ampBass     = get ("ampBass");
    fx.ampMid      = get ("ampMid");
    fx.ampTreble   = get ("ampTreble");
    fx.ampPresence = get ("ampPresence");
    fx.ampMaster   = get ("ampMaster");
    fx.cabOn       = get ("cabOn") > 0.5f;
    fx.cabType     = (int) get ("cabType");
    fx.cabMicType  = (int) get ("cabMicType");
    fx.cabPos      = get ("cabPos");
    fx.cabDist     = get ("cabDist");
    fx.outGain     = get ("outGain");
}

void AIOGuitarProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    // RECORD GUIDE: hand the guitar input to the guide (never to the output)
    if (const int nIn = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels()); nIn > 0)
        inputTap.push (buffer.getArrayOfReadPointers(), nIn, n);
    else
        inputTap.channels.store (0);
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
    if (req == 1) { strum.flush (midi); previewRunning = true; previewPpq = previewFrom.load(); synth.allNotesOff (0, true); }
    if (req == 2 && previewRunning) { strum.flush (midi); previewRunning = false; synth.allNotesOff (0, true); }

    const bool syncPlay = ! previewRunning && get ("scoreMode") > 0.5f && playing && havePpq && audioTable != nullptr && ! audioTable->entries.empty();
    // a jump (cycle, click in the ruler): judged on the sample position when the host gives it,
    // so a tempo change inside a block is not mistaken for one
    bool jumped = false;
    if (timeInSamples >= 0 && expectedSample >= 0) jumped = std::abs (timeInSamples - expectedSample) > 2;
    if (havePpq && expectedPpq >= 0.0 && ppq < expectedPpq - 0.01) jumped = true;   // went backwards (cycle), whatever the sample clock says
    else if (havePpq && expectedPpq >= 0.0)
        jumped = std::abs (ppq - expectedPpq) > 0.01 + n * std::abs (bpm - lastBpm) / 60.0 / sr;
    if (syncWasPlaying && (! syncPlay || jumped)) { strum.flush (midi); synth.allNotesOff (0, true); }   // stop / cycle jump: let held score notes ring out
    if (syncPlay && ! syncWasPlaying) { strum.flush (midi); synth.allNotesOff (0, true); }              // host notes held from before must not hang
    if (jumped || (expectedPpq >= 0.0 && ! playing)) strum.flush (midi);       // strings still waiting to be strummed: drop
    if (expectedPpq >= 0.0 && ! playing && ! previewRunning) synth.allNotesOff (0, true);   // transport stopped: let everything ring out
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
            if (audioTable == nullptr || previewPpq * Score::kTicks > (double) audioTable->lastEnd + Score::kTicks)
            {
                previewRunning = false;
                strum.flush (midi);
                synth.allNotesOff (0, true);
            }
        }
    }
    else hookPpq0 = ppq;
    // STRUM: spread chords string by string (after the score notes are in, before the synth)
    strum.process (midi, n, strumSet, synth, audioTable.get(), (playing && havePpq) || previewRunning, hookPpq0, ppqPerSample,
                   previewRunning || syncPlay);
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

    // ---- EFFECTS: pedals and AMP in the chosen order -> CAB -> OUTPUT. Nothing playing and the tails gone: skip all of it (saves CPU)
    rig.comp.setOn (get ("ccOn") > 0.5f);
    rig.drive.setOn (get ("pdOn") > 0.5f);
    rig.fuzz.setOn (get ("wfOn") > 0.5f);
    rig.comp.setParams (get ("ccComp"), get ("ccLevel"), get ("ccBlend"));
    rig.drive.setParams (get ("pdGain"), get ("pdTone"), get ("pdLevel"));
    rig.fuzz.setParams (get ("wfSustain"), get ("wfTone"), get ("wfVolume"));

    const bool silentIn = buffer.getMagnitude (0, n) < 1.0e-6f && (buffer.getNumChannels() < 2 || buffer.getMagnitude (1, 0, n) < 1.0e-6f);
    quietSamples = silentIn ? quietSamples + n : 0;
    // rest once nothing comes in for 0.5 s AND what comes out (the cabinet's tail) has died away
    const bool sleeping = quietSamples > (int) (sr * 0.5) && lastOutPeak < 1.0e-5f;
    fxSleeping.store (sleeping);
    if (sleeping)
    {
        fx.offline = isNonRealtime();
        rig.rest (fx, (int) get ("fxOrder"), n);
        rig.comp.snapParams (get ("ccComp"), get ("ccLevel"), get ("ccBlend"));    // the next note starts as set
        rig.drive.snapParams (get ("pdGain"), get ("pdTone"), get ("pdLevel"));
        rig.fuzz.snapParams (get ("wfSustain"), get ("wfTone"), get ("wfVolume"));
        buffer.clear();
        return;
    }
    fx.offline = isNonRealtime();
    if (buffer.getNumChannels() >= 2)
        rig.process (buffer, n, fx, (int) get ("fxOrder"));

    // safety on the way out: linear up to -6 dBFS, rounded off above, never over 0 dBFS
    // (the pedals alone reach +26 dBFS, a clean amp turned all the way up +7)
        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            auto* d = buffer.getWritePointer (c);
            for (int i = 0; i < n; ++i)
            {
                const float x = d[i], a = std::abs (x);
                if (a > 0.5f) d[i] = std::copysign (0.5f + 0.5f * std::tanh ((a - 0.5f) * 2.0f), x);   // linear up to -6 dBFS, never above 0 dBFS
            }
        }
    lastOutPeak = buffer.getMagnitude (0, n);
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

void AIOGuitarProcessor::putTakes (const std::vector<Kit::Sample>& takes)
{
    if (takes.empty()) return;
    auto kit = editKit ? std::make_shared<Kit::GuitarKit> (*editKit) : std::make_shared<Kit::GuitarKit>();
    for (auto& t : takes)
    {
        kit->samples.erase (std::remove_if (kit->samples.begin(), kit->samples.end(), [&] (const Kit::Sample& s)
        {
            if (s.slot != t.slot || s.up != t.up) return false;
            return t.slot == Kit::slotBrush ? false : (s.string == t.string && s.fret == t.fret);
        }), kit->samples.end());
        kit->samples.push_back (t);
    }
    kit->encoded = {};   // re-encode
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
                                          "vibRate", "vibDelay", "gtVolume", "gtRelease", "gtMute", "linkGroup", "scoreMode",
                                          "strumOn", "strumSpeed", "upStrings" };   // pedals / amp ARE part of the sound presets

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
    state.removeChild (state.getChildWithName ("CABIR"), nullptr);
    {
        const juce::ScopedLock sl (irLock);
        if (cabIR.getNumSamples() > 0)
        {
            juce::ValueTree ir ("CABIR");
            ir.setProperty ("name", cabIRName, nullptr);
            ir.setProperty ("rate", cabIRRate, nullptr);
            ir.setProperty ("data", juce::var (juce::MemoryBlock (cabIR.getReadPointer (0), sizeof (float) * (size_t) cabIR.getNumSamples())), nullptr);
            state.appendChild (ir, nullptr);
        }
    }
    juce::MemoryOutputStream mos (destData, false);
    mos.writeInt (0x47494f41);   // "AIOG"
    mos.writeInt (3);
    state.writeToStream (mos);
    if (editKit != nullptr) editKit->encoded.writeToStream (mos);
    else juce::ValueTree ("NOKIT").writeToStream (mos);
    score.toTree().writeToStream (mos);
}

// "Pedal Order" (0.2.0 and before: Drive > Fuzz / Fuzz > Drive, the comp first and the amp last) -> "Effects Order"
static void migrateOrder (juce::ValueTree& tree)
{
    juce::ValueTree oldOrder, newOrder;
    for (auto c : tree)
        if (c.hasType ("PARAM"))
        {
            if (c["id"].toString() == "pedalOrder") oldOrder = c;
            if (c["id"].toString() == "fxOrder")    newOrder = c;
        }
    if (oldOrder.isValid() && ! newOrder.isValid())
    {
        juce::ValueTree m ("PARAM");
        m.setProperty ("id", "fxOrder", nullptr);
        m.setProperty ("value", (float) rig::fromOldPedalOrder ((float) oldOrder["value"] > 0.5f), nullptr);
        tree.appendChild (m, nullptr);
    }
    if (oldOrder.isValid()) tree.removeChild (oldOrder, nullptr);
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
    // projects saved before the AMP page was rebuilt (2026-11-09): channel -> model, the old cabinet numbers
    {
        auto param = [&tree] (const char* id) { for (auto c : tree) if (c.hasType ("PARAM") && c["id"].toString() == id) return c; return juce::ValueTree(); };
        auto oldCh = param ("ampChannel");
        if (oldCh.isValid() && ! param ("ampModel").isValid())
        {
            const int ch = juce::jlimit (0, 2, (int) (float) oldCh["value"]);
            juce::ValueTree m ("PARAM"); m.setProperty ("id", "ampModel", nullptr); m.setProperty ("value", (float) (ch == 0 ? 0 : (ch == 1 ? 3 : 4)), nullptr);
            tree.appendChild (m, nullptr);
            if (auto cab = param ("cabType"); cab.isValid() && ! param ("cabMicType").isValid())
            {
                const int old = juce::jlimit (0, 2, (int) (float) cab["value"]);   // 4x12 Closed, 2x12 Combo, 1x12 Open
                cab.setProperty ("value", (float) (old == 0 ? 2 : (old == 1 ? 1 : 0)), nullptr);
            }
        }
    }
    // projects / presets from before 0.3.0: "Pedal Order" (Drive > Fuzz / Fuzz > Drive) -> the order of all four blocks
    migrateOrder (tree);
    const auto irTree = tree.getChildWithName ("CABIR");
    tree.removeChild (irTree, nullptr);
    if (! irTree.isValid())
    {
        const juce::ScopedLock sl (irLock);
        cabIR.setSize (0, 0); cabIRName = {};
        rig.chain.getCab().clearIR();
    }
    if (irTree.isValid())
        if (auto* mb = irTree["data"].getBinaryData(); mb != nullptr && mb->getSize() >= sizeof (float) * 16)
        {
            juce::AudioBuffer<float> b (1, (int) (mb->getSize() / sizeof (float)));
            std::memcpy (b.getWritePointer (0), mb->getData(), sizeof (float) * (size_t) b.getNumSamples());
            setCabIR (std::move (b), (double) irTree.getProperty ("rate", 48000.0), irTree["name"].toString());
        }
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

//==============================================================================
// Cabinet IR (CAB "Loaded IR"): mono, at most 2 s, kept in the project
juce::String AIOGuitarProcessor::loadCabIR (const juce::File& file)
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (file));
    if (r == nullptr) return juce::String (juce::CharPointer_UTF8 (u8"読めないファイルです"));
    const int len = (int) juce::jmin<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 0.5));   // a cabinet IR is < 0.25 s; longer ones cost CPU for nothing
    if (len < 16) return juce::String (juce::CharPointer_UTF8 (u8"短すぎます"));
    juce::AudioBuffer<float> in ((int) r->numChannels, len);
    r->read (&in, 0, len, 0, true, true);
    juce::AudioBuffer<float> mono (1, len);
    mono.clear();
    for (int c = 0; c < in.getNumChannels(); ++c) mono.addFrom (0, 0, in, c, 0, len, 1.0f / (float) in.getNumChannels());
    if (mono.getMagnitude (0, 0, len) < 1.0e-6f) return juce::String (juce::CharPointer_UTF8 (u8"無音のファイルです"));
    setCabIR (std::move (mono), r->sampleRate, file.getFileNameWithoutExtension());
    if (auto* p = apvts.getParameter ("cabType")) p->setValueNotifyingHost (p->convertTo0to1 ((float) (ampsim::numCabs - 1)));
    return {};
}

void AIOGuitarProcessor::setCabIR (juce::AudioBuffer<float>&& ir, double rate, const juce::String& name)
{
    // the silent end costs CPU for nothing: cut where it stays 80 dB under the peak (+5 ms).
    // Here, so IRs saved in projects before this was added are cut too
    if (const int len = ir.getNumSamples(); len > 0)
    {
        const float peak = ir.getMagnitude (0, 0, len);
        int last = len - 1;
        while (last > 0 && std::abs (ir.getSample (0, last)) < peak * 1.0e-4f) --last;
        ir.setSize (1, juce::jmin (len, last + 1 + (int) (rate * 0.005)), true, false, true);
    }
    {
        const juce::ScopedLock sl (irLock);
        cabIR.makeCopyOf (ir);
        cabIRRate = rate;
        cabIRName = name;
    }
    rig.chain.getCab().loadIR (std::move (ir), rate);
}

juce::String AIOGuitarProcessor::getCabIRName() const
{
    const juce::ScopedLock sl (irLock);
    return cabIRName;
}
