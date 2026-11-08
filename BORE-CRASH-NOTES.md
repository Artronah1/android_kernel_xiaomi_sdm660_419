# BORE fork-storm crash — investigation notes

Tree: `/mnt/data/kirill/Projects/kernels/san-resukisu-base` @ `v2.8.0-san-base`
Linux 4.19.325 · device 3759ac66 (tulip, SDM636, 3GB, LOS 22.2 / Android 15)

## Symptom
6-worker **fork-only** storm
```sh
i=0; while [ "$i" -lt 4000 ]; do ( : ) & wait; i=$((i+1)); done
```
silently reboots the phone in ~10–15 s. A fork-free busy loop (load 9.1) survives.
No pstore/ramoops; `PANIC_ON_OOPS=y`, `QCOM_WATCHDOG_V2=y`, DEBUG_FS off, no lockup/hung
detectors → a watchdog bite on an **IRQ-off hang**, not an Oops. PMIC shows
`Triggered from PS_HOLD` (same as a normal reset), so it is indistinguishable by text.

## Live bisect (sysctl only, no rebuild)
| knob | value | result |
|---|---|---|
| sched_bore | 0 | SURVIVES 60 s |
| sched_burst_inherit_type | 0 | SURVIVES |
| sched_burst_inherit_type | 2 | REBOOTS |
| sched_burst_penalty_scale | 0 | SURVIVES |
| sched_credit_cap_us | 0 | STILL REBOOTS (sleep-credit excluded) |
| nice -n 19 storm | | STILL REBOOTS (weight delta excluded → trigger is commit work itself) |

## Root cause (fix attempt 2)
`task_fork_bore()` (kernel/sched/bore.c:333) was called from **two** sites:
1. `task_fork_fair()` — fair.c:12737, **inside `rq_lock`** (pi_lock + rq->lock held).
2. `sched_post_fork()` — core.c:3442, called at kernel/fork.c:2289 after
   `tasklist_lock` is dropped.

`task_fork_bore()` takes `read_lock(&tasklist_lock)`. Site (1) therefore takes
**rq->lock → tasklist_lock**, but the canonical order is **tasklist_lock → rq->lock**
(e.g. `exit_notify()` holds `tasklist_lock` and calls `do_notify_parent()` →
`__wake_up_sync_key()` → `try_to_wake_up()` → pi_lock/rq->lock).
Under a fork storm + process churn this is an **ABBA deadlock → IRQ-off hang →
watchdog reset**. That explains why the plain busy loop survives but the storm kills.

Site (2) additionally ran `bore_eevdf_commit()` **lockless** (no rq->lock): it calls
`reweight_task()` → `reweight_entity()`, which touches `cfs_rq_of(se)`; for a
never-enqueued child `p->se.cfs_rq == NULL` (cleared by `__sched_fork`), so it is a
latent NULL/corruption path. Upstream BORE 4.19 legacy calls the fork hook **only**
from `sched_post_fork()` and does **no** reweight there.

### Why fix attempt 1 failed
Attempt 1 only added `if (!p->se.on_rq) return;` to `bore_eevdf_commit()`.
That suppressed the reweight (child is off-rq at both sites) but left the
**lock inversion at fair.c:12737** intact → still rebooted.
→ the trigger is the **lock-order violation**, not the reweight.

## Fix (attempt 2 — current uncommitted diff)
- kernel/sched/fair.c: **deleted** `task_fork_bore(p);` from `task_fork_fair()`
  (matches upstream 4.19 legacy: no fork hook in fair.c).
- kernel/sched/bore.c `task_fork_bore()`: **removed** the trailing
  `#ifdef CONFIG_SCHED_EEVDF bore_eevdf_commit(p); #endif` block, replaced by a
  comment. No reweight happens at fork; child inherits the parent's
  `se.load.weight` via `dup_task_struct()` and the inherited penalty `p->bore.penalty`
  is set; the weight is (re)applied on the first `update_curr_bore()` under rq->lock.
- kernel/sched/bore_eevdf.c keeps the defensive `!se.on_rq` guard (harmless).

`task_fork_bore()` is now reachable only from `sched_post_fork()` (no rq->lock held);
it takes `tasklist_lock` itself → correct order.

## RESULT (fix attempt 2) — VERIFIED FIXED
Build: `Artronah-San-Kernel-KSU-SUSFS-tulip-20261108-2318.zip`, md5 `0786db9940a298a7207d53ef10590b86`,
kernel `#1 SMP PREEMPT Sun Nov 8 23:18:50 MSK 2026`, flashed via recovery sideload.
New boot_id `2d8b3096-fec2-4304-b998-9e066e8dec8a` (boot backup pre-fix2 md5 `3ee951ed807e900d92282225ffdd62a0`).

Binary check (`llvm-objdump`): `task_fork_fair` no longer references `task_fork_bore`;
`sched_post_fork` still does.

Storm battery on the new kernel — **boot_id never changed in any round**:
| round | mode | result |
|---|---|---|
| 1 | fork | SURVIVED (DONE @122s) |
| 2 | fork | SURVIVED |
| 3 | exec | SURVIVED (slower, ~60s hb) |
| r1–r3 | fork | SURVIVED ×3 |
The identical storm rebooted the device twice on the pre-fix builds.
`dmesg` clean: 0 BUG/Oops/KASAN/watchdog-bite/lockup/hung-task/RCU-stall.
Conclusion: **reboots were caused by our BORE fork-hook port, not the phone.** Fixed.

## Test procedure
Flashed build = boot_id changes on real reboot. `sched_burst_inherit_type=2` default.
Run the fork storm; poll `/proc/sys/kernel/random/boot_id`; expect survive ≥60 s.

## Device helpers (/data/local/tmp)
`forkstorm.sh [fork|exec]`, `stormtest.sh [knob] [val]` (exit 10 = reboot, 0 = survived),
`nice19.sh`, `nicecheck.sh`.

## Build gotchas
- always `rm -rf include/generated arch/arm64/include/generated out/` before a clean build.
- boot partition node = `/dev/block/mmcblk0p58` (`/dev/block/by-name/boot` is a symlink →
  `dd` returns 0 bytes).
- flash only via `adb reboot recovery` + `adb sideload <zip>` (fastboot does not work).

## Open / secondary suspects (if attempt 2 still reboots)
- fair_eevdf.c:93-96 — san-only 7.0.1 delta: `if (bore_enabled()) limit >>= 1;` in
  `entity_lag()`. Locking-neutral, likely innocent.
- `sched_init_bore()` never runs (`bore_reciprocal_lut[]` all-zero; `sched_credit_key`
  never enabled; banner never prints) → wrong averaging only, not a crash.
