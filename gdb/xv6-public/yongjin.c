#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char * argv[])
{
    int i=0;
    int tits[300];
    while(i<300)
    {
      tits[i] = uptime();
      i++;
    }
    for(i=0; i<300; i++)
    {
      printf(1, "tick[%d] = %d\n", i, tits[i]);
    }

    // YONGJIN IS A NAUGHTY BOI
}
