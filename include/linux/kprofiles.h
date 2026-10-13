/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Kprofiles API
 * Copyright (C) 2021-2023 Dakkshesh <dakkshesh5@gmail.com>.
 */

#ifndef _LINUX_KPROFILES_H
#define _LINUX_KPROFILES_H

#ifdef CONFIG_KPROFILES

/* Profile modes */
#define KP_MODE_DISABLED     0
#define KP_MODE_BATTERY      1
#define KP_MODE_BALANCED     2
#define KP_MODE_PERFORMANCE  3

/* Get current profile mode (0-3) */
extern int kp_active_mode(void);

/* Set profile mode (0-3) */
extern int kp_set_mode(unsigned int mode);

/* Set profile mode for a duration, then rollback */
extern int kp_set_mode_rollback(unsigned int mode, unsigned int duration_ms);

#else /* !CONFIG_KPROFILES */

static inline int kp_active_mode(void) { return KP_MODE_DISABLED; }
static inline int kp_set_mode(unsigned int mode) { return 0; }
static inline int kp_set_mode_rollback(unsigned int mode, unsigned int duration_ms) { return 0; }

#endif /* CONFIG_KPROFILES */

#endif /* _LINUX_KPROFILES_H */
