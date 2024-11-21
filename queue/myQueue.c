
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>

#define DEVICE_NAME "myQueue" // Define the name of the device
#define QUEUE_SIZE 11       // Fixed size for the queue

// Circular queue implementation
static char queue[QUEUE_SIZE];    // Circular buffer to hold characters
static int front = 0;             // Index of the first character in the queue
static int rear = 0;              // Index of the next empty slot
static int count = 0;             // Number of elements in the queue
static DEFINE_MUTEX(queue_lock);  // Lock to ensure thread safety

// Function prototypes for file operations
static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char __user *, size_t, loff_t *);

// File operations structure
static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .release = dev_release,
};

static int major_number; // Variable to store the major number for the device

// Module initialization function
static int __init myQueue_init(void) {
    // Register the character device and get the major number
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ERR "myQueue module load failed\n");
        return major_number;
    }
    printk(KERN_INFO "myQueue module loaded with major number = %d\n", major_number);
    return 0;
}

// Module cleanup function
static void __exit myQueue_exit(void) {
    unregister_chrdev(major_number, DEVICE_NAME); // Unregister the character device
    printk(KERN_INFO "myQueue module has been unloaded\n");
}

// Function called when the device is opened
static int dev_open(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "DEVICE_NAME = myQueue has been opened\n");
    return 0;
}

// Function called when the device is released (closed)
static int dev_release(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO "DEVICE_NAME = myQueue has been closed\n");
    return 0;
}


static ssize_t dev_read(struct file *filep, char __user *buffer, size_t len, loff_t *offset)
{
    static bool read_done = false; // Track if a single character was read

    // Prevent multiple reads per open
    if (read_done) {
        read_done = false; // Reset for the next open
        return 0; // Indicate EOF
    }

    char output[2];
    int ret;

    if (len < 2) {
        printk(KERN_ERR "myQueue: Buffer too small\n");
        return -EINVAL;
    }

    mutex_lock(&queue_lock);
    if (count == 0) {
        printk(KERN_INFO "myQueue: Queue is empty\n");
        mutex_unlock(&queue_lock);
        return 0;
    }

    output[0] = queue[front];
    front = (front + 1) % QUEUE_SIZE;
    count--;
    mutex_unlock(&queue_lock);

    output[1] = '\n';
    ret = copy_to_user(buffer, output, 2);
    if (ret != 0) {
        printk(KERN_ERR "myQueue: Copy to user failed\n");
        return -EFAULT;
    }

    printk(KERN_INFO "myQueue: Read '%c'\n", output[0]);
    read_done = true;
    return 2;
}




static ssize_t dev_write(struct file *filep, const char __user *buffer, size_t len, loff_t *offset)
{
    char ch;
    int ret;
    size_t i = 0;

    if (len == 0) {
        return 0; // Nothing to write
    }

    while (i < len) {
        // Copy one character at a time from user space to kernel space
        ret = copy_from_user(&ch, &buffer[i], sizeof(char));
        if (ret != 0) {
            printk(KERN_ERR "myQueue: Failed to copy data from user space\n");
            return -EFAULT;
        }

        i++;

        // Stop processing if a newline is encountered
        if (ch == '\n') {
            break;
        }

        // Acquire the lock to ensure thread safety
        mutex_lock(&queue_lock);

        // Check if the queue is full
        if (count == QUEUE_SIZE) {
            printk(KERN_INFO "myQueue: Queue is full\n");
            mutex_unlock(&queue_lock);
            return -ENOMEM; // Return error: No space left in the queue
        }

        // Enqueue the character
        queue[rear] = ch;
        rear = (rear + 1) % QUEUE_SIZE;
        count++;

        mutex_unlock(&queue_lock);

        printk(KERN_INFO "myQueue: Wrote character '%c', queue size: %d\n", ch, count);
    }

    return i; // Return the number of characters successfully written
}



// Macros to register the module initialization and exit functions
module_init(myQueue_init);
module_exit(myQueue_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Masiha");
MODULE_DESCRIPTION("A simple Linux char device for a queue implementation");
MODULE_VERSION("1.0");
