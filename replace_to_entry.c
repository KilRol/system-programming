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

#define MAX_UNIQUE_SYM 256

pthread_mutex_t mutex;
sem_t semaphore;
int processing = 0;

FILE *logbuf;
regex_t regex;
int reti;
char errbuf[100];

typedef struct Node_t {
    char *data;
    struct Node_t *next;
} Node;

typedef struct Queue_s {
    struct Node_t *head;
} Queue;

void enqueue_file(Queue *queue, char *filename) {
    pthread_mutex_lock(&mutex);
    Node *t = (Node *) malloc(sizeof(Node));
    t->data = (char *) malloc(PATH_MAX * sizeof(char));

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
    sem_post(&semaphore);
    processing++;
    pthread_mutex_unlock(&mutex);
}

void dequeue_file(Queue *queue) {
    Node *t = queue->head;
    queue->head = queue->head->next;
    free(t->data);
    free(t);
}

int count_entry(FILE *fd, int *table) {
    int ptr = 0;
    while (!feof(fd)) {
        int ch = fgetc(fd);
        if (ch != -1) {
            table[ch]++;
            ptr++;
        }
    }
    return ptr;
}

void process_file(char *filename) {
    FILE *fd = fopen(filename, "r");
    if (!fd) {
        printf("File \"%s\" cannot open to read\n", filename);
        fprintf(logbuf, "File \"%s\" cannot open to read\n", filename);
        return;
    }
    int *table = (int *) malloc(MAX_UNIQUE_SYM * sizeof(int));
    int len = count_entry(fd, table);
    fseek(fd, 0, SEEK_SET);
    int *out = (int *) malloc((len + 1) * sizeof(int));
    for (int i = 0; i < len; i++) {
        int ch = fgetc(fd);
        out[i] = table[ch];
    }
    fclose(fd);
    fd = fopen(filename, "w");
    if (!fd) {
        printf("File \"%s\" cannot open to write\n", filename);
        fprintf(logbuf, "File \"%s\" cannot open to write\n", filename);
        return;
    }
    for (int i = 0; i < len; i++) {
        fprintf(fd, "%d ", out[i]);
    }
    fclose(fd);
    free(table);
    free(out);
}

void *thread_func(void *args) {
    Queue *queue = args;
    while (1) {
        sem_wait(&semaphore);
        if (queue->head != NULL) {
            pthread_mutex_lock(&mutex);
            process_file(queue->head->data);
            fprintf(logbuf, "\"%s\" processed successfully\n", queue->head->data);
            dequeue_file(queue);
            processing--;
            pthread_mutex_unlock(&mutex);
        }
    }
    return NULL;
}

int enqueue_all_files(char *path, int depth, Queue *queue) {
    int err = 0;
    DIR *dir = opendir(path);
    struct dirent *st_dir;

    errno = 0;

    while ((st_dir = readdir(dir)) != NULL) {
        if (strcmp(st_dir->d_name, ".") != 0 && strcmp(st_dir->d_name, "..") != 0) {
            char new_path[PATH_MAX];
            strcpy(new_path, path);
            if (st_dir->d_type == DT_REG) {
                reti = regexec(&regex, strcat(new_path, st_dir->d_name), 0, NULL, 0);
                err = errno;
                if (!reti) {
                    enqueue_file(queue, new_path);
                    fprintf(logbuf, "\"%s\" in queue\n", new_path);
                } else if (reti == REG_NOMATCH) {
                    fprintf(logbuf, "\"%s\" not match with regex\n", new_path);
                } else {
                    regerror(reti, &regex, errbuf, sizeof(errbuf));
                    fprintf(logbuf, "Regex match failed: %s\n", errbuf);
                    return err;
                }
            } else {
                strcpy(new_path, path);
                enqueue_all_files(strcat(strcat(new_path, st_dir->d_name), "/"), depth + 1, queue);
            }
        }
    }
    if (errno != 0) {
        err = errno;
        fprintf(logbuf, "readdir failed\n");
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
    logbuf = fopen("replace_to_entry.log", "w");
    if (argc != 4) {
        puts("Error! Use ./exec path regex threads_num");
        fprintf(logbuf, "Error! Use ./exec dir regex threads_num\n");
        return -1;
    }

    reti = regcomp(&regex, argv[2], 0);
    if (reti) {
        puts("Could not compile regex");
        fprintf(logbuf, "Could not compile regex\n");
        return -2;
    }

    int err = 0;
    int threads_num = to_int(argv[3], &err);
    if (err == -1) {
        fprintf(logbuf, "Incorrect number of threads\n");
    }
    pthread_t *threads = (pthread_t *) malloc(threads_num * sizeof(pthread_t));

    Queue queue;
    queue.head = NULL;

    for (int i = 0; i < threads_num; i++) {
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
        fprintf(logbuf, "stat failed\n");
        return err;
    }
    int isFile = 0;
    if ((st.st_mode & S_IFMT) != S_IFDIR) {
        if ((st.st_mode & S_IFMT) == S_IFREG) {
            reti = regexec(&regex, path, 0, NULL, 0);
            if (!reti) {
                enqueue_file(&queue, path);
                fprintf(logbuf, "\"%s\" added to queue\n", path);
                isFile = 1;
            } else if (reti == REG_NOMATCH) {
                fprintf(logbuf, "\"%s\" not match with regex\n", path);
            } else {
                regerror(reti, &regex, errbuf, sizeof(errbuf));
                fprintf(logbuf, "Regex match failed: %s\n", errbuf);
                return -3;
            }
        } else {
            puts("This path is not a directory");
            fprintf(logbuf, "\"%s\" is not a directory\n", path);
            return -1;
        }
    }
    if (!isFile) {
        if (path[strlen(path) - 1] != '/') {
            strcat(path, "/");
        }
        enqueue_all_files(path, 0, &queue);
    }

    int sem_state;
    do { sem_getvalue(&semaphore, &sem_state); } while (sem_state != 0 || processing != 0);

    for (int i = 0; i < threads_num; i++) {
        pthread_cancel(threads[i]);
    }

    regfree(&regex);
    sem_destroy(&semaphore);
    pthread_mutex_destroy(&mutex);
    free(threads);
    fprintf(logbuf, "All files processed successfully\n");
    return 0;
}