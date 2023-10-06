# ThreadPool
===================

# base version
1. 基于继承和模板，实现任意类型 Any。
2. 基于 mutex 和 condition_variable，实现 semaphore。
3. 实现 任务抽象基类 Task, 利用多态实现 其 run() 方法。
4. 基于 Any 和 semaphore，实现 Result 对 任务进行打包，返回线程执行任务的结果。

# final version
1. 利用标准库 std::packaged_task<> 对任务进行打包。
2. 利用标准库 std::future<> 获取任务执行结果。
3. 利用可变参模板模板编程，简化任务的封装与执行。