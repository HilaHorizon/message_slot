#ifndef CHARDEV_H
#define CHARDEV_H

#include <linux/ioctl.h>

// The major device number.
// We don't rely on dynamic registration
// any more. We want ioctls to know this
// number at compile time.
// #define MAJOR_NUM 244
#define MAJOR_NUM 235

// Set the message of the device driver
#define MSG_SLOT_CHANNEL _IOW(MAJOR_NUM, 0, unsigned long)
#define MSG_SLOT_SET_CEN _IOW(MAJOR_NUM, 1, unsigned long)

#define DEVICE_RANGE_NAME "message_slot"
#define BUF_LEN 80
#define DEVICE_FILE_NAME "simple_message_slot"
#define SUCCESS 0
// Maximum message length
#define MAX_MSG_LEN 128
// Maximum number of message slots (minor numbers 0-255)
#define MAX_SLOTS 256
#endif
