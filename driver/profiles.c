#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "profiles.h"
#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#define STATUS_VERSION 1

static struct profile_table __rcu *g_table;
static DEFINE_MUTEX(g_table_lock);
static const char *g_default_problem;
static atomic_long_t g_next_claim = ATOMIC_LONG_INIT(0);

const struct profile_table *profiles_current(void) {
    return rcu_dereference(g_table);
}

static const struct profile_table *table_locked(void) {
    return rcu_dereference_protected(g_table, lockdep_is_held(&g_table_lock));
}

static struct profile_table *table_copy(gfp_t flags) {
    return kmemdup(table_locked(), sizeof(struct profile_table), flags);
}

static void table_publish_and_unlock(struct profile_table *next, struct accel_profile *unused) {
    struct profile_table *old = rcu_dereference_protected(g_table, lockdep_is_held(&g_table_lock));

    next->generation = old->generation + 1;
    rcu_assign_pointer(g_table, next);
    mutex_unlock(&g_table_lock);
    synchronize_rcu();
    kfree(old);
    kvfree(unused);
}

static __u64 claim_id(struct file *file) {
    return (unsigned long) ((struct seq_file *) file->private_data)->private;
}

int profiles_set_default(const struct yeetmouse_profile_args *args, const struct accel_device *device,
                         const char *problem) {
    struct accel_profile *profile = problem ? NULL : kvzalloc(sizeof(*profile), GFP_KERNEL);
    struct profile_table *next = NULL;
    int error = -EINVAL;

    if (!problem && !profile) {
        problem = "the driver ran out of memory";
        error = -ENOMEM;
    }
    if (!problem)
        problem = yeetmouse_scaling_problem(device->pre_scale, device->min_time, device->max_time, device->fixed_time);
    if (!problem)
        problem = profile_from_args(profile, args);

    mutex_lock(&g_table_lock);
    if (!problem) {
        next = table_copy(GFP_KERNEL);
        if (!next) {
            problem = "the driver ran out of memory";
            error = -ENOMEM;
        }
    }
    g_default_problem = problem;
    if (problem) {
        mutex_unlock(&g_table_lock);
        pr_err("refused the parameters: %s\n", problem);
        kvfree(profile);
        return error;
    }
    swap(next->default_profile, profile);
    next->default_device = *device;
    table_publish_and_unlock(next, profile);
    return 0;
}

static long profiles_load(const void __user *from) {
    struct yeetmouse_profile_args *args;
    struct accel_profile *profile, *replaced;
    struct profile_table *next;
    const char *problem;
    long error;

    args = memdup_user(from, sizeof(*args));
    if (IS_ERR(args))
        return PTR_ERR(args);
    profile = kvzalloc(sizeof(*profile), GFP_KERNEL);
    if (!profile) {
        kfree(args);
        return -ENOMEM;
    }
    problem = profile_from_args(profile, args);
    if (problem) {
        pr_err("refused profile %s: %s\n", yeetmouse_name_valid(args->name) ? args->name : "with an invalid name",
               problem);
        error = -EINVAL;
        goto free_profile;
    }

    mutex_lock(&g_table_lock);
    next = table_copy(GFP_KERNEL);
    error = next ? table_load(next, args->name, profile, &replaced) : -ENOMEM;
    if (error) {
        mutex_unlock(&g_table_lock);
        kfree(next);
        goto free_profile;
    }
    table_publish_and_unlock(next, replaced);
    kfree(args);
    return 0;

free_profile:
    kvfree(profile);
    kfree(args);
    return error;
}
static long profiles_drop(const void __user *from) {
    struct yeetmouse_name_args args;
    struct accel_profile *dropped;
    struct profile_table *next;
    long error;

    if (copy_from_user(&args, from, sizeof(args)))
        return -EFAULT;

    mutex_lock(&g_table_lock);
    next = table_copy(GFP_KERNEL);
    error = next ? table_drop(next, args.name, &dropped) : -ENOMEM;
    if (error) {
        mutex_unlock(&g_table_lock);
        kfree(next);
        return error;
    }
    table_publish_and_unlock(next, dropped);
    return 0;
}

static long profiles_set_devices(const void __user *from) {
    struct yeetmouse_devices_args *args;
    struct profile_table *next;
    long error;

    args = memdup_user(from, sizeof(*args));
    if (IS_ERR(args))
        return PTR_ERR(args);

    mutex_lock(&g_table_lock);
    next = table_copy(GFP_KERNEL);
    error = next ? table_set_devices(next, args) : -ENOMEM;
    kfree(args);
    if (error) {
        mutex_unlock(&g_table_lock);
        kfree(next);
        return error;
    }
    table_publish_and_unlock(next, NULL);
    return 0;
}

static long profiles_claim(struct file *file, const void __user *from) {
    struct yeetmouse_name_args args;
    struct profile_table *next;
    long error;

    if (copy_from_user(&args, from, sizeof(args)))
        return -EFAULT;

    mutex_lock(&g_table_lock);
    next = table_copy(GFP_KERNEL);
    error = next ? table_claim(next, claim_id(file), args.name) : -ENOMEM;
    if (error) {
        mutex_unlock(&g_table_lock);
        kfree(next);
        return error;
    }
    table_publish_and_unlock(next, NULL);
    return 0;
}

static long profiles_ioctl(struct file *file, unsigned int command, unsigned long argument) {
    const void __user *from = (const void __user *) argument;

    switch (command) {
        case YEETMOUSE_IOCTL_LOAD_PROFILE:
            return profiles_load(from);
        case YEETMOUSE_IOCTL_DROP_PROFILE:
            return profiles_drop(from);
        case YEETMOUSE_IOCTL_SET_DEVICES:
            return profiles_set_devices(from);
        case YEETMOUSE_IOCTL_CLAIM:
            return profiles_claim(file, from);
        default:
            return -ENOTTY;
    }
}

static int profiles_show(struct seq_file *m, void *unused) {
    const struct profile_table *table;
    int i;

    mutex_lock(&g_table_lock);
    table = table_locked();
    seq_printf(m, "version %d\ngeneration %llu\n", STATUS_VERSION, table->generation);
    seq_printf(m, "default digest=%016llx pre_scale=%lld min_time=%lld max_time=%lld fixed_time=%d\n",
               table->default_profile->digest, table->default_device.pre_scale, table->default_device.min_time,
               table->default_device.max_time, table->default_device.fixed_time);
    if (g_default_problem)
        seq_printf(m, "default refused reason=%s\n", g_default_problem);
    for (i = 0; i < YEETMOUSE_MAX_PROFILES; i++)
        if (table->profiles[i].profile)
            seq_printf(m, "profile %s digest=%016llx\n", table->profiles[i].name, table->profiles[i].profile->digest);
    for (i = 0; i < table->device_count; i++) {
        const struct device_line *line = &table->devices[i];

        if (line->disabled)
            seq_printf(m, "device %04x:%04x disabled\n", line->vendor, line->product);
        else
            seq_printf(m, "device %04x:%04x profile=%s pre_scale=%lld min_time=%lld max_time=%lld fixed_time=%d\n",
                       line->vendor, line->product, table->profiles[line->profile].name, line->device.pre_scale,
                       line->device.min_time, line->device.max_time, line->device.fixed_time);
    }
    for (i = 0; i < table->claim_count; i++)
        seq_printf(m, "claim profile=%s\n", table->profiles[table->claims[i].profile].name);
    mice_status(m, table);
    mutex_unlock(&g_table_lock);
    return 0;
}

static int profiles_open(struct inode *inode, struct file *file) {
    int error;

    file->private_data = NULL;
    error = single_open(file, profiles_show, (void *) atomic_long_inc_return(&g_next_claim));
    return error ? error : nonseekable_open(inode, file);
}

static int profiles_release(struct inode *inode, struct file *file) {
    struct profile_table *next;
    const struct profile_table *table;
    __u64 id = claim_id(file);
    int i;
    bool held = false;

    mutex_lock(&g_table_lock);
    table = table_locked();
    for (i = 0; i < table->claim_count; i++)
        held |= table->claims[i].id == id;
    if (held) {
        next = table_copy(GFP_KERNEL | __GFP_NOFAIL);
        table_release(next, id);
        table_publish_and_unlock(next, NULL);
    } else {
        mutex_unlock(&g_table_lock);
    }
    return single_release(inode, file);
}

static const struct file_operations profiles_fops = {
    .owner = THIS_MODULE,
    .open = profiles_open,
    .read = seq_read,
    .release = profiles_release,
    .unlocked_ioctl = profiles_ioctl,
    .compat_ioctl = compat_ptr_ioctl,
};

static struct miscdevice profiles_device = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "yeetmouse",
    .fops = &profiles_fops,
    .mode = 0660,
};

int profiles_init(void) {
    struct profile_table *empty = kzalloc(sizeof(*empty), GFP_KERNEL);

    if (!empty)
        return -ENOMEM;
    RCU_INIT_POINTER(g_table, empty);
    return 0;
}

int profiles_register(void) {
    return misc_register(&profiles_device);
}

void profiles_unregister(void) {
    misc_deregister(&profiles_device);
}

void profiles_exit(void) {
    struct profile_table *table;
    int i;

    table = rcu_dereference_protected(g_table, true);
    RCU_INIT_POINTER(g_table, NULL);
    synchronize_rcu();
    for (i = 0; i < YEETMOUSE_MAX_PROFILES; i++)
        kvfree(table->profiles[i].profile);
    kvfree(table->default_profile);
    kfree(table);
}
