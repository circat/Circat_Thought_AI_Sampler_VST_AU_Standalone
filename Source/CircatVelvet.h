#pragma once

#include <juce_graphics/juce_graphics.h>
#include <cmath>

// Procedural satin-velvet panel for the Circat master look. Rendered once into
// an Image (call from resized()), then blitted every frame. Asset-free and
// resolution independent; tint it per plugin via the Palette (WULF orange,
// GHOST teal, AKAK espresso, ...). Intended to move to Releases/Shared/Circat.
namespace circat::velvet
{
    struct Palette
    {
        juce::Colour top;      // lit edge of the pile
        juce::Colour bottom;   // shadowed base
        juce::Colour sheen;    // warm highlight where the light rakes across
    };

    inline juce::Image makeNoise (int n, juce::Random& r)
    {
        juce::Image ni (juce::Image::ARGB, n, n, true);
        juce::Image::BitmapData bd (ni, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
            {
                const float v = r.nextFloat();
                const juce::uint8 g8 = v > 0.5f ? 255 : 0;
                const juce::uint8 a = (juce::uint8) juce::jmap (std::abs (v - 0.5f), 0.0f, 0.5f, 0.0f, 24.0f);
                bd.setPixelColour (x, y, juce::Colour (g8, g8, g8).withAlpha (a));
            }
        return ni;
    }

    inline juce::Image render (int w, int h, Palette p, juce::uint32 seed = 0x5eed7a11u)
    {
        if (w <= 0 || h <= 0)
            return {};

        juce::Image img (juce::Image::ARGB, w, h, false);
        juce::Graphics g (img);
        juce::Random r ((juce::int64) seed);

        const auto fw = (float) w, fh = (float) h;

        // 1 — base vertical gradient, lit at the top, sinking into shadow
        juce::ColourGradient base (p.top, fw * 0.5f, 0.0f, p.bottom, fw * 0.5f, fh, false);
        base.addColour (0.30, p.top.interpolatedWith (p.bottom, 0.28f));
        base.addColour (0.72, p.top.interpolatedWith (p.bottom, 0.82f));
        g.setGradientFill (base);
        g.fillAll();

        // 2 — vertical pile grain: dense faint hairlines, alternating lift / shade
        for (int x = 0; x < w; x += 2)
        {
            const bool lift = ((x >> 1) & 1) == 0;
            const float a = 0.012f + 0.022f * r.nextFloat();
            g.setColour ((lift ? juce::Colours::white : juce::Colours::black).withAlpha (a));
            g.drawVerticalLine (x, 0.0f, fh);
        }

        // 3 — satin sheen: a few broad soft horizontal bands where the pile catches light
        for (int i = 0; i < 4; ++i)
        {
            const float cy = fh * (0.12f + 0.22f * (float) i) + (r.nextFloat() - 0.5f) * 60.0f;
            const float half = 70.0f + 90.0f * r.nextFloat();
            juce::ColourGradient band (p.sheen.withAlpha (0.0f), 0.0f, cy - half,
                                       p.sheen.withAlpha (0.06f), 0.0f, cy, false);
            band.addColour (1.0, p.sheen.withAlpha (0.0f));
            juce::ColourGradient band2 (p.sheen.withAlpha (0.055f), 0.0f, cy,
                                        p.sheen.withAlpha (0.0f), 0.0f, cy + half, false);
            g.setGradientFill (band);  g.fillRect (0.0f, cy - half, fw, half);
            g.setGradientFill (band2); g.fillRect (0.0f, cy, fw, half);
        }

        // 4 — anisotropic key sheen from the upper-left (directional gloss on the fabric)
        juce::ColourGradient key (p.sheen.withAlpha (0.11f), fw * 0.30f, fh * 0.08f,
                                  p.sheen.withAlpha (0.0f), fw * 0.95f, fh * 1.0f, true);
        g.setGradientFill (key);
        g.fillAll();

        // 5 — micro pile speckle
        auto noise = makeNoise (220, r);
        g.setOpacity (1.0f);
        g.drawImage (noise, juce::Rectangle<float> (0.0f, 0.0f, fw, fh),
                     juce::RectanglePlacement::stretchToFit);
        g.drawImageTransformed (noise, juce::AffineTransform::scale (fw / 220.0f * 0.53f,
                                                                    fh / 220.0f * 0.53f)
                                           .translated (fw * 0.11f, fh * 0.07f));

        // 6 — corner vignette for depth
        juce::ColourGradient vig (juce::Colours::transparentBlack, fw * 0.5f, fh * 0.5f,
                                  juce::Colours::black.withAlpha (0.52f), 0.0f, 0.0f, true);
        vig.addColour (0.48, juce::Colours::transparentBlack);
        vig.addColour (0.82, juce::Colours::black.withAlpha (0.16f));
        g.setGradientFill (vig);
        g.fillAll();

        return img;
    }
}
