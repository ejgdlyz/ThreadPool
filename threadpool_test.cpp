#include <iostream>
#include <chrono>
#include <thread>

using namespace std;

#include "threadpool.h"

class MyTask : public Task
{
public:
    MyTask()
    {}
    ~MyTask()
    {}
    // run 的返回值如何设计能够接收任意的类型
    void run()
    {
        std::cout << "tid: " << std::this_thread::get_id() << " begin!" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::cout << "tid: " << std::this_thread::get_id() << " end!" << std::endl;
    }
};

int main()
{
    ThreadPool pool;
    pool.start(4);  // 默认启动 4 个线程，每个线程创建后都会执行 threadFunc 这个函数
    
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());
    pool.submitTask(std::make_shared<MyTask>());

    getchar();

    return 0;
}