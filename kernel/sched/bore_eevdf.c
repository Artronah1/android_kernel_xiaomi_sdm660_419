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

	p->bore.stop_update = 1;
	reweight_task(p, effective_prio_bore(p));
	sched_eevdf_apply_slice(&p->se, bore_eevdf_slice(p));
	p->bore.stop_update = 0;
}
