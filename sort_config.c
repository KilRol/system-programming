#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <regex.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAX_SECTIONS 100
#define MAX_SETTINGS 100
#define MAX_STR_LEN 80

pthread_mutex_t mtx;
sem_t sem;
int proc = 0;

FILE *log_file;
regex_t regex;
int reti;
char msgbuf[100];

typedef struct Section_t {
    char *name;
    char **settings;
    int settings_count;
} Section;

typedef struct Node_t {
    char *data;
    struct Node_t *next;
} Node;

typedef struct Queue_s {
    struct Node_t *head;
} Queue;

void enqueue_file(Queue *queue, char *filename) {
    pthread_mutex_lock(&mtx);
    Node *t = (Node *) malloc(sizeof(Node));
    t->data = (char *) malloc(MAX_STR_LEN * sizeof(char));

    strcpy(t->data, filename);
    t->next = NULL;

    if (!queue->head) {
        queue->head = t;
    } else {
        Node *cur = queue->head;
        while (cur->next) {
            cur = cur->next;
        }
        cur->next = t;
    }
    sem_post(&sem);
    proc++;
    pthread_mutex_unlock(&mtx);
}

void dequeue_file(Queue *queue) {
    Node *t = queue->head;
    queue->head = queue->head->next;
    free(t->data);
    free(t);
}

int parse_to_section(FILE *fd, Section *sections) {
    int ptr = 0;

    char *str = (char *) malloc(MAX_STR_LEN * sizeof(char));
    do {
        fgets(str, MAX_STR_LEN, fd);
    } while (str[0] == '\n');

    while (!feof(fd)) {
        char *section_name = (char *) malloc(MAX_STR_LEN * sizeof(char));
        char **settings = (char **) malloc(MAX_SETTINGS * sizeof(char *));
        for (int i = 0; i < MAX_SETTINGS; i++) {
            settings[i] = (char *) malloc(MAX_STR_LEN * sizeof(char));
        }
        int settings_ptr = 0;
        Section section = {
                .name = section_name,
                .settings = settings};
        strcpy(section.name, str);
        fgets(str, 80, fd);
        while (str[0] != '[') {
            if (feof(fd) && str[0] != '\n') {
                strcat(str, "\n");
                strcpy(section.settings[settings_ptr++], str);
                break;
            }
            if (str[0] != '\n') {
                strcpy(section.settings[settings_ptr++], str);
            }
            char *a = fgets(str, MAX_STR_LEN, fd);
            if (a == NULL) {
                break;
            }
        }
        section.settings_count = settings_ptr;
        sections[ptr++] = section;
    }
    free(str);
    return ptr;
}

int comparator(const void *p, const void *q) {
    char *str1 = *(char **) p;
    char *str2 = *(char **) q;

    return strcmp(str1, str2);
}

int section_comparator(const void *p, const void *q) {
    char *str1 = ((Section *) p)->name;
    char *str2 = ((Section *) q)->name;

    return strcmp(str1, str2);
}

void process_file(char *filename) {
    FILE *fd = fopen(filename, "r");
    if (!fd) {
        printf("File \"%s\" cannot open\n", filename);
        fprintf(log_file, "File \"%s\" cannot open\n", filename);
        return;
    }
    Section *sections = (Section *) malloc(MAX_SECTIONS * sizeof(Section));
    int sections_size = parse_to_section(fd, sections);
    fclose(fd);
    for (int i = 0; i < sections_size; i++) {
        qsort((void *) sections[i].settings, sections[i].settings_count, sizeof(char *), comparator);
    }
    qsort((void *) sections, sections_size, sizeof(Section), section_comparator);
    fd = fopen(filename, "w");
    for (int i = 0; i < sections_size; i++) {
        fprintf(fd, "%s", sections[i].name);
        int setting_size = sections[i].settings_count;
        for (int j = 0; j < setting_size; j++) {
            fprintf(fd, "%s", sections[i].settings[j]);
        }
    }
    fclose(fd);
    for (int i = 0; i < sections_size; i++) {
        free(sections[i].name);
        for (int j = 0; j < MAX_SETTINGS; j++) {
            free(sections[i].settings[j]);
        }
        free(sections[i].settings);
    }
    free(sections);
}

void *thread_func(void *args) {
    Queue *queue = args;
    while (1) {
        sem_wait(&sem);
        if (queue->head != NULL) {
            pthread_mutex_lock(&mtx);
            process_file(queue->head->data);
            fprintf(log_file, "\"%s\" has been sorted successfully\n", queue->head->data);
            dequeue_file(queue);
            proc--;
            pthread_mutex_unlock(&mtx);
        }
    }
    return NULL;
}

int get_dir_info(char *path, int depth, Queue *queue) {
    int err = 0;
    DIR *dir = opendir(path);
    struct dirent *st_dir;

    errno = 0;

    while ((st_dir = readdir(dir)) != NULL) {
        if (strcmp(st_dir->d_name, ".") != 0 && strcmp(st_dir->d_name, "..") != 0) {
            if (st_dir->d_type == DT_REG) {
                reti = regexec(&regex, strcat(path, st_dir->d_name), 0, NULL, 0);
                if (!reti) {
                    enqueue_file(queue, path);
                    fprintf(log_file, "\"%s\" has been added to queue\n", path);
                } else if (reti == REG_NOMATCH) {
                    puts("Path does not match with regex");
                    fprintf(log_file, "\"%s\" does not match with regex\n", path);
                } else {
                    regerror(reti, &regex, msgbuf, sizeof(msgbuf));
                    fprintf(log_file, "Regex match failed: %s\n", msgbuf);
                    return -3;
                }
            } else {
                char new_path[PATH_MAX];
                strcpy(new_path, path);
                get_dir_info(strcat(strcat(new_path, st_dir->d_name), "/"), depth + 1, queue);
            }
        }
    }
    if (errno != 0) {
        err = errno;
        puts("readdir failed");
        fprintf(log_file, "readdir failed\n");
        return err;
    }
    free(dir);
    return 0;
}

int to_int(const char *number, int *err) {
    int res = 0;
    for (int i = 0; number[i] != '\0'; i++) {
        if ('0' <= number[i] && number[i] <= '9') {
            res = 10 * res + (number[i] - '0');
        } else {
            *err = -1;
            return 0;
        }
    }
    return res;
}

int main(int argc, char *argv[]) {
    log_file = fopen("sort_config.log", "w");
    if (argc != 4) {
        puts("Wrong format!\nUse ./exec dir regex threads_count");
        fprintf(log_file, "Wrong format: use ./exec dir regex threads_count\n");
        return -1;
    }

    reti = regcomp(&regex, argv[2], 0);
    if (reti) {
        puts("Could not compile regex");
        fprintf(log_file, "Could not compile regex\n");
        return -2;
    }

    int err = 0;
    int threads_count = to_int(argv[3], &err);
    if (err == -1) {
        fprintf(log_file, "Wrong number of threads\n");
    }
    pthread_t *threads = (pthread_t *) malloc(threads_count * sizeof(pthread_t));

    Queue queue;
    queue.head = NULL;

    for (int i = 0; i < threads_count; i++) {
        pthread_create(&threads[i], NULL, thread_func, (void *) (&queue));
    }

    char path[PATH_MAX];
    strcpy(path, argv[1]);

    struct stat st;
    int ret;
    ret = stat(path, &st);

    if (ret == -1) {
        err = errno;
        puts("stat failed");
        fprintf(log_file, "stat failed\n");
        return err;
    }
    int isFile = 0;
    if ((st.st_mode & S_IFMT) != S_IFDIR) {
        if ((st.st_mode & S_IFMT) == S_IFREG) {
            reti = regexec(&regex, path, 0, NULL, 0);
            if (!reti) {
                enqueue_file(&queue, path);
                fprintf(log_file, "\"%s\" has been added to queue\n", path);
                isFile = 1;
            } else if (reti == REG_NOMATCH) {
                puts("Path does not match with regex");
                fprintf(log_file, "\"%s\" does not match with regex\n", path);
            } else {
                regerror(reti, &regex, msgbuf, sizeof(msgbuf));
                fprintf(log_file, "Regex match failed: %s\n", msgbuf);
                return -3;
            }
        } else {
            puts("This path is not a directory");
            fprintf(log_file, "\"%s\" is not a directory or filepath\n", path);
            return -1;
        }
    }
    if (!isFile) {
        if (path[strlen(path) - 1] != '/') {
            strcat(path, "/");
        }
        get_dir_info(path, 0, &queue);
    }

    int sem_state;
    do {
        sem_getvalue(&sem, &sem_state);
    } while (sem_state != 0 || proc != 0);

    for (int i = 0; i < threads_count; i++) {
        pthread_cancel(threads[i]);
    }

    sem_destroy(&sem);
    pthread_mutex_destroy(&mtx);
    regfree(&regex);
    free(threads);
    fprintf(log_file, "Files sorted successfully\n");
    return 0;
}