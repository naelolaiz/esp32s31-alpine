# SMP experiment

Kernel changes for the [SMP guide](../../docs/steps/smp.md), kept apart from
`fragments/` and `patches/` because they are an experiment: an SMP kernel
with Espressif's drivers starts hart 1 but cannot use it yet (see the
[study](../../docs/journal/2026-10-10-smp-study.md)).

- `smp.config`: the three options the guide sets with `make linux-menuconfig`.
- `0001-clocksource-esp32s31-systimer-tick-the-boot-CPU-only.patch`: keeps the
  systimer as hart 0's tick once two CPUs are possible. `git format-patch`
  output against the pinned kernel commit; the guide applies it with
  `patch -p1`.
