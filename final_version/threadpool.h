#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <iostream>
#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <thread>
#include <future>

const size_t TASK_MAX_THRESHOLD = 1024; // 2; 
const size_t THREAD_MAX_THRESHOLD = 10;
const size_t THREAD_MAX_IDLE_TIME = 10;  // cached 模式下，线程空闲 60s, 回收

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
    using ThreadFunc = std::function<void(size_t)>;

    Thread(ThreadFunc func) : func_(func), threadId_(generateId_++)
    {}

    ~Thread() = default;

    // 启动线程
    void start()
    {
        
        std::thread t(func_, threadId_);  // C++11，线程对象 t，和线程函数 func_
        
        t.detach();  // linux pthread_detach
    }

    // 获取线程 id
    size_t getId() const
    {
        return threadId_;
    }

private:
    ThreadFunc func_;  // 函数对象
    static size_t generateId_;
    size_t threadId_;  // 保存线程 id
};

size_t Thread::generateId_ = 0;

// 线程池类型 
class ThreadPool
{
public:
    ThreadPool(): 
        initThreadSize_(0),  // 初始化为 CPU 核的数量
        taskSize_(0),
        threadSizeThresHold_(THREAD_MAX_THRESHOLD), 
        curThreadSize_(0),
        taskQueMaxThreshold_(TASK_MAX_THRESHOLD),
        poolMode_(PoolMode::MODE_FIXED), 
        isPoolRunning_(false), 
        idleThreadSize_(0)
    {}
    ~ThreadPool()
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
    void setMode(PoolMode mode)
    {
        if (checkRunningState())
            return;
        
        poolMode_ = mode;
    }

    // 设置 Task 任务队列阈值
    void setTaskQueMaxThreshold(size_t threshold)
    {
        if (checkRunningState())
            return;
        taskQueMaxThreshold_ = threshold;
    }

    // cached 模型是 线程数量阈值
    void setThreadSizeThreshold(size_t threshold)
    {
        if (checkRunningState())
            return;
        if (poolMode_ == PoolMode::MODE_CACHED)
        {
            threadSizeThresHold_ = threshold;
        }
    }

    // 给线程池提交任务
    // 使用可变参模板编程
    template<typename Func, typename... Args>
    auto submitTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))>
    {
        // 打包任务，放入任务队列中
        using RType = decltype(func(args...));

        // RType() 的参数不用写，直接使用绑定器将参数绑定到函数对象上
        auto task = std::make_shared<std::packaged_task<RType()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...));

        std::future<RType> result = task->get_future();

        // 获取锁
        std::unique_lock<std::mutex> lock(taskQueMtx_);
        
        // 线程通信  等待任务队列有空余
        if (!notFull_.wait_for(lock, std::chrono::seconds(1), 
            [&]()->bool {return taskQue_.size() < taskQueMaxThreshold_;}))
        {
            // 等待 1s 返回，失败 
            std::cerr << "task queue is full, fail to submit task!" << std::endl;
            
            // 失败 返回一个 类型空值
            auto task = std::make_shared<std::packaged_task<RType()>>(
                []() -> RType {return RType();});
            (*task)();  // 执行一次任务， 不然只是打包
            return task->get_future();

        } 
        
        // 如果有空余，把任务放入任务队列中
        taskQue_.emplace([task]() {(*task)();});
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

        return result;

    }

    // 开启线程池 
    void start(size_t initThreadSize = std::thread::hardware_concurrency())
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

    // 禁止线程池的拷贝构造和拷贝赋值
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    // 定义线程函数
    void threadFunc(size_t threadId)  // ThreadPool 对象绑定到 此函数的 this 指针
    {
        auto lastTime = std::chrono::high_resolution_clock().now();

        // 不断地从任务队列取任务
        for(;;)
        {
            Task task;
            {
                // 获取锁
                std::unique_lock<std::mutex> lock(taskQueMtx_);

                std::cout << "tid: " << std::this_thread::get_id() << "尝试获取任务..." << std::endl;
                
                while (taskQue_.size() == 0)
                {
                    // 等待全部任务执行完毕，再回收线程
                    if (!isPoolRunning_)
                    {
                        threads_.erase(threadId);
                        std::cout << "thread Id: " << std::this_thread::get_id() << "  >>> exit!" << std::endl; 
                        exitCond_.notify_all();
                        return;
                    }
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
                
                idleThreadSize_--;  // 空闲线程数量 -1

                std::cout << "tid: " << std::this_thread::get_id() << "获取任务成功..." << std::endl;

                // 从任务队列取任务
                // task = taskQue_.front();
                task = std::move(taskQue_.front());
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
                task();  // 执行 function<void()>
            }

            idleThreadSize_++;  // 处理完
            lastTime = std::chrono::high_resolution_clock().now();  // 更新线程执行完任务的时间
        }
    }

    // 检查 pool 的运行状态
    bool checkRunningState() const
    {
        return isPoolRunning_;
    }

private:
    // 线程
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;   // 线程列表, 不存副本
    size_t initThreadSize_;                                      // 初始线程数量
    size_t threadSizeThresHold_;                                 // 阈值 防止 cached 模式增长过大 
    std::atomic_int curThreadSize_;                              // 当前线程池中的线程总量
    std::atomic_int idleThreadSize_;                             // 空闲线程的数量


    // Task 任务 -> 函数对象
    // 函数对象的类型是什么？不同的任务函数返回值不同，参数列表也不同。
    // 参数可以通过绑定器和任务函数绑定在一起。但是，函数的返回值无法预估。
    // using Task = std::function<返回值()>;  () 里可以不写参数，因为可以通过绑定器将参数和函数对象绑定
    using Task = std::function<void()>;
    std:: queue<Task>  taskQue_;                    // 任务队列
    std::atomic_uint taskSize_;                     // 任务数量 
    size_t taskQueMaxThreshold_;                    // 任务队列上限阈值  

    // 线程通信
    std::mutex taskQueMtx_;             // 保证任务队列的线程安全
    std::condition_variable notFull_;   // 任务队列不满
    std::condition_variable notEmpty_;  // 任务队列不空
    std::condition_variable exitCond_;  // 等待线程资源回收

    PoolMode poolMode_;                 // 当前线程池的工作模式
    std::atomic_bool isPoolRunning_;    // 当前线程池的启动状态

};

#endif