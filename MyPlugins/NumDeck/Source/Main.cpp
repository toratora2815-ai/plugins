#include <JuceHeader.h>
#include "MainComponent.h"

class NumDeckApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Num Deck"; }
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
            : DocumentWindow (name, juce::Colour (0xff0d1218), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* content = new MainComponent();
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (360, 250, 1800, 1300);
            if (! restoreWindowStateFromString (content->savedWindowState()))
                centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        ~MainWindow() override
        {
            if (auto* c = dynamic_cast<MainComponent*> (getContentComponent()))
                c->saveWindowState (getWindowStateAsString());
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (NumDeckApp)
