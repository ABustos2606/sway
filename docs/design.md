# Sway: design

Moves a control signal between two set points, Point A and Point B: a steady sweep, jumps, one sweep per stomp, or random points between them. Cabled to a module's CV input, it lets a stomp sweep a filter, a delay's feedback or a chorus's depth without wiring an LFO, a slew limiter and a range scaler together. Written for Peepo (Ingen on JACK, Raspberry Pi 5). This document is the spec: ports and behaviour change here first.

Plugin: **Sway** (`https://github.com/ABustos2606/sway#sway`), an `lv2:UtilityPlugin`. Peepo lists it under Control.

## Levels

Peepo's CV modules follow Poly's convention: a CV value of 1.0 is 5 V. **Go** sees a rise above **0.4** (2 V) and is ready again once it falls to **0.05** or below, as Poly's modules read gates and as MIDI program change reads its triggers. Peepo's Foot switch module (0 or 1) drives it directly; latching or not makes no difference, because only rises count.

**Out** is `A + (B - A) * p`, where `p` is the position, 0 at A and 1 at B. A and B run from -1 to 1, and B may be below A.

## Ports

Indices and symbols are permanent once a patch is saved with them; new ports go at the end.

| Index | Symbol | Kind | Range, default | What it is |
| --- | --- | --- | --- | --- |
| 0 | `out` | CV out | A to B | The moving value |
| 1 | `go` | CV in | trigger | Starts, stops or turns the movement, by Shape |
| 2 | `shape` | control | Sweep, Jump, One sweep, Smooth random, Step random, One random; Sweep | How it moves |
| 3 | `point_a` | control | -1 to 1; 0 | Where it rests and starts |
| 4 | `point_b` | control | -1 to 1; 1 | The other end |
| 5 | `time` | control | 0.02 to 30 s, logarithmic; 1 s | How long one move takes, with Sync off |
| 6 | `sync` | control | Off, On; Off | On: one move takes Beats at BPM instead of Time |
| 7 | `bpm` | control | 20 to 300; 120 | The tempo for Sync; Peepo sets it from the patch tempo |
| 8 | `beats` | control | 1/4 beat, 1/2 beat, 1 beat, 2 beats, 1 bar, 2 bars, 4 bars, 8 bars; 1 bar | How long one move takes, with Sync on. A bar is 4 beats |
| 9 | `start` | control | Off, On; On | On: the repeating shapes move as soon as the patch loads, before any stomp |
| 10 | `position` | control out | 0 to 1 | `p`, for the module page |

## Behaviour

A **move** is one trip of the full distance from A to B or back. It takes the move time `T`: Time, or with Sync on `beats * 60 / BPM`, with Beats as 0.25, 0.5, 1, 2, 4, 8, 16 or 32 beats.

- **Sweep:** `p` rises from 0 to 1 in `T`, falls back in `T`, and so on: a triangle with a period of `2T`.
- **Jump:** `p` is 0 for `T`, then 1 for `T`, and so on.
- **One sweep:** `p` rests at 0. Each Go heads for the other end at the move speed (`1/T` per second) and stops there. A Go during a move turns it back from where it is, so the return takes as long as the way there did.
- **Smooth random:** every `T`, a new random target in 0 to 1; `p` glides there in a straight line over `T`.
- **Step random:** every `T`, `p` jumps to a new random value in 0 to 1.
- **One random:** `p` rests. Each Go picks a new random target and glides there over `T`; a Go during a glide starts a new one from where it is.

**Moving and stopped.** The four repeating shapes (Sweep, Jump, Smooth random, Step random) are either moving or stopped. They start moving if Start moving is On, stopped if it is Off. Go flips between them. A stopped shape returns to A: Sweep and Smooth random glide back at the move speed, Jump and Step random jump at once. Starting again begins from where `p` is, heading for B, so Sweep never jumps.

**Changes while moving.**

- Changing Time, Sync, BPM or Beats changes the speed from that sample on; `p` never jumps.
- Changing A or B moves Out at once, by the same `p`. Peepo sends knob changes in small steps, so no smoothing is added.
- Changing Shape keeps `p` and the moving or stopped state, and carries on in the new shape from there. A change to a one-shot shape leaves it resting where it is until the next Go.

**Random values** come from a small generator in each instance (xorshift, seeded from the instance's address), never shared and never allocated in `run()`. Consecutive random targets are at least 0.1 apart, so a move is never invisible.

Each Go acts at the frame of its rise; several in one period act in order.

## Host limits that shape the code

- **Ingen runs a block in slices** when a control input changes during a period, moving the CV pointers to each slice. Sway is per sample, so it simply carries on: nothing it computes may depend on where a slice starts. The plugin test runs it sliced at random points and unsliced and needs the same output, sample for sample.
- **No allocation, locks or I/O in `run()`.** All state is fixed-size.
- **Blocks may run on several threads;** instances share nothing.
- **No LV2 state.** Everything is in control ports, so patches save it. The moving or stopped state is not saved: a patch loads as Start moving says.

## Not in the first version

- A curve control (ease in and out, or exponential) for the sweeps.
- A momentary mode: moving only while the stomp is held.
- A CV input for the move time, or for A and B.
- More outputs (inverted, or a second Sway at a ratio of the first).
- Moving plain knobs. A CV cable reaches only CV inputs; plugins whose controls are plain knobs (most Gx, x42 and MDA ones) cannot be swept by Sway or any other module. That is for Peepo to solve, not this plugin.
