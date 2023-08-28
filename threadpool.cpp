#include <iostream>
#include <functional>
#include <thread>

#include "threadpool.h"

const size_t TASK_MAX_THRESHOLD = 4;

ThreadPool::ThreadPool()
    : initThreadSize_(0),  // 初始化为 CPU 核的数量
    taskSize_(0),
    taskQueMaxThreshold_(TASK_MAX_THRESHOLD),
    poolMode_(PoolMode::MODE_FIXED)
{}

ThreadPool::~ThreadPool()
{}

// 设置线程池的工作模式
void ThreadPool::setMode(PoolMode mode)
{
    poolMode_ = mode;
}

// 设置 Task 任务队列阈值
void ThreadPool::setTaskQueMaxThreshold(size_t threshold)
{
    taskQueMaxThreshold_ = threshold;
}

// 给线程池提交任务
// 用户调用该接口，传入任务对象，生产任务
Result ThreadPool::submitTask(std::shared_ptr<Task> sp)
{
    // 获取锁
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    
    // 线程通信  等待任务队列有空余
    // 用户提交任务，最长不能阻塞 1s，否则提交任务失败，返回 
    // // 条件满足返回 或 持续1s返回
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

    // 返回任务的 Result 对象
    return Result(sp);
}

// 开启线程池 
void ThreadPool::start(size_t initThreadSize)
{
    // 记录初始线程数量
    initThreadSize_ = initThreadSize;
    
    // 创建线程对象
    for (size_t i = 0; i < initThreadSize_; i++)
    {
        // 创建 thread 线程对象时，把线程函数给 thread 线程对象
        auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
        threads_.emplace_back(std::move(ptr));
    }

    // 启动所有线程
    for (size_t i = 0; i < initThreadSize_; i++)
    {
        threads_[i]->start();  // 执行线程函数
    }
}

// 定义线程函数
// 线程池中的所有线程从任务队列里消费任务
void ThreadPool::threadFunc()
{
    // std::cout << "begin threadFunc tid: " << std::this_thread::get_id() << std::endl;
    // std::cout << "end threadFunc tid: " << std::this_thread::get_id() << std::endl;
    
    // 不断地从任务队列取任务
    for (;;)
    {
        std::shared_ptr<Task> task;

        {
            // 获取锁
            std::unique_lock<std::mutex> lock(taskQueMtx_);

            std::cout << "tid: " << std::this_thread::get_id() << "尝试获取任务..." << std::endl;
            
            // 等待 notEmpty_ 条件
            notEmpty_.wait(lock, [&](){return taskQue_.size() > 0;});

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
            // task->run();  // 1. 执行任务 // 2. 任务返回值通过 setVal 给 Result
            task->exec();
        }
    }
}

//********************** 线程方法实现 *****************

Thread::Thread(ThreadFunc func) : func_(func)
{}

Thread::~Thread()
{}

// 启动线程
void Thread::start()
{
    // 执行线程函数，由线程池来指定 
    // 并且，线程函数所要访问的变量也都在线程池中
    // 因此，需要将线程函数定义到线程池中，而不是线程类中

    // 创建一个线程来执行线程函数
    std::thread t(func_);  // C++11，线程对象 t，和线程函数 func_
    // 退出作用域后，局部对象 t 析构，但是线程函数还在
    
    // 将线程对象设置为分离线程，这样线程对象和执行函数互不相关，析构线程对象不影响线程函数
    t.detach();  // linux pthread_detach

}

// *************************** Task *******************************
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

// ********************************* Result 实现 *********************************
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

void Result::setAny(Any any)
{
    // 存储 task 的返回值
    this->any_ = std::move(any);

    sem_.post();  // 已经获取的任务的返回值，增加信号量资源
}
