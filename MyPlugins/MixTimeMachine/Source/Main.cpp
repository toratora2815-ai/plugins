#include <JuceHeader.h>
#include "MainComponent.h"

class MixTimeMachineApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Mix Time Machine"; }
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

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff111418), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (1100, 720, 2400, 1600);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            if (auto* c = getContentComponent()) c->grabKeyboardFocus();
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (MixTimeMachineApp)
