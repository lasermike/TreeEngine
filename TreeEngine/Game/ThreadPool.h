#ifndef THREAD_POOL_H
#define THREAD_POOL_H

// From: https://github.com/progschj/ThreadPool

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

struct WorkData
{
	UINT workType;
	UINT start;
	UINT end;
	void* param1;
};

class ThreadPool {
public:
    ThreadPool(size_t);
    template<class F>
    auto enqueue(F&& f, WorkData pWork) 
        -> std::future<typename std::result_of<F(WorkData)>::type>;
	void WaitTilDone();

    //template<class F, class... Args>
    //auto enqueue(F&& f, Args&&... args) 
    //    -> std::future<typename std::result_of<F(Args...)>::type>;
    ~ThreadPool();

	UINT GetNumWorkers() { return numWorkers; }

private:
    // need to keep track of threads so we can join them
    std::vector< std::thread > workers;
    // the task queue
    std::queue< std::function<void()> > tasks;
    
    // synchronization
    std::mutex queue_mutex, workingThreads_mutex;
    std::condition_variable workAvailableCondition, workersIdleCondition;
    bool stop;
	UINT numWorkers;
	UINT workingThreads;
};
 
// the constructor just launches some amount of workers
inline ThreadPool::ThreadPool(size_t threads)
    :   stop(false), numWorkers((UINT) threads), workingThreads(0)
{
    for(size_t i = 0;i<threads;++i)
        workers.emplace_back(
            [this]
            {
                for(;;)
                {
                    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"Thread pool lock");

                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->workAvailableCondition.wait(lock,
                            [this]{ return this->stop || !this->tasks.empty(); });
                        if(this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    {
                        std::unique_lock<std::mutex> lock(this->workingThreads_mutex);
                        this->workingThreads++;
                    }

                    PIXEndEvent();
                    PIXBeginEvent(TREE_COLOR_DRAW_TEXT, L"Thread pool task");

                    task();

                    {
                        std::unique_lock<std::mutex> lock(this->workingThreads_mutex);
                        this->workingThreads--;
                        workersIdleCondition.notify_one();
                    }

                    PIXEndEvent();
                }
            }
        );
}

// add new work item to the pool
//template<class F, class... Args>
//auto ThreadPool::enqueue(F&& f, Args&&... args) 
//    -> std::future<typename std::result_of<F(Args...)>::type>
template<class F>
auto ThreadPool::enqueue(F&& f, WorkData workData) 
    -> std::future<typename std::result_of<F(WorkData)>::type>
{
    //using return_type = typename std::result_of<F(WorkData)>::type;

    auto task = std::make_shared< std::packaged_task<std::result_of<F(WorkData)>::type()> >(
            std::bind(std::forward<F>(f), std::forward<WorkData>(workData))
        );
        
    std::future<std::result_of<F(WorkData)>::type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex);

        // don't allow enqueueing after stopping the pool
        if(stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks.emplace([task](){ (*task)(); });
    }
    workAvailableCondition.notify_one();
    return res;
}

// the destructor joins all threads
inline ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    workAvailableCondition.notify_all();
    for(std::thread &worker: workers)
        worker.join();
}

inline void ThreadPool::WaitTilDone()
{
    std::unique_lock<std::mutex> lock(this->workingThreads_mutex);
	this->workersIdleCondition.wait(lock,
		[this]
		{ 
			return this->tasks.empty() && this->workingThreads == 0; 
		});
}

#endif

