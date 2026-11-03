#include <JuceHeader.h>
#include "MainComponent.h"

class ReferenceMonitorApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Reference Monitor"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override           { return false; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr) mainWindow->toFront (true);
    }

    class MainWindow : public juce::DocumentWindow,
                       private juce::Timer
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff111418), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* content = new MainComponent();
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (720, 820, 2400, 1600);
            // 前に置いた場所に出す（その画面がつながっていなければ、今ある画面に収める）
            if (! restoreWindowStateFromString (content->savedWindowState()))
                centreWithSize (getWidth(), getHeight());
            restored = true;
            setVisible (true);
            content->grabKeyboardFocus();
        }
        ~MainWindow() override { stopTimer(); saveState(); }

        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }

        // 動かしたり大きさを変えたりしたら、少し待ってから覚える（急に終了しても位置が残るように）
        void moved() override   { DocumentWindow::moved();   if (restored) startTimer (800); }
        void resized() override { DocumentWindow::resized(); if (restored) startTimer (800); }

    private:
        bool restored = false;

        void timerCallback() override { stopTimer(); saveState(); }
        void saveState()
        {
            if (auto* mc = dynamic_cast<MainComponent*> (getContentComponent()))
                mc->saveWindowState (getWindowStateAsString());
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (ReferenceMonitorApp)
