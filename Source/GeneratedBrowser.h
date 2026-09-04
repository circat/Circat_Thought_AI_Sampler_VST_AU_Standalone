#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <algorithm>

// Lightweight overlay browser for the generated one-shots kept in the temp
// folder. Newest first. LOAD hands a file back to the editor; EXPORT copies it
// somewhere permanent; the temp folder is never touched by the user directly.
class GeneratedBrowser final : public juce::Component,
                               private juce::ListBoxModel,
                               private juce::Timer
{
public:
    std::function<void (juce::File)> onLoad;
    std::function<void ()> onClose;

    explicit GeneratedBrowser (juce::File directory) : dir (std::move (directory))
    {
        formats.registerBasicFormats();
        list.setModel (this);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff10090c));
        list.setRowHeight (26);
        addAndMakeVisible (list);

        auto style = [this] (juce::TextButton& b, const juce::String& t)
        {
            b.setButtonText (t);
            b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232732));
            b.setColour (juce::TextButton::textColourOffId, juce::Colour (0xffcbd0d7));
            addAndMakeVisible (b);
        };
        style (loadBtn, "LOAD");
        style (exportBtn, "EXPORT\xE2\x80\xA6");
        style (revealBtn, "SHOW FOLDER");
        style (closeBtn, "CLOSE");
        loadBtn.onClick  = [this] { doLoad(); };
        exportBtn.onClick = [this] { doExport(); };
        revealBtn.onClick = [this] { dir.revealToUser(); };
        closeBtn.onClick  = [this] { if (onClose) onClose(); };

        refresh();
        startTimerHz (2);
        setInterceptsMouseClicks (true, true);
    }

    void refresh()
    {
        auto found = dir.findChildFiles (juce::File::findFiles, false, "*.wav");
        found.sort();
        std::reverse (found.begin(), found.end()); // newest (timestamped name) first
        if (found == files) return;
        files = std::move (found);
        list.updateContent();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.7f));
        g.setColour (juce::Colour (0xff1a1d24));
        g.fillRoundedRectangle (panel().toFloat(), 8.0f);
        g.setColour (juce::Colour (0x33ffffff));
        g.drawRoundedRectangle (panel().toFloat(), 8.0f, 1.0f);
        g.setColour (juce::Colour (0xffd9a557));
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        g.drawText ("GENERATED SAMPLES", panel().reduced (18).removeFromTop (22),
                    juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff5f6672));
        g.setFont (11.0f);
        g.drawText (juce::String (files.size()) + " in temp \xE2\x80\x94 export to keep permanently",
                    panel().reduced (18).removeFromTop (40).removeFromBottom (16),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto p = panel().reduced (18);
        p.removeFromTop (46);
        auto row = p.removeFromBottom (30);
        loadBtn.setBounds (row.removeFromLeft (90));       row.removeFromLeft (8);
        exportBtn.setBounds (row.removeFromLeft (100));    row.removeFromLeft (8);
        revealBtn.setBounds (row.removeFromLeft (120));
        closeBtn.setBounds (row.removeFromRight (90));
        p.removeFromBottom (10);
        list.setBounds (p);
    }

private:
    juce::Rectangle<int> panel() const
    {
        return juce::Rectangle<int> (0, 0, juce::jmin (620, getWidth() - 80),
                                     juce::jmin (460, getHeight() - 80))
            .withCentre (getLocalBounds().getCentre());
    }

    int getNumRows() override { return files.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
    {
        if (! juce::isPositiveAndBelow (row, files.size())) return;
        if (selected) { g.setColour (juce::Colour (0xff313747)); g.fillRect (0, 0, w, h); }
        const auto& f = files.getReference (row);
        g.setColour (juce::Colour (0xffe8eaed));
        g.setFont (12.0f);
        g.drawText (f.getFileNameWithoutExtension(), 10, 0, w - 120, h, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff9ba1ab));
        g.setFont (10.5f);
        g.drawText (juce::String (f.getSize() / 1024) + " KB", w - 110, 0, 100, h,
                    juce::Justification::centredRight);
    }

    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { doLoad(); }

    void doLoad()
    {
        const int row = list.getSelectedRow();
        if (juce::isPositiveAndBelow (row, files.size()) && onLoad)
            onLoad (files.getReference (row));
    }

    void doExport()
    {
        const int row = list.getSelectedRow();
        if (! juce::isPositiveAndBelow (row, files.size())) return;
        const auto source = files.getReference (row);
        chooser = std::make_unique<juce::FileChooser> ("Export generated sample", juce::File(), "*.wav");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [source] (const juce::FileChooser& fc)
            {
                const auto dest = fc.getResult();
                if (dest != juce::File())
                    source.copyFileTo (dest.withFileExtension ("wav"));
            });
    }

    void timerCallback() override { refresh(); }

    juce::File dir;
    juce::Array<juce::File> files;
    juce::AudioFormatManager formats;
    juce::ListBox list;
    juce::TextButton loadBtn, exportBtn, revealBtn, closeBtn;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GeneratedBrowser)
};
