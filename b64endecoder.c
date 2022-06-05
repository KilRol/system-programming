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

int mode = 0;
#define BLOCK_SIZE 8192

pthread_mutex_t mtx;
sem_t sem;
int proc = 0;

FILE *log_file;
regex_t regex;
int reti;
char msgbuf[100];

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

static char base64_table[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

void base64_encode(char *src, size_t len, char *outStr) {
    char *out, *pos;
    char *end, *in;

    out = (char *) &outStr[0];

    end = src + len;
    in = src;
    pos = out;
    while (end - in >= 3) {
        *pos++ = base64_table[in[0] >> 2];
        *pos++ = base64_table[((in[0] & 0x03) << 4) | (in[1] >> 4)];
        *pos++ = base64_table[((in[1] & 0x0f) << 2) | (in[2] >> 6)];
        *pos++ = base64_table[in[2] & 0x3f];
        in += 3;
    }

    if (end - in) {
        *pos++ = base64_table[in[0] >> 2];
        if (end - in == 1) {
            *pos++ = base64_table[(in[0] & 0x03) << 4];
            *pos++ = '=';
        } else {
            *pos++ = base64_table[((in[0] & 0x03) << 4) |
                                  (in[1] >> 4)];
            *pos++ = base64_table[(in[1] & 0x0f) << 2];
        }
        *pos++ = '=';
    }
}

static const int B64index[256] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 62, 63, 62, 62, 63, 52, 53, 54, 55,
                                  56, 57, 58, 59, 60, 61, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 5, 6,
                                  7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0,
                                  0, 0, 0, 63, 0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
                                  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51};

void base64decode(const char *p, const size_t len, char *str, size_t L, int pad) {
    for (size_t i = 0, j = 0; i < L; i += 4) {
        int n = B64index[(int) p[i]] << 18 | B64index[(int) p[i + 1]] << 12 | B64index[(int) p[i + 2]] << 6 |
                B64index[(int) p[i + 3]];
        str[j++] = n >> 16;
        str[j++] = n >> 8 & 0xFF;
        str[j++] = n & 0xFF;
    }
    if (pad) {
        int n = B64index[(int) p[L]] << 18 | B64index[(int) p[L + 1]] << 12;
        str[strlen(str)] = n >> 16;

        if (len > L + 2 && p[L + 2] != '=') {
            n |= B64index[(int) p[L + 2]] << 6;
            char s[2];
            s[0] = (char) (n >> 8 & 0xFF);
            s[1] = 0;
            strcat(str, s);
        }
    }
}

void process_file(char *filename) {
    FILE *fd = fopen(filename, "r");
    if (!fd) {
        printf("File \"%s\" cannot open to read\n", filename);
        fprintf(log_file, "File \"%s\" cannot open to read\n", filename);
        return;
    }
    char *str = (char *) malloc(BLOCK_SIZE * sizeof(char));
    for (int i = 0; !feof(fd); i++) {
        int ch = fgetc(fd);
        if (ch != -1) {
            str[i] = (char) ch;
        }
    }
    unsigned long len = strlen(str);
    if (!mode) {
        size_t olen = 4 * ((len + 2) / 3);
        if (olen < len) {
            return;
        }
        char *outStr = (char *) malloc(olen * sizeof(char));
        base64_encode(str, len, outStr);
        fclose(fd);
        fd = fopen(filename, "w");
        if (!fd) {
            printf("File \"%s\" cannot open to write\n", filename);
            fprintf(log_file, "File \"%s\" cannot open to write\n", filename);
            return;
        }
        fprintf(fd, "%s", outStr);
        free(outStr);
    } else {
        len++;
        int pad = len > 0 && (len % 4 || str[len - 1] == '=');
        const size_t L = ((len + 3) / 4 - pad) * 4;
        char *outStr = (char *) malloc(L / 4 * 3 + pad * sizeof(char));
        base64decode(str, len, outStr, L, pad);
        fd = fopen(filename, "w");
        if (!fd) {
            printf("File \"%s\" cannot open to write\n", filename);
            fprintf(log_file, "File \"%s\" cannot open to write\n", filename);
            return;
        }
        fprintf(fd, "%s", outStr);
        free(outStr);
    }
    free(str);
}

void *thread_func(void *args) {
    Queue *queue = args;
    while (1) {
        sem_wait(&sem);
        if (queue->head != NULL) {
            pthread_mutex_lock(&mtx);
            process_file(queue->head->data);
            if (mode) {
                fprintf(log_file, "\"%s\" decoded successfully\n", queue->head->data);
            } else {
                fprintf(log_file, "\"%s\" encoded successfully\n", queue->head->data);
            }
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
            char new_path[PATH_MAX];
            strcpy(new_path, path);
            if (st_dir->d_type == DT_REG) {
                reti = regexec(&regex, strcat(new_path, st_dir->d_name), 0, NULL, 0);
                err = errno;
                if (!reti) {
                    enqueue_file(queue, new_path);
                    fprintf(log_file, "\"%s\" in queue\n", new_path);
                } else if (reti == REG_NOMATCH) {
                    fprintf(log_file, "\"%s\" not match with regex\n", new_path);
                } else {
                    regerror(reti, &regex, msgbuf, sizeof(msgbuf));
                    fprintf(log_file, "Regex match failed: %s\n", msgbuf);
                    return err;
                }
            } else {
                strcpy(new_path, path);
                get_dir_info(strcat(strcat(new_path, st_dir->d_name), "/"), depth + 1, queue);
            }
        }
    }
    if (errno != 0) {
        err = errno;
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
    log_file = fopen("b64endecoder.log", "w");
    if (argc != 5) {
        puts("Wrong format!\nUse ./exec dir regex threads_count");
        fprintf(log_file, "Wrong format: use ./exec dir regex threads_count\n");
        return -1;
    }

    if (!strcmp(argv[4], "1")) {
        mode = 1;
    }
    else if (!strcmp(argv[4], "0")) {
        mode = 0;
    }
    else {
        fprintf(log_file, "Incorrect mode\n");
        return -2;
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
                fprintf(log_file, "\"%s\" added to queue\n", path);
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
            fprintf(log_file, "\"%s\" is not a directory\n", path);
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
    fprintf(log_file, "Files processed successfully\n");
    return 0;
}