#pragma once

#include "PromptData.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <initializer_list>

// Prompt builder overlay. Three independent axes plus instrument + character:
//   VOICING       — how many notes: single / dyad / chord / cluster
//   ARTICULATION  — the envelope/attack: pluck / stab / sustain / swell / drone
//   NON-TONAL     — texture / hit / noise; overrides voicing + articulation
// HARMONY is contextual: a single note/dyad takes a PITCH (note + octave), a
// chord/cluster takes ROOT + QUALITY. A single note has no key.
class PromptMixer final : public juce::Component,
                          private juce::ListBoxModel
{
public:
    std::function<void (juce::String)> onUsePrompt;
    std::function<void ()> onClose;

    PromptMixer()
    {
        addGroup (voicing, 101, { { "SINGLE", 0 }, { "DYAD", 9 }, { "CHORD", 1 }, { "CLUSTER", 10 } });
        addGroup (artic,   102, { { "PLUCK", 3 }, { "STAB", 2 }, { "SUSTAIN", 4 }, { "SWELL", 7 }, { "DRONE", 5 } });
        addGroup (fx,      103, { { "TONAL", -1 }, { "TEXTURE", 6 }, { "FX HIT", 8 }, { "NOISE", 11 } });
        voicing[0]->setToggleState (true, juce::dontSendNotification);
        artic[2]->setToggleState (true, juce::dontSendNotification);   // SUSTAIN
        fx[0]->setToggleState (true, juce::dontSendNotification);      // TONAL

        groupBox.addItem ("ALL GROUPS", 1);
        for (int i = 0; i < promptdata::kInstrumentGroupCount; ++i)
            groupBox.addItem (promptdata::kInstrumentGroups[i], i + 2);
        groupBox.setSelectedId (1, juce::dontSendNotification);
        groupBox.onChange = [this] { applyFilter(); };
        addAndMakeVisible (groupBox);

        search.setTextToShowWhenEmpty ("search instruments\xE2\x80\xA6", juce::Colour (0xff5f6672));
        search.onTextChange = [this] { applyFilter(); };
        addAndMakeVisible (search);

        list.setModel (this);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff10090c));
        list.setRowHeight (22);
        addAndMakeVisible (list);

        // pitch = note + octave, for single / dyad. Key/quality is meaningless here.
        for (int oct = 1; oct <= 6; ++oct)
            for (auto* n : { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" })
                pitchBox.addItem (juce::String (n) + juce::String (oct), pitchBox.getNumItems() + 1);
        pitchBox.setSelectedId (12 * 2 + 1, juce::dontSendNotification); // C3
        pitchBox.onChange = [this] { rebuildPreview(); };
        addAndMakeVisible (pitchBox);

        rootBox.addItemList ({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1);
        rootBox.setSelectedId (1, juce::dontSendNotification);
        rootBox.onChange = [this] { rebuildPreview(); };
        addAndMakeVisible (rootBox);
        qualityBox.addItemList ({ "maj", "min", "sus2", "sus4", "dim", "aug", "maj7", "min7", "dom7", "add9" }, 1);
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

        auto lbl = [&g] (juce::Rectangle<int> r, const juce::String& t)
        {
            g.setColour (juce::Colour (0xff5f6672));
            g.setFont (10.0f);
            g.drawText (t, r, juce::Justification::topLeft, false);
        };
        lbl ({ secVoicing.getX(),  secVoicing.getY() - 13,  160, 12 }, "VOICING");
        lbl ({ secArtic.getX(),    secArtic.getY() - 13,    160, 12 }, "ARTICULATION");
        lbl ({ secFx.getX(),       secFx.getY() - 13,       160, 12 }, "NON-TONAL (overrides above)");
        lbl ({ colInst.getX(),     colInst.getY() - 13,     160, 12 }, "INSTRUMENT");
        lbl ({ colRight.getX(),    colRight.getY() - 13,    200, 12 }, tonal() ? "PITCH / HARMONY" : "CHARACTER");
    }

    void resized() override
    {
        auto p = panel().reduced (18);
        p.removeFromTop (26);
        auto bottom = p.removeFromBottom (30);
        useBtn.setBounds (bottom.removeFromRight (120));
        bottom.removeFromRight (8);
        closeBtn.setBounds (bottom.removeFromRight (90));
        p.removeFromBottom (8);
        preview.setBounds (p.removeFromBottom (52));
        p.removeFromBottom (12);

        auto colLeft = p.removeFromLeft (150);
        p.removeFromLeft (14);
        colRight = p.removeFromRight (190);
        p.removeFromRight (14);
        colInst = p;

        auto place = [] (juce::OwnedArray<GlyphToggle>& gr, juce::Rectangle<int>& col)
        {
            for (auto* b : gr) { b->setBounds (col.removeFromTop (28)); col.removeFromTop (3); }
        };
        colLeft.removeFromTop (2);
        secVoicing = colLeft;                    place (voicing, colLeft);
        colLeft.removeFromTop (16); secArtic = colLeft;  place (artic, colLeft);
        colLeft.removeFromTop (16); secFx = colLeft;     place (fx, colLeft);

        auto ci = colInst;
        auto head = ci.removeFromTop (22);
        groupBox.setBounds (head.removeFromLeft (head.getWidth() / 2 - 4));
        search.setBounds (head.withTrimmedLeft (8));
        ci.removeFromTop (6);
        list.setBounds (ci);

        auto cr = colRight;
        pitchBox.setBounds (cr.removeFromTop (24));
        cr.removeFromTop (6);
        auto hr = cr.removeFromTop (24);
        rootBox.setBounds (hr.removeFromLeft (58));
        hr.removeFromLeft (6);
        qualityBox.setBounds (hr);
        cr.removeFromTop (14);
        for (auto* c : charChips) { c->setBounds (cr.removeFromTop (21)); }
    }

private:
    struct GlyphSpec { const char* name; int glyph; };

    struct GlyphToggle : juce::Button
    {
        GlyphToggle (const juce::String& caption, int glyphId) : juce::Button (caption), glyph (glyphId) {}
        void paintButton (juce::Graphics& g, bool over, bool) override
        {
            const bool on = getToggleState();
            const bool dim = ! isEnabled();
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (on ? juce::Colour (0xff313747) : (over && ! dim ? juce::Colour (0xff2a2f3b) : juce::Colour (0xff1e222a)));
            g.fillRoundedRectangle (r, 4.0f);
            if (on) { g.setColour (juce::Colour (0xffd9a557)); g.drawRoundedRectangle (r, 4.0f, 1.0f); }
            auto icon = r.removeFromLeft (30.0f).reduced (7.0f);
            g.setColour ((on ? juce::Colour (0xffd9a557) : juce::Colour (0xffcbd0d7)).withAlpha (dim ? 0.35f : 1.0f));
            if (glyph >= 0) drawGlyph (g, icon, glyph);
            g.setFont (10.5f);
            g.drawText (getButtonText(), r.withTrimmedLeft (2.0f), juce::Justification::centredLeft, false);
        }
        static void drawGlyph (juce::Graphics& g, juce::Rectangle<float> a, int id)
        {
            const float cx = a.getCentreX(), cy = a.getCentreY(), w = a.getWidth(), h = a.getHeight();
            switch (id)
            {
                case 0: g.fillEllipse (cx - 3, cy - 3, 6, 6); break;
                case 1: for (int i = -1; i <= 1; ++i) g.fillRect (a.getX(), cy + i * 5.0f - 1.0f, w, 2.0f); break;
                case 2: g.fillEllipse (a.getX(), cy - 3, 6, 6);
                        { juce::Path t; t.addTriangle (a.getX() + 9, cy - 5, a.getX() + 9, cy + 5, a.getRight(), cy); g.fillPath (t); } break;
                case 3: { juce::Path d; d.addEllipse (cx - 4, a.getY(), 8, 8); d.addTriangle (cx - 4, a.getY() + 4, cx + 4, a.getY() + 4, cx, a.getBottom()); g.fillPath (d); } break;
                case 4: g.fillRoundedRectangle (a.getX(), cy - 2, w, 4.0f, 2.0f); break;
                case 5: g.drawEllipse (cx - 6, cy - 6, 12, 12, 1.5f); g.drawEllipse (cx - 3, cy - 3, 6, 6, 1.5f); break;
                case 6: for (int i = 0; i < 7; ++i) g.fillEllipse (a.getX() + (i * 37 % (int) w), a.getY() + (i * 53 % (int) h), 2.5f, 2.5f); break;
                case 7: { juce::Path s; s.startNewSubPath (a.getX(), a.getBottom()); s.lineTo (a.getRight(), a.getY()); g.strokePath (s, juce::PathStrokeType (1.6f)); } break;
                case 8: { juce::Path z; z.startNewSubPath (cx + 2, a.getY()); z.lineTo (cx - 4, cy + 1); z.lineTo (cx + 1, cy + 1); z.lineTo (cx - 3, a.getBottom()); z.lineTo (cx + 5, cy - 2); z.lineTo (cx, cy - 2); z.closeSubPath(); g.fillPath (z); } break;
                case 9: g.fillEllipse (cx - 7, cy - 3, 6, 6); g.fillEllipse (cx + 1, cy - 3, 6, 6); break;               // dyad
                case 10: for (int i = -2; i <= 2; ++i) g.fillRect (a.getX(), cy + i * 3.2f - 1.0f, w, 1.6f); break;      // cluster
                case 11: { juce::Path n; n.startNewSubPath (a.getX(), cy); for (int i = 1; i <= 8; ++i) n.lineTo (a.getX() + i * w / 8.0f, cy + ((i % 2) ? -h * 0.35f : h * 0.35f)); g.strokePath (n, juce::PathStrokeType (1.3f)); } break;
                default: break;
            }
        }
        int glyph;
    };

    void addGroup (juce::OwnedArray<GlyphToggle>& into, int radioId, std::initializer_list<GlyphSpec> specs)
    {
        for (auto s : specs)
        {
            auto* b = into.add (new GlyphToggle (s.name, s.glyph));
            b->setRadioGroupId (radioId);
            b->setClickingTogglesState (true);
            b->onClick = [this] { syncEnable(); rebuildPreview(); };
            addAndMakeVisible (b);
        }
    }

    juce::Rectangle<int> panel() const
    {
        return juce::Rectangle<int> (0, 0, juce::jmin (820, getWidth() - 60),
                                     juce::jmin (560, getHeight() - 60))
            .withCentre (getLocalBounds().getCentre());
    }

    int idx (const juce::OwnedArray<GlyphToggle>& gr) const
    {
        for (int i = 0; i < gr.size(); ++i) if (gr[i]->getToggleState()) return i;
        return 0;
    }
    bool tonal() const { return idx (fx) == 0; }
    bool chordish() const { const int v = idx (voicing); return v == 2 || v == 3; }

    void syncEnable()
    {
        const bool t = tonal();
        for (auto* b : voicing) b->setEnabled (t);
        for (auto* b : artic)   b->setEnabled (t);
        pitchBox.setEnabled (t && ! chordish());
        rootBox.setEnabled (t && chordish());
        qualityBox.setEnabled (t && chordish());
        repaint();
    }

    // ── ListBoxModel
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
        const int gid = groupBox.getSelectedId() - 2;
        filtered.clearQuick();
        for (int i = 0; i < promptdata::kInstrumentCount; ++i)
        {
            const auto& it = promptdata::kInstruments[i];
            if (gid >= 0 && ! juce::String (it.group).equalsIgnoreCase (promptdata::kInstrumentGroups[gid])) continue;
            if (q.isNotEmpty() && ! juce::String (it.name).containsIgnoreCase (q)
                               && ! juce::String (it.tags).containsIgnoreCase (q)) continue;
            filtered.add (i);
        }
        list.updateContent();
        for (int r = 0; r < filtered.size(); ++r)
            if (filtered[r] == selectedInstrument) { list.selectRow (r); break; }
        repaint();
    }

    juce::String assemble() const
    {
        juce::StringArray parts;
        if (juce::isPositiveAndBelow (selectedInstrument, promptdata::kInstrumentCount))
            parts.add (promptdata::kInstruments[selectedInstrument].tags);

        if (! tonal())
        {
            static const char* fxPhrase[] = { "", "granular tonal texture, shimmering, static",
                                              "cinematic impact hit, metallic, short tail",
                                              "filtered noise sweep, single gesture, no pitch" };
            parts.add (fxPhrase[idx (fx)]);
        }
        else
        {
            static const char* voiceNoun[] = { "single note", "two-note dyad", "chord", "dense chord cluster" };
            static const char* articAdj[]  = { "plucked", "sharp stabbed", "sustained", "slowly swelling", "sustained drone" };
            juce::String s = juce::String (articAdj[idx (artic)]) + " " + voiceNoun[idx (voicing)];
            if (chordish())
                s << ", " << rootBox.getText() << " " << qualityBox.getText() << ", static harmony";
            else
                s << " at pitch " << pitchBox.getText() << " (no key)";
            parts.add (s);
        }

        juce::StringArray chars;
        for (int i = 0; i < numChars; ++i)
            if (charChips[i]->getToggleState()) chars.add (kChars[i]);
        if (! chars.isEmpty()) parts.add (chars.joinIntoString (", "));

        parts.add ("dry studio, single isolated one-shot");
        return parts.joinIntoString (", ");
    }

    void rebuildPreview() { preview.setText (assemble(), false); }

    static constexpr const char* kChars[] =
    {
        "warm", "bright", "dark", "gritty", "lo-fi", "wide stereo",
        "cinematic", "analog", "vintage", "close mic", "airy", "sub heavy",
    };
    static constexpr int numChars = (int) (sizeof (kChars) / sizeof (kChars[0]));

    juce::OwnedArray<GlyphToggle> voicing, artic, fx;
    juce::ComboBox groupBox, pitchBox, rootBox, qualityBox;
    juce::TextEditor search;
    juce::ListBox list;
    juce::OwnedArray<juce::ToggleButton> charChips;
    juce::TextEditor preview;
    juce::TextButton useBtn, closeBtn;
    juce::Array<int> filtered;
    int selectedInstrument = -1;
    juce::Rectangle<int> secVoicing, secArtic, secFx, colInst, colRight;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PromptMixer)
};
