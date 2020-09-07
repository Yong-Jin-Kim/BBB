#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char * argv[])
{
  int a, b, c;
  a = 1;
  b = 2;
  c = 3;
  while(a < 100) {
    print_order(a, b, c);
    a+=2;
    b+=2;
    c+=2;
  }
  exit();
}
