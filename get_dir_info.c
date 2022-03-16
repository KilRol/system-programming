#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <bits/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int get_dir_info(char *path, int depth)
{
    DIR *dir = opendir(path);
    struct dirent *st_dir;

    errno = 0;

    while ((st_dir = readdir(dir)) != NULL)
    {
        if (strcmp(st_dir->d_name, ".") && strcmp(st_dir->d_name, ".."))
        {
            for (int i = 0; i < depth; i++)
            {
                printf("|  ");
            }
            puts(st_dir->d_name);
            if (st_dir->d_type == DT_DIR)
            {
                char npath[PATH_MAX];
                strcpy(npath, path);
                get_dir_info(strcat(strcat(npath, st_dir->d_name), "/"), depth + 1);
            }
        }
    }
    if (errno != 0)
    {
        perror("readdir");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    char path[PATH_MAX];

    if (argc < 2)
    {
        strcpy(path, getcwd(NULL, 0));
    }
    else if (argc == 2)
    {
        strcpy(path, argv[1]);
    }
    else
    {
        puts("Wrong number of arguments\nPlease, use ./<exec> <path>");
        return -1;
    }

    struct stat st;
    int ret;
    ret = stat(path, &st);

    if (ret == -1)
    {
        perror("stat");
        return -1;
    }

    if ((st.st_mode & S_IFMT) != S_IFDIR)
    {
        puts("This path is not a directory");
        return -1;
    }

    if (path[strlen(path) - 1] != '/')
    {
        strcat(path, "/");
    }

    get_dir_info(path, 0);

    return 0;
}
