#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <assert.h>
#include <sys/time.h>
#include <unistd.h>

// 插入队列
#define LL_ADD(item, list)   \
    do {         \
    item->next = list; \
    item->prev = NULL; \
    /*list->prev = item;*/ \
    list = item;  \
    } while(0)  
    
// 删除队列
#define LL_REMOVE(item, list) \
    do { \
    assert(list != NULL);  \
    if (item->prev != NULL) { item->prev->next = item->next; } \
    if (item->next != NULL) { item->next->prev = item->prev; } \
    if (item == list) list = item->next; \
    item->prev = item->next = NULL; \
    } while(0) 
    

// 任务实体具体回调的处理函数别名
typedef int (*WorkerFunc)(void* data);

// 工作线程实体
typedef struct Worker
{
    pthread_t threadId;
    struct ThreadPool* pool;
    //WorkerFunc func;
    
    struct Worker* prev;
    struct Worker* next;
} Worker;

// 任务实体
typedef struct Task
{
    void* data;
    WorkerFunc func;
    
    struct Task* prev;
    struct Task* next;
} Task;

// 线程池实体
typedef struct ThreadPool
{
    Worker* workers;
    //int     workerCnt;
    struct Task* waitingTasks;
    
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
} ThreadPool;

// 创建线程回调函数
static void* ThreadFunc(void* args)
{
    Worker* worker = (Worker*)args;
    
    while (1) 
    {
        pthread_mutex_lock(&worker->pool->mutex);
        
        while (worker->pool->waitingTasks == NULL)
        {
            pthread_cond_wait(&worker->pool->cond, &worker->pool->mutex);
        }
        
        
        Task* task = worker->pool->waitingTasks;
        if (task != NULL)
        {
            LL_REMOVE(task, worker->pool->waitingTasks);
        }
        
        // 从任务列表取出任务即可释放锁资源
        pthread_mutex_unlock(&worker->pool->mutex);
        
        if (task == NULL) continue;
        
        // do task
        task->func(task->data);
        
        free(task);
    }
    return NULL;
}

int QtpThreadPoolCreate(ThreadPool** pool, size_t threadSum)
{
    assert(pool != NULL);
    if (threadSum < 1) return -1;
    
    ThreadPool* threadPool = (ThreadPool*) malloc(sizeof(ThreadPool));
    memset(threadPool, 0x00, sizeof(ThreadPool));
    
    threadPool->workers = NULL;
    //threadPool->workerCnt = threadSum;
    
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    memcpy(&threadPool->mutex, &mutex, sizeof(mutex));
    pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
    memcpy(&threadPool->cond, &cond, sizeof(cond));
    
    int i = 0;
    for (; i < threadSum; ++i)
    {
        Worker* worker = (Worker*) malloc(sizeof(Worker));
        if (worker == NULL)
        {
            goto End;
        }
        worker->pool = threadPool;
        pthread_create(&worker->threadId, NULL, ThreadFunc, (void*)worker);
        pthread_detach(worker->threadId);
        LL_ADD(worker, threadPool->workers);
        printf("create %d pthread: %ld. \n", i, worker->threadId);
    }
End:
    if (i != threadSum)
    {
        while (threadPool->workers != NULL)
        {
            Worker* worker = threadPool->workers;
            LL_REMOVE(worker, threadPool->workers);
            free(worker);
            worker == NULL;
        }
        return -1;
    }
    *pool = threadPool;
    return 0;
}

int QtpThreadPoolDestroy(ThreadPool* pool)
{
    assert(pool != NULL);
    
    while (pool->workers != NULL)
    {
        Worker* worker = pool->workers;
        LL_REMOVE(worker, pool->workers);
        free(worker);
        worker = NULL;
    }
    
    free(pool);
}

// 添加工作任务到线程池的任务队列中，然后唤醒空闲线程
int QtpThreadPoolRunTask(ThreadPool* pool, Task* task)
{
    pthread_mutex_lock(&pool->mutex);
    
    // 添加任务到线程池任务队列
    LL_ADD(task, pool->waitingTasks);
    
    // 唤醒空闲线程
    pthread_cond_signal(&pool->cond);
    
    pthread_mutex_unlock(&pool->mutex);
}

int QtpThreadPoolShutdown(ThreadPool* pool)
{
    
}


/*
 * test QtpThreadPool API
 * 
 * QtpThreadPoolCreate
 * QtpThreadPoolRunTask
 * QtpThreaddPoolDestroy
 */
#define MAX_THREAD_COUNT 10
#define MAX_TASK_COUNT 100

static long Now()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

static int PrintTaskData(void* args)
{
    int* index = (int*)args;
    
    if (index == NULL) return -1;
    
    printf("index: %d, pthread_id: %ld, time:%ld \n", *index, pthread_self(), Now());
    
    // 释放任务数据
    free(index);
    
    return 0;
}

int main__test(int argc, char *argv[])
{
    // 创建线程池
    ThreadPool* pool = NULL;
    if (QtpThreadPoolCreate(&pool, MAX_THREAD_COUNT) == 0)
    {
        printf("创建线程池成功。\n");
    }
    
    // 创建大量任务并通过线程池执行
    for (int i = 0; i < MAX_TASK_COUNT; ++i)
    {
        Task* task = (Task*)malloc(sizeof(Task));
        task->data = malloc(sizeof(int));
        memcpy(task->data, &i, sizeof(int));
        task->func = PrintTaskData;
        QtpThreadPoolRunTask(pool, task);
    }
    
    sleep(1);
    QtpThreadPoolDestroy(pool);
    
    printf("线程池销毁。 \n");
    
    return 0;
}
