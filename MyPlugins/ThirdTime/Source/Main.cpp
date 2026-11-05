#include <JuceHeader.h>
#include "MainComponent.h"
#include "ThirdMac.h"

class ThirdTimeApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Third Time"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override           { return getCommandLineParameters().contains ("--snapshot"); }

    void initialise (const juce::String& args) override
    {
        // 確認用: --snapshot <フォルダ> で見本の画面を PNG に書き出して終わる
        if (args.contains ("--snapshot"))
        {
            const juce::File dir (args.fromFirstOccurrenceOf ("--snapshot", false, false).trim().unquoted());
            MainComponent c (false);
            c.loadDemo();
            juce::PNGImageFormat png;
            juce::FileOutputStream os (dir.getChildFile ("third_0.png"));
            os.setPosition (0); os.truncate();
            png.writeImageToStream (c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f), os);
            quit();
            return;
        }

        mainWindow = std::make_unique<MainWindow> (getApplicationName());
        auto* comp = mainWindow->content;
        comp->onShowRequest = [this]
        {
            if (mainWindow == nullptr) return;
            mainWindow->setVisible (true);
            if (auto* p = mainWindow->getPeer()) third::mac::showWindow (p->getNativeHandle());
        };
        comp->onHideRequest = [this] { if (mainWindow != nullptr) mainWindow->setVisible (false); };
        juce::PropertiesFile p (MainWindow::opts());
        if (! p.getBoolValue ("seen", false) || ! args.contains ("--background"))
        {
            p.setValue ("seen", true);
            comp->onShowRequest();
        }
    }

    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override { if (mainWindow) mainWindow->content->onShowRequest(); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainComponent* content = nullptr;
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff0f1216), DocumentWindow::closeButton | DocumentWindow::minimiseButton)
        {
            setUsingNativeTitleBar (true);
            content = new MainComponent();
            setContentOwned (content, true);
            setResizable (true, false);
            setResizeLimits (600, 460, 1800, 1400);
            centreWithSize (getWidth(), getHeight());
            const auto pos = juce::PropertiesFile (opts()).getValue ("window");
            if (pos.isNotEmpty()) restoreWindowStateFromString (pos);
        }
        ~MainWindow() override
        {
            juce::PropertiesFile p (opts());
            p.setValue ("window", getWindowStateAsString());
        }
        void closeButtonPressed() override { setVisible (false); }

        static juce::PropertiesFile::Options opts()
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "Third Time Window"; o.folderName = "Third Time"; o.filenameSuffix = ".settings";
            return o;
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (ThirdTimeApp)
