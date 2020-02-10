#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <assert.h>
#include <unistd.h>


// 插入链表简单操作
#define LL_ADD(item, list) \
    do \
{      \
    item->prev = NULL; \
    item->next = list; \
    list = item; \
} while (0)


// 删除链表节点简单操作
#define LL_REMOVE(item, list) \
    do \
{      \
    if (item->prev != NULL) item->prev->next = item->next; \
    if (item->next != NULL) item->next->prev = item->prev; \
    if (list == item) list = item->next; \
    item->prev = item->next = NULL; \
} while(0)

// 工作线程实体
typedef struct StWorker
{
    pthread_t threadId;
    struct QtpThreadPool* stThreadPool;
    int isTerminated; //线程终止标志
    
    struct StWorker* prev;
    struct StWorker* next;    
} StWorker;

// 工作任务实体
typedef struct StTask
{
    void* data;
    int (*TaskSelfFunc)(void* data);
    
    struct StTask* prev;
    struct StTask* next;
    
} StTask;

// 线程池实体
typedef struct QtpThreadPool
{
    struct StWorker* workers;
    struct StTask* waitingTasks;
    
    pthread_cond_t cond;
    pthread_mutex_t mutex;
} QtpThreadPool;


// 线程执行函数
void* ThreadCallBack(void* args)
{
    StWorker* worker = (StWorker*) args;
    assert(worker != NULL);
    
    while (1)
    {
        pthread_mutex_lock(&worker->stThreadPool->mutex);
        
        //  没有任务则阻塞等待
        while (worker->stThreadPool->waitingTasks == NULL)
        {
           if (worker->isTerminated != 0) break;
           pthread_cond_wait(&worker->stThreadPool->cond, &worker->stThreadPool->mutex);
        }
        
        // 线程中间退出
        if (worker->isTerminated != 0)
        {
            pthread_mutex_unlock(&worker->stThreadPool->mutex);
            pthread_exit(&worker->isTerminated);
        }
        
        StTask* task = worker->stThreadPool->waitingTasks;
        if (task != NULL)
        {
            LL_REMOVE(task, worker->stThreadPool->waitingTasks);
        }

        // 取出任务节点即可释放锁
        pthread_mutex_unlock(&worker->stThreadPool->mutex);
        
        if (task == NULL) continue;
        
        // 执行任务操作不需要占有锁
        task->TaskSelfFunc(task);
    }
    
    return NULL;
}


// 创建线程池
int QtpThreadPoolCreate(struct QtpThreadPool** pool, int threadCnt)
{
    assert(pool != NULL);
    if (threadCnt < 1) return -1;
    
    struct QtpThreadPool* threadPool = (QtpThreadPool*) malloc(sizeof(QtpThreadPool));
    if (threadPool == NULL)
    {
        return -1;
    }
    
    threadPool->waitingTasks = NULL;
    threadPool->workers = NULL;
    
    pthread_mutex_init(&threadPool->mutex, NULL);
    pthread_cond_init(&threadPool->cond, NULL);
    
    int i = 0;
    for (; i < threadCnt; ++i)
    {
        StWorker* worker = (StWorker*) malloc(sizeof(StWorker));
        if (worker == NULL)
        {
            goto End;
        }
        worker->stThreadPool = threadPool;
        worker->isTerminated = 0;
        LL_ADD(worker, worker->stThreadPool->workers);
        if (pthread_create(&worker->threadId, NULL, ThreadCallBack, worker) != 0)
        {
            goto End;
        }
        pthread_detach(worker->threadId); // 设置线程单元为分离态
    }
    
End:
    if (i < threadCnt)
    {
        // 创建线程池时，中间失败
        while (threadPool->workers != NULL)
        {
            StWorker* worker = threadPool->workers;
            LL_REMOVE(worker, threadPool->workers);
            free(worker);
        }
        free(threadPool);
        *pool = NULL;
        
        return -1;
    }
    *pool = threadPool;
    
    return 0;
}


// 线程池销毁操作
int QtpThreadPoolDestroy(QtpThreadPool* pool)
{
    assert(pool != NULL);
    
    while (pool->workers != NULL)
    {
        StWorker* worker = pool->workers;
        LL_REMOVE(worker, pool->workers);
        free(worker);
    }
    
    while (pool->waitingTasks != NULL)
    {
        StTask* waitingTask = pool->waitingTasks;
        LL_REMOVE(waitingTask, pool->waitingTasks);
        free(waitingTask);
    }
    
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    
    return 0;
}

// 线程池任务调度
int QtpThreadPoolRunTask(QtpThreadPool* pool, StTask* task)
{
    assert(pool != NULL && task != NULL);
    
    pthread_mutex_lock(&pool->mutex);
    
    // 添加任务到任务列表
    LL_ADD(task, pool->waitingTasks);
    
    // 唤醒空闲线程执行任务
    pthread_cond_signal(&pool->cond);
    
    pthread_mutex_unlock(&pool->mutex);
}


// 线程池各工作线程终止
int QtpThreadPoolShutdown(QtpThreadPool* pool)
{
    assert(pool != NULL);
    
    pthread_mutex_lock(&pool->mutex);
    
    while (pool->workers != NULL)
    {
        pool->workers->isTerminated = 1;
        pool->workers = pool->workers->next;
    }
    //广播通知线程池终止
    pthread_cond_broadcast(&pool->cond);
    
    pthread_mutex_unlock(&pool->mutex);
}


/* QtpThreaadPool Test*/
int PrintTaskData(void* data)
{
    StTask* task = (StTask*)data;
    assert(task != NULL);
    
    printf("Task data: %d, task thread id: %ld. \n", *((int*)task->data), pthread_self());
    
    free(task);
}

#define MAX_THREAD_SIZE 10
#define MAX_TASK_SIZE 100

int main()
{
    QtpThreadPool* pool = NULL;
    if (QtpThreadPoolCreate(&pool, MAX_THREAD_SIZE) == 0)
    {
        printf("创建线程池成功. \n");
    }
    
    for (int i = 0; i < MAX_TASK_SIZE; ++i)
    {
        StTask* task = (StTask*)malloc(sizeof(StTask));
        task->TaskSelfFunc = PrintTaskData;
        task->data = (int*)malloc(sizeof(int));
        *((int*)task->data) = i;
        QtpThreadPoolRunTask(pool, task);
    }
    
    // 等待一会，避免线程池有的线程还没执行完，就终止线程池, main 函数返回
    sleep(1);
    
    QtpThreadPoolShutdown(pool);
    
    if (QtpThreadPoolDestroy(pool) == 0)
    {
        printf("销毁线程池成功. \n");
    }
    
    return 0;
}
