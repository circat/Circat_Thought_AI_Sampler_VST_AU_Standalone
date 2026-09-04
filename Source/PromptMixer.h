#pragma once

#include "PromptData.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// Prompt builder overlay: pick a SOUND TYPE (icon), an INSTRUMENT (from the
// generated table), optional HARMONY (chord only) and CHARACTER tags. The four
// axes are assembled into one comma-separated prompt string.
class PromptMixer final : public juce::Component,
                          private juce::ListBoxModel
{
public:
    std::function<void (juce::String)> onUsePrompt;
    std::function<void ()> onClose;

    struct SoundType { const char* name; const char* phrase; int glyph; };

    PromptMixer()
    {
        for (int i = 0; i < numTypes; ++i)
        {
            auto* b = typeButtons.add (new GlyphToggle (kTypes[i].name, kTypes[i].glyph));
            b->setRadioGroupId (100);
            b->setClickingTogglesState (true);
            b->onClick = [this] { syncEnable(); rebuildPreview(); };
            addAndMakeVisible (b);
        }
        typeButtons[0]->setToggleState (true, juce::dontSendNotification);

        groupBox.addItem ("ALL GROUPS", 1);
        for (int i = 0; i < promptdata::kInstrumentGroupCount; ++i)
            groupBox.addItem (promptdata::kInstrumentGroups[i], i + 2);
        groupBox.setSelectedId (1, juce::dontSendNotification);
        groupBox.onChange = [this] { applyFilter(); };
        addAndMakeVisible (groupBox);

        search.setTextToShowWhenEmpty ("search instruments…", juce::Colour (0xff5f6672));
        search.onTextChange = [this] { applyFilter(); };
        addAndMakeVisible (search);

        list.setModel (this);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff10090c));
        list.setRowHeight (22);
        addAndMakeVisible (list);

        rootBox.addItemList ({ "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }, 1);
        rootBox.setSelectedId (1, juce::dontSendNotification);
        rootBox.onChange = [this] { rebuildPreview(); };
        addAndMakeVisible (rootBox);
        qualityBox.addItemList ({ "maj","min","sus2","sus4","dim","maj7","min7","7","add9" }, 1);
        qualityBox.setSelectedId (2, juce::dontSendNotification);
        qualityBox.onChange = [this] { rebuildPreview(); };
        addAndMakeVisible (qualityBox);

        for (int i = 0; i < numChars; ++i)
        {
            auto* c = charChips.add (new juce::ToggleButton (kChars[i]));
            c->setColour (juce::ToggleButton::textColourId, juce::Colour (0xffcbd0d7));
            c->onClick = [this] { rebuildPreview(); };
            addAndMakeVisible (c);
        }

        preview.setMultiLine (true, true);
        preview.setReadOnly (true);
        preview.setCaretVisible (false);
        preview.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0b0d0f));
        preview.setColour (juce::TextEditor::textColourId, juce::Colour (0xff9ed6b4));
        preview.setColour (juce::TextEditor::outlineColourId, juce::Colour (0x22ffffff));
        preview.setFont (juce::Font (12.0f));
        addAndMakeVisible (preview);

        auto style = [this] (juce::TextButton& b, const juce::String& t, juce::Colour text)
        {
            b.setButtonText (t);
            b.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff232732));
            b.setColour (juce::TextButton::textColourOffId, text);
            addAndMakeVisible (b);
        };
        style (useBtn, "USE PROMPT", juce::Colour (0xffd9a557));
        style (closeBtn, "CLOSE", juce::Colour (0xffcbd0d7));
        useBtn.onClick = [this] { if (onUsePrompt) onUsePrompt (assemble()); if (onClose) onClose(); };
        closeBtn.onClick = [this] { if (onClose) onClose(); };

        applyFilter();
        syncEnable();
        rebuildPreview();
        setInterceptsMouseClicks (true, true);
    }

    // Pre-select a type/instrument/character set (used by the quick templates).
    void preset (const juce::String& typeName, const juce::String& instrumentName,
                 const juce::StringArray& characters)
    {
        for (int i = 0; i < numTypes; ++i)
            typeButtons[i]->setToggleState (juce::String (kTypes[i].name).equalsIgnoreCase (typeName),
                                            juce::dontSendNotification);
        for (int i = 0; i < promptdata::kInstrumentCount; ++i)
            if (instrumentName.isNotEmpty()
                && juce::String (promptdata::kInstruments[i].name).containsIgnoreCase (instrumentName))
            { selectedInstrument = i; break; }
        for (int i = 0; i < numChars; ++i)
            charChips[i]->setToggleState (characters.contains (kChars[i], true), juce::dontSendNotification);
        applyFilter();
        syncEnable();
        rebuildPreview();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::black.withAlpha (0.72f));
        g.setColour (juce::Colour (0xff1a1d24));
        g.fillRoundedRectangle (panel().toFloat(), 8.0f);
        g.setColour (juce::Colour (0x33ffffff));
        g.drawRoundedRectangle (panel().toFloat(), 8.0f, 1.0f);
        g.setColour (juce::Colour (0xffd9a557));
        g.setFont (juce::Font (13.0f, juce::Font::bold));
        g.drawText ("PROMPT BUILDER", panel().reduced (18).removeFromTop (20), juce::Justification::centredLeft);

        auto section = [&g] (juce::Rectangle<int> r, const juce::String& t)
        {
            g.setColour (juce::Colour (0xff5f6672));
            g.setFont (10.0f);
            g.drawText (t, r, juce::Justification::topLeft, false);
        };
        section ({ colType.getX(), colType.getY() - 14, 120, 12 }, "SOUND TYPE");
        section ({ colInst.getX(), colInst.getY() - 14, 120, 12 }, "INSTRUMENT");
        section ({ colRight.getX(), colRight.getY() - 14, 120, 12 }, "HARMONY / CHARACTER");
    }

    void resized() override
    {
        auto p = panel().reduced (18);
        p.removeFromTop (28);
        auto bottom = p.removeFromBottom (30);
        useBtn.setBounds (bottom.removeFromRight (120));
        bottom.removeFromRight (8);
        closeBtn.setBounds (bottom.removeFromRight (90));
        p.removeFromBottom (8);
        preview.setBounds (p.removeFromBottom (54));
        p.removeFromBottom (12);

        colType = p.removeFromLeft (128);
        p.removeFromLeft (14);
        colRight = p.removeFromRight (188);
        p.removeFromRight (14);
        colInst = p;

        auto t = colType;
        for (auto* b : typeButtons) { b->setBounds (t.removeFromTop (42)); t.removeFromTop (4); }

        auto ci = colInst;
        auto head = ci.removeFromTop (22);
        groupBox.setBounds (head.removeFromLeft (head.getWidth() / 2 - 4));
        search.setBounds (head.withTrimmedLeft (8));
        ci.removeFromTop (6);
        list.setBounds (ci);

        auto cr = colRight;
        auto hr = cr.removeFromTop (24);
        rootBox.setBounds (hr.removeFromLeft (58));
        hr.removeFromLeft (6);
        qualityBox.setBounds (hr);
        cr.removeFromTop (10);
        for (auto* c : charChips) { c->setBounds (cr.removeFromTop (22)); }
    }

private:
    // ── glyph toggle ───────────────────────────────────────────────────────
    struct GlyphToggle : juce::Button
    {
        GlyphToggle (const juce::String& caption, int glyphId) : juce::Button (caption), glyph (glyphId) {}
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            const bool on = getToggleState();
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (on ? juce::Colour (0xff313747) : (over ? juce::Colour (0xff2a2f3b) : juce::Colour (0xff1e222a)));
            g.fillRoundedRectangle (r, 4.0f);
            if (on) { g.setColour (juce::Colour (0xffd9a557)); g.drawRoundedRectangle (r, 4.0f, 1.0f); }
            auto icon = r.removeFromLeft (34.0f).reduced (8.0f);
            g.setColour (on ? juce::Colour (0xffd9a557) : juce::Colour (0xffcbd0d7));
            drawGlyph (g, icon, glyph);
            g.setFont (11.0f);
            g.drawText (getButtonText(), r.withTrimmedLeft (2.0f), juce::Justification::centredLeft, false);
        }
        static void drawGlyph (juce::Graphics& g, juce::Rectangle<float> a, int id)
        {
            const float cx = a.getCentreX(), cy = a.getCentreY(), w = a.getWidth(), h = a.getHeight();
            switch (id)
            {
                case 0: g.fillEllipse (cx - 3, cy - 3, 6, 6); break;                       // single note
                case 1: for (int i = -1; i <= 1; ++i) g.fillRect (a.getX(), cy + i * 5.0f - 1.0f, w, 2.0f); break; // chord
                case 2: g.fillEllipse (a.getX(), cy - 3, 6, 6);                             // stab
                        { juce::Path tri; tri.addTriangle (a.getX() + 9, cy - 5, a.getX() + 9, cy + 5, a.getRight(), cy); g.fillPath (tri); } break;
                case 3: { juce::Path d; d.addEllipse (cx - 4, a.getY(), 8, 8); d.addTriangle (cx - 4, a.getY() + 4, cx + 4, a.getY() + 4, cx, a.getBottom()); g.fillPath (d); } break; // pluck
                case 4: g.fillRoundedRectangle (a.getX(), cy - 2, w, 4.0f, 2.0f); break;    // long sustain / pad
                case 5: g.drawEllipse (cx - 6, cy - 6, 12, 12, 1.5f); g.drawEllipse (cx - 3, cy - 3, 6, 6, 1.5f); break; // drone
                case 6: for (int i = 0; i < 7; ++i) g.fillEllipse (a.getX() + (i * 37 % (int) w), a.getY() + (i * 53 % (int) h), 2.5f, 2.5f); break; // texture
                case 7: { juce::Path s; s.startNewSubPath (a.getX(), a.getBottom()); s.lineTo (a.getRight(), a.getY()); g.strokePath (s, juce::PathStrokeType (1.6f)); } break; // sweep
                case 8: { juce::Path z; z.startNewSubPath (cx + 2, a.getY()); z.lineTo (cx - 4, cy + 1); z.lineTo (cx + 1, cy + 1); z.lineTo (cx - 3, a.getBottom()); z.lineTo (cx + 5, cy - 2); z.lineTo (cx, cy - 2); z.closeSubPath(); g.fillPath (z); } break; // fx hit
                default: break;
            }
        }
        int glyph;
    };

    juce::Rectangle<int> panel() const
    {
        return juce::Rectangle<int> (0, 0, juce::jmin (780, getWidth() - 60),
                                     juce::jmin (540, getHeight() - 60))
            .withCentre (getLocalBounds().getCentre());
    }

    int getNumRows() override { return filtered.size(); }

    void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool sel) override
    {
        if (! juce::isPositiveAndBelow (row, filtered.size())) return;
        const auto& it = promptdata::kInstruments[filtered[row]];
        if (sel) { g.setColour (juce::Colour (0xff313747)); g.fillRect (0, 0, w, h); }
        g.setColour (juce::Colour (0xffe8eaed));
        g.setFont (11.5f);
        g.drawText (it.name, 8, 0, w - 90, h, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff5f6672));
        g.setFont (9.0f);
        g.drawText (it.group, w - 86, 0, 80, h, juce::Justification::centredRight);
    }

    void selectedRowsChanged (int row) override
    {
        selectedInstrument = juce::isPositiveAndBelow (row, filtered.size()) ? filtered[row] : -1;
        rebuildPreview();
    }

    void applyFilter()
    {
        const juce::String q = search.getText().trim();
        const int gid = groupBox.getSelectedId() - 2; // -1 == all
        filtered.clearQuick();
        for (int i = 0; i < promptdata::kInstrumentCount; ++i)
        {
            const auto& it = promptdata::kInstruments[i];
            if (gid >= 0 && ! juce::String (it.group).equalsIgnoreCase (promptdata::kInstrumentGroups[gid])) continue;
            if (q.isNotEmpty()
                && ! juce::String (it.name).containsIgnoreCase (q)
                && ! juce::String (it.tags).containsIgnoreCase (q)) continue;
            filtered.add (i);
        }
        list.updateContent();
        for (int r = 0; r < filtered.size(); ++r)
            if (filtered[r] == selectedInstrument) { list.selectRow (r); break; }
        repaint();
    }

    int activeType() const
    {
        for (int i = 0; i < numTypes; ++i) if (typeButtons[i]->getToggleState()) return i;
        return 0;
    }

    void syncEnable()
    {
        const bool chord = juce::String (kTypes[activeType()].name).containsIgnoreCase ("chord");
        rootBox.setEnabled (chord);
        qualityBox.setEnabled (chord);
    }

    juce::String assemble() const
    {
        juce::StringArray parts;
        if (juce::isPositiveAndBelow (selectedInstrument, promptdata::kInstrumentCount))
            parts.add (promptdata::kInstruments[selectedInstrument].tags);

        const auto& st = kTypes[activeType()];
        if (juce::String (st.name).containsIgnoreCase ("chord"))
            parts.add (juce::String (st.phrase).replace ("%CHORD%",
                rootBox.getText() + " " + qualityBox.getText()));
        else
            parts.add (st.phrase);

        juce::StringArray chars;
        for (int i = 0; i < numChars; ++i)
            if (charChips[i]->getToggleState()) chars.add (kChars[i]);
        if (! chars.isEmpty()) parts.add (chars.joinIntoString (", "));

        parts.add ("dry studio, single isolated one-shot");
        return parts.joinIntoString (", ");
    }

    void rebuildPreview() { preview.setText (assemble(), false); }

    // ── data ───────────────────────────────────────────────────────────────
    static constexpr SoundType kTypes[] =
    {
        { "Single Note",  "single sustained note, clean tone", 0 },
        { "Chord",        "sustained chord, %CHORD%, static harmony", 1 },
        { "Stab",         "sharp chord stab, tight fast transient, short decay", 2 },
        { "Pluck",        "plucked note, quick percussive attack, medium decay", 3 },
        { "Long Sustain", "long evolving sustain, slow attack, smooth tail", 4 },
        { "Drone",        "static harmonic drone, no movement", 5 },
        { "Texture",      "granular tonal texture, shimmering, static", 6 },
        { "Sweep",        "rising swell, single gesture", 7 },
        { "FX Hit",       "cinematic impact hit, metallic, short tail", 8 },
    };
    static constexpr int numTypes = (int) (sizeof (kTypes) / sizeof (kTypes[0]));

    static constexpr const char* kChars[] =
    {
        "warm", "bright", "dark", "gritty", "lo-fi", "wide stereo",
        "cinematic", "analog", "vintage", "close mic", "airy", "sub heavy",
    };
    static constexpr int numChars = (int) (sizeof (kChars) / sizeof (kChars[0]));

    juce::OwnedArray<GlyphToggle> typeButtons;
    juce::ComboBox groupBox, rootBox, qualityBox;
    juce::TextEditor search;
    juce::ListBox list;
    juce::OwnedArray<juce::ToggleButton> charChips;
    juce::TextEditor preview;
    juce::TextButton useBtn, closeBtn;
    juce::Array<int> filtered;
    int selectedInstrument = -1;
    juce::Rectangle<int> colType, colInst, colRight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PromptMixer)
};
