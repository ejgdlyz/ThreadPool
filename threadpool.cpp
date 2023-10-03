#include <iostream>
#include <functional>
#include <thread>

#include "threadpool.h"

const size_t TASK_MAX_THRESHOLD = 1024;
const size_t THREAD_MAX_THRESHOLD = 10;
const size_t THREAD_MAX_IDLE_TIME = 10;  // cached 模式下，线程空闲 60s, 回收

ThreadPool::ThreadPool(): 
    initThreadSize_(0),  // 初始化为 CPU 核的数量
    taskSize_(0),
    threadSizeThresHold_(THREAD_MAX_THRESHOLD), 
    curThreadSize_(0),
    taskQueMaxThreshold_(TASK_MAX_THRESHOLD),
    poolMode_(PoolMode::MODE_FIXED), 
    isPoolRunning_(false), 
    idleThreadSize_(0)
{}

ThreadPool::~ThreadPool()
{
    isPoolRunning_ = false;
    
    // notEmpty_.notify_all();  // 唤醒 threadFunc 中的线程, 它们可能在阻塞或者运行

    // 等待线程池中所有的线程返回
    // 线程有两种状态：阻塞 和 执行任务中
    std::unique_lock<std::mutex> lock(taskQueMtx_);

    notEmpty_.notify_all();  // 唤醒 threadFunc 中的线程, 它们可能在阻塞或者运行

    exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0;});  // 等待线程队列中所有线程先析构
}

// 设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
    if (checkRunningState())
        return;
    
    poolMode_ = mode;
}

// 设置 Task 任务队列阈值
void ThreadPool::setTaskQueMaxThreshold(size_t threshold)
{
    if (checkRunningState())
        return;
    taskQueMaxThreshold_ = threshold;
}

void ThreadPool::setThreadSizeThreshold(size_t threshold)
{
    if (checkRunningState())
        return;
    if (poolMode_ == PoolMode::MODE_CACHED)
    {
        threadSizeThresHold_ = threshold;
    }
}


// 给线程池提交任务
// 用户调用该接口，传入任务对象，生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
    // 获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    
    // 线程通信  等待任务队列有空余
    // 用户提交任务，最长不能阻塞 1s，否则提交任务失败，返回 
    // 条件满足返回 或 持续1s返回
    if (!notFull_.wait_for(lock, std::chrono::seconds(1), 
        [&]()->bool {return taskQue_.size() < taskQueMaxThreshold_;}))
    {
        // 等待 1s 返回，失败
        std::cerr << "task queue is full, fail to submit task!" << std::endl;
        return Result(sp, false);

    } 
    
    // 如果有空余，把任务放入任务队列中
    taskQue_.emplace(sp);
    taskSize_++;

    // 新放了任务以后，任务队列不空，在 notEmpty_ 上进行通知
    // 分配线程，执行任务
    notEmpty_.notify_all();

    // cached 模式，根据任务数量和空闲线程的数量，判断是否需要创建新的线程
    if (poolMode_ == PoolMode::MODE_CACHED && taskSize_ > idleThreadSize_ && curThreadSize_ < threadSizeThresHold_)
    {
        std::cout << ">>> create new thread Id..." << std::endl; 

        // 创建新的线程对象
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        size_t threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
        threads_[curThreadSize_]->start();  // 启动线程
        curThreadSize_++;
        idleThreadSize_++;
    }

    // 返回任务的 Result 对象
    return Result(sp);
}

// 开启线程池 
void ThreadPool::start(size_t initThreadSize)
{
    isPoolRunning_ = true;  // 线程池的运行状态 

    // 记录初始线程数量
    initThreadSize_ = initThreadSize;
    curThreadSize_ = initThreadSize;

    // 创建线程对象
    for (size_t i = 0; i < initThreadSize_; i++)
    {
        // 创建 thread 线程对象时，把线程函数给 thread 线程对象
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
        size_t threadId = ptr->getId();
        threads_.emplace(threadId, std::move(ptr));
    }

    // 启动所有线程
    for (size_t i = 0; i < initThreadSize_; i++)
    {
        threads_[i]->start();  // 执行线程函数
        idleThreadSize_++;     // 空闲线程的数量
    }
}

// 定义线程函数
// 线程池中的所有线程从任务队列里消费任务
void ThreadPool::threadFunc(size_t threadId)
{
    auto lastTime = std::chrono::high_resolution_clock().now();

    // 不断地从任务队列取任务
    while (isPoolRunning_)
    {
        std::shared_ptr<Task> task;

        {
            // 获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx_);

            std::cout << "tid: " << std::this_thread::get_id() << "尝试获取任务..." << std::endl;
            
            while (!isPoolRunning_ && taskQue_.size() == 0)
            {
                // cached 模式下，可能创建了很多线程，当它们的空闲时间超过 60s 就回收
                if (poolMode_ == PoolMode::MODE_CACHED)
                {
                    // 每 1s 返回一次，需要区分：超时返回和有任务待执行返回
                    // 超时返回
                    if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
                    {
                        auto now = std::chrono::high_resolution_clock().now();
                        auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                        if (dur.count() >= THREAD_MAX_IDLE_TIME && curThreadSize_ > initThreadSize_)
                        {
                            // 回收当前线程
                            // 线程对象从线程列表中删除 threadfunc 绑定哪一个 -> thread 对象? 
                            // 线程 id -> 线程对象 -> 删除线程, 通过 map 将线程 id 和 线程对象绑定
                            threads_.erase(threadId);
                            curThreadSize_--;
                            idleThreadSize_--;
                            std::cout << "thread Id: " << std::this_thread::get_id() << "  >>> exit!" << std::endl; 
                            return;
                        }
                    }
                } 
                else
                {
                    // 等待 notEmpty_ 条件
                    notEmpty_.wait(lock);
                }
            }
            if (!isPoolRunning_)  // 如果是线程池要析构
            {
                break;
            }
            
            idleThreadSize_--;  // 空闲线程数量 -1

            std::cout << "tid: " << std::this_thread::get_id() << "获取任务成功..." << std::endl;

            // 从任务队列取任务
            task = taskQue_.front();
            taskQue_.pop();
            taskSize_--;
            
            // 如果还有剩余任务，继续通知其他线程执行任务
            if (taskQue_.size() > 0)
            {
                notEmpty_.notify_all();
            }

            // 取出一个任务，进行通知，可以继续提交任务
            notFull_.notify_all();

        }  // 局部对象出了作用域会自动析构，从而释放锁
        
        // 当前线程负责执行这个任务
        if (task != nullptr)
        {
            // 1. 执行任务 // 2. 任务返回值通过 setVal 给 Result
            task->exec();  
        }

        idleThreadSize_++;  // 处理完
        lastTime = std::chrono::high_resolution_clock().now();  // 更新线程执行完任务的时间
    }

    threads_.erase(threadId);
    std::cout << "thread Id: " << std::this_thread::get_id() << "  >>> exit!" << std::endl; 
    exitCond_.notify_all();
}

bool ThreadPool::checkRunningState() const
{
    return isPoolRunning_;
}
//******************************************************** 线程方法实现 ***************************************************

Thread::Thread(ThreadFunc func) : func_(func), threadId_(genetateId_++)
{}

Thread::~Thread()
{}

size_t Thread::genetateId_ = 0;

size_t Thread::getId() const
{
    return threadId_;
}

// 启动线程
void Thread::start()
{
    // 执行线程函数，由线程池来指定 
    // 并且，线程函数所要访问的变量也都在线程池中
    // 因此，需要将线程函数定义到线程池中，而不是线程类中

    // 创建一个线程来执行线程函数
    std::thread t(func_, threadId_);  // C++11，线程对象 t，和线程函数 func_
    // 退出作用域后，局部对象 t 析构，但是线程函数还在
    
    // 将线程对象设置为分离线程，这样线程对象和执行函数互不相关，析构线程对象不影响线程函数
    t.detach();  // linux pthread_detach

}

// ******************************************************* Task ***************************************************
Task::Task() : result_(nullptr)
{}

void Task::exec()
{
    if (result_ != nullptr)
        result_->setAny(run());  // 这里发生多态调用
}

void Task::setResult(Result* res)
{
    result_ = res;
}

// ****************************************************** Result 实现 ***********************************************
Result::Result(std::shared_ptr<Task> task, bool isValid) : isValid_(isValid), task_(task)
{
    task_->setResult(this);
}

Any Result::get()
{
    if (!isValid_)
    {
        return "";
    }
    sem_.wait();  // task 如果没有执行完，这里会阻塞用户的线程
    return std::move(any_);
}

// Task 执行完毕后, 在 exec 中调用此方法 设置 Any
// Any 设置完毕后，通过信号量 通知 Result::get() 中阻塞的线程获取返回结果
void Result::setAny(Any any)  
{
    // 存储 task 的返回值
    this->any_ = std::move(any);

    sem_.post();  // 已经获取的任务的返回值，增加信号量资源
}
