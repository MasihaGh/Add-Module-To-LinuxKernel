#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/jiffies.h>

#define DEVICE_NAME "myQueue"
#define QUEUE_SIZE 10

static char queue[QUEUE_SIZE];
static int front = 0, rear = 0, count = 0;
static DEFINE_MUTEX(queue_lock);
static DECLARE_WAIT_QUEUE_HEAD(queue_wait);

static int blocking_mode = 0;
module_param(blocking_mode, int, S_IRUGO);
MODULE_PARM_DESC(blocking_mode, "Enable (1) or disable (0) blocking mode");

static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char __user *, size_t, loff_t *);

static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};

static int major_number;

static int __init myQueue_init(void)
{
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0)
    {
        printk(KERN_ERR "myQueue: Failed to register device\n");
        return major_number;
    }
    printk(KERN_INFO "myQueue: Registered with major number %d\n", major_number);
    return 0;
}

static void __exit myQueue_exit(void)
{
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "myQueue: Unregistered and exiting\n");
}

static int dev_open(struct inode *inodep, struct file *filep)
{
    printk(KERN_INFO "myQueue: Device opened\n");
    return 0;
}

static int dev_release(struct inode *inodep, struct file *filep)
{
    printk(KERN_INFO "myQueue: Device closed\n");
    return 0;
}

static ssize_t dev_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset)
{
    char ch;
    int ret;

    if (len < 1)
    {
        return -EINVAL;
    }

    if (blocking_mode)
    {
        int timeout = wait_event_interruptible_timeout(queue_wait, count > 0, 10 * HZ);
        if (timeout == 0)
        {
            printk(KERN_INFO "myQueue: Blocking read timed out (queue is empty)\n");
            return -EAGAIN;
        }
    }

    mutex_lock(&queue_lock);

    if (count == 0)
    {
        mutex_unlock(&queue_lock);
        printk(KERN_INFO "myQueue: Queue is empty (non-blocking mode)\n");
        return 0;
    }

    ch = queue[front];
    front = (front + 1) % QUEUE_SIZE;
    count--;

    mutex_unlock(&queue_lock);

    wake_up_interruptible(&queue_wait);

    ret = copy_to_user(buffer, &ch, 1);
    if (ret != 0)
    {
        printk(KERN_ERR "myQueue: Failed to copy data to user space\n");
        return -EFAULT;
    }

    printk(KERN_INFO "myQueue: Read character '%c', queue size: %d\n", ch, count);
    return 1;
}

static ssize_t dev_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset)
{
    char ch;
    int ret;
    size_t i = 0;

    if (len == 0)
    {
        return 0;
    }

    while (i < len)
    {
        ret = copy_from_user(&ch, &buffer[i], sizeof(char));
        if (ret != 0)
        {
            printk(KERN_ERR "myQueue: Failed to copy data from user space\n");
            return -EFAULT;
        }

        i++;

        if (ch == '\n' || ch == '\0')
        {
            break;
        }

        if (blocking_mode)
        {
            int timeout = wait_event_interruptible_timeout(queue_wait, count < QUEUE_SIZE, 10 * HZ);
            if (timeout == 0)
            {
                printk(KERN_INFO "myQueue: Blocking write timed out (queue is full)\n");
                return -EAGAIN;
            }
        }

        mutex_lock(&queue_lock);

        if (count == QUEUE_SIZE)
        {
            mutex_unlock(&queue_lock);
            printk(KERN_INFO "myQueue: Queue is full (non-blocking mode)\n");
            return -ENOMEM;
        }

        queue[rear] = ch;
        rear = (rear + 1) % QUEUE_SIZE;
        count++;

        mutex_unlock(&queue_lock);

        wake_up_interruptible(&queue_wait);

        printk(KERN_INFO "myQueue: Wrote character '%c', queue size: %d\n", ch, count);
    }

    return i;
}

module_init(myQueue_init);
module_exit(myQueue_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Masiha");
MODULE_DESCRIPTION("A char device with blocking and non-blocking modes for read and write");
MODULE_VERSION("2.0");
