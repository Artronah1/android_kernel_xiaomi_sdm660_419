/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Burst-Oriented Response Enhancer — public interface.
 *
 * Scoring ported from firelzrd BORE (kernel/sched/bore.c).  Our apply path
 * targets a hand-rolled 4.19 EEVDF (see fair_eevdf.c / bore_eevdf.c), so the
 * 6.12-only pieces of upstream 7.0.1 (protect_slice, sched_delayed,
 * requeue_delayed_entity) are intentionally omitted.
 */
#ifndef _KERNEL_SCHED_BORE_H
#define _KERNEL_SCHED_BORE_H

#ifdef CONFIG_SCHED_BORE
#define SCHED_BORE_PROGNAME "BORE CPU Scheduler modification"
#define SCHED_BORE_VERSION  "7.0.1"
#define SCHED_BORE_AUTHOR   "Masahito Suzuki"

extern u32 sched_bore;
extern u32 sched_burst_inherit_type;
extern u32 sched_burst_smoothness;
extern u32 sched_burst_penalty_offset;
extern u32 sched_burst_penalty_scale;
extern u32 sched_burst_cache_lifetime;
extern u32 sched_credit_cap_us;
DECLARE_STATIC_KEY_TRUE(sched_bore_enabled);
DECLARE_STATIC_KEY_FALSE(sched_credit_key);

void sched_init_bore(void);
void reset_task_bore(struct task_struct *p);
void update_curr_bore(struct task_struct *p, u64 delta_exec);
void restart_burst_bore(struct task_struct *p);
void restart_burst_rescale_deadline_bore(struct task_struct *p);
void task_fork_bore(struct task_struct *p);
void bore_note_sleep(struct task_struct *p, u64 now);
u64  bore_credit_ns(struct task_struct *p);
u8 bore_apply_score(struct task_struct *p);

static inline bool bore_enabled(void)
{
	return static_branch_likely(&sched_bore_enabled);
}

static inline u8 bore_score(struct task_struct *p)
{
	return p->bore.penalty >> 8;
}
#else
#define sched_bore 0
static inline bool bore_enabled(void) { return false; }
static inline void sched_init_bore(void) { }
static inline void reset_task_bore(struct task_struct *p) { }
static inline void update_curr_bore(struct task_struct *p, u64 delta_exec) { }
static inline void restart_burst_bore(struct task_struct *p) { }
static inline void restart_burst_rescale_deadline_bore(struct task_struct *p) { }
static inline void task_fork_bore(struct task_struct *p) { }
static inline void bore_note_sleep(struct task_struct *p, u64 now) { }
static inline u64 bore_credit_ns(struct task_struct *p) { return 0; }
static inline u8 bore_score(struct task_struct *p) { return 0; }
static inline u8 bore_apply_score(struct task_struct *p) { return 0; }
#endif

#endif
