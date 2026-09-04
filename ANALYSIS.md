# Circat Thought – Standanalyse & Verbesserungsvorschläge

Stand: 2026-09-04. Basis: `Source/`, `backend/`, `CMakeLists.txt`, `HANDOFF.md`.

---

## 1. Was in dieser Sitzung bereits behoben wurde

| Thema | Änderung | Datei |
|---|---|---|
| **Filter stumm beim Laden / über Grenzwert** | Chamberlin-SVF → TPT/ZDF-State-Variable-Filter (unbedingt stabil). Alter Filter divergierte oberhalb ≈ fs/6 (bei fs 44,1 kHz ≈ 7,35 kHz). Default-Cutoff war 8 kHz → NaN → Stille. | `ThoughtSampler.cpp` |
| Filter-Release-Bug | `else if (stage == 3)` → `else if (filterStage == 3)`; `noteOff` setzt jetzt beide Stages; Non-Finite-Guard. | `ThoughtSampler.cpp` |
| **Volume-Meter** | Linearer Peak → dB-Mapping (−60…+6 dB), eigener `BarMeter` statt `ProgressBar` mit „%"-Text, Refresh 8 → 30 Hz. | `PluginEditor.*` |
| **LLM-Geschwindigkeit** | `pingpong`-Sampler + fp16-Autocast + TF32; Default-Steps 100 → 14; optional `CIRCAT_COMPILE=1`. Erwartung: ~3–5× schneller pro Generierung. | `stable_audio_bridge.py` |
| Templates | 4 → 18 One-Shot-Prompt-Presets. | `PluginEditor.*` |
| UI-Angleichung | Gemeinsame Circat-Palette + `LookAndFeel` nach S612/AKAK-Master (Velvet `#201914`/`#37302A`, Brass `#D9A557`, Karten `#1A1D24`). Rotary-Knöpfe mit 270°-Bogen. | `PluginEditor.*` |

---

## 2. Offene Schwachstellen (nach Priorität)

### P1 – Korrektheit / Stabilität

1. **State-Persistenz unvollständig.** `getStateInformation` speichert nur `prompt`, `sampleStart`, `sampleEnd`. Loop-Modus, Filter, ADSR (Amp + Filter), Drive, Output-Gain, AI-Parameter, `promptPreset` gehen beim erneuten Laden des Projekts / Preset verloren. Alle diese Werte liegen bereits als `std::atomic` im Processor – nur das Schreiben/Lesen im ValueTree fehlt. **Größter Reibungspunkt für Nutzer.**
2. **Editor ↔ Processor nicht rückgekoppelt.** Öffnet man den Editor neu, zeigen alle Slider ihre Konstruktor-Defaults, nicht den echten Processor-Zustand (`getAmpAttack()` etc. existieren, werden aber im Editor-Ctor nicht gelesen). Nach jedem GUI-Close/Reopen springt der sichtbare Zustand.
3. **Voice-Stealing primitiv.** `processBlock` nimmt bei Voll-Polyphonie `voices.front()` – kein Alter/Release-Priorisieren. Bei Akkord-Spiel hörbare Abrisse. Mindest-Fix: älteste Voice (kleinste `envelope` in Stage ≥ 2) stehlen.
4. **`autoSlice` blockiert theoretisch.** Läuft auf dem Message-Thread über den gesamten Buffer (bis 10 Mio Samples). Bei großem Sample kurzer UI-Freeze. In Worker verschieben oder Fenstergröße begrenzen.
5. **Kein Parameter-Smoothing.** Cutoff/Drive/Gain/Loop-Punkte werden pro Note-On übernommen, nicht interpoliert. Cutoff-Sprünge während gehaltener Note sind nicht hörbar (Wert erst bei nächster Note aktiv) – gewollt? Falls Live-Filter-Sweeps gewünscht: `SmoothedValue` + pro-Block-Update im Voice.

### P2 – Architektur

6. **Prozess-Lebenszyklus.** `LocalAiWorker` startet `start_stable_audio.bat` bei jeder Plugin-Instanz. HANDOFF nennt „verify no orphan bridge process". Die Bridge hat einen File-Lock (gut), aber das Plugin sendet nie `unload` beim Schließen und killt den `ChildProcess` nicht. Mehrere DAW-Sessions → Bridge bleibt, VRAM belegt. Fix: Ref-Count-Datei oder `/v1/model/unload` in `~LocalAiWorker` (nur wenn letzte Instanz).
7. **HTTP im Worker über `juce::URL`.** `withConnectionTimeoutMs(600000)` – 10 Min Blockade möglich, kein Cancel. Ein „ABORT GENERATION"-Button wäre sinnvoll (Bridge-seitig Interrupt-Flag).
8. **Reference-Audio verdrahtet, aber tot.** `setReferenceAudio` / `pendingReferencePath` existieren im Worker, aber der Editor hat keinen funktionierenden Button dafür (`reference` deklariert, nie `addAndMakeVisible`). Entweder fertig bauen (Bridge unterstützt `reference_audio_path` – aber `stable_audio_bridge.py` ignoriert das Feld komplett) oder entfernen.
9. **WULF-Input hart auf `D:/VSTPluginsDev/WULF-AD/...`** – nur dein Rechner. Als Git-Submodule oder FetchContent einbinden, sonst baut niemand sonst die Drive-Stufe.
10. **Mock- vs. echte Bridge.** `mock_bridge.py` nutzt `/v1/generate` + base64; `stable_audio_bridge.py` nur `/v1/generate.wav`. `LocalAiWorker` spricht nur `.wav`. Mock ist damit für die aktuelle Client-Version teils inkompatibel (health-Feldnamen `model_ready`/`status` fehlen im Mock → Editor zeigt „waiting for bridge"). Mock-Health an echtes Schema angleichen.

### P3 – DSP-Qualität

> Richtung 2026-09-04: S612-Companding / Bit-Crush **nicht** gewünscht. S612 war
> nur Arbeitsbasis. Circat Thought = moderner Sampler, einziges „vintage" Element
> ist die WULF-Gain-Stufe. Punkt 12 entfällt.

11. **Resampling linear interpoliert.** Bei Transposition nach oben (`increment > 1`) Aliasing. Für einen modernen Sampler: Hermite/4-Punkt-Interpolation oder Mip-Sampling einbauen.
12. *(entfallen — S612-Companding nicht gewünscht)*
13. **Filter ohne Oversampling.** Der neue TPT-SVF ist stabil, aber bei hohen Resonanzen + hohem Cutoff entsteht Aliasing. S612-Master oversampled den Filter 4×. Für Circat Thought optional 2× reichen.
14. **Loop-Crossfade nur im Forward-Loop (`loopMode == 1`).** Ping-Pong (`loopMode == 2`) hat keinen Fade → Klick an den Wendepunkten.

### P4 – Backend / LLM

15. **Stable Audio Open rendert immer das volle Trainingsfenster (~47 s Latents), egal welche `duration`.** Das ist der eigentliche Kostentreiber, nicht die Step-Zahl. Optionen:
    - Steps runter (erledigt: 14 + pingpong).
    - fp16 / TF32 (erledigt).
    - `torch.compile` (optional-Flag gesetzt).
    - **Alternativmodell prüfen:** Stable Audio Open **small** (341 M, deutlich schneller) oder ein destilliertes Consistency-Modell. Für One-Shots/Stabs reicht die kleinere Qualität oft.
    - Batch-Warmup: erste Generierung nach Load ist wegen Kernel-Autotuning langsam – eine Dummy-Generierung direkt nach `ready`.
16. **Kein Ergebnis-Cache.** Gleicher Prompt + Seed + Params → neu gerendert. `hash → wav` auf Platte (`%LOCALAPPDATA%`) spart bei A/B-Vergleichen viel.
17. **Prompt-Anhang doppelt.** Editor-Template UND `stable_audio_bridge.generate()` hängen jeweils „dry studio, no drums…" an. Ergebnis: sehr langer, redundanter Prompt (T5 schneidet bei 512 Tokens). Negativ-Tags nur an EINER Stelle.
18. **Kein echtes Negative-Prompting.** SAO 1.0 unterstützt `negative_conditioning` in `generate_diffusion_cond`. Aktuell werden „no drums" etc. als Positiv-Text übergeben – schwächer. Echtes `negative_conditioning=[{"prompt": "drums, beat, rhythm, melody, vocals, reverb", ...}]` nutzen.

### P5 – Projekt / Build / Lizenz

19. **Zwei GitHub-Repos** für dasselbe Projekt: `circat/CircatThoughtVSTi` (privat) und `circat/Circat_Thought_AI_Sampler_VST_AU_Standalone` (öffentlich, aktuelles `origin`). Eines archivieren.
20. **JUCE 7.0.9 / GPL.** Kommerzielles Closed-Source-Release braucht JUCE-Lizenz. Für Public-GPL-Release ok. Entscheidung dokumentieren.
21. **Keine CI.** GitHub Actions Windows-Runner: configure + Smoke-Test + `pluginval`. `pluginval_Windows` liegt bereits in `D:/VSTPluginsDev/`.
22. **Tests dünn.** Nur `SamplerSmoke.cpp` (1 Note, Magnitude > 0). Fehlen: Filter-Stabilität über den ganzen Cutoff-Bereich (Regression für den heutigen Bug!), Loop-Wraparound, State save/load Roundtrip, Denormal-/NaN-Check nach 10 s Dauerbetrieb.

---

## 3. Empfohlene nächste Schritte (konkret, klein)

1. **State-Persistenz vervollständigen** (P1-1/2) – 1 Datei, hoher Nutzen. ValueTree um alle Parameter erweitern + Editor-Ctor liest Processor-Getter.
2. **Filter-Regressionstest** in `SamplerSmoke` – Sweep Cutoff 20 Hz…20 kHz, assert `isfinite` und Magnitude < 4.
3. **Prompt-Redundanz entschärfen** + echtes `negative_conditioning` (P4-17/18).
4. **Bridge-Shutdown im Plugin** (P2-6) – Orphan-Prozesse vermeiden.
5. **Moderne Sampler-Features** statt S612-Emulation: Hermite-Interpolation (P3-11), Velocity→Filter/Amp-Mod, Round-Robin über die letzten N Generierungen, Sample-Reverse, Pitch-Bend-Range.
6. **GitHub Actions** – configure + Smoke + pluginval.
7. **Stable Audio Open small** als wählbares Backend-Modell (P4-15).

## 5. Erledigt in Sitzung 2 (2026-09-04)

- LOAD/UNLOAD-Buttons entfernt; Modell lädt automatisch beim ersten GENERATE (`LocalAiWorker::ensureModelReady`).
- Waveform-Fenster zeigt animierten Lade-/Generierungs-Status bis neue Waveform vorliegt.
- Jede Generierung wird als 24-bit WAV in `%LOCALAPPDATA%\CIRCAT\CircatThought\Generated\` abgelegt (Ringpuffer 60). Neuer `GeneratedBrowser`-Overlay: laden / exportieren / Ordner öffnen.
- Logo-Klick → `circat::AboutPanel` (shared) + „SEND LOG" via `circat::Log`. Session-Header in `prepareToPlay`.
- 7 fehlende Processor-Setter implementiert; volle State-Persistenz.
- README: Speicherbedarf, Modellgröße, Referenz-Testsystem.

---

## 4. Beobachtungen zur UI (nach Angleichung)

- Layout ist funktional, aber dicht. Der S612-Master arbeitet mit klaren Sektions-Karten + Sieben-Segment-Displays. Circat Thought könnte den `commandView` als CRT-Display (Scanlines, `controlsDisplayScanlineOpacity` aus den Tokens) stilisieren.
- `generationProgressBar` ist noch `juce::ProgressBar` mit „%"-Text – konsistenterweise auch auf eine schmale Brass-Leiste umstellen.
- Filter-Knöpfe sollten die Filter-Arc-Farbe `#63B4A6` (Token `arcFilter`) tragen, nicht Brass – so trennt der Master optisch die Parametergruppen.
- Kein Resize/Scaling. S612-Master hat `resizeMin/Max` in den Tokens. Für 4K-Screens relevant.
