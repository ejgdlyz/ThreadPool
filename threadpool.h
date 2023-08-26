#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>

// 任务抽象基类
class Task
{
public:
    // 用户自定义任务类型，从 Task 继承，并重写 run()，实现自定义任务处理
    virtual void run() = 0;
};

// 线程池支持两种模式
enum class PoolMode
{
    MODE_FIXED,  // 固定数量的线程 
    MODE_CACHED  // 线程数量可动态增长
};

// 线程类型
class Thread
{
public:
    // 线程函数对象类型
    using ThreadFunc = std::function<void()>;

    Thread(ThreadFunc func);
    ~Thread();

    // 启动线程
    void start();
private:
    ThreadFunc func_;  // 函数对象
};

/*
example:
ThreadPool pool;
pool.start(4);

class MyTask : public Task
{
public:
    void run() {  // 线程代码...}    
};

pool.submitTask(std::make_shared<MyTask> )
*/

// 线程池类型 
class ThreadPool
{
public:
    ThreadPool();
    ~ThreadPool();

    // 设置线程池的工作模式
    void setMode(PoolMode mode);

    // 设置 Task 任务队列阈值
    void setTaskQueMaxThreshold(size_t threshold);

    // 给线程池提交任务
    void submitTask(std::shared_ptr<Task> sp);

    // 开启线程池 
    void start(size_t initThreadSize = 4);

    // 禁止线程池的拷贝构造和拷贝赋值
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    // 定义线程函数
    void threadFunc();  // ThreadPool 对象绑定到 此函数的 this 指针

private:
    // 线程
    std::vector<std::unique_ptr<Thread>> threads_;  // 线程列表, 不存副本
    size_t initThreadSize_;  // 初始线程数量
    // 阈值 防止 cached 模式增长过大 

    // 任务
    std:: queue<std::shared_ptr<Task>>  taskQue_; // 任务队列
    std::atomic_uint taskSize_;  // 任务数量 
    size_t taskQueMaxThreshold_;  // 任务队列上限阈值  

    // 线程通信
    std::mutex taskQueMtx_;  // 保证任务队列的线程安全
    std::condition_variable notFull_;  // 任务队列不满
    std::condition_variable notEmpty_;  // 任务队列不空

    PoolMode poolMode_;  // 当前线程池的工作模式
};



#endif