#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#define BUFF_SIZE	4096U

int main(int argc, char *argv[]) {
    // For cat, we have to iterate over all command-line arguments of
    // our process. Thereby, argv[0] is our program binary itself ("./cat").
    int idx;
    char buff[BUFF_SIZE];

    int fh;

    for (idx = 1; idx < argc; idx++) {
	fh=open(argv[idx],O_RDONLY);
	if ( fh < 0 )
	{
		perror("open");
		return -1;
	}
	while(1)
	{
		int bytes = read(fh, &buff[0] , BUFF_SIZE);

		if (bytes < 0 )
		{
			perror("read");
			return -1;
		}
		else if (bytes == 0)
			break;
		
		char *p = &buff[0];

		while (bytes > 0)
		{
			int ret = write(1,p, bytes);
			if (ret < 0)
			{
				perror("write");
				return -1;
			}

			p +=ret;
			bytes-=ret;
		}
	}

	close(fh);
    }

    return 0;
}
