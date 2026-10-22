# Sway: plan

Started 22 October 2026. [design.md](design.md) is the spec; this is the order of work. Tick a step only when it has been checked the way it says.

## 1. Repository

- [x] Layout copied from MIDI program change: `Makefile`, `LICENSE` (GPL-2.0-or-later, as the sister plugins; changed from MIT on 22 October 2026: Sway is our own code, not a Mutable Instruments port), `.gitattributes` (LF), `AGENTS.md` and `CLAUDE.md`, `README.md`, `src/exports.map`.
- [x] Ports: `src/ports.hpp` and `sway.lv2/sway.ttl`, eleven ports; `tests/ttl_check.py` compares them (passes in WSL on the source bundle).
- [ ] The GitHub repository `https://github.com/ABustos2606/sway`, and the first commit (the user's).

## 2. The motion (`src/motion.hpp`, `tests/test_motion.cpp`)

A class with no LV2 in it: `Motion(rate, seed)`, `reset()`, and `float step(float go, const Settings&)` returning `p` per sample. Start moving is read at the first sample after `reset()`, as control values are only sure in `run()`.

- [x] The trigger: rise above 0.4, ready at 0.05 or below; a held stomp is one press.
- [x] Move time from Time, or from Beats and BPM with Sync on.
- [x] Sweep and Jump: exact timing over many periods at 48 kHz (a 1 s Sweep reaches B at sample 48000 and A at 96000, within one sample).
- [x] One sweep: rest, go, turn back mid-way (the return takes as long as the way there did).
- [x] The three random shapes: always between 0 and 1, targets at least 0.1 apart, new values every `T`, Smooth random continuous.
- [x] Moving and stopped: Start moving, Go flipping, the return to A (glide or jump by shape), a restart with no jump.
- [x] Changes while moving: speed change with no jump in `p`; a shape change keeps `p`.
- [x] Out = A + (B - A) * p, with B below A too.
- [x] Nothing in `step` allocates; extreme or non-finite controls are clamped.

## 3. The plugin (`src/lv2_entry.cpp`, `tests/test_lv2.cpp`)

- [x] The glue: connect ports, clamp controls, `activate` resets (Start moving is read at the first sample), `run` steps each sample and writes Out and Position.
- [x] `test_lv2`: dlopen the built `.so`, one descriptor with the right URI, then run the same input sliced at random points (pointers moved per slice, as Ingen does) and unsliced: the outputs must match sample for sample, and Go must act at its own frame.
- [x] `make test` passes in WSL; `make lint` with lv2lint if it is installed (22 October 2026: one warning, no author email).

## 4. In WSL's Ingen

- [ ] Load it in the development engine (`tools/ingen-spike/env.sh`), cable a Foot switch to Go and Out to a Poly module's CV input (Chorus (CV) or Flanger (CV)).
- [ ] **Measure what CV range that input responds to** (does 0 to 1 cover the knob's range, does it add to the knob?). If Poly's inputs use a different span, change A and B's range and defaults here before any patch is saved with them.
- [ ] Each shape heard or measured, and a tempo change followed with Sync on.

## 5. In Peepo (`C:\Pedal PI`)

- [ ] Pin the repository at a commit in `tools/ingen-spike/14-more-plugins.sh`.
- [ ] Add the URI to `config/catalogue/inventory.txt`.
- [ ] Overlay entry in `config/catalogue/overlay.json`: name Sway, category Control, the description from the Turtle, `"tempo": "bpm"`, Time logarithmic, and groups for the module page (for example Movement: Shape, Start moving; Points: Point A, Point B; Speed: Time, Sync, Beats, BPM).
- [ ] Regenerate `docs/33-module-list.md` with `tools/document-catalogue.py`, and grep the catalogue export for the URI before saying it is offered.

## 6. On the Pi

- [ ] Build in the arm64 build root and install from there, so the next package does not wipe it.
- [ ] A stomp through Sway into a module's CV input, every shape, with xruns measured.
- [ ] Then the version is done: update the README if anything changed, and record it in Peepo's docs.

## Later

The items under "Not in the first version" in [design.md](design.md), once the first version has been played.
