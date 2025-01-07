#pragma once
#include<thread>
#include<mutex>
#include<future>
#include<condition_variable>
#include<functional>
#include<queue>

//ThreadPool
class  ThreadPool {
public:
	//构造函数
	ThreadPool(int threadnums);

	//添加任务到任务队列
	template<typename F,typename ...Arg>//F是放进来的函数类型，...Arg指的是函数里的所有参数
	//F&& f,Arg&&... arg是万能引用，传递左值时变成左值引用，传递右值时保持右值引用的状态
	//->是类型后置的意思，可以把函数返回值后置
	auto enques(F&& f, Arg&&... arg)->std::future<typename std::result_of<F(Arg...)>::type>;

	//析构函数
	~ThreadPool();

private:
	void worker();//线程的执行内容
	bool isstop;//标识，当前线程池是不是停止
	std::condition_variable cv;//条件变量，若任务队列为空，需要唤醒线程让其执行
	std::mutex mymutex;
	std::vector<std::thread> threads;//线程集合（线程池）
	//任务队列（使用packaged_task封装函数，返回void类型，使用future调用返回值）
	std::queue<std::function<void()>> myque;
};

//构造函数
ThreadPool::ThreadPool(int threadnums) :isstop(false) {
	for (size_t i = 0; i < threadnums; i++) {
		threads.emplace_back([this]() {
			this->worker();
			});
	}
}

//析构函数
ThreadPool::~ThreadPool() {
	//更改停止标识
	{
		std::unique_lock<std::mutex>lock(mymutex);
		isstop = true;
	}

	//通知唤醒所有阻塞中的线程
	cv.notify_all();

	//确保线程执行完成
	for (std::thread& onethread : threads) {
		onethread.join();//join();将线程加入到主线程中，以免有的线程还没运行完成就被析构了
	}
}

//添加任务
template<typename F, typename ...Arg>
auto ThreadPool::enques(F&& f, Arg&&... arg)->std::future<typename std::result_of<F(Arg...)>::type> {
	//获得f执行后的类型
	using functype = typename std::result_of<F(Arg...)>::type;

	//获得一个智能指针，指向一个被包装为functype()的packaged_task
	//bind将f和arg绑定为一个无参函数也就是functype（）
	//forward是完美转发，不改变原对象的类型（左值/右值）
	auto task = std::make_shared<std::packaged_task<functype()>>(
		std::bind(std::forward<F>(f),std::forward<Arg>(arg)...)
		);

	//获得future
	std::future<functype> rsfuture = task->get_future();

	//将任务添加到队列
	{
		std::lock_guard<std::mutex> lockguard(this->mymutex);
		if (isstop) {
			throw std::runtime_error("出错：线程池已经停止了！");
		}
		//将任务添加到队列
		myque.emplace([task]() {
			(*task)();
			});
	}

	//通知线程去执行任务
	cv.notify_one();
	
	//返回future
	return rsfuture;
}

//每个具体的工作任务
void ThreadPool::worker() {
	while (true) {

		//定义任务
		std::function<void()> task;

		//从队列中区的一个任务
		{
			std::unique_lock<std::mutex> lock(mymutex);

			//条件变量：若当前线程池停止或者任务队列为空时，让线程处于等待状态
			cv.wait(lock, [this] {return this->isstop || !this->myque.empty(); });

			if (isstop && myque.empty()) return;
			//将任务队列队首的任务赋给task，并将其弹出
			task = std::move(this->myque.front());
			this->myque.pop();
		}
		task();
	}
}