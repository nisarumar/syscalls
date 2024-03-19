#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>

// We define two CPP macros to hide the ugliness of these compiler
// attributes.
//
// - section: This variable attribute pushes the variable into the
//            named ELF section. Thereby, we let the linker collect
//            variable definitions and concentrate them in one place
//
// - aligned: Normally, variables can be densely packed in the
//            data/BSS section. However, as we want to replace parts
//            of our data section with a file mapping, we obey the
//            MMU's granularity (4096 bytes).
#define persistent   __attribute__((section("persistent")))
#define PAGE_SIZE 4096
#define page_aligned __attribute__((aligned(PAGE_SIZE)))

// This is a special variable that we use in setup_persistent to
// locate the start of the persistent section. We page align it, such
// that its address is placed at the beginning of a page. This is
// equivalent to an HEX address ending in 3 zeros.
//
// For example: &persistent_start = 0x4b8000
//
struct {
	page_aligned int persistent_start;


// This is our actual persistent variable. We will use it as a
// counter, how often the program was executed.
	int foobar;
}psec={0xdeadbeef,0x1234};

// Although this variable is placed between persistent_{start,end}, it
// does *NOT* have the persistent variable, whereby it is place not
// within the persistent section.,
int barfoo = 42;

// A second special variable that marks the end of the persistent
// region. We also make it page aligned, whereby the linker will not
// place other

int setup_persistent(char *fn) {
    // FIXME: Install persistent mapping and return 0;
    struct stat s;
    int r = stat(fn, &s);

    int fd = open(fn, O_RDWR|O_CREAT, 0600);
    if (fd<0)
    {
	    perror("file open");
	    return -1;
    }
    if ( r == -1 || s.st_size != sizeof(psec) )
    {
	    if(ftruncate(fd, sizeof(psec)) != 0)
	    {
		    perror("truncate");
		    return -1;
	    }
	    if(write(fd, &psec, sizeof(psec)) != sizeof(psec))
	    {
		    perror("write");
		    return -1;
	    }
    }

  int *map=mmap(&psec, sizeof(psec), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, fd, 0);

  if (map == MAP_FAILED)
	  return -1;

   close(fd);
		   

    return 0;
}

int main(int argc, char *argv[]) {
	printf("psecsize: %ld\n", sizeof(psec));
    printf("section persistent: %p--%p\n", &psec, &psec + 1);
    // Install the persistent mapping
    if (setup_persistent("mmap.persistent") == -1) {
        perror("setup_persistent");
        return -1;
    }

    // For foobar, we see that each invokation of the programm will
    // yield an incremented result.
    // For barfoo, which is *NOT* in the persistent section, we will
    // always get the same result.
    printf("foobar(%p) = %d\n", &psec.foobar, psec.foobar++);
    printf("barfoo(%p) = %d\n", &barfoo, barfoo++);

    {// This is ugly and you should not do this in production code.

        // In order to see the memory mappings of the currently
        // running process, we use the pmap (for process-map) tool to
        // query the kernel (/proc/self/maps)
        char cmd[256];
        snprintf(cmd, 256, "pmap %d", getpid());
        printf("---- system(\"%s\"):\n", cmd);
        system(cmd);
    }

    return 0;
}
