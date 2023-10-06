
#include "threadpool.h"
#include <chrono>

using namespace std;

int sum1(int a, int b)
{
    this_thread::sleep_for(chrono::seconds(2));
    return a + b;
}

int sum2(int a, int b, int c)
{
    this_thread::sleep_for(chrono::seconds(2));
    return a + b + c;
}

int main()
{
    ThreadPool pool;
    // pool.setMode(PoolMode::MODE_CACHED);
    pool.start(2);
    std::future<int> res1 = pool.submitTask(sum1, 1, 2);
    std::future<int> res2 = pool.submitTask(sum2, 1, 2, 3);
    std::future<int> res3 = pool.submitTask([](int start, int end) -> int {
        int sum = 0;
        for (int i = start; i <= end; ++i)
        {
            sum += i;
        }
        return sum;
    }, 1, 100);
    std::future<int> res4 = pool.submitTask([](int start, int end) -> int {
        int sum = 0;
        for (int i = start; i <= end; ++i)
        {
            sum += i;
        }
        return sum;
    }, 1, 100);
    std::future<int> res5 = pool.submitTask([](int start, int end) -> int {
        int sum = 0;
        for (int i = start; i <= end; ++i)
        {
            sum += i;
        }
        return sum;
    }, 1, 100);


    std::cout << res1.get() << std::endl;
    std::cout << res2.get() << std::endl;
    std::cout << res3.get() << std::endl;
    std::cout << res4.get() << std::endl;
    std::cout << res5.get() << std::endl;


    return 0;
}