#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/time.h>
#include <linux/kernel_stat.h>
#include <linux/syscalls.h>
#include <linux/fcntl.h>
#include <asm/uaccess.h>
#include <linux/kallsyms.h>
#include <linux/version.h>

#define UF_VERSION "procfs_1"

/* fudge factor */
static long fudge = 0;
module_param(fudge, long, 0000);
MODULE_PARM_DESC(fudge, "Amount to add to uptime, in seconds");

/* existing uptime pde */
struct proc_dir_entry *__uptime;
/* proc_create_single_data() call */
static struct proc_dir_entry *(*__proc_create_single_data)(const char *,
    umode_t, struct proc_dir_entry *, int (*)(struct seq_file *, void *),
    void *);
/* uptime_proc_fops file operations */
struct file_operations *__uptime_proc_fops;

static int uptime_fudger_proc_show(struct seq_file *m, void *v)
{
    struct timespec64 uptime;
    struct timespec64 idle;
    u64 nsec;
    u32 rem;
    int i;

    nsec = 0;
    for_each_possible_cpu(i)
        nsec += (__force u64) kcpustat_cpu(i).cpustat[CPUTIME_IDLE];

    /* this function is only available in newer kernels */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0)
    ktime_get_boottime_ts64(&uptime);
#else
    get_monotonic_boottime64(&uptime);
#endif

    /* idle time */
    idle.tv_sec = div_u64_rem(nsec, NSEC_PER_SEC, &rem);
    idle.tv_nsec = rem;

    /* fudging */
    uptime.tv_sec += fudge;
    idle.tv_sec += fudge;

    /* identical interface to builtin uptime file */
    seq_printf(m, "%lu.%02lu %lu.%02lu\n",
            (unsigned long) uptime.tv_sec,
            (uptime.tv_nsec / (NSEC_PER_SEC / 100)),
            (unsigned long) idle.tv_sec,
            (idle.tv_nsec / (NSEC_PER_SEC / 100)));

    return 0;
}

static int uptime_fudger_proc_open(struct inode *inode, struct  file *file) {
    return single_open(file, uptime_fudger_proc_show, NULL);
}

static const struct file_operations uptime_fudger_proc_fops = {
    .owner = THIS_MODULE,
    .open = uptime_fudger_proc_open,
    .read = seq_read,
    .llseek = seq_lseek,
    .release = single_release,
};

static int __init uptime_fudger_proc_init(void) {
    struct proc_dir_entry *proc_root;
    rwlock_t *proc_subdir_lock;
    struct proc_dir_entry *(*pde_subdir_find)(struct proc_dir_entry *,
        const char *, unsigned int);

    /* first we need to find the existing uptime proc handler */
    /* locate the proc_root entry */
    proc_root = (struct proc_dir_entry *)(uintptr_t)
        kallsyms_lookup_name("proc_root");
    /* then find the lock used to read/write safely */
    proc_subdir_lock = (rwlock_t *)(uintptr_t)
        kallsyms_lookup_name("proc_subdir_lock");
    /* locate the proc find function */
    pde_subdir_find = (struct proc_dir_entry *(*)(struct proc_dir_entry *,
        const char *, unsigned int))(uintptr_t)
        kallsyms_lookup_name("pde_subdir_find");
    /* locate the proc create function */
    __proc_create_single_data = (struct proc_dir_entry *(*)(const char *,
        umode_t, struct proc_dir_entry *, int (*)(struct seq_file *, void *),
        void *))(uintptr_t)kallsyms_lookup_name("proc_create_single_data");
    /* proc_create_single_data() is on newer kernels; on older ones, */
    /* need to find the uptime_proc_fops struct to use proc_create() */
    __uptime_proc_fops = (struct file_operations *)(uintptr_t)
        kallsyms_lookup_name("uptime_proc_fops");

    /* check if anything is null */
    if ((!proc_root || !proc_subdir_lock || !pde_subdir_find) ||
            !(__proc_create_single_data || __uptime_proc_fops)) {
        printk("[uptime_fudger]" KERN_EMERG "Error loading symbols: " UF_VERSION
                "%p %p %p %p %p\n",
                proc_root, proc_subdir_lock, pde_subdir_find,
                __proc_create_single_data, __uptime_proc_fops);
        printk("[uptime_fudger] Please create an issue on GitHub indicating "
                "the kernel version with the above message\n");
        return -1;
    }

    /* lock the subdir_lock */
    write_lock(proc_subdir_lock);

    /* locate the uptime node */
    __uptime = (*pde_subdir_find)(proc_root, "uptime", 6);

    /* unlock the subdir_lock */
    write_unlock(proc_subdir_lock);

    /* remove the existing uptime procfile, if it exists */
    if (__uptime) {
        printk("[uptime_fudger] removing existing uptime procfile\n");
        remove_proc_entry("uptime", NULL);
    }

    /* create our own version */
    proc_create("uptime", 0, NULL, &uptime_fudger_proc_fops);

    return 0;
}

static void __exit uptime_fudger_proc_exit(void) {
    int (*uptime_proc_show)(struct seq_file *, void *v);

    /* remove our version of the uptime proc */
    remove_proc_entry("uptime", NULL);

    /* check if we should restore the uptime procfile from the system */
    if (!__uptime) {
        return;
    }

    printk("[uptime_fudger] restoring existing uptime procfile\n");

    /* locate uptime_proc_show() from fs/proc/uptime.c */
    uptime_proc_show = (int (*)(struct seq_file *, void *v))(uintptr_t)
        kallsyms_lookup_name("uptime_proc_show");

    /* restore the original uptime procfile */
    if (__proc_create_single_data) {
        /* prefer using proc_create_single_data() for newer kernel versions */
        (*__proc_create_single_data)("uptime", 0, NULL,
            uptime_proc_show, NULL);
    } else {
        /* on older versions, we need to use proc_create() still */
        proc_create("uptime", 0, NULL, __uptime_proc_fops);
    }
}

MODULE_LICENSE("GPL");
module_init(uptime_fudger_proc_init);
module_exit(uptime_fudger_proc_exit);
