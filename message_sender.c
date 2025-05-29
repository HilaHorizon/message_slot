#include "message_slot.h"

#include <fcntl.h> /* open */
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h> /* ioctl */
#include <unistd.h>    /* exit */
#include <string.h>
#define ARG_AMOUNT 5
int main(int argc, char *argv[]) {

  char *slot_file_path;
  int target_message_channel_id;
  int censorship_mode;
  char *message_data;
  int message_length;
  int file_desc;
  int ret_val;

  if(argc!=ARG_AMOUNT){
    perror("amount of arguments is wrong for message sender\n");
    exit(1);
  }
  slot_file_path=argv[1];
  target_message_channel_id = strtoul(argv[2], NULL, 10);
  censorship_mode = strtoul(argv[3], NULL, 10);
  message_data=argv[4];
  message_length=strlen(message_data);

  if (!message_data || message_length==0){
    perror("no message sent from user in message sender\n");
    exit(1);
  }

  file_desc = open(slot_file_path, O_RDWR);
  if (file_desc < 0) {
    perror("Can't open device file in message sender \n");
    exit(1);
  }
  ret_val = ioctl(file_desc, MSG_SLOT_SET_CEN, censorship_mode);
  if (ret_val<0){
    perror("ioctl MSG_SLOT_SET_CEN error in message sender\n");
    close(file_desc);
    exit(1);
  } 

  ret_val = ioctl(file_desc, MSG_SLOT_CHANNEL, target_message_channel_id);
  if (ret_val<0){
    perror("ioctl MSG_SLOT_CHANNEL error in message sender\n");
    close(file_desc);
    exit(1);
  }

  ret_val = write(file_desc, message_data, message_length);
  if (ret_val!=message_length){
    perror("write error in message sender\n");
    close(file_desc);
    exit(1);
  }

  close(file_desc);
  return 0;
}
