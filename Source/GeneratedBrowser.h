#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <algorithm>

// Lightweight overlay browser for the generated one-shots kept in the temp
// folder. Newest first. LOAD hands a file back to the editor; EXPORT copies it
// somewhere permanent; the temp folder is never touched by the user directly.
// Selecting a row auto-auditions it through the processor's preview player.
class GeneratedBrowser final : public juce::Component,
                               private juce::ListBoxModel,
                               private juce::Timer
{
public:
    std::function<void (juce::File)> onLoad;
    std::function<void ()> onClose;
    std::function<void (juce::File)> onPreview;   // start audition
    std::function<void ()> onStopPreview;
    std::function<void (float)> onPreviewGainDb;  // preview level changed
    std::function<bool ()> isPreviewPlaying;

    explicit GeneratedBrowser (juce::File directory, float initialGainDb = -6.0f)
        : dir (std::move (directory))
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
        style (playBtn, juce::CharPointer_UTF8 ("\xE2\x96\xB6"));
        style (stopBtn, juce::CharPointer_UTF8 ("\xE2\x96\xA0"));
        style (loadBtn, "LOAD");
        style (exportBtn, juce::CharPointer_UTF8 ("EXPORT\xE2\x80\xA6"));
        style (revealBtn, "SHOW FOLDER");
        style (closeBtn, "CLOSE");
        playBtn.onClick  = [this] { previewSelected(); };
        stopBtn.onClick  = [this] { if (onStopPreview) onStopPreview(); };
        loadBtn.onClick  = [this] { doLoad(); };
        exportBtn.onClick = [this] { doExport(); };
        revealBtn.onClick = [this] { dir.revealToUser(); };
        closeBtn.onClick  = [this] { stopPreview(); if (onClose) onClose(); };

        autoPlay.setButtonText ("AUTOPLAY");
        autoPlay.setToggleState (true, juce::dontSendNotification);
        autoPlay.setColour (juce::ToggleButton::textColourId, juce::Colour (0xffcbd0d7));
        addAndMakeVisible (autoPlay);

        gain.setSliderStyle (juce::Slider::LinearHorizontal);
        gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        gain.setRange (-60.0, 6.0, 0.1);
        gain.setValue (initialGainDb, juce::dontSendNotification);
        gain.setColour (juce::Slider::trackColourId, juce::Colour (0xff9ed6b4));
        gain.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
        gain.onValueChange = [this] { if (onPreviewGainDb) onPreviewGainDb ((float) gain.getValue()); };
        addAndMakeVisible (gain);

        refresh();
        startTimerHz (4);
        setInterceptsMouseClicks (true, true);
    }

    ~GeneratedBrowser() override { stopPreview(); }

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
        g.drawText (juce::String (files.size()) + juce::String (juce::CharPointer_UTF8 (" in temp \xE2\x80\x94 export to keep permanently")),
                    panel().reduced (18).removeFromTop (40).removeFromBottom (16),
                    juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto p = panel().reduced (18);
        p.removeFromTop (46);

        auto bottom = p.removeFromBottom (30);
        loadBtn.setBounds (bottom.removeFromLeft (84));    bottom.removeFromLeft (8);
        exportBtn.setBounds (bottom.removeFromLeft (92));  bottom.removeFromLeft (8);
        revealBtn.setBounds (bottom.removeFromLeft (112));
        closeBtn.setBounds (bottom.removeFromRight (80));
        p.removeFromBottom (8);

        auto transport = p.removeFromBottom (26);
        playBtn.setBounds (transport.removeFromLeft (34));   transport.removeFromLeft (4);
        stopBtn.setBounds (transport.removeFromLeft (34));   transport.removeFromLeft (10);
        autoPlay.setBounds (transport.removeFromLeft (96));  transport.removeFromLeft (8);
        gain.setBounds (transport);
        p.removeFromBottom (8);

        list.setBounds (p);
    }

private:
    juce::Rectangle<int> panel() const
    {
        return juce::Rectangle<int> (0, 0, juce::jmin (620, getWidth() - 80),
                                     juce::jmin (480, getHeight() - 80))
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

    void selectedRowsChanged (int) override { if (autoPlay.getToggleState()) previewSelected(); }
    void listBoxItemClicked (int, const juce::MouseEvent&) override { if (autoPlay.getToggleState()) previewSelected(); }
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { doLoad(); }

    void previewSelected()
    {
        const int row = list.getSelectedRow();
        if (juce::isPositiveAndBelow (row, files.size()) && onPreview)
            onPreview (files.getReference (row));
    }

    void stopPreview() { if (onStopPreview) onStopPreview(); }

    void doLoad()
    {
        const int row = list.getSelectedRow();
        if (juce::isPositiveAndBelow (row, files.size()) && onLoad)
        {
            stopPreview();
            onLoad (files.getReference (row));
        }
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
    juce::TextButton playBtn, stopBtn, loadBtn, exportBtn, revealBtn, closeBtn;
    juce::ToggleButton autoPlay;
    juce::Slider gain;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GeneratedBrowser)
};
