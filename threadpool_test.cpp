#include <iostream>
#include <chrono>
#include <thread>

using namespace std;

#include "threadpool.h"

/*
    有些场景希望能获取任务的返回值
    例：
        1 + 2 + ... 30000 的和
        thread1 sum([1, 10000])
        thread2 sum([10001, 20000])
        thread3 sum([20001, 30000])
        main thread 给每个线程分配计算的区间，并等待他们的返回结果，合并这些结果即可
*/

using ULong = unsigned long long;

class MyTask : public Task
{
public:
    MyTask(ULong begin, ULong end): begin_(begin), end_(end)
    {}
    ~MyTask()
    {}
    Any run()
    {
        std::cout << "tid: " << std::this_thread::get_id() << " begin!" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        ULong sum = 0;
        for (ULong i = begin_; i <= end_; i++)
        {
            sum += i;
        }
        std::cout << "tid: " << std::this_thread::get_id() << " end!" << std::endl;
        
        return sum;  
    }
private:
    ULong begin_;
    ULong end_;
};

int main()
{
    {

        ThreadPool pool;
        pool.start(4);  
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        ULong  sum1 = res1.get().cast_<ULong>(); 
        
        cout << sum1 << endl;
    }
    cout << "main over!" << endl;

#if 0
    {
        ThreadPool pool;
        pool.setMode(PoolMode::MODE_CACHED);  // 设置线程池模式
        pool.start(4);  // 默认启动 4 个线程，每个线程创建后都会执行 threadFunc 这个函数
        
        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        Result res4 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

        Result res5 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        Result res6 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

        ULong  sum1 = res1.get().cast_<ULong>(); 
        ULong  sum2 = res2.get().cast_<ULong>(); 
        ULong  sum3 = res3.get().cast_<ULong>(); 
        ULong  sum4 = res4.get().cast_<ULong>(); 

        ULong  sum5 = res5.get().cast_<ULong>(); 
        ULong  sum6 = res6.get().cast_<ULong>(); 


        // Master - SLave 模型
        // Master 线程用来分解任务，然后给各个 Slave 线程分配任务
        // 等待各个 Slave 线程执行完任务，返回结果
        // Master 线程合并各个任务结果，输出 
        cout << (sum1 + sum2 + sum3) << endl;
    }
    
    getchar();
#endif

    return 0;
}