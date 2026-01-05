#include "kernel/types.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
  int t = atoi(argv[1]);
  settickets(t);
  while(1) {}
  return 0;
}

