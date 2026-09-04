# Third-party notices

## JUCE

The plugin uses JUCE 7.0.9. This repository is distributed under GPL-3.0-or-later
to use JUCE under its GPL terms. A closed-source distribution requires an
appropriate JUCE commercial licence.

JUCE: https://github.com/juce-framework/JUCE

## Stable Audio Tools

The Python bridge uses `stable-audio-tools`, distributed under the MIT License.

Source and licence: https://github.com/Stability-AI/stable-audio-tools

## Stable Audio Open model weights

The model weights are not part of this repository, are downloaded locally by
the installer, and are **not** licensed under GPL. They are governed by the
Stability AI Community License and its Acceptable Use Policy. Users must accept
the model terms at Hugging Face before downloading the weights.

Model licence: https://huggingface.co/stabilityai/stable-audio-open-1.0/blob/main/LICENSE.md

## WULF input-stage integration

During local Windows development, CMake can optionally use the separate WULF-AD
source tree when it is present. WULF-AD is not bundled in this repository.
