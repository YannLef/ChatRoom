#ifndef CLIENT_H
#define CLIENT_H

void catch_ctrl_c_and_exit(int sig);
void recv_msg_handler();
void send_msg_handler();
void str_trim_lf (char* arr, int length);
void str_overwrite_stdout();

#endif
