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

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff111418), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, true);
            setResizeLimits (720, 820, 2400, 1600);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            if (auto* mc = dynamic_cast<MainComponent*> (getContentComponent())) mc->grabKeyboardFocus();
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (ReferenceMonitorApp)
