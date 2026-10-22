# Sway

An LV2 plugin that moves a control signal between two set points, written for Peepo and other LV2 hosts.
GPL-2.0-or-later, Copyright (c) 2026 Alejandro Bustos; see [LICENSE](LICENSE).

**Sway** (`https://github.com/ABustos2606/sway#sway`) has one CV output, to cable to any module's CV input,
and one trigger input, **Go**, for a foot switch. Its shapes:

| Shape | What it does | What Go does |
| --- | --- | --- |
| Sweep | Glides from A to B and back, over and over | Stops it (back to A) or starts it |
| Jump | Jumps between A and B | Stops it (back to A) or starts it |
| One sweep | Rests at A | Glides to the other point; pressed mid-way, turns back |
| Smooth random | Glides to a new random point between A and B, again and again | Stops it (back to A) or starts it |
| Step random | Jumps to a new random point between A and B, again and again | Stops it (back to A) or starts it |
| One random | Rests | Glides to a new random point between A and B |

**Time** is how long one move takes; with **Sync** on, it is a number of **Beats** at **BPM** instead, which
Peepo sets from the patch tempo. **Start moving** decides whether the repeating shapes move as soon as the
patch loads. **Position** shows where it is between A (0) and B (1). [docs/design.md](docs/design.md) is the
spec and [docs/plan.md](docs/plan.md) the plan.

## Build and test

Needs a C++17 compiler, `pkg-config` and the LV2 headers (`lv2-dev`); the tests need `python3-rdflib`.

```sh
make            # build/sway.lv2
make test       # Turtle against the ports, the motion, and the plugin sliced as Ingen runs it
make install PREFIX=/usr LV2DIR=/usr/lib/lv2
```
