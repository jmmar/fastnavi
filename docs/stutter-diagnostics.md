# Investigating individual hitches

First-use water, pot effects and new areas can introduce new shader variants,
pipelines and textures. Smooth repeated visits make those plausible suspects.
A white surface can also be a missing resource while it becomes ready. These
observations alone do not establish a cause or a periodic main-thread CPU task.
Do not change synchronization, shader compilation or texture budgets until a capture
distinguishes them. Keep the same build and graphics settings for comparisons.

## First capture

The existing flight recorder is enough for the first measurement. Close the running
game normally, then launch the built game directly through the helper:

```powershell
python tools\capture_stutter.py "C:\path\to\game\ocarina.exe"
```

Use the actual built game executable, not NX2navi.exe in the launcher's top folder.
An older game's executable may be named nx2navi.exe; the helper checks for game-data
folders beside it before launching. No installed files or graphics settings are
rewritten. Save normally and exit normally when done. Diagnostics apply only to the
child process. A second game should not already be running.

Spend one or two minutes repeating the same shallow-water entry, pot break and
Kokiri route. F8 marks an event, including a flash that did not have a slow frame.
Wait a few seconds after an event before quitting: the recorder collects 30 more
presents before dumping it. Automatic triggers capture frames over 40 ms, at most
once every ten seconds. A capture holds the preceding ring and aftermath, not a
continuous session. Frame generation can obscure interpretation; for a follow-up
baseline turn it off through the menu and note that change.

Outputs go under `build/diagnostics/<session>/`. The helper writes `summary.md`
after the game exits. To summarize again:

```powershell
python tools\stutter_report.py build\diagnostics\<session>
```

The initial tier has 600 timing records and texture events, no thumbnails, no CPU
sampling and no verbose performance trace. It uses existing runtime instrumentation;
there are no new per-frame timers. Counters and the recorder still add overhead, and
writing dumps can affect later frames. In particular, the recorder's default image
ring would consume about 527 MiB; this tier avoids it.

The summary deduplicates overlapping dumps within a session. It reports captured
intervals and evidence, not whole-session FPS, causal certainty or invented GPU/main
thread utilization. ASTC decode time is nested in texture time and is not added again.

## Escalate only for an unresolved question

| Evidence | Next step |
| --- | --- |
| Pipeline/translation waits, skipped shader/pipeline draws | Inspect shader preparation and matching log lines. Repeat the encounter immediately and after restart to test cache persistence. Do not enable GPL on RX 6000 as a generic fix. |
| Texture upload/decode time and new texture events | Compare the warmed route. If uploads recur, inspect residency, eviction and memory pressure before lowering details. |
| Fence or drain waits | Determine what work is ahead of the wait. A wait is not proof of a slow shader or a CPU bottleneck. |
| Spike with little recorded work | Repeat briefly with `--detail`, which enables existing performance trace. Only then consider the existing thread sampler and call-chain reports for unexplained CPU work. |
| White flash or missing effect | Repeat briefly with `--visuals` and press F8. This enables 120 thumbnails (about 106 MiB), texture events and timing. It adds GPU copies and dump work. Read them with tools/flight_view.py and compare pipeline/texture skips. |

Keep captures from different settings in separate sessions. A more expensive tier is
for diagnosis, not a performance benchmark. Flight files and screenshots can contain
game-derived material; they remain in ignored build folders and must not be committed.
No captures are uploaded by these tools, and the helper does not copy full environment
variables, settings files, tokens or keys.

## Contextual hold-B running

The current controls describe Switch B as put-away/cancel/back and held Switch A as
dash. Pad labels depend on the selected layout; physical Xbox B is not necessarily
Switch B. The desired behavior is: preserve normal B actions, but allow held B while
moving in ordinary gameplay to request the faster post-roll running state, with or
without sword/shield drawn.

This is not implemented by the diagnostic tools. Input scaling cannot exceed the
game's full-stick speed cap or select a run animation. Forwarding B to A without
gameplay state could activate talk, open, pick-up or climb. A global speed multiplier
would also affect movement states the feature is not meant to change.

Implementation needs a version-checked hook into the game's movement/action
controller: identify the post-roll fast-run state, its speed and animation transition,
and distinguish it from ordinary held-A dash. Determine whether equipped and
holstered locomotion share that state. Gate the request to eligible ground movement,
exclude menus/dialogue, swimming, climbing, cutscenes and incompatible combat states,
preserve B's normal action edges and release cleanly. Keep it opt-in, with remappable
input, rather than changing default bindings.

The host input layer exposes buttons and sticks but has no verified context gate for
this action. Investigation requires the user's locally generated game code and
instrumented gameplay; no guessed addresses or edits to generated C belong in the
repository. A permanent hook must be made in the generator/runtime patch mechanism
and validated for game version 1.1.1. Tests must cover cancel/back, put-away, interaction,
targeting, water entry, equipment transitions, release and different frame rates before
calling it safe to use. The first experiment should compare the existing held-A dash
with the desired post-roll animation, not assume they are identical.
