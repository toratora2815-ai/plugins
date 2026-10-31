// Mac 以外（テスト用ビルド）の代わりの実装。音の取り込みと Music 操作は何もしない
#include "Capture.h"
#include "MusicControl.h"

#if ! JUCE_MAC
struct CaptureEngine::Impl {};
CaptureEngine::CaptureEngine (Engine& e) : impl (std::make_unique<Impl>()), engine (e) {}
CaptureEngine::~CaptureEngine() = default;
juce::Array<OutputDevice> CaptureEngine::listOutputDevices() { juce::Array<OutputDevice> a; a.add ({ "test-out", "Test Output" }); return a; }
juce::String CaptureEngine::defaultOutputUID() { return "test-out"; }
std::vector<AppAudioProcess> CaptureEngine::listAudioProcesses() { return {}; }
juce::String CaptureEngine::makeSignature (const juce::String& o, const juce::String& m, const juce::String& r, const std::vector<AppAudioProcess>&)
{ return o + "|" + m + "|" + r; }
juce::String CaptureEngine::start (const juce::String&, const juce::String&, const juce::String&) { return "Audio capture needs macOS 14.2+"; }
void CaptureEngine::stop() {}
bool CaptureEngine::isRunning() const { return false; }
double CaptureEngine::getSampleRate() const { return 48000.0; }
juce::String CaptureEngine::getLog() const { return {}; }

MusicControl::State MusicControl::poll() { State s; s.error = "unsupported"; return s; }
void MusicControl::setPosition (double) {}
void MusicControl::play() {}
void MusicControl::pause() {}
#endif
