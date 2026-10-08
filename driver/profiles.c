#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "profiles.h"
#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

static struct profile_table __rcu *g_table;
static DEFINE_MUTEX(g_table_lock);
static atomic_long_t g_next_claim = ATOMIC_LONG_INIT(0);

const struct profile_table *profiles_current(void) {
    return rcu_dereference(g_table);
}

static struct profile_table *table_copy(gfp_t flags) {
    return kmemdup(rcu_dereference_protected(g_table, lockdep_is_held(&g_table_lock)), sizeof(struct profile_table),
                   flags);
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

static long profiles_load(const void __user *from) {
    struct yeetmouse_profile_args *args;
    struct accel_profile *profile, *replaced;
    struct profile_table *next;
    long error;

    args = memdup_user(from, sizeof(*args));
    if (IS_ERR(args))
        return PTR_ERR(args);
    profile = kvzalloc(sizeof(*profile), GFP_KERNEL);
    if (!profile) {
        kfree(args);
        return -ENOMEM;
    }
    error = profile_from_args(profile, args);
    if (error)
        goto free_profile;

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
    error = next ? table_claim(next, (unsigned long) file->private_data, args.name) : -ENOMEM;
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

static int profiles_open(struct inode *inode, struct file *file) {
    file->private_data = (void *) atomic_long_inc_return(&g_next_claim);
    return nonseekable_open(inode, file);
}

static int profiles_release(struct inode *inode, struct file *file) {
    struct profile_table *next;
    unsigned long id = (unsigned long) file->private_data;
    int i;
    bool held = false;

    mutex_lock(&g_table_lock);
    next = table_copy(GFP_KERNEL | __GFP_NOFAIL);
    for (i = 0; i < next->claim_count; i++)
        held |= next->claims[i].id == id;
    if (!held) {
        mutex_unlock(&g_table_lock);
        kfree(next);
        return 0;
    }
    table_release(next, id);
    table_publish_and_unlock(next, NULL);
    return 0;
}

static const struct file_operations profiles_fops = {
    .owner = THIS_MODULE,
    .open = profiles_open,
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
    int error;

    if (!empty)
        return -ENOMEM;
    RCU_INIT_POINTER(g_table, empty);
    error = misc_register(&profiles_device);
    if (error) {
        RCU_INIT_POINTER(g_table, NULL);
        kfree(empty);
    }
    return error;
}

void profiles_exit(void) {
    struct profile_table *table;
    int i;

    misc_deregister(&profiles_device);
    table = rcu_dereference_protected(g_table, true);
    RCU_INIT_POINTER(g_table, NULL);
    synchronize_rcu();
    for (i = 0; i < YEETMOUSE_MAX_PROFILES; i++)
        kvfree(table->profiles[i].profile);
    kfree(table);
}
