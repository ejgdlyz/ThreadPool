#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>

// Any 类型， 可以接收任意数据
class Any
{
public: 
    Any() = default;
    ~Any() = default;
    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;
    Any(Any&&) = default;
    Any& operator=(Any&&) =default;

    // 让Any接收任意其他的数据
    template <class T>
    Any(T data) : base_(std::make_unique<Derived<T>>(data))
    {}

    // 提取 Any 对象中存储的 数据, 返回值自己指定 模板
    template <class T>
    T cast_()
    {
        // 从 base_ 中找出派生类，从而取出成员变量 data
        // 基类指针 转 派生类指针  (基类指针确实指向派生类对象才能向下转型) RTTI 
        Derived<T>* pd = dynamic_cast<Derived<T>*> (base_.get());  //  
        if (pd == nullptr)
        {
            throw "type is unmatch!";
        }
        return pd->data_;

    }
private:
    // 基类类型
    class Base
    {
        public:
        virtual ~Base() = default;  // 堆上创建的派生类对象，父类的析构函数用虚析构函数
    };
    
    // 派生类   
    template<class T>
    class Derived : public Base
    {
        public:
            Derived(T data): data_(data)
            {}
            T data_;  // 保存了任意的其他类型
    };
    std::unique_ptr<Base> base_;
};

// 信号量实现
class Semaphore
{
public:
    Semaphore(int limit = 0) : resLimit_(limit)
    {}
    ~Semaphore() = default;
    
    // 获取一个信号量资源
    void wait()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        
        // 等待信号量有资源，否则，阻塞当前线程
        cond_.wait(lock, [&]()-> bool {return resLimit_ > 0;});
        resLimit_--;

    }

    // 增加一个信号量资源
    void post()
    {
        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_++;
        cond_.notify_all();
    }
private:
    int resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;

};

class Task;  // Task 的前置声明
// 实现接收 提交到线程池的 Task 执行完成后的返回值类型 Result
class Result
{
public:
    Result(std::shared_ptr<Task> task, bool isValid = true);
    ~Result() = default;

    void setAny(Any any);

    // 用户调用 get 方法获取 task 的返回值
    Any get();

private:
    Any any_;  // 存储任务的返回值
    Semaphore sem_;  // 线程通信 信号量
    std::shared_ptr<Task> task_;  // 指向对应的 获取返回值的任务对象
    std::atomic_bool isValid_;  // 返回值是否有效
};

// 任务抽象基类
class Task
{
public:
    Task();
    ~Task() = default;
    void exec();
    void setResult(Result* res);

    // 用户自定义任务类型，从 Task 继承，并重写 run()，实现自定义任务处理
    virtual Any run() = 0;

private:
    Result* result_;  // Result 的生命周期 > Task
    
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
    Result submitTask(std::shared_ptr<Task> sp);

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