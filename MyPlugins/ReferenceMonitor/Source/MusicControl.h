#pragma once
// Mac の「ミュージック」アプリを外から操作する（再生位置の取得・移動、再生・一時停止）
// 初回だけ「ミュージックを操作してよいか」の許可が出る
#include <JuceHeader.h>

class MusicControl
{
public:
    struct State
    {
        bool running = false, playing = false;
        double position = 0.0, duration = 0.0;
        juce::String title, artist, trackId, error;
    };

    // メッセージスレッドから呼ぶ
    State poll();
    void setPosition (double seconds);
    void play();
    void pause();
};
