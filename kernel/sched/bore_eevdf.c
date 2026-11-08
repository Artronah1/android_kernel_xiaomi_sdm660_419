/* SPDX-License-Identifier: GPL-2.0 */
/*
 * BORE apply path for EEVDF: burst score becomes CFS load.weight.
 *
 * effective_prio_bore() and BORE_PRIO_MAX now live in bore.c (shared with
 * core.c set_load_weight()).
 */

static u64 bore_eevdf_slice(struct task_struct *p)
{
	u8 score = bore_apply_score(p);
	u64 slice = sysctl_sched_min_granularity;

	if (score)
		slice >>= score / 8;
	if (slice < 100000ULL)
		slice = 100000ULL;
	return slice;
}

static void bore_eevdf_commit(struct task_struct *p)
{
	if (p->bore.stop_update)
		return;
	if (p->sched_class != &fair_sched_class)
		return;
	if (!p->se.deadline)
		return;
	/*
	 * Only a task already on a runqueue may be reweighted.  At fork the
	 * child's sched_entity has on_rq==0 and its se.deadline/slice/vlag are
	 * inherited garbage from dup_task_struct(); reweight_task()/
	 * sched_eevdf_apply_slice() would then corrupt the destination cfs_rq
	 * (and, from sched_post_fork(), do so without rq->lock).  The base
	 * weight is already set by set_load_weight() during sched_fork(), and
	 * the running-task path (update_curr_bore) still commits normally.
	 */
	if (!p->se.on_rq)
		return;

	p->bore.stop_update = 1;
	reweight_task(p, effective_prio_bore(p));
	sched_eevdf_apply_slice(&p->se, bore_eevdf_slice(p));
	p->bore.stop_update = 0;
}
