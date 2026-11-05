#include <JuceHeader.h>
#include "MainComponent.h"

class SoundSketchApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Sound Sketch"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override           { return getCommandLineParameters().contains ("--snapshot"); }

    void initialise (const juce::String& args) override
    {
        // 確認用: --snapshot <フォルダ> [<口マネの wav>] で画面を PNG に書き出して終わる
        if (args.contains ("--snapshot"))
        {
            auto rest = args.fromFirstOccurrenceOf ("--snapshot", false, false).trim();
            juce::StringArray parts;
            parts.addTokens (rest, " ", "\"");
            parts.removeEmptyStrings();
            const juce::File dir (parts[0].unquoted());
            MainComponent c (false);
            c.waitForLibrary();
            auto shot = [&] (const juce::String& name)
            {
                juce::PNGImageFormat png;
                juce::FileOutputStream os (dir.getChildFile (name));
                os.setPosition (0); os.truncate();
                png.writeImageToStream (c.createComponentSnapshot (c.getLocalBounds(), true, 2.0f), os);
            };
            shot ("sketch_0.png");
            if (parts.size() > 1) { c.searchWithFile (juce::File (parts[1].unquoted())); shot ("sketch_1.png"); }
            quit();
            return;
        }
        mainWindow = std::make_unique<MainWindow> (getApplicationName());
    }

    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override { if (mainWindow) mainWindow->toFront (true); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow (const juce::String& name)
            : DocumentWindow (name, juce::Colour (0xff0e1117), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent(), true);
            setResizable (true, false);
            setResizeLimits (640, 480, 2400, 1800);
            centreWithSize (getWidth(), getHeight());
            const auto pos = juce::PropertiesFile (opts()).getValue ("window");
            if (pos.isNotEmpty()) restoreWindowStateFromString (pos);
            setVisible (true);
        }
        ~MainWindow() override
        {
            juce::PropertiesFile p (opts());
            p.setValue ("window", getWindowStateAsString());
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

        static juce::PropertiesFile::Options opts()
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "Sound Sketch Window"; o.folderName = "Sound Sketch"; o.filenameSuffix = ".settings";
            return o;
        }
    };

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (SoundSketchApp)
