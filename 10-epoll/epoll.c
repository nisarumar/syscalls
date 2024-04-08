#define _GNU_SOURCE
#include <stdio.h>
#include <spawn.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <assert.h>
#include <limits.h>


#define die(msg) do { perror(msg); exit(EXIT_FAILURE); } while(0)

/* For each filter process, we will generate a proc object */
struct proc {
    char    *cmd;   // command line
    pid_t   pid;    // process id of running process. 0 if exited
    int     stdin;  // stdin file descriptor of process (pipe)
    int     stdout; // stdout file descriptor of process
};

static int nprocs;         // Number of started filter processes
static struct proc *procs; // Dynamically-allocated array of procs

void epoll_add(int epollfd, int fd, int events, uint32_t index)
{
	struct epoll_event ev; 
	ev.events = events;
	ev.data.u64 = (uint64_t) index;

	if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &ev) == -1)
	{
		die("epoll_add");
	}

}

void epoll_del (int epollfd, int fd)
{
	if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) == -1)
	{
		die("epoll_del");
	}
}

////////////////////////////////////////////////////////////////
// HINT: You have already seen this in the in the select exercise
////////////////////////////////////////////////////////////////

// This function starts the filter (proc->cmd) as a new child process
// and connects its stdin and stdout via pipes (proc->{stdin,stdout})
// to the parent process.
//
// We also start the process wrapped by stdbuf(1) to force
// line-buffered stdio for a more interactive experience on the terminal
static int start_proc(struct proc *proc) {
    // We build an array for execv that uses the shell to execute the
    // given command.
    char *argv[] = {"sh", "-c", proc->cmd, 0 };

    // We create two pipe pairs, where [0] is the reading end
    // and [1] the writing end of the pair. We also set the O_CLOEXEC
    // flag to close both descriptors when the child process is exec'ed.
    int stdin[2], stdout[2];
	if (pipe2(stdin,  O_CLOEXEC)) return -1;
    if (pipe2(stdout, O_CLOEXEC)) return -1;

    // For starting the filter, we use posix_spawn, which gives us an
    // interface around fork+exec to perform standard process
    // spawning. We use a filter action to copy our pipe descriptors to
    // the stdin (0) and stdout (1) handles within the child.
    // Internally, posix_spawn will do a dup2(2). For example,
    //
    //     dup2(stdin[0], STDIN_FILENO);
    posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, stdin[0],  STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&fa, stdout[1], STDOUT_FILENO);

    // "magic variable": array of pointers to environment variantes.
    // This symbol is described in environ(2)
    extern char **environ;

    // We spawn the filter process.
    int e;
    if (!(e = posix_spawn(&proc->pid, "/bin/sh", &fa, 0, argv,  environ))) {
        // On success, we free the allocated memory.
        posix_spawn_file_actions_destroy(&fa);

        // We are within the parent process. Therefore, we copy our
        // pipe ends to the proc object and close the ends that are
        // also used within the child (to save file descriptors)

        // stdin of filter
        proc->stdin = stdin[1]; // write end
        close(stdin[0]);        // read end

        // stdout of filter
        proc->stdout = stdout[0]; // read end
        close(stdout[1]);         // write end

        return 0;
	} else {
        // posix_spawn failed.
        errno = e;
        return -1;
    }
}


// FIXME: Implement a 'int copy_splice(int in_fd, int out_fd);'

// This function prints an array of uint64_t (elements) as line with
// throughput measures. The function throttles its output to one line
// per second.
//
int copy_splice(int in_fd, int out_fd)
{
	static char buff[4096] = {};

	int len = splice(in_fd, 0, out_fd, 0, INT_MAX, SPLICE_F_NONBLOCK);

	if (len >= 0) return len;

	if (errno == EAGAIN)
		return 0;

	if (errno != EINVAL) die("splice");


	len = read(in_fd, &buff[0], sizeof(buff)); //one time as epoll is on read end and level triggered; if there is data in pipe it will generate another event;
	if (len < 0)
	{
		die("read");
	}

	int bytes = 0;

	while (bytes < len ) //loop because we want to write every byte; no epolling on write end;
	{
		int ret = write(out_fd, &buff[bytes], len);
		if (ret < 0)
		{
			die ("write");
		}
		else
		{
			bytes += ret;
		}

	}

	return bytes;
}

// Example Output:
//  2860.20MiB/s 2860.26MiB/s 2860.23MiB/s 2860.25MiB/s 2860.29MiB/s
void print_throughput(uint64_t *bytes, int elements) {
    static struct timespec last = { 0 };

    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) < 0)
        die("clock_gettime");

    if (now.tv_sec > last.tv_sec && last.tv_sec > 0) {
        double delta = now.tv_sec - last.tv_sec;
        delta += (now.tv_nsec - last.tv_nsec) / 1e9;
        for (int i = 0; i < elements; i++) {
            fprintf(stderr, " %.2fMiB/s", bytes[i]/delta/1024/1024);
        }
        fprintf(stderr, "\n");
        memset(bytes, 0, elements * sizeof(uint64_t));
        last = now;
    } else if (last.tv_sec == 0) {
        last = now;
    }
}

int main(int argc, char *argv[]) {
    if (argc <= 1) {
        fprintf(stderr, "usage: %s [CMD-1]", argv[0]);
        return -1;
    }
    // We allocate an array of proc objects
    nprocs = argc - 1;
    procs   = malloc(nprocs * sizeof(struct proc));
    if (!procs) die("malloc");

    // Initialize proc objects and start the filter
    for (int i = 0; i < nprocs; i++) {
        procs[i].cmd  = argv[i+1];
        int rc = start_proc(&procs[i]);
        if (rc < 0) die("start_filter");

        fprintf(stderr, "[%s] Started filter as pid %d\n", procs[i].cmd, procs[i].pid);
    }

    // FIXME: Arrange file descriptors in pairs of input -> output
    // 2D array where 0 is read end and 1 is write end of the next process;
    // e.g 2 processes [0][2] = {STDIIN_FILENO, PROCESS0_STDIN_WRITE_END}, [1][2]= {PROCESS0_STDOUT_READEND, PROCESS1_STDIN_WRITE_END}, [2][2]={PROCESS1_STDOUT_READEND, THIS_STDOUT_WRITE_END}
    enum {
	    read = 0,
	    write,
	    arr_sz
    };

    int fd_pairs[nprocs+1][arr_sz]; 
    fd_pairs[0][read] = STDIN_FILENO;
    for (int i = 0; i< nprocs; i++)
    {
	    fd_pairs[i][write] = procs[i].stdin;
	    fd_pairs[i+1][read] = procs[i].stdout;
    }

    fd_pairs[nprocs][write] = STDOUT_FILENO;

    // FIXME: Setup epoll to listen on the input descriptors
    // register for all the read fds
    int epollfd = epoll_create1(0);

    if (epollfd == -1)
    {
	    die("epoll_create1");
    }

    for (int i = 0; i< nprocs+1; i++ )
    {
	    epoll_add( epollfd, fd_pairs[i][read], EPOLLIN, i );
    }

    struct epoll_event events[10];

    int pairs = nprocs+1;

    uint64_t throughput[pairs];

    memset(&throughput[0], 0x00, sizeof(throughput));


    // FIXME: Receive events and copy data around.
    
    while (pairs)
    {
	    int nfds = epoll_wait(epollfd, events, 10, -1);
	    if (nfds == -1 )
	    {
		    die("epoll_wait");
	    }
	    for (int i = 0; i< nfds; i++)
	    {
		    uint64_t pair = events[i].data.u64;


		    if (events[i].events & EPOLLIN)
		    {
			   uint64_t bytes = copy_splice(fd_pairs[pair][read], fd_pairs[pair][write]);
			   throughput[pair] += bytes;

		    }
		    if (events[i].events & EPOLLHUP )
		    {
			    epoll_del(epollfd,fd_pairs[pair][read]);
			    close(fd_pairs[pair][read]);
			    close(fd_pairs[pair][write]);
			    pairs--;
		    }
	    }


    print_throughput(&throughput[0], pairs);

    }
    // FIXME: call print_throughput from time to time.
}
