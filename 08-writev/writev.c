#define _GNU_SOURCE
#include <string.h>
#include <sys/time.h>
#include <errno.h>
#include <stdio.h>
#include <sys/uio.h>
#include <stdio.h>
#include <memory.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>

#define die(msg) do { perror(msg); exit(EXIT_FAILURE); } while(0)

int compfunc(const void * objA, const void* objB)
{
	struct iovec * a = (struct iovec *) objA;
	struct iovec * b = (struct iovec *) objB;

	return strncmp((const char *) a->iov_base, (const char *) b->iov_base, a->iov_len > b->iov_len ? b->iov_len : a->iov_len );
}

int main() {
    // Read in lines (HINT: getline(3))

    struct iovec iov[4096];
    size_t dummy = 0;
    size_t nread = 0;
    char * line;
    size_t iov_sz = 0;

    while(dummy=0, nread = getline(&line, &dummy, stdin)!=-1)
    {
	   iov[iov_sz].iov_base = line;
	   iov[iov_sz++].iov_len = strlen(line);
    }

    // Shuffle lines (HINT: random(3))
    qsort(iov, iov_sz, sizeof(struct iovec), compfunc);
    // Dump lines with writev(2)
    writev(STDOUT_FILENO,iov, iov_sz);

    return 0;
}
