#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/kprobes.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/namei.h>
#include <linux/cdev.h>
#include <linux/device.h> 

#define DEVICE_NAME "project_monitor"
#define BUF_LEN 256

MODULE_LICENSE("GPL");
MODULE_AUTHOR("dewnoi");

static int major_number;
static char msg_buffer[BUF_LEN];
static int msg_len = 0;
static int data_ready = 0;
static unsigned long target_inode = 0; 
static DECLARE_WAIT_QUEUE_HEAD(wait_q);
static struct class* monitor_class  = NULL;
static struct device* monitor_device = NULL;

static int handler_pre_mkdir(struct kprobe *p, struct pt_regs *regs)
{
    struct inode *parent_inode = (struct inode *)regs->si;
    struct dentry *dentry = (struct dentry *)regs->dx;
    const char *name = dentry->d_name.name;

    if (target_inode != 0 && parent_inode->i_ino != target_inode) {
        return 0;
    }

    snprintf(msg_buffer, BUF_LEN, "[DIR] Created directory '%s'\n", name);
    msg_len = strlen(msg_buffer);
    data_ready = 1;
    wake_up_interruptible(&wait_q);
    return 0;
}

static int handler_pre_unlink(struct kprobe *p, struct pt_regs *regs)
{
    struct inode *parent_inode = (struct inode *)regs->si; 
    struct dentry *dentry = (struct dentry *)regs->dx;     
    const char *name = dentry->d_name.name;

    if (target_inode != 0 && parent_inode->i_ino != target_inode) {
        return 0;
    }

    snprintf(msg_buffer, BUF_LEN, "[FILE] Removed file '%s'\n", name);
    msg_len = strlen(msg_buffer);
    data_ready = 1;
    wake_up_interruptible(&wait_q);
    return 0;
}

static int handler_pre_create(struct kprobe *p, struct pt_regs *regs)
{
    struct inode *parent_inode = (struct inode *)regs->di; 
    struct dentry *dentry = (struct dentry *)regs->si;     
    const char *name = dentry->d_name.name;

    if (target_inode != 0 && parent_inode->i_ino != target_inode) {
        return 0;
    }

    snprintf(msg_buffer, BUF_LEN, "[FILE] Created file '%s'\n", name);
    msg_len = strlen(msg_buffer);
    data_ready = 1;
    wake_up_interruptible(&wait_q);
    return 0;
}

static int handler_pre_rmdir(struct kprobe *p, struct pt_regs *regs)
{
    struct inode *parent_inode = (struct inode *)regs->si; 
    struct dentry *dentry = (struct dentry *)regs->dx;     
    const char *name = dentry->d_name.name;

    // --- กรอง: เช็ค Inode แม่ ---
    if (target_inode != 0 && parent_inode->i_ino != target_inode) {
        return 0;
    }

    snprintf(msg_buffer, BUF_LEN, "[DIR] Removed directory '%s'\n", name);
    msg_len = strlen(msg_buffer);
    data_ready = 1;
    wake_up_interruptible(&wait_q);
    return 0;
}

static int handler_pre_read(struct kprobe *p, struct pt_regs *regs)
{
    struct file *file = (struct file *)regs->di;
    
    if (!file || !file->f_path.dentry) return 0;

    struct dentry *dentry = file->f_path.dentry;
    struct inode *parent_inode = dentry->d_parent->d_inode;
    const char *name = dentry->d_name.name;

    if (target_inode != 0 && parent_inode->i_ino != target_inode) {
        return 0;
    }

    snprintf(msg_buffer, BUF_LEN, "[FILE] Read file '%s' \n", name);
    msg_len = strlen(msg_buffer);
    data_ready = 1;
    wake_up_interruptible(&wait_q);
    return 0;
}

static int handler_pre_write(struct kprobe *p, struct pt_regs *regs)
{
    struct file *file = (struct file *)regs->di;

    if (!file || !file->f_path.dentry) return 0;

    struct dentry *dentry = file->f_path.dentry;
    struct inode *parent_inode = dentry->d_parent->d_inode;
    const char *name = dentry->d_name.name;

    if (target_inode != 0 && parent_inode->i_ino != target_inode) {
        return 0;
    }

    snprintf(msg_buffer, BUF_LEN, "[FILE] Write file '%s' \n", name);
    msg_len = strlen(msg_buffer);
    data_ready = 1;
    wake_up_interruptible(&wait_q);
    return 0;
}

static int handler_pre_rename(struct kprobe *p, struct pt_regs *regs)
{
    struct renamedata *rd = (struct renamedata *)regs->di;
    struct inode *old_parent = rd->old_dir;      
    struct inode *new_parent = rd->new_dir;      
    struct dentry *old_file = rd->old_dentry; 
    struct dentry *new_file = rd->new_dentry;
    const char *old_name = old_file->d_name.name;
    const char *new_name = new_file->d_name.name;

    if (!old_file || !new_file) return 0;

    if(old_parent->i_ino != new_parent->i_ino){
        return 0;
    }

    if (target_inode != 0 && old_parent->i_ino != target_inode) {
        return 0;
    }

    snprintf(msg_buffer, BUF_LEN, "[FILE/DIR] Rename file/direcotry from '%s' to  '%s'\n", old_name, new_name);
    msg_len = strlen(msg_buffer);
    data_ready = 1;
    wake_up_interruptible(&wait_q);
    return 0;
}

//create dir
static struct kprobe kp_mkdir = { .symbol_name = "vfs_mkdir", .pre_handler = handler_pre_mkdir };
//delete file
static struct kprobe kp_unlink = { .symbol_name = "vfs_unlink", .pre_handler = handler_pre_unlink };
//create file
static struct kprobe kp_create = { .symbol_name = "security_inode_create", .pre_handler = handler_pre_create };
//delete dir
static struct kprobe kp_rmdir = { .symbol_name = "vfs_rmdir", .pre_handler = handler_pre_rmdir };
//rename
static struct kprobe kp_rename = { .symbol_name = "vfs_rename", .pre_handler = handler_pre_rename };
//read
static struct kprobe kp_read = { .symbol_name = "vfs_read", .pre_handler = handler_pre_read };
//write
static struct kprobe kp_write = { .symbol_name = "vfs_write", .pre_handler = handler_pre_write };

static ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset)
{
    wait_event_interruptible(wait_q, data_ready != 0);
    if (copy_to_user(buffer, msg_buffer, msg_len) == 0) {
        data_ready = 0;
        return msg_len;
    }
    return -EFAULT;
}

static ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset)
{
    char kbuf[32];
    if (len > 31) len = 31;
    if (copy_from_user(kbuf, buffer, len)) return -EFAULT;
    kbuf[len] = '\0';
    
    if (kstrtoul(kbuf, 10, &target_inode) == 0) {
        pr_info("MONITOR: Now watching Inode %lu\n", target_inode);
    }
    return len;
}

static struct file_operations fops = {
    .read = dev_read,
    .write = dev_write, 
};

static int __init monitor_init(void)
{
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    monitor_class = class_create("monitor_class");
    monitor_device = device_create(monitor_class, NULL, MKDEV(major_number, 0), NULL, DEVICE_NAME);
    register_kprobe(&kp_mkdir);
    register_kprobe(&kp_unlink);
    register_kprobe(&kp_create);
    register_kprobe(&kp_rmdir);
    register_kprobe(&kp_read);
    register_kprobe(&kp_write);
    register_kprobe(&kp_rename);
    return 0;
}

static void __exit monitor_exit(void)
{
    unregister_kprobe(&kp_mkdir);
    unregister_kprobe(&kp_unlink);
    unregister_kprobe(&kp_create);
    unregister_kprobe(&kp_rmdir);
    unregister_kprobe(&kp_read);
    unregister_kprobe(&kp_write);
    unregister_kprobe(&kp_rename);
    device_destroy(monitor_class, MKDEV(major_number, 0));
    class_unregister(monitor_class);
    class_destroy(monitor_class);
    unregister_chrdev(major_number, DEVICE_NAME);
}

module_init(monitor_init);
module_exit(monitor_exit);
