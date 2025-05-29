// Declare what kind of code we want
// from the header files. Defining __KERNEL__
// and MODULE allows us to access kernel-level
// code not usually available to userspace programs.
#undef __KERNEL__
#define __KERNEL__
#undef MODULE
#define MODULE

#include <linux/fs.h>     /* for register_chrdev */
#include <linux/kernel.h> /* We're doing kernel work */
#include <linux/module.h> /* Specifically, a module */
#include <linux/slab.h>
#include <linux/string.h>  /* for memset. NOTE - not string.h!*/
#include <linux/uaccess.h> /* for get_user and put_user */

MODULE_LICENSE("GPL");

// Our custom definitions of IOCTL operations
#include "message_slot.h"


// struct chardev_info {
//   spinlock_t lock;
// };


/**
 * Channel structure - represents a single message channel within a message slot
 * Each channel stores exactly one message (the last one written)
 */
struct channel {
    unsigned int channel_id;    // The channel identifier (non-zero)
    char *message_data;         // Pointer to the stored message content
    int message_length;         // Length of the stored message (1-128 bytes)
    struct channel *next;       // Next channel in the linked list
};

/**
 * Message slot structure - represents one message slot device file
 * Identified by its minor number, contains multiple channels
 */
struct message_slot {
    int minor_number;           // Device minor number (0-255)
    struct channel *channels;   // Head of the channels linked list
};

/**
 * File descriptor context - stores per-fd state
 * Allocated in device_open() and stored in file->private_data
 */
struct msg_slot_fd {
    unsigned int channel_id;        // Currently selected channel ID (0 = none set)
    int censorship_enabled;         // Censorship mode: 0 = disabled, 1 = enabled
    struct message_slot *slot;      // Pointer to the associated message slot
};

static struct message_slot* message_slots[MAX_SLOTS];


// used to prevent concurent access into the same device
// static int dev_open_flag = 0; // for spinlock

// static struct chardev_info device_info;



//================== DEVICE FUNCTIONS ===========================
static int device_open(struct inode *inode, struct file *file) {
  // unsigned long flags; // for spinlock
  // printk("Invoking device_open(%p)\n", file);

  // // We don't want to talk to two processes at the same time
  // spin_lock_irqsave(&device_info.lock, flags);
  // if (1 == dev_open_flag) {
  //   spin_unlock_irqrestore(&device_info.lock, flags);
  //   return -EBUSY;
  // }

  // ++dev_open_flag;
  // spin_unlock_irqrestore(&device_info.lock, flags);


  struct msg_slot_fd *fd_data;
  int minor;

  minor = iminor(inode);
  // Validate minor number (should be 0-255)
  if (minor < 0 || minor >= MAX_SLOTS) {
      printk(KERN_ERR "message_slot: invalid minor number %d\n", minor);
      return -EINVAL;
  }

  // Check if message slot for this minor number already exists
  if (message_slots[minor] == NULL) {
      // First time this slot is opened - create it
      message_slots[minor] = kmalloc(sizeof(struct message_slot), GFP_KERNEL);
      if (!message_slots[minor]) {
          printk(KERN_ERR "message_slot: failed to allocate memory for slot %d\n", minor);
          return -ENOMEM;
      }
        
      memset(message_slots[minor], 0, sizeof(struct message_slot));
      // Initialize the message slot
      message_slots[minor]->minor_number = minor;
      message_slots[minor]->channels = NULL;
        
      printk(KERN_INFO "message_slot: created new message slot with minor %d\n", minor);
    }

  fd_data = kmalloc(sizeof(struct msg_slot_fd), GFP_KERNEL);
  if (!fd_data){
    printk(KERN_ERR "message_slot: failed to allocate fd context for slot %d\n", minor);
    return -ENOMEM;

  }

  // Initialize file descriptor context
  memset(fd_data, 0, sizeof(struct msg_slot_fd));
  /* locate the slot based on minor number */
  fd_data->slot = message_slots[minor];
  fd_data->censorship_enabled = 0;
  file->private_data = fd_data;

  printk(KERN_DEBUG "message_slot: opened device file with minor %d\n", minor);

  return SUCCESS;
}

//---------------------------------------------------------------

static int device_release(struct inode *inode, struct file *file) {
  // unsigned long flags; // for spinlock
  // printk("Invoking device_release(%p,%p)\n", inode, file);

  // // ready for our next caller
  // spin_lock_irqsave(&device_info.lock, flags);
  // --dev_open_flag;
  // spin_unlock_irqrestore(&device_info.lock, flags);

  kfree(file->private_data);
  return SUCCESS;
}

//---------------------------------------------------------------


//Find a channel by ID in a message slot
static struct channel* find_channel(struct message_slot *slot, unsigned int channel_id)
{
    struct channel *curr = slot->channels;
    
    while (curr != NULL) {
        if (curr->channel_id == channel_id) {
            return curr;
        }
        curr = curr->next;
    }
    
    return NULL;
}
//---------------------------------------------------------------

// Create a new channel in a message slot
static struct channel* create_channel(struct message_slot *slot, unsigned int channel_id)
{
    struct channel *new_channel;
    
    // Allocate new channel
    new_channel = kmalloc(sizeof(struct channel), GFP_KERNEL);
    if (!new_channel) {
        printk(KERN_ERR "message_slot: failed to allocate channel %u\n", channel_id);
        return NULL;
    }
    
    // Initialize channel (zero out all fields first)
    memset(new_channel, 0, sizeof(struct channel));
    new_channel->channel_id = channel_id;
    // message_data is NULL, message_length is 0 due to memset
    
    // Add to front of linked list
    new_channel->next = slot->channels;
    slot->channels = new_channel;
    
    printk(KERN_DEBUG "message_slot: created channel %u\n", channel_id);
    return new_channel;
}
//---------------------------------------------------------------

static int apply_censorship(char *dest, const char __user *src, int length)
{
    int i;
    char temp_char;
    
    for (i = 0; i < length; i++) {
        // Copy character from user space
        if (copy_from_user(&temp_char, src + i, 1)) {
            return -EFAULT;
        }
        
        // Apply censorship: replace every 3rd character (positions 2, 5, 8, etc.)
        if ((i + 1) % 3 == 0) {
            dest[i] = '#';
        } else {
            dest[i] = temp_char;
        }
    }
    
    return 0;
}
//---------------------------------------------------------------

// a processs which has already opened
// the device file attempts to write to it
static ssize_t device_write(struct file *file, const char __user *buffer,
                            size_t length, loff_t *offset) {

    struct msg_slot_fd *fd_data;
    struct channel *chan;
    char *new_message;
    int ret;
    
    // Get file descriptor context
    fd_data = (struct msg_slot_fd*)file->private_data;
    if (!fd_data) {
        printk(KERN_ERR "message_slot: no fd context in write\n");
        return -EINVAL;
    }
    
    // Check if channel has been set
    if (fd_data->channel_id == 0) {
        printk(KERN_DEBUG "message_slot: no channel set for write\n");
        return -EINVAL;
    }

     // Validate message length (must be 1-128 bytes)
    if (length == 0 || length > MAX_MSG_LEN) {
        printk(KERN_DEBUG "message_slot: invalid message length %zu\n", length);
        return -EMSGSIZE;
    }

    // Validate user buffer
    if (!buffer) {
        printk(KERN_DEBUG "message_slot: null buffer in write\n");
        return -EINVAL;
    }

    // Find or create the channel
    chan = find_channel(fd_data->slot, fd_data->channel_id);
    if (!chan) {
        chan = create_channel(fd_data->slot, fd_data->channel_id);
        if (!chan) {
            return -ENOMEM;
        }
    }

    // Allocate memory for the new message
    new_message = kmalloc(length, GFP_KERNEL);
    if (!new_message) {
        printk(KERN_ERR "message_slot: failed to allocate message buffer\n");
        return -ENOMEM;
    }
    
    // Copy message from user space with optional censorship
    if (fd_data->censorship_enabled) {
        ret = apply_censorship(new_message, buffer, length);
        if (ret < 0) {
            kfree(new_message);
            printk(KERN_ERR "message_slot: failed to copy message from user\n");
            return ret;
        }
    } else {
        if (copy_from_user(new_message, buffer, length)) {
            kfree(new_message);
            printk(KERN_ERR "message_slot: failed to copy message from user\n");
            return -EFAULT;
        }
    }

    // Replace existing message (free old one if it exists)
    if (chan->message_data) {
        kfree(chan->message_data);
    }
    
    // Store new message
    chan->message_data = new_message;
    chan->message_length = length;
    
    printk(KERN_DEBUG "message_slot: wrote %zu bytes to channel %u\n", 
           length, fd_data->channel_id);
    
    return length;
}
//---------------------------------------------------------------

// a process which has already opened
// the device file attempts to read from it
static ssize_t device_read(struct file *file, char __user *buffer,
                           size_t length, loff_t *offset) {
    struct msg_slot_fd *fd_data;
    struct channel *chan;
    char *message_data;
    int message_length;

    // Get file descriptor context
    fd_data = (struct msg_slot_fd*)file->private_data;
    if (!fd_data) {
        printk(KERN_ERR "message_slot: no fd context in write\n");
        return -EINVAL;
    }
    
    // Check if channel has been set
    if (fd_data->channel_id == 0) {
        printk(KERN_DEBUG "message_slot: no channel set for write\n");
        return -EINVAL;
    }

    // Find the channel
    chan = find_channel(fd_data->slot, fd_data->channel_id);
    if (!chan) {
        printk(KERN_DEBUG "message_slot: channel %u not found\n", fd_data->channel_id);
        return -EINVAL;
    }

    message_length=chan->message_length;
    message_data=chan->message_data;

    if(message_length==0 || !message_data){
      printk(KERN_DEBUG "message_slot: no message in channel %u\n", fd_data->channel_id);
      return -EWOULDBLOCK;
    }

    if(message_length>length){
      printk(KERN_DEBUG "message_slot: buffer too small (need %d, got %zu)\n", 
               message_length, length);
      return -ENOSPC;
    }

    // Copy message to user space
    if (copy_to_user(buffer, message_data, message_length)) {
        printk(KERN_ERR "message_slot: failed to copy message to user\n");
        return -EFAULT;
    }
    
    printk(KERN_DEBUG "message_slot: read %d bytes from channel %u\n", 
           message_length, fd_data->channel_id);
    
    return message_length;

}
//----------------------------------------------------------------
static long device_ioctl(struct file *file, unsigned int ioctl_command_id,
                         unsigned long ioctl_param) {

  struct msg_slot_fd *fd_data = file->private_data;
  unsigned int param;
  fd_data = (struct msg_slot_fd*)file->private_data;

  if (!fd_data) {
        printk(KERN_ERR "message_slot: no fd context in ioctl\n");
        return -EINVAL;
  }

  param = (unsigned int)ioctl_param;
    
  switch (ioctl_command_id) {
      case MSG_SLOT_CHANNEL:
          // Set channel ID - must be non-zero
          if (param == 0) {
              printk(KERN_DEBUG "message_slot: invalid channel id 0\n");
              return -EINVAL;
          }
            
          fd_data->channel_id = param;
          printk(KERN_DEBUG "message_slot: set channel id to %u\n", param);
          break;
            
      case MSG_SLOT_SET_CEN:
          // Set censorship mode - 0 for disabled, 1 for enabled
          if (param != 0 && param != 1) {
              printk(KERN_DEBUG "message_slot: invalid censorship mode %u\n", param);
              return -EINVAL;
          }
            
          fd_data->censorship_enabled = (int)param;
          printk(KERN_DEBUG "message_slot: set censorship to %s\n", 
                   param ? "enabled" : "disabled");
          break;
            
      default:
          printk(KERN_DEBUG "message_slot: unknown ioctl command %u\n", ioctl_command_id);
          return -EINVAL;
    }

  return SUCCESS;
}


//==================== DEVICE SETUP =============================

// This structure will hold the functions to be called
// when a process does something to the device we created
struct file_operations Fops = {
    .owner = THIS_MODULE,
    .read = device_read,
    .write = device_write,
    .open = device_open,
    .unlocked_ioctl = device_ioctl,
    .release = device_release,
};

//---------------------------------------------------------------
// Initialize the module - Register the character device
static int __init simple_init(void) {
  int rc = -1;
  // init dev struct
  // memset(&device_info, 0, sizeof(struct chardev_info));
  // spin_lock_init(&device_info.lock); // for spinlock

  // Register driver capabilities. Obtain major num
  rc = register_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME, &Fops);

  // Negative values signify an error
  if (rc < 0) {
    printk(KERN_ALERT "%s registraion failed for  %d\n", DEVICE_FILE_NAME,
           MAJOR_NUM);
    return rc;
  }

  printk("Registeration is successful. ");
  printk("If you want to talk to the device driver,\n");
  printk("you have to create a device file:\n");
  printk("mknod /dev/%s c %d 0\n", DEVICE_FILE_NAME, MAJOR_NUM);
  printk("You can echo/cat to/from the device file.\n");
  printk("Dont forget to rm the device file and "
         "rmmod when you're done\n");

  return 0;
}

//---------------------------------------------------------------
static void free_channels(struct channel *head)
{
    struct channel *curr = head;
    struct channel *next;

    while (curr) {
        next = curr->next;
        kfree(curr->message_data);   // free message data
        kfree(curr);        // free the channel node
        curr = next;
    }
}

static void __exit simple_cleanup(void) {

  int i;
  for (i = 0; i < MAX_SLOTS; i++){
    if (message_slots[i]!=NULL){
      free_channels(message_slots[i]->channels);
      kfree(message_slots[i]);
    }
  }
  // Unregister the device
  // Should always succeed
  unregister_chrdev(MAJOR_NUM, DEVICE_RANGE_NAME);
}

//---------------------------------------------------------------
module_init(simple_init);
module_exit(simple_cleanup);

//========================= END OF FILE =========================
