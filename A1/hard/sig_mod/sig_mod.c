/*
 * Signal generator module, made for COL331 A1
 * Copyright 2023 Aniruddha Deb
 *
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/proc_fs.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/pid.h>

#define DELAY 1*HZ
#define PROCFILE_NAME "sig_target"
#define BUFSIZE 512
#define PID_LIMIT 256

MODULE_LICENSE("GPL");
MODULE_AUTHOR ("Aniruddha Deb");

static struct proc_dir_entry *sig_target;
static int n_pids = 0;

struct pid_signal {
    pid_t pid;
    int signal;
    struct list_head list;
};

static DEFINE_MUTEX(pid_ll_mtx);
static LIST_HEAD(pid_sig_ll);

void send_signals(struct timer_list* timer) {

    struct pid_signal *pid_sig, *t;
    struct pid *pid_struct;
    struct task_struct *task;
    struct kernel_siginfo info;

    memset(&info, 0, sizeof(struct kernel_siginfo));

    mutex_lock(&pid_ll_mtx);
    list_for_each_entry_safe(pid_sig, t, &pid_sig_ll, list) {
	pid_struct = find_get_pid(pid_sig->pid);
	task = pid_task(pid_struct, PIDTYPE_PID);

        info.si_signo = pid_sig->signal;
        if (task != NULL) {
            int ret = send_sig_info(pid_sig->signal, &info, task);
            if (ret < 0) {
                printk("Error sending signal\n");
            }
        }
        else {
            // remove this node from the LL; task doesn't exist
            list_del(&pid_sig->list);
            kfree(pid_sig);
        }
    }
    mutex_unlock(&pid_ll_mtx);

    mod_timer(timer, jiffies+DELAY);
}

DEFINE_TIMER(timer, send_signals);

static ssize_t sig_tgt_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos) {
    int num,c;
    int pid, signal;
    char buf[BUFSIZE];
    if(*ppos > 0 || count > BUFSIZE)
    	return -EFAULT;
    if(copy_from_user(buf, ubuf, count))
    	return -EFAULT;

    num = sscanf(buf,"%d, %d", &pid, &signal);
    if(num != 2)
    	return -EFAULT;
    if (pid <= 300) // defined in pid.c; basically kernel process pids
        return -EFAULT;

    // has to be synchronized, otherwise if A adds a pid when B is not seeing,
    // then we'd have more PID's than the limit
    mutex_lock(&pid_ll_mtx);
    if (n_pids >= PID_LIMIT) {
        // have to unlock the mutex inside before returning, otherwise will
        // remain locked
        mutex_unlock(&pid_ll_mtx);
        return -EFAULT;
    }
    mutex_unlock(&pid_ll_mtx);

    struct pid_signal *pid_sig;
    pid_sig = kmalloc(sizeof(*pid_sig), GFP_KERNEL);
    pid_sig->pid = pid;
    pid_sig->signal = signal;
    
    mutex_lock(&pid_ll_mtx);
    list_add_tail(&pid_sig->list, &pid_sig_ll);
    n_pids += 1;
    mutex_unlock(&pid_ll_mtx);

    c = strlen(buf);
    *ppos = c;
    return c;
}

static ssize_t sig_tgt_read(struct file *file, char __user *ubuf, size_t count, loff_t *ppos) {
    struct pid_signal *pid_sig;
    char buf[BUFSIZE];
    int len=0;
    if(*ppos > 0 || count < BUFSIZE)
    	return 0;

    mutex_lock(&pid_ll_mtx);
    list_for_each_entry(pid_sig, &pid_sig_ll, list) {
        len += sprintf(buf+len, "%d, %d\n", pid_sig->pid, pid_sig->signal);
    }
    mutex_unlock(&pid_ll_mtx);
    
    if(copy_to_user(ubuf,buf,len))
    	return -EFAULT;
    *ppos = len;
    return len;
}

static const struct proc_ops sig_target_ops = {
    .proc_write = sig_tgt_write,
    .proc_read = sig_tgt_read
};

static int __init sig_mod_init(void)
{
    sig_target = proc_create(PROCFILE_NAME, 00622, NULL, &sig_target_ops);

    mutex_init(&pid_ll_mtx);

    timer.expires = jiffies + DELAY;
    add_timer(&timer);
    return 0;
}

static void __exit sig_mod_exit(void)
{
    struct pid_signal *pid_sig, *t;

    del_timer_sync(&timer);
    proc_remove(sig_target);

    // clear up the LL
    mutex_lock(&pid_ll_mtx);
    list_for_each_entry_safe(pid_sig, t, &pid_sig_ll, list) {
        list_del(&pid_sig->list);
        kfree(pid_sig);
    }
    mutex_unlock(&pid_ll_mtx);

    mutex_destroy(&pid_ll_mtx);

}

module_init(sig_mod_init);
module_exit(sig_mod_exit);
