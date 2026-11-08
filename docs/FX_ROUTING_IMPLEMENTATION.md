# FX ROUTING - implementation

How the post-voice effects are routed (the handoff's master spec, built against the audited code:
`docs/FX_ROUTING_AUDIT.md`). Updated at every gate.

## Architecture

```
voices (sources A B C, MORPH, per-voice filter, VCA, DRIFT)      locked upstream
  -> half-band decimation -> Juno chorus -> KALEIDOSCOPE          locked upstream
  -> fx::FxEngine (dsp/fx)                                        the routed slot
       CLASSIC: osp::PostProcessor::process - the original chain, bit for bit
       CUSTOM:  the compiled graph: the original four through PostProcessor's stage entry
                points (customDrive / customMovement / customEcho / customSpace), the new
                effects through their own processors (fx::Processor)
  -> DC blocker, output, softGuard
```

| Piece | Where | Thread |
| --- | --- | --- |
| `fx::Graph` (types, `classicGraph`, `validate`, `toText` / `fromText`) | `dsp/fx/FxGraph.*` | message |
| `fx::compile` -> `CompiledGraph` (immutable) | `dsp/fx/FxEngine.cpp` | message |
| hand-off | `osp::ModelExchange<CompiledGraph>` (lock-free, deferred free) | both |
| `fx::FxEngine::process` | `dsp/fx/FxEngine.cpp`, called by `Synth::renderChunk` | audio |
| the original four's stages | OSP `PostProcessor::custom*` (opt-in; `process()` untouched) | audio |
| new effects' settings | `fx::Settings` in `Patch::fx` (host parameters `analog.fx.*`) | audio |
| the route's state | `AnalogEngine::setFxRoute` / `fxRoute`; saved as `<FxRouting schema graph>` | message |

### Graph grammar (schema 1)

`v1 custom|classic` then the chain: `fx <type>.<n> on|off`, `par send|linear 2 lane <gain> { ... } lane <gain> { ... }`,
`split bands2|bands3|ms <lowHz> <highHz> <slope> <n> lane <gain> { ... } ...`.
Each processor at most once; pools: DRIVE, MOVEMENT, ECHO, SPACE one each (the instrument's own),
SPREAD, COMPRESSOR, EQ, FX FILTER, PHASER, FLANGER, UTILITY two each, FREQ SHIFTER and
CONVOLUTION one each; at most 16 effects, 4 levels of nesting.

### Semantics

- CLASSIC is not executed as a graph: `PostProcessor::process`. `classicGraph()` is how it is
  drawn and the starting point of the first edit.
- Inserts (DRIVE, MOVEMENT, new effects): in place; a new effect's own MIX / OUTPUT inside it.
- ECHO: `y = x + wet` (its level the ECHO macro, its wet alone from `customEcho`).
- SPACE: `y = d x + wet`, `d = 1 - 0.2 SPACE` (CLASSIC's law, from `customSpace`).
- Parallel `send`: `y = x + sum g (lane (x) - x)` - CLASSIC's ECHO || SPACE (tested < -100 dB
  from CLASSIC). Parallel `linear` (a stack the musician makes): `y = sum g lane (x)`, gains 0.5 /
  0.5 by default: two untouched lanes give `x` exactly.
- Bypass glides over 20 ms; ECHO, SPACE and tail processors stop hearing input while their tails
  drain (a bypass is not a removal, and not the macro at 0).
- A new route arrives at a block boundary: a 5 ms duck out, the switch, 5 ms back in; the
  processors keep their state, so tails go on. Before any sound, the first route applies at once.

## State (version 12)

The route is the engine's own state (`AnalogEngine::saveState`): `<FxRouting schema="1" graph="v1 custom ..."/>`,
written only once the route is CUSTOM. Absent (every session and preset from before 12, and an
untouched one now) or unreadable: CLASSIC. A preset without one brings CLASSIC back. A graph the
build cannot play is refused (`setFxRoute` returns false; nothing changes).

## Tests

| Gate | Tests |
| --- | --- |
| 0 | `[fx][legacy]` 114 frozen renders (`tests/golden/fx-baseline-renders.txt`) |
| 1 | `[fx][graph]` saved form, validation; `[fx][route]` CLASSIC bit for bit with a route present; `[analog][fx][state]` |
| 3 | `[fx][route]` CUSTOM classic = CLASSIC (< -100 dB), other orders, ECHO -> SPACE only when wired, linear unity, bypass tails, route swaps |
