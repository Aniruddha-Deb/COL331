/*
 * Deadline monotonic scheduling for the linux kernel
 * Copyright 2023 Aniruddha Deb
 *
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/pid.h>
#include <linux/syscalls.h>
#include <linux/list.h>
#include <linux/kthread.h>

#define DEBUG 1
#define debugk(...) do { if (DEBUG) printk(__VA_ARGS__); } while (0)

// TODO synchronize access to this list
static LIST_HEAD(task_list);
struct task_struct* dispatcher_thread;

#define STATE_RUNNING 1;
#define STATE_SLEEPING 2;
#define STATE_READY 3;

struct dm_pcb {
        struct list_head list_node;
	struct task_struct* linux_task;
	struct timer_list timer;
	unsigned int period;
	unsigned int deadline;
	unsigned int duration;
	unsigned int state;
};

struct dm_pcb* current_task;

// TC: O(n)
struct dm_pcb* pcb_search(pid_t pid) {

	struct dm_pcb *pcb;
	list_for_each_entry(pcb, &task_list, list_node) {
		if (pcb->linux_task->pid == pid)
			return pcb;
	}

	return NULL;
}

void pcb_insert(struct dm_pcb *data) {

	// need to keep the list sorted
	struct dm_pcb *pcb;
	list_for_each_entry(pcb, &task_list, list_node) {
		if (pcb->deadline >= data->deadline) break;
	}
	// does list_add add it before the head or after the head?
	list_add(&data->list_node, pcb->list_node.prev);
}

unsigned int compute_interference(int i, int t) {
	unsigned int I = 0;
	struct dm_pcb *pcb;
	int j = 0;
	list_for_each_entry(pcb, &task_list, list_node) {
		if (++j >= i) break;
		I += ((t+pcb->period-1)/pcb->period)*pcb->duration;
	}
	return I;
}

int is_schedulable(void) {
	
	// admission bound
	// task_list has to be sorted in order of the RT process priorities..
	// the priorities would be determined by the deadlines and periods,
	// and the process priorities themselves...
	//
	// TODO how do we keep them sorted?
	// use a ~RB-tree with the deadlines as a key~ sorted LL
	struct dm_pcb *pcb;
	unsigned int running_t = 0;
	unsigned int interference = 0;

	int i=0;
	list_for_each_entry(pcb, &task_list, list_node) {
		unsigned int t = running_t + pcb->duration;
		int cont = 1;
		while (cont) {
			interference = compute_interference(i, t);
			if (interference + pcb->duration <= t) cont = false;
			else t = interference + pcb->duration;

			if (t > pcb->deadline) goto unschedulable;
		}
		i++;
		running_t += pcb->duration;
	}
	return 1;
	
unschedulable:
	return 0;
}

int timer_callback(unsigned long data) {
	
	struct dm_pcb* pcb = (struct dm_pcb*)data;
	// set the current state to ready and trigger the dispatcher
	mod_timer(&pcb->timer, jiffies + msecs_to_jiffies(pcb->period));
	if(current_task != pcb) {
		pcb->state = STATE_READY;
		wake_up_process(dispatcher_thread);
	}
}

void __log_pid_list(void) {

	struct dm_pcb *pcb;
	list_for_each_entry(pcb, &task_list, list_node) {
		debugk("[%u (%u,%u,%u)]", pcb->linux_task->pid, pcb->period, pcb->deadline, pcb->duration);
		debugk("       v");
	}
}

int __remove(pid_t pid) {
	struct dm_pcb* pcb_struct = pcb_search(pid);
	if (!pcb_struct) return -EINVAL;
	list_del(&pcb_struct->list_node);
	del_timer_sync(&pcb_struct->timer);
	kfree(pcb_struct);
	debugk("Removed pid %u\n", pid);
	return 0;
}

int __register_dm(pid_t pid, unsigned int period, unsigned int deadline, 
		unsigned int duration) {

	debugk("Registering pid %u. Current list: \n", pid);
	__log_pid_list();

	if (pcb_search(pid)) {
		debugk("Task with given pid exists. Not re-registering\n");
		return -EINVAL;
	}

	struct dm_pcb* new_pcb;
        new_pcb = (struct dm_pcb*)kmalloc(sizeof(*new_pcb), GFP_KERNEL);

	new_pcb->period = period;
	new_pcb->deadline = deadline;
	new_pcb->duration = duration;

        struct pid* pid_struct = find_get_pid(pid);
        struct task_struct* task = pid_task(pid_struct, PIDTYPE_PID);
	if (task == NULL) {
		kfree(new_pcb);
		debugk("Could not find task with given pid\n");
		return -EINVAL;
	}
	new_pcb->linux_task = task;

	init_timer(&new_pcb->timer);
	new_pcb->timer.expires = jiffies + msecs_to_jiffies(new_pcb->period));
	new_pcb->timer.data = (unsigned long)new_pcb;
	new_pcb->timer.function = timer_callback;
	add_timer(&new_pcb->timer);

	pcb_insert(new_pcb);
	if (!is_schedulable()) {
		debugk("List of tasks is not schedulable\n");
		__remove(pid);
		return -EINVAL;
	}

	debugk("Successfully scheduled task with pid %u\n", pid);
	return 0;
}

void __list() {
	struct dm_pcb* pcb;
	list_for_each_entry(pcb, &task_list, list_node) {
		printk("PID: %u\tperiod: %u\tdeadline: %u\t%u\n",
			pcb->linux_task->pid, pcb->period, pcb->deadline, 
			pcb->duration);
	}
}

int __yield(pid_t pid) {

	struct dm_pcb* pcb;
	list_for_each_entry(pcb, &task_list, list_node) {
		if (pcb->linux_task->pid == pid) {
			// put task to sleep
			// need to send signals to make this happen...
			struct kernel_siginfo info;
			info.si_signo = pid_sig->signal;
			int ret = send_sig_info(SIGSTOP, &info, pcb->linux_task);
			
			pcb->state = STATE_SLEEPING;
			current_task = NULL;
			break;
		}
	}
	// wake up the dispatcher_thread
	wake_up_process(dispatcher_thread);
	return 0;
}

int dispatcher_thread_callback() {
	// preempt the current task
	// find the highest priority ready task in our task queue and run it
	//
	// ? What if we're already running a task that is in our task queue?
	// If there's a higher priority ready task in our task queue, then it
	// implies that it needs to run before the current task, even if the
	// current task is real time...
	
	// find the highest priority (smallest deadline) ready task first
	struct dm_pcb* pcb;
	struct dm_pcb* hp_pcb = NULL;
	list_for_each_entry(pcb, &task_list, list_node) {
		if (pcb->state == STATE_READY && 
		    (hp_pcb == NULL || pcb->deadline <= hp_pcb->deadline)) {
			hp_pcb = pcb;
		}
	}

	// if there's no ready next process, return
	if (!hp_pcb) return 0;

	// check if there's a currently running task with a higher priority than
	// the highest priority ready task.
	if (current_task && current_task->deadline < hp_pcb->deadline) return 0;

	// if there's a running task, we need to preempt it out (it has a lower
	// priority than hp_pcb)
	if (current_task) {
		struct sched_param sp1;
		sp1.sched_priority = 0;
		sched_setscheduler(current_task->linux_task, SCHED_NORMAL, &sp1);
		current_task->state = STATE_READY; // recall that this task has
						   // to call yield() to go to 
						   // sleep
	}

	// need to run the highest priority task 

	struct kernel_siginfo info;
	info.si_signo = SIGCONT;
	send_sig_info(SIGCONT, &info, hp_pcb->linux_task);
	// wake_up_process(hp_pcb->linux_task);

	struct sched_param sparam;
	sparam.sched_priority = 99;
	sched_setscheduler(hp_pcb->linux_task, SCHED_FIFO, &sparam);
	hp_pcb->state = STATE_RUNNING;
	current_task = hp_pcb;
	return 0;
}


SYSCALL_DEFINE4(register_dm, pid_t, pid, unsigned int, period, 
		unsigned int, deadline, unsigned int, exec_time) {

	return __register_dm(pid, period, deadline, exec_time);
}

SYSCALL_DEFINE4(register_rm, pid_t, pid, unsigned int, period, 
		unsigned int, deadline, unsigned int, exec_time) {

	if (period != deadline) return -EINVAL;
	return __register_dm(pid, deadline, deadline, exec_time);
}

SYSCALL_DEFINE1(yield, pid_t, pid) {
	return __yield(pid);
}

SYSCALL_DEFINE1(remove, pid_t, pid) {

	return __remove(pid);
}

SYSCALL_DEFINE0(list) {
	__list();
	return 0;
}

