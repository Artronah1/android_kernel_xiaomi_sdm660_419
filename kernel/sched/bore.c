// SPDX-License-Identifier: GPL-2.0
/*
 * Burst-Oriented Response Enhancer — CFS vruntime scoring.
 *
 * Score core backported from firelzrd BORE 7.0.1; applied through our
 * hand-rolled 4.19 EEVDF bridge (bore_eevdf.c).  The 6.12-only pieces of
 * upstream 7.0.1 (protect_slice, sched_delayed, requeue_delayed_entity,
 * base_slice) are intentionally omitted — this tree has no such machinery.
 *
 * Deliberately kept from the previous 6.8 port (behaviour, not bugs):
 *   - sched_burst_penalty_scale = 1280   (upstream 7.0.1: 1536)
 *   - sched_burst_cache_lifetime = 60ms   (upstream 7.0.1: 75ms)
 * Both remain runtime-tunable via sysctl.
 */
#include "sched.h"
#include <linux/sched/task.h>

#ifdef CONFIG_SCHED_BORE

u32 __read_mostly sched_bore = 1;
DEFINE_STATIC_KEY_TRUE(sched_bore_enabled);
u32 __read_mostly sched_burst_inherit_type = 2;
u32 __read_mostly sched_burst_smoothness = 1;
u32 __read_mostly sched_burst_penalty_offset = 24;
u32 __read_mostly sched_burst_penalty_scale = 1280;
u32 __read_mostly sched_burst_cache_lifetime = 60000000;
u32 __read_mostly sched_credit_cap_us = 16000;
DEFINE_STATIC_KEY_FALSE(sched_credit_key);

#define BORE_MAX_PENALTY ((40U << 8) - 1)
#define BORE_CACHE_SAMPLE 63
#define BORE_CACHE_SCAN (BORE_CACHE_SAMPLE * 2)

/*
 * Reciprocal table for 1/n, n in [1, 63]; removes the division from the
 * burst-inheritance averaging hot path.  lut[n] = (2^32 - 1 + n) / n.
 */
static u32 bore_reciprocal_lut[BORE_CACHE_SAMPLE + 1];

static u32 bore_log2p1(u64 v)
{
	int clz, exp;
	u32 mant;

	if (!v)
		return 0;
	clz = __builtin_clzll(v);
	exp = 64 - clz;
	mant = (u32)((v << clz) << 1 >> (64 - 8));
	return (exp << 8) | mant;
}

static u32 calc_burst_penalty(u64 burst_time)
{
	u32 greed = bore_log2p1(burst_time);
	u32 tolerance = sched_burst_penalty_offset << 8;
	s32 diff = (s32)greed - (s32)tolerance;
	u32 scaled;

	if (diff < 0)
		diff = 0;
	scaled = (u32)diff * sched_burst_penalty_scale >> 10;
	if (scaled > BORE_MAX_PENALTY)
		scaled = BORE_MAX_PENALTY;
	return scaled;
}

static u32 binary_smooth(u32 new, u32 old)
{
	s32 inc = (s32)new - (s32)old;

	if (inc >= 0)
		return old + ((u32)inc >> sched_burst_smoothness);
	return new;
}

static void bore_set_penalty(struct task_struct *p)
{
	u16 pen;

	if (p->flags & PF_KTHREAD) {
		p->bore.penalty = 0;
		return;
	}
	pen = p->bore.curr_penalty;
	if (p->bore.prev_penalty > pen)
		pen = p->bore.prev_penalty;
	p->bore.penalty = pen;
}

#ifdef CONFIG_SCHED_EEVDF
static void bore_eevdf_commit(struct task_struct *p);
#endif

/*
 * Sleep credit (7.0.1): remember when a task went to sleep so that, on
 * wakeup, place_entity() can pull its virtual deadline earlier by the
 * time it slept (capped at sched_credit_cap_us).  bore_credit_ns() is
 * one-shot: it clears the timestamp once consumed.
 */
void bore_note_sleep(struct task_struct *p, u64 now)
{
	p->bore.credit_sleep = now;
}

u64 bore_credit_ns(struct task_struct *p)
{
	u64 cap = (u64)sched_credit_cap_us * 1000ULL;
	u64 slept = 0, credit = 0;

	if (p->bore.credit_sleep) {
		slept = rq_clock(task_rq(p)) - p->bore.credit_sleep;
		if ((s64)slept > 0) {
			p->bore.credit_sleep = 0;
			credit = slept < cap ? slept : cap;
		} else {
			slept = 0;
		}
	}

	return credit;
}

void update_curr_bore(struct task_struct *p, u64 delta_exec)
{
	u32 curr;

	if (!bore_enabled())
		return;

	p->bore.burst_time += delta_exec;
	curr = calc_burst_penalty(p->bore.burst_time);
	p->bore.curr_penalty = curr;
	if (curr <= p->bore.prev_penalty)
		return;
	bore_set_penalty(p);
#ifdef CONFIG_SCHED_EEVDF
	bore_eevdf_commit(p);
#endif
}

void restart_burst_bore(struct task_struct *p)
{
	u32 smoothed;

	if (!bore_enabled())
		return;

	smoothed = binary_smooth(p->bore.curr_penalty, p->bore.prev_penalty);
	p->bore.prev_penalty = smoothed;
	p->bore.curr_penalty = 0;
	p->bore.burst_time = 0;
	bore_set_penalty(p);
#ifdef CONFIG_SCHED_EEVDF
	bore_eevdf_commit(p);
#endif
}

u8 bore_apply_score(struct task_struct *p)
{
	if (!bore_enabled())
		return 0;
	if (p->flags & PF_KTHREAD)
		return 0;
	if (p->policy == SCHED_BATCH || p->policy == SCHED_IDLE)
		return 0;
	if ((p->flags & PF_WAKE_UP_IDLE) || schedtune_prefer_idle(p))
		return 0;
#ifdef CONFIG_UCLAMP_TASK
	if (uclamp_is_used()) {
		if (uclamp_eff_value(p, UCLAMP_MAX) < SCHED_CAPACITY_SCALE / 5)
			return 0;
		if (uclamp_eff_value(p, UCLAMP_MIN))
			return 0;
	}
#endif
	return bore_score(p);
}

#define BORE_PRIO_MAX 39

/*
 * Effective (score-adjusted) nice-based priority index used as the CFS
 * load weight selector.  Lives here rather than in bore_eevdf.c so that
 * core.c set_load_weight() can consult it regardless of EEVDF.
 */
u8 effective_prio_bore(struct task_struct *p)
{
	int prio = p->static_prio - MAX_RT_PRIO;

	prio += bore_apply_score(p);
	if (prio < 0)
		prio = 0;
	if (prio > BORE_PRIO_MAX)
		prio = BORE_PRIO_MAX;
	return prio;
}

#ifdef CONFIG_SCHED_EEVDF
#include "bore_eevdf.c"

/*
 * Rescale a virtual slice from old_prio to new_prio through the real
 * weight ratio, matching upstream 7.0.1:
 *   w = sched_prio_to_weight[prio],  inv_w = sched_prio_to_wmult[prio]
 *   delta * w_old / 1024 * inv_w_new / 2^32
 */
static inline u64 rescale_slice(u64 delta, u8 old_prio, u8 new_prio)
{
	u64 unscaled, rescaled;

	unscaled = mul_u64_u32_shr(delta, sched_prio_to_weight[old_prio], 10);
	rescaled = mul_u64_u32_shr(unscaled, sched_prio_to_wmult[new_prio], 22);
	return rescaled;
}

/*
 * Recompute the burst penalty and, if the effective priority went UP
 * (i.e. the task became less penalised), stretch the remaining virtual
 * deadline proportionally so the task is not unfairly short-changed.
 */
void restart_burst_rescale_deadline_bore(struct task_struct *p)
{
	struct sched_entity *se = &p->se;
	s64 vscaled, vremain = se->deadline - se->vruntime;
	u8 old_prio = effective_prio_bore(p);

	restart_burst_bore(p);

	{
		u8 new_prio = effective_prio_bore(p);

		if (old_prio > new_prio) {
			vscaled = rescale_slice(abs(vremain), old_prio, new_prio);
			if (unlikely(vremain < 0))
				vscaled = -vscaled;
			se->deadline = se->vruntime + vscaled;
		}
	}
}
#endif

static bool bore_inheritable(struct task_struct *p)
{
	return p->sched_class == &fair_sched_class &&
	       !p->exit_state && !(p->flags & PF_KTHREAD);
}

static u32 count_children_upto2(struct task_struct *p)
{
	struct list_head *h = &p->children;

	if (h->next == h)
		return 0;
	if (h->next->next == h)
		return 1;
	return 2;
}

static u32 bore_average(u32 sum, u32 cnt)
{
	if (cnt <= 1)
		return cnt ? sum : 0;
	return (u32)(((u64)sum * bore_reciprocal_lut[cnt]) >> 32);
}

static void update_child_burst_cache(struct task_struct *p, u64 now)
{
	struct task_struct *child;
	u32 cnt = 0, sum = 0, scan = 0, avg;

	list_for_each_entry(child, &p->children, sibling) {
		if (scan++ >= BORE_CACHE_SCAN)
			break;
		if (!bore_inheritable(child))
			continue;
		cnt++;
		sum += child->bore.penalty;
		if (cnt >= BORE_CACHE_SAMPLE)
			break;
	}
	avg = bore_average(sum, cnt);
	if (avg < p->bore.penalty)
		avg = p->bore.penalty;
	p->bore.child_burst = avg;
	p->bore.child_burst_cnt = cnt;
	p->bore.child_burst_cached = now;
}

static u32 inherit_from_parent(struct task_struct *parent, u64 now)
{
	if (now - parent->bore.child_burst_cached > sched_burst_cache_lifetime)
		update_child_burst_cache(parent, now);
	return parent->bore.child_burst;
}

static u32 inherit_from_ancestor(struct task_struct *parent, u64 now)
{
	struct task_struct *anc = parent;

	while (anc->real_parent != anc && count_children_upto2(anc) == 1)
		anc = anc->real_parent;
	return inherit_from_parent(anc, now);
}

static u32 inherit_from_thread_group(struct task_struct *p, u64 now)
{
	struct task_struct *leader = p->group_leader;
	struct task_struct *t;
	u32 cnt = 0, sum = 0, scan = 0, avg;

	if (now - leader->bore.group_burst_cached <= sched_burst_cache_lifetime)
		return leader->bore.group_burst;

	for_each_thread(leader, t) {
		if (scan++ >= BORE_CACHE_SCAN)
			break;
		if (!bore_inheritable(t))
			continue;
		cnt++;
		sum += t->bore.penalty;
		if (cnt >= BORE_CACHE_SAMPLE)
			break;
	}
	avg = bore_average(sum, cnt);
	if (avg < leader->bore.penalty)
		avg = leader->bore.penalty;
	leader->bore.group_burst = avg;
	leader->bore.group_burst_cached = now;
	return avg;
}

void task_fork_bore(struct task_struct *p)
{
	u64 now;
	u32 inherited = 0;

	if (!bore_enabled() || !bore_inheritable(p))
		return;

	now = ktime_get_ns();
	read_lock(&tasklist_lock);
	if (!thread_group_leader(p))
		inherited = inherit_from_thread_group(p, now);
	else if (sched_burst_inherit_type == 2)
		inherited = inherit_from_ancestor(p->real_parent, now);
	else if (sched_burst_inherit_type == 1)
		inherited = inherit_from_parent(p->real_parent, now);
	read_unlock(&tasklist_lock);

	if (p->bore.prev_penalty < inherited)
		p->bore.prev_penalty = inherited;
	p->bore.curr_penalty = 0;
	p->bore.burst_time = 0;
	p->bore.credit_sleep = 0;
	bore_set_penalty(p);
	/*
	 * NOTE: deliberately NO bore_eevdf_commit() here.
	 *
	 * task_fork_bore() is called from sched_post_fork() in kernel/fork.c,
	 * which runs *after* tasklist_lock is dropped and *without* rq->lock.
	 * Committing there calls reweight_task()/reweight_entity(), which
	 * unconditionally touches the destination cfs_rq (dequeue_load_avg/
	 * enqueue_load_avg/mutating avg.load_*) -> a lockless, cross-CPU
	 * corruption of the runqueue while a fork storm is in flight.
	 *
	 * The running-task paths (update_curr_bore(), restart_burst_bore())
	 * commit under rq->lock, and the child's BORE weight is (re)applied
	 * there on its first run; the fork-time weight is inherited from the
	 * parent.  This matches upstream BORE 4.19 legacy, whose
	 * sched_post_fork_bore() only sets the penalty fields.
	 */
}

void reset_task_bore(struct task_struct *p)
{
	memset(&p->bore, 0, sizeof(p->bore));
}

void __init sched_init_bore(void)
{
	int i;

	bore_reciprocal_lut[0] = 0;
	for (i = 1; i <= BORE_CACHE_SAMPLE; i++)
		bore_reciprocal_lut[i] = (u32)div64_u64(0xffffffffULL + i, i);

	reset_task_bore(&init_task);
	if (sched_bore)
		static_branch_enable(&sched_bore_enabled);
	else
		static_branch_disable(&sched_bore_enabled);
	if (sched_credit_cap_us)
		static_branch_enable(&sched_credit_key);
	pr_info("%s %s by %s\n",
		SCHED_BORE_PROGNAME, SCHED_BORE_VERSION, SCHED_BORE_AUTHOR);
}

#ifdef CONFIG_SYSCTL
static int bore_zero;
static int bore_one = 1;
static int bore_two = 2;
static int bore_three = 3;
static int bore_sixthree = 63;
static int bore_12bit = 4095;
static int bore_million = 1000000;

/*
 * Re-evaluate every fair task's BORE-adjusted weight, e.g. after the user
 * toggles sched_bore.  4.19 has no scoped_guard(), so take tasklist_lock
 * by hand; lock order tasklist_lock -> rq->lock is the standard one.
 */
static void readjust_all_task_weights(void)
{
	struct task_struct *task;
	struct rq *rq;
	struct rq_flags rf;

	write_lock_irq(&tasklist_lock);
	for_each_process(task) {
		if (task->sched_class != &fair_sched_class || task->exit_state)
			continue;
		rq = task_rq_lock(task, &rf);
		update_rq_clock(rq);
		reweight_task(task, effective_prio_bore(task));
		task_rq_unlock(rq, task, &rf);
	}
	write_unlock_irq(&tasklist_lock);
}

static int sched_bore_sysctl(struct ctl_table *table, int write,
			     void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	if (ret || !write)
		return ret;
	if (sched_bore)
		static_branch_enable(&sched_bore_enabled);
	else
		static_branch_disable(&sched_bore_enabled);
	readjust_all_task_weights();
	return 0;
}

static int sched_credit_cap_us_sysctl(struct ctl_table *table, int write,
				      void __user *buffer, size_t *lenp,
				      loff_t *ppos)
{
	int ret;

	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	if (ret || !write)
		return ret;
	if (sched_credit_cap_us)
		static_branch_enable(&sched_credit_key);
	else
		static_branch_disable(&sched_credit_key);
	return 0;
}

static struct ctl_table sched_bore_table[] = {
	{
		.procname	= "sched_bore",
		.data		= &sched_bore,
		.maxlen		= sizeof(u32),
		.mode		= 0644,
		.proc_handler	= sched_bore_sysctl,
		.extra1		= &bore_zero,
		.extra2		= &bore_one,
	},
	{
		.procname	= "sched_burst_inherit_type",
		.data		= &sched_burst_inherit_type,
		.maxlen		= sizeof(u32),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &bore_zero,
		.extra2		= &bore_two,
	},
	{
		.procname	= "sched_burst_smoothness",
		.data		= &sched_burst_smoothness,
		.maxlen		= sizeof(u32),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &bore_zero,
		.extra2		= &bore_three,
	},
	{
		.procname	= "sched_burst_penalty_offset",
		.data		= &sched_burst_penalty_offset,
		.maxlen		= sizeof(u32),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &bore_zero,
		.extra2		= &bore_sixthree,
	},
	{
		.procname	= "sched_burst_penalty_scale",
		.data		= &sched_burst_penalty_scale,
		.maxlen		= sizeof(u32),
		.mode		= 0644,
		.proc_handler	= proc_dointvec_minmax,
		.extra1		= &bore_zero,
		.extra2		= &bore_12bit,
	},
	{
		.procname	= "sched_burst_cache_lifetime",
		.data		= &sched_burst_cache_lifetime,
		.maxlen		= sizeof(u32),
		.mode		= 0644,
		.proc_handler	= proc_dointvec,
	},
	{
		.procname	= "sched_credit_cap_us",
		.data		= &sched_credit_cap_us,
		.maxlen		= sizeof(u32),
		.mode		= 0644,
		.proc_handler	= sched_credit_cap_us_sysctl,
		.extra1		= &bore_zero,
		.extra2		= &bore_million,
	},
	{ }
};

static int __init sched_bore_sysctl_init(void)
{
	register_sysctl("kernel", sched_bore_table);
	return 0;
}
late_initcall(sched_bore_sysctl_init);
#endif

#endif /* CONFIG_SCHED_BORE */
