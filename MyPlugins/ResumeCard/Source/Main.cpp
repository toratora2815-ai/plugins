#include <JuceHeader.h>
#include "MainComponent.h"

class ResumeCardApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Resume Card"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override           { return getCommandLineParameters().contains ("--snapshot"); }

    void initialise (const juce::String& args) override
    {
        // 確認用: --snapshot <フォルダ> で見本のカードを PNG に書き出して終わる
        if (args.contains ("--snapshot"))
        {
            const juce::File dir (args.fromFirstOccurrenceOf ("--snapshot", false, false).trim().unquoted());
            MainComponent c (false);
            c.loadDemo();
            juce::PNGImageFormat png;
            juce::FileOutputStream os (dir.getChildFile ("resume_0.png"));
            os.setPosition (0); os.truncate();
            png.writeImageToStream (c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f), os);
            quit();
            return;
        }

        mainWindow = std::make_unique<MainWindow> (getApplicationName());
        auto* comp = mainWindow->content;
        comp->onShowRequest = [this] (bool focus)
        {
            if (mainWindow == nullptr) return;
            mainWindow->setVisible (true);
            if (auto* peer = mainWindow->getPeer()) rc::showWindow (peer->getNativeHandle(), focus);
        };
        comp->onHideRequest = [this] { if (mainWindow != nullptr) mainWindow->setVisible (false); };

        // 初めて起動したときは画面を出す（2 回目からはメニューバーで待つ）
        juce::PropertiesFile p (MainWindow::opts());
        if (! p.getBoolValue ("seen", false) || ! args.contains ("--background"))
        {
            p.setValue ("seen", true);
            comp->showLatest (true);
        }
    }

    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override { if (mainWindow != nullptr) mainWindow->content->showLatest (true); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainComponent* content = nullptr;
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff0f1115), DocumentWindow::closeButton | DocumentWindow::minimiseButton)
        {
            setUsingNativeTitleBar (true);
            content = new MainComponent();
            setContentOwned (content, true);
            setResizable (true, false);
            setResizeLimits (760, 480, 2000, 1400);
            centreWithSize (getWidth(), getHeight());
            const auto pos = juce::PropertiesFile (opts()).getValue ("window");
            if (pos.isNotEmpty()) restoreWindowStateFromString (pos);
        }
        ~MainWindow() override
        {
            juce::PropertiesFile p (opts());
            p.setValue ("window", getWindowStateAsString());
        }
        // 閉じてもアプリは終わらない（メニューバーで待つ）
        void closeButtonPressed() override { setVisible (false); }

        static juce::PropertiesFile::Options opts()
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "Resume Card Window"; o.folderName = "Resume Card"; o.filenameSuffix = ".settings";
            return o;
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (ResumeCardApp)
