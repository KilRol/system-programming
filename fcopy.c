#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

int fcopy(char *source, char *dest)
{
    int fd = open(source, O_RDONLY);
    if (fd == -1)
    {
        perror("File opening failed");
        return -1;
    }

    int newfd = creat(dest, 0777);
    if (newfd == -1)
    {
        perror("Copy creation failed");
        return 0;
    }

    ssize_t ret;
    char buf[BUFSIZ];

    while ((ret = read(fd, buf, BUFSIZ)) != 0)
    {
        if (ret == -1)
        {
            if (errno == EINTR)
            {
                continue;
            }
            else if (errno == EAGAIN)
            {
                // do something useful
                continue;
            }
            perror("read");
            break;
        }

        ssize_t wret;
        ssize_t len = ret;

        while (len != 0 && (wret = write(newfd, buf, len)) != 0)
        {
            if (wret == -1)
            {
                if (errno == EINTR)
                {
                    continue;
                }
                perror("write");
                break;
            }
            len -= wret;
        }
    }

    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        printf("Wrong number of arguments\n");
        printf("Please, use ./[exec] [original_name] [copy_name]\n");
        return 0;
    }

    fcopy(argv[1], argv[2]);

    return 0;
}