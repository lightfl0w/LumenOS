#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int shared;

static void *worker(void *arg) {
    long id = (long)arg;
    for (int i = 0; i < 100; i++) {
        void *p = malloc(128);
        if (p) { *(int *)p = i; free(p); }
        pthread_mutex_lock(&lock);
        shared += (int)id;
        pthread_mutex_unlock(&lock);
    }
    return (void *)(id + 1);
}

int main(void) {
    pthread_t t[4];
    for (long i = 0; i < 4; i++) {
        if (pthread_create(&t[i], NULL, worker, (void *)i) != 0) {
            printf("PTHREAD FAIL create %ld\n", i);
            return 1;
        }
    }
    for (int i = 0; i < 4; i++) {
        void *ret = NULL;
        if (pthread_join(t[i], &ret) != 0) {
            printf("PTHREAD FAIL join %d\n", i);
            return 1;
        }
    }
    printf("PTHREAD PASS shared=%d\n", shared);
    return 0;
}
