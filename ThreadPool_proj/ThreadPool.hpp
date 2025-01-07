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
	//���캯��
	ThreadPool(int threadnums);

	//��������������
	template<typename F,typename ...Arg>//F�ǷŽ����ĺ������ͣ�...Argָ���Ǻ���������в���
	//F&& f,Arg&&... arg���������ã�������ֵʱ�����ֵ���ã�������ֵʱ������ֵ���õ�״̬
	//->�����ͺ��õ���˼�����԰Ѻ�������ֵ����
	auto enques(F&& f, Arg&&... arg)->std::future<typename std::result_of<F(Arg...)>::type>;

	//��������
	~ThreadPool();

private:
	void worker();//�̵߳�ִ������
	bool isstop;//��ʶ����ǰ�̳߳��ǲ���ֹͣ
	std::condition_variable cv;//�������������������Ϊ�գ���Ҫ�����߳�����ִ��
	std::mutex mymutex;
	std::vector<std::thread> threads;//�̼߳��ϣ��̳߳أ�
	//������У�ʹ��packaged_task��װ����������void���ͣ�ʹ��future���÷���ֵ��
	std::queue<std::function<void()>> myque;
};

//���캯��
ThreadPool::ThreadPool(int threadnums) :isstop(false) {
	for (size_t i = 0; i < threadnums; i++) {
		threads.emplace_back([this]() {
			this->worker();
			});
	}
}

//��������
ThreadPool::~ThreadPool() {
	//����ֹͣ��ʶ
	{
		std::unique_lock<std::mutex>lock(mymutex);
		isstop = true;
	}

	//֪ͨ�������������е��߳�
	cv.notify_all();

	//ȷ���߳�ִ�����
	for (std::thread& onethread : threads) {
		onethread.join();//join();���̼߳��뵽���߳��У������е��̻߳�û������ɾͱ�������
	}
}

//�������
template<typename F, typename ...Arg>
auto ThreadPool::enques(F&& f, Arg&&... arg)->std::future<typename std::result_of<F(Arg...)>::type> {
	//���fִ�к������
	using functype = typename std::result_of<F(Arg...)>::type;

	//���һ������ָ�룬ָ��һ������װΪfunctype()��packaged_task
	//bind��f��arg��Ϊһ���޲κ���Ҳ����functype����
	//forward������ת�������ı�ԭ��������ͣ���ֵ/��ֵ��
	auto task = std::make_shared<std::packaged_task<functype()>>(
		std::bind(std::forward<F>(f),std::forward<Arg>(arg)...)
		);

	//���future
	std::future<functype> rsfuture = task->get_future();

	//��������ӵ�����
	{
		std::lock_guard<std::mutex> lockguard(this->mymutex);
		if (isstop) {
			throw std::runtime_error("�����̳߳��Ѿ�ֹͣ�ˣ�");
		}
		//��������ӵ�����
		myque.emplace([task]() {
			(*task)();
			});
	}

	//֪ͨ�߳�ȥִ������
	cv.notify_one();
	
	//����future
	return rsfuture;
}

//ÿ������Ĺ�������
void ThreadPool::worker() {
	while (true) {

		//��������
		std::function<void()> task;

		//�Ӷ���������һ������
		{
			std::unique_lock<std::mutex> lock(mymutex);

			//��������������ǰ�̳߳�ֹͣ�����������Ϊ��ʱ�����̴߳��ڵȴ�״̬
			cv.wait(lock, [this] {return this->isstop || !this->myque.empty(); });

			if (isstop && myque.empty()) return;
			//��������ж��׵����񸳸�task�������䵯��
			task = std::move(this->myque.front());
			this->myque.pop();
		}
		task();
	}
}