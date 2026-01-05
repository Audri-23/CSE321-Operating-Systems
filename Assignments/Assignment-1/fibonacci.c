#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
void *sequence_generation(void *arg1);
void *value_search(void *arg2);

int main(){
    pthread_t t1;
    pthread_t t2;
    int n;
    int *arr;
    printf("Enter the term of fibonacci sequence:\n");
    scanf("%d", &n);
    if (n<0){
        printf("Invalid input!\n");
        return 0;
    }
    else if (n>40){
        while(n>40){
            printf("Enter a value less than or equal 40: \n");
            scanf("%d", &n);
        }
    }
    int s;
    printf("How many numbers you are willing to search?:\n");
    scanf("%d",&s);
    int keys[s];
    for (int i=0; i<s; i++){
        printf("Enter search %d:\n", (i+1));
        scanf("%d", &keys[i]);
    }
    pthread_create(&t1, NULL, sequence_generation, &n);
    pthread_join(t1,(void **)&arr);
    for (int i=0; i<=n; i++){
        printf("a[%d] = %d\n", i, arr[i]);
    }
    void *diff[4];
    diff[0]=arr;
    diff[1]=&n;
    diff[2]=keys;
    diff[3]=&s;
    pthread_create(&t2, NULL, value_search, diff);
    pthread_join(t2, NULL);

    free(arr);
    return 0;
}

void *sequence_generation(void *arg1){
    int num= *(int *)arg1;
    int *temp_arr=malloc(sizeof(int)*(num+1));
    if (num>=0){
        temp_arr[0]=0;
    }
    if (num>=1){
        temp_arr[1]=1;
    }
    if (num>1){
        int a=0;
        int b=1;
        for (int i=2; i<=num; i++){
            int current_sum=a+b;
            temp_arr[i]=current_sum;
            a=b;
            b=current_sum;
        }
    }
    return temp_arr;
}

void *value_search(void *arg2){
    void **diff=(void **)arg2;
    int *arr=(int *)diff[0];
    int n= *(int *)diff[1];
    int *keys=(int *)diff[2];
    int s= *(int *)diff[3];
    for (int i=0; i<s; i++){
        if(keys[i]>n || keys[i]<0){
            printf("result of search #%d = -1\n", i+1);
        }
        else{
            int index=keys[i];
            printf("result of search #%d = %d\n", i+1, arr[index]);
        }
    }
    return NULL;

}