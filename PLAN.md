# Circat Thought – Standalone MVP

## Ziel

Ein Windows-Standalone-Instrument auf JUCE-Basis: Prompt-Eingabe, lokale Mock-AI-Bridge, asynchrones Laden eines erzeugten WAV-Samples und chromatisches MIDI-Playback ohne Echtzeitverletzungen.

## Lieferumfang dieser Phase

1. CMake-/JUCE-Projekt für Standalone (VST3-Konfiguration vorbereitet).
2. Echtzeitfester Sampler-Kern mit polyphonem MIDI-Playback und atomarem Sample-Swap.
3. Prompt-UI mit Generieren-Status.
4. Lokaler Mock-Generator, der ein valides WAV erstellt; reale ACE-Step/MiniMax-Adapter folgen in einer separaten Phase.
5. Build- und Smoke-Test-Anleitung.

## Architektur

`Editor -> GenerationWorker -> LocalAiClient -> Mock bridge -> WAV decode/validate -> SampleStore publish -> audio thread -> Sampler voices`

Der Audio-Thread führt weder I/O noch Locks, Speicherallokationen oder Netzwerkzugriffe aus.

## Grenzen

Die echte Modellinstallation (ACE-Step/MiniMax, CUDA, Lizenzannahme) und der VST3-Export sind nach dem validierten Standalone-MVP vorgesehen.
