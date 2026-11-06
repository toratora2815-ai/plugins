#include "PluginProcessor.h"
#include "PluginEditor.h"

ForgeMixerProcessor::ForgeMixerProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", [] {
          std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
          v.push_back (plink::makeGroupParameter (1));
          return juce::AudioProcessorValueTreeState::ParameterLayout { v.begin(), v.end() };
      }())
{
    startTimerHz (30);
}

void ForgeMixerProcessor::command (float token, float gainDb, float pan, float width)
{
    const juce::ScopedLock l (lock);
    const double until = juce::Time::getMillisecondCounterHiRes() + 600.0;   // 放してから 0.6 秒は出し続ける
    for (auto& c : cmds) if (std::abs (c.token - token) < 0.5f) { c = { token, gainDb, pan, width, until }; return; }
    if ((int) cmds.size() >= fslink::mixer::maxCommands) cmds.erase (cmds.begin());
    cmds.push_back ({ token, gainDb, pan, width, until });
}

void ForgeMixerProcessor::timerCallback()
{
    auto& ep = link.ep();
    const double now = juce::Time::getMillisecondCounterHiRes();
    const juce::ScopedLock l (lock);
    cmds.erase (std::remove_if (cmds.begin(), cmds.end(), [now] (const Command& c) { return c.untilMs < now; }), cmds.end());
    for (int i = 0; i < (int) cmds.size(); ++i)
    {
        const int b = i * fslink::mixer::stride;
        ep.publish (b, cmds[(size_t) i].token); ep.publish (b + 1, cmds[(size_t) i].gainDb);
        ep.publish (b + 2, cmds[(size_t) i].pan); ep.publish (b + 3, cmds[(size_t) i].width);
    }
    ep.publish (fslink::mixer::count, (float) cmds.size());
    ep.stamp();
}

std::vector<ForgeMixerProcessor::Track> ForgeMixerProcessor::tracks() const
{
    std::vector<Track> out;
    if (! link.on()) return out;
    plink::Peer peers[plink::numSlots];
    const plink::Kind k = fslink::kindStrip;
    const int n = link.ep().list (&k, 1, peers, plink::numSlots);
    for (int i = 0; i < n; ++i)
    {
        const auto& p = peers[i];
        if (p.valuesAgeMs > 2000) continue;
        Track t;
        using namespace fslink::strip;
        t.token = p.v[token]; t.gainDb = p.v[gainDb]; t.pan = p.v[pan]; t.width = p.v[width];
        t.levelDb = p.v[levelDb]; t.low = p.v[lowDb]; t.mid = p.v[midDb]; t.high = p.v[highDb]; t.mute = p.v[mute] > 0.5f;
        t.name = p.name[0] != 0 ? juce::String::fromUTF8 (p.name) : "Track " + juce::String (p.slotIndex + 1);
        t.slot = p.slotIndex;
        out.push_back (t);
    }
    std::sort (out.begin(), out.end(), [] (const Track& a, const Track& b) { return a.name < b.name; });
    return out;
}

juce::AudioProcessorEditor* ForgeMixerProcessor::createEditor() { return new ForgeMixerEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ForgeMixerProcessor(); }
