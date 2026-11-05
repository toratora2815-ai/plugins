#include <JuceHeader.h>
#include "MainComponent.h"

class HumKeysApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Hum Keys"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override           { return getCommandLineParameters().contains ("--snapshot"); }

    void initialise (const juce::String& args) override
    {
        // 確認用: --snapshot <フォルダ> で、両方の画面を PNG に書き出して終わる
        if (args.contains ("--snapshot"))
        {
            const juce::File dir (args.fromFirstOccurrenceOf ("--snapshot", false, false).trim().unquoted());
            MainComponent c (false);
            for (int m = 0; m < 3; ++m)
            {
                c.showSnapshot (m);
                juce::PNGImageFormat png;
                juce::FileOutputStream os (dir.getChildFile ("humkeys_" + juce::String (m) + ".png"));
                os.setPosition (0); os.truncate();
                png.writeImageToStream (c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f), os);
            }
            c.writeTakeForSnapshot (dir.getChildFile ("humkeys_take.mid"));
            quit();
            return;
        }
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
            : DocumentWindow (name, juce::Colour (0xff0e1117), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* content = new MainComponent();
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (620, 470, 1800, 1300);
            if (! restoreWindowStateFromString (content->savedWindowState()))
                centreWithSize (getWidth(), getHeight());
            setVisible (true);
            content->applySavedViewState();
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

START_JUCE_APPLICATION (HumKeysApp)
