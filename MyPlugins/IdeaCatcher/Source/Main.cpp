#include <JuceHeader.h>
#include "MainComponent.h"
#include "MacSupport.h"

class IdeaCatcherApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Idea Catcher"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override           { return getCommandLineParameters().contains ("--snapshot"); }

    void initialise (const juce::String& args) override
    {
        // 確認用: --snapshot <フォルダ> [<声の wav>] で画面を PNG に書き出して終わる
        if (args.contains ("--snapshot"))
        {
            auto rest = args.fromFirstOccurrenceOf ("--snapshot", false, false).trim();
            juce::StringArray parts;
            parts.addTokens (rest, " ", "\"");
            const juce::File dir (parts[0].unquoted());
            MainComponent c (false);
            auto shot = [&] (const juce::String& name)
            {
                juce::PNGImageFormat png;
                juce::FileOutputStream os (dir.getChildFile (name));
                os.setPosition (0); os.truncate();
                png.writeImageToStream (c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f), os);
            };
            shot ("idea_0.png");
            if (parts.size() > 1 && c.loadForSnapshot (juce::File (parts[1].unquoted())))
            {
                shot ("idea_1.png");
                c.command ("edit");
                shot ("idea_2.png");
                std::cout << "midi: " << c.midiFile().getFullPathName() << std::endl;
            }
            quit();
            return;
        }

        mainWindow = std::make_unique<MainWindow> (getApplicationName());
        auto* comp = mainWindow->content;
        comp->isWindowVisible = [this] { return mainWindow != nullptr && mainWindow->isVisible() && ! mainWindow->isMinimised(); };
        comp->onShowRequest = [this] (bool floating, bool focus)
        {
            if (mainWindow == nullptr) return;
            mainWindow->setVisible (true);
            if (auto* peer = mainWindow->getPeer())
                mac::showWindow (peer->getNativeHandle(), floating, focus);
        };
        // 動いているときにアイコンから開かれたら、画面を出す
        mac::onReopen ([this] { if (mainWindow != nullptr) mainWindow->content->showMe (true); });
        // 初めて起動したとき（とログイン時以外で開いたとき）は画面を出す
        if (! args.contains ("--background")) comp->showMe (true);
    }

    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr) mainWindow->content->showMe (true);
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainComponent* content = nullptr;
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff0e1117), DocumentWindow::closeButton | DocumentWindow::minimiseButton)
        {
            setUsingNativeTitleBar (true);
            content = new MainComponent();
            setContentOwned (content, true);
            setResizable (false, false);
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
        void moved() override { DocumentWindow::moved(); }

        static juce::PropertiesFile::Options opts()
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "Idea Catcher Window"; o.folderName = "Idea Catcher"; o.filenameSuffix = ".settings";
            return o;
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (IdeaCatcherApp)
