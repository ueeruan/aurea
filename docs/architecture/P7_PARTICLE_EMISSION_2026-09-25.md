# Continuous particles with a one-shot burst — 2026-09-25

The existing particle renderer has actual analytic GPU trajectories, 3D camera
integration, emitter history, trails, and deterministic seeking. This checkpoint
fixes that implementation rather than replacing it or claiming a new engine.

The shader used all primary instance slots, including the one-shot burst, to
calculate the period of continuous particle births. Adding 100 burst particles
to a rate-10/lifetime-1 emitter consequently extended the continuous cycle from
roughly 1.4 seconds to 11.4 seconds. Once the original particles died, the emitter
could remain blank for several seconds even though its continuous rate was on.

The regression compares actual GPU captures at three seconds, after the burst
has died, with and without that burst. It covers fixed and animated emission,
reverse seeking, and save/reopen. The resulting continuous image must match.

The shader now calculates the cycle from continuous-flow slots only, leaving
one-shot burst timing unchanged. Runtime verification is pending the coordinated
build. The earlier `particle-burst-baseline.log` ran zero tests because its
binary preceded the new regression; it is not validation. Native iOS execution
remains pending.

The first runtime comparison failed six image comparisons despite the period
fix. The second root cause was the draw instance count in both static and
animated emission preparation: `slots` already included burst, but submission
added burst again. The extra instances entered the auxiliary-particle path even
when auxiliary count was zero; real auxiliary counts were also under-submitted.
Both paths now submit `min(cap, slots * auxMul)`. The regression remains unchanged
and is being rerun against this combined fix.

Combined GPU regression passed in the coordinated host run: 38 checks, zero
failures, including reverse seek and save/reload. Native iOS execution remains
pending; this is actual host GPU validation, not physical phone evidence.
