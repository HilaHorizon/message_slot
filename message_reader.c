#include "message_slot.h"

#include <fcntl.h> /* open */
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h> /* ioctl */
#include <unistd.h>    /* exit */
#include <string.h>

#define ARG_AMOUNT 3

int main(int argc, char *argv[]) {
  char *slot_file_path;
  int target_message_channel_id;
  int file_desc;
  int ret_val;
  int message_length;
  char buffer[MAX_MSG_LEN];

  if(argc!=ARG_AMOUNT){
    perror("amount of arguments is wrong for message reader\n");
    exit(1);
  }
  slot_file_path=argv[1];
  target_message_channel_id = strtoul(argv[2], NULL, 10);

  file_desc = open(slot_file_path, O_RDWR);
  if (file_desc < 0) {
    perror("Can't open device file in message reader\n");
    exit(1);
  }

  ret_val = ioctl(file_desc, MSG_SLOT_CHANNEL, target_message_channel_id);
  if (ret_val<0){
    perror("ioctl MSG_SLOT_CHANNEL error in message reader\n");
    close(file_desc);
    exit(1);
  }

  ret_val = read(file_desc, buffer, MAX_MSG_LEN);
  if (ret_val<0){
    perror("read error in message reader\n");
    close(file_desc);
    exit(1);
  }
  
  message_length=ret_val;
  close(file_desc);

  if (write(STDOUT_FILENO, buffer, message_length) != message_length) {
      perror("write to standart output error in message reader\n");
      close(file_desc);
      exit(1);
  }

  return 0;
}
