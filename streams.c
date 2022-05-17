#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <string.h>

pthread_mutex_t mutex;
pthread_mutex_t mutex2;
sem_t semaphore;
FILE* fout;

typedef struct job_t {
    int id;
    char* data;
} job;

struct Node {
    job data;
    struct Node* next;
};

typedef struct Queue_s {
    struct Node* head;

} Queue;
 
void enqueue_job(Queue* queue, job* data) {
    pthread_mutex_lock(&mutex); 
    struct Node* newNode = (struct Node*)malloc(sizeof(struct Node));
   
    newNode->data = *data;
    newNode->next = NULL;

    if (queue->head == NULL) {
        queue->head = newNode;
    }
    else {
        struct Node* cur = queue->head;
        while (cur->next) {
            cur = cur->next;
        }    
        cur->next = newNode;
    }
    sem_post(&semaphore);
    pthread_mutex_unlock(&mutex);
}

void del(Queue* queue) {
    struct Node* prev = queue->head;

    queue->head = prev->next;
    free(prev->data.data);
    free(prev);
}

void* printStr(void* data) {
    Queue* queue = (Queue*)(data);
    
    while(1) {
        sem_wait(&semaphore);
        if (queue->head == NULL) {
            continue;
        }  

        pthread_mutex_lock(&mutex);

        fprintf(fout,"thread_id: %ld", pthread_self());
        fprintf(fout, "\tid: %d\tdata: %s\n",queue->head->data.id, queue->head->data.data);
        del(queue);   

        pthread_mutex_unlock(&mutex);
    }
    
    return NULL;
}

int main()
{
    fout = fopen("fout.txt", "w");
    if (!fout) {
        return -1;
    }

    Queue queue;
    queue.head = NULL;
    pthread_mutex_init(&mutex, NULL);

    int thread_count = 4;
    pthread_t threads[thread_count];

    sem_init(&semaphore, 0, 0);
    
    int id = 0;

    for (int i = 0; i < thread_count; i++) {
        pthread_create(&threads[i], NULL, printStr, (void*)&queue);
    }

    char* str = malloc(100 * sizeof(char*));

    while(1) {
        if(scanf("%s", str) == EOF) {
            break;
        }
        job j = {.data = malloc(120 * sizeof(char)), .id=id++};
        strcpy(j.data, str);
        enqueue_job(&queue, &j);
    }

    int sem_state;
    do {
        sem_getvalue(&semaphore, &sem_state);
    } while (sem_state != 0);
    
    for (int i = 0; i < thread_count; i++) {
        pthread_cancel(threads[i]);
    }

    fclose(fout);
    free(str);  
    sem_destroy(&semaphore);
    
    return 0;
}