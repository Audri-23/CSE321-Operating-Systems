#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
sem_t table_empty;
sem_t semA;
sem_t semB;
sem_t semC;
pthread_mutex_t table;

void *supplier(void *arg){
    int n= *(int *)arg;

    for (int i=0; i<n; i++){
        sem_wait(&table_empty);
        pthread_mutex_lock(&table);
        int not_supply= rand()%3;

        if (not_supply==0){
            printf("Supplier places: Bread and Cheese\n");
            sem_post(&semC);
        }
        else if (not_supply==1){
            printf("Supplier places: Bread and Lettuce\n");
            sem_post(&semB);
        }
        else{
            printf("Supplier places: Cheese and Lettuce\n");
            sem_post(&semA);
        }
        pthread_mutex_unlock(&table);
    }
    return NULL;
}

void *makerA(void *argA){
    while (1){
        sem_wait(&semA);
        pthread_mutex_lock(&table);
        printf("Maker A picks up Cheese and Lettuce\nMaker A is making the sandwich...\nMaker A finished making the sandwich and eats it\nMaker A signals Supplier\n");
        pthread_mutex_unlock(&table);
        sem_post(&table_empty);
    }
    
    return NULL;
}

void *makerB(void *argB){
    while (1){
        sem_wait(&semB);
        pthread_mutex_lock(&table);
        printf("Maker B picks up Bread and Lettuce\nMaker B is making the sandwich...\nMaker B finished making the sandwich and eats it\nMaker B signals Supplier\n");
        pthread_mutex_unlock(&table);
        sem_post(&table_empty);
    }
    
    return NULL;
}

void *makerC(void *argC){
    while (1){
        sem_wait(&semC);
        pthread_mutex_lock(&table);
        printf("Maker C picks up Bread and Cheese\nMaker C is making the sandwich...\nMaker C finished making the sandwich and eats it\nMaker C signals Supplier\n");
        pthread_mutex_unlock(&table);
        sem_post(&table_empty);
    }
    
    
    return NULL;
}


int main(){
    srand(time(NULL));
    int num;
    scanf("%d", &num);
    pthread_t splr,A,B,C;

    sem_init(&semA, 0, 0);
    sem_init(&semB, 0, 0);
    sem_init(&semC, 0, 0);
    sem_init(&table_empty, 0, 1);
    pthread_mutex_init(&table, NULL);

    pthread_create(&splr,NULL, supplier, &num);
    pthread_create(&A, NULL, makerA, NULL);
    pthread_create(&B, NULL, makerB, NULL);
    pthread_create(&C, NULL, makerC, NULL);
    pthread_join(splr, NULL);
    // pthread_join(A, NULL);
    // pthread_join(B, NULL);
    // pthread_join(C, NULL);
    pthread_mutex_destroy(&table);
    sem_destroy(&table_empty);
    sem_destroy(&semA);
    sem_destroy(&semB);
    sem_destroy(&semC);
    return 0;
}