#include "types.h"
#include "stat.h"
#include "user.h"

#define NUM_THREAD 10

int gcnt;
void nop(){ }

void*
racingthreadmain(void *arg)
{
  int tid = (int) arg;
  int i;
  int tmp;
  printf(1, "_____ %d\n", (uint)arg);
  for(i = 0; i < 10000; i++) {
    tmp = gcnt;
    tmp++;
    nop();
    gcnt = tmp;
  }
  thread_exit((void *)(tid+1));
}

int
main(int argc, char *argv[])
{
  thread_t threads[NUM_THREAD];
  int i;
  void *retval;
  gcnt = 0;
  
  for(i = 0; i < NUM_THREAD; i++){
    if(thread_create(&threads[i], racingthreadmain, (void*)i) != 0) {
      printf(1, "panic at thread_create %d\n", i);
      return -1;
    }
  }
  //printf(1, "MIDDLE\n");
  for(i = 0; i < NUM_THREAD; i++){
    printf(1, "join %d called\n", i);
    if(thread_join(threads[i], &retval) != 0 || (int)retval != i+1) {
      printf(1, "panic at thread_join %d with value %d\n", i, (int)retval);
      return -1;
    }
  }
  printf(1, "%d\n", gcnt);
  return 0;
}
