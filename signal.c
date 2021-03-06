#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

int thread_flag;
pthread_cond_t thread_flag_cv; 
pthread_mutex_t thread_flag_mutex;

void initialize_flag(){
    pthread_mutex_init (&thread_flag_mutex,NULL);
    pthread_cond_init (&thread_flag_cv,NULL);
    thread_flag = 0;
}

void* thread_function(){
    while(1) {
        pthread_mutex_lock(&thread_flag_mutex);
        while(!thread_flag) pthread_cond_wait(&thread_flag_cv,&thread_flag_mutex);
        pthread_mutex_unlock (&thread_flag_mutex);
        if (thread_flag == 101) {
            pthread_exit(NULL);
        }
        printf("%d\n", rand());
        thread_flag--;
    }
    return NULL;
}

void set_thread_flag(int flag_value){
    pthread_mutex_lock(&thread_flag_mutex);
    thread_flag =  flag_value;
    pthread_cond_signal(&thread_flag_cv);
    pthread_mutex_unlock(&thread_flag_mutex);
}

int main() {
    initialize_flag();

    pthread_t thread;
    pthread_create(&thread, NULL, thread_function, NULL);

    while (1) {
        int d;
        scanf("%d", &d);
        set_thread_flag(d);
        if (d == 101) {
            break;
        }
    }
    
    pthread_join(thread, NULL);
    return 0;
}