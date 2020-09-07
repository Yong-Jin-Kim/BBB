#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char * argv[])
{
	printf(1, "My pid is %d, level is %d\n", getpid(), getlev());
	printf(1, "My ppid is %d\n", getppid());
	exit();
}
