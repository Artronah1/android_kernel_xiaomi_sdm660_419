# Memory / reclaim patches ported into san-base

Reference tree: `kernel-clang20-test` (v2.5.9-clover-mm).
Target: `san-resukisu-base` (v2.8.0-san-base), Linux 4.19.325.

## Batch A — kshrinkd / kswapd / swappiness / vmpressure / watermarks (no MGLRU scan-logic change)
- `mm/vmscan.c` get_swappiness(): threshold `<= 0` -> `< MIN_LRU_BATCH` + count
  LRU_KSWAPD_SWAP_FULL / LRU_DIRECT_SWAP_FULL. Fixes 100% system_server CPU spin
  under memory pressure (MGLRU swap-full reactivation).
- `include/linux/vm_event_item.h`: add the two enum events (NO vmstat_text labels —
  net of the follow-up "remove dummy labels" fix; labels shifted node stats).
- `mm/vmpressure.c`: vmpressure_global() early-returns when scanned==0 instead of
  reporting a bogus 100% pressure.
- `mm/page_alloc.c`:
  - kshrinkd_waiters: `smp_mb__after_atomic(); wake_all_kshrinkds(ac);`
  - watermarks sized without CMA pages (`zone_cma_pages()` / `zone_wmark_pages()`),
    so min_free_kbytes is not inflated by CMA -> fixes CMA exhaustion.
- `mm/vmscan.c` kshrinkd(): proper reclaim_state + `PF_MEMALLOC|PF_KSWAPD`,
  correct slab priority wrap `pri = pri ? pri - 1 : DEF_PRIORITY` (unsigned underflow fix).
- `mm/vmscan.c` balance_pgdat(): do not abort kswapd when waiters hit zero.
- `mm/vmscan.c`: congestion waits in reclaim throttle `HZ/10` -> `msecs_to_jiffies(20)`.
- `mm/vmscan.c` shrink_page_list(): keep_locked only for non-anon non-swapcache
  mapped referenced pages (don't wrongly pin anon).
- `mm/vmscan.c` init_lru_gen(): set `lru_gen_min_ttl = HZ` (1s thrashing guard).
- `mm/page_alloc.c` build_zonelists(): `node_load[node] += load` (accumulated load).

## Batch B — small safe backports
- `mm/zsmalloc.c`: only one active pool compaction context (`compaction_in_progress`).
- `mm/percpu-km.c`: `pcpu_chunk_populated(chunk, 0, chunk->nr_pages, false)`.
- `mm/mremap.c`: already matched main (rmap locks forced) — no change needed.
- `fs/f2fs`: `MAX_FLUSH_RETRY_COUNT 3` for f2fs_enable_checkpoint().

## Batch C — MGLRU reclaim quality
- per-zone reclaim: `sort_page()` takes `sc`; ineligible `zone > sc->reclaim_idx`;
  `scan_pages()` iterates all zones.
- `sort_page()` protected condition `tier > tier_idx || tier == MAX_NR_TIERS - 1`.
- `evict_pages()`: NR_ISOLATED accounting + reclaim_stat + wakeup_flusher_threads +
  sc->nr.* accumulation.
- `include/linux/mm_inline.h` lru_gen_add_page(): four-case placement (protect young).
- `mm/workingset.c` lru_gen_refault(): underprotected page-cache fix.

Status: see git log for the port commits.
