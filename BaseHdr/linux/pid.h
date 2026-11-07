/*
 * DCL pid.h -- the pid surface drm_file.c names.
 *
 * mainline splits this across <linux/pid_types.h> (enum pid_type) and
 * <linux/pid.h> (struct pid and the accessors), and it reaches drm_file.c
 * because mainline's <linux/fs.h> includes <linux/pid.h> (fs.h:20). DCL's
 * fs.h is pinned and includes nothing, so the whole surface lands in
 * <linux/file.h> with the rest of the vocabulary fs.h owes the drm set --
 * see the note at the top of that header for why file.h is that carrier.
 *
 * Both halves are in this one file because nothing else in the tree needs
 * either, and a header holding a five-value enum and four functions is not
 * worth splitting the way mainline splits it: the split exists there so
 * <linux/types.h> can carry the enum, and no source here reaches the enum
 * that way.
 *
 * There is no pid table, and that is the fact everything below follows from.
 * DCL identifies a task by the scheduler context dcl_current() hands back
 * (<linux/sched.h>), not by a number, so there is no pid for a task to
 * report and no table to look one up in.
 */
#ifndef _LINUX_PID_H
#define _LINUX_PID_H

struct pid;
struct task_struct;

/*
 * enum pid_type -- mainline's five values in mainline's order, copied from
 * <linux/pid_types.h>. Only PIDTYPE_TGID is named in this tree, at
 * drm_file.c:1030; the other four are here so that naming the next one is a
 * copy and not a guess about what number it should carry.
 */
enum pid_type {
	PIDTYPE_PID,
	PIDTYPE_TGID,
	PIDTYPE_PGID,
	PIDTYPE_SID,
	PIDTYPE_MAX,
};

/*
 * get_pid() / put_pid() -- the refcount pair. DCL's are the identity and a
 * no-op, which is only sound because there is nothing to refcount: they both
 * take whatever task_tgid() handed out (below, NULL) and pass it on. Every
 * call site in this tree has been checked against that --
 * drm_file.c:145 stores the result, :193 and :273 put it on file teardown,
 * :476-481 do get_pid/replace/put_pid around drm_file_update_pid's swap --
 * and all four deal in NULL.
 */
static inline struct pid* get_pid(struct pid* pid)
{
	return pid;
}

static inline void put_pid(struct pid* pid)
{
	(void)pid;
}

/*
 * task_tgid() -- the thread-group pid of a task. NULL, because there is no
 * pid to report (see the top of this file). It is not a stub that ought to
 * become real when pids arrive: a NULL here is already the correct answer
 * for the one caller's model, which is a single client that never changes --
 * drm_file.c:145 stores it, and drm_file.c:465-471 compares the next call's
 * result against the stored one and returns early when they are equal, which
 * NULL == NULL is. The swap at :476-481 is therefore never reached, and
 * drm_file.c:1029-1034 reads it back as `task ? ... : "Unset"`.
 */
static inline struct pid* task_tgid(struct task_struct* task)
{
	(void)task;
	return NULL;
}

/*
 * task_pid_nr() -- the numeric process id, for a debug line. 0 is the answer
 * mainline gives for a task outside the calling namespace, and drm_file.c:243
 * and :333 print it as `%d` beside current->comm in drm_dbg_core(), which is
 * a line DCL's logging drops at compile time anyway.
 */
static inline int task_pid_nr(struct task_struct* task)
{
	(void)task;
	return 0;
}

/*
 * pid_task() -- the task owning a pid, in a given pid type. NULL: there is
 * no table to look in. The single caller (drm_file.c:1030) prints the result
 * behind a NULL check, so an empty log is the whole cost -- "comm: Unset
 * pid: 0" on a path that only runs when drm_file_err() is already logging an
 * error.
 *
 * A static inline and not the exported function mainline has in
 * kernel/pid.c, so no link symbol is added for a body that returns NULL.
 */
static inline struct task_struct* pid_task(struct pid* pid, enum pid_type type)
{
	(void)pid;
	(void)type;
	return NULL;
}

#endif /* _LINUX_PID_H */
