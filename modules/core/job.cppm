export module lumina.core.job;

import lumina.core.log;
import std;

using std::size_t, std::uint32_t, std::uint64_t;


namespace lumina::job
{

constexpr size_t cache_line_size = 64u;
constexpr size_t job_padding_size = cache_line_size - sizeof(std::function<void()>) - sizeof(void*) - sizeof(uint32_t) - sizeof(bool); 

export struct __attribute__((packed)) job_t
{
	std::function<void()> func{};
	job_t* parent{nullptr};
	std::atomic<uint32_t> jobs_running{0u};
	std::atomic<bool> finished{true};
	std::byte padding[job_padding_size];
};

thread_local uint32_t current_thread_id = ~0u;

export uint32_t get_thread_id()
{
	return current_thread_id;
}

constexpr size_t max_concurrent_jobs = 16384;
thread_local job_t* g_jobAllocator;
thread_local uint64_t g_allocCounter = 0u;

job_t* allocate_job()
{
	job_t* j = &g_jobAllocator[g_allocCounter & (max_concurrent_jobs - 1zu)];

	++g_allocCounter;
	return j;
}

struct ThreadInfo
{
	std::thread thread;
	std::string name;

	std::condition_variable work_available;
	std::condition_variable work_done;

	size_t jobs_running{0u};
	job_t* active_job{nullptr};

	std::mutex jobs_mutex;
	std::queue<job_t*> jobs;
};

struct JobSystemContext
{
	std::atomic<bool> waiting{false};
	std::atomic<bool> running{false};

	std::condition_variable work_available;
	std::condition_variable work_done;

	size_t jobs_running{0u};
	std::mutex jobs_mutex;
	std::queue<job_t*> jobs;

	uint32_t num_threads{0u};
	std::unique_ptr<ThreadInfo[]> threads{nullptr};
};
JobSystemContext* ctx;

void worker(uint32_t id)
{
	job_t* j;

	current_thread_id = id;

	auto& this_thread = ctx->threads[id];
	g_jobAllocator = new job_t[max_concurrent_jobs];

	for(;;)
	{
		std::unique_lock<std::mutex> lock{ctx->jobs_mutex};
		ctx->work_available.wait(lock, []
		{
			return !ctx->jobs.empty() || !ctx->running;
		});

		if(!ctx->running)
			break;

		j = ctx->jobs.front();
		ctx->jobs.pop();

		++ctx->jobs_running;
		if(!this_thread.active_job)
			this_thread.active_job = j;

		lock.unlock();
		j->func();
		lock.lock();

		--j->jobs_running;

		if(!j->jobs_running)
		{
			j->finished = true;
			std::atomic_notify_all(&j->finished);
		}

		if(j->parent)
		{
			--j->parent->jobs_running;
			if(!j->parent->jobs_running)
			{
				j->parent->finished = true;
				std::atomic_notify_all(&j->parent->finished);
			}
			j->parent = nullptr;
		}

		if(this_thread.active_job == j)
			this_thread.active_job = nullptr;

		--ctx->jobs_running;
		if(ctx->waiting && !ctx->jobs_running && ctx->jobs.empty())
			ctx->work_done.notify_all();
	}

	delete[] g_jobAllocator;
}

}

export namespace lumina::job
{

void init(uint32_t concurrency = 0)
{
	ctx = new JobSystemContext();

	if(concurrency == 0)
		concurrency = std::thread::hardware_concurrency();

	log::info("job_system: running on {} threads", concurrency);
	log::info("job_system: cache line {} bytes, expanding job_t by {} bytes", cache_line_size, job_padding_size);
       	ctx->num_threads = concurrency - 1;
	ctx->threads = std::make_unique_for_overwrite<ThreadInfo[]>(ctx->num_threads);	

	current_thread_id = 0;
	g_jobAllocator = new job_t[max_concurrent_jobs];

	ctx->running = true;

	for(uint32_t i = 0; i < ctx->num_threads; ++i)
	{
		ctx->threads[i].name = "worker" + std::to_string(i + 1);
		ctx->threads[i].thread = std::thread(worker, i);
	}
}

void wait_for_work()
{
	std::unique_lock<std::mutex> lock{ctx->jobs_mutex};

	ctx->waiting = true;
	ctx->work_done.wait(lock, []
	{
		return !ctx->jobs_running && ctx->jobs.empty();
	});
	ctx->waiting = false;
}

void shutdown()
{
	wait_for_work();
	ctx->running = false;
	ctx->work_available.notify_all();

	for(uint32_t i = 0; i < ctx->num_threads; i++)
		ctx->threads[i].thread.join();

	delete[] g_jobAllocator;
	delete ctx;
}

void wait(job_t* j)
{
	std::atomic_wait(&j->finished, false);
}

void wait(const std::vector<job_t*>& jobs)
{
	for(auto job : jobs)
		std::atomic_wait(&job->finished, false);
}

void wait(std::initializer_list<job_t*> jobs)
{
	for(auto job : jobs)
		std::atomic_wait(&job->finished, false);
}

job_t* schedule(std::function<void()>&& f)
{
	job_t* j = allocate_job();
	j->func = std::move(f);
	j->finished = false;
	j->jobs_running = 1u;

	auto& this_thread = ctx->threads[current_thread_id];
	if(this_thread.active_job)
	{
		this_thread.active_job->jobs_running++;
		j->parent = this_thread.active_job;
	}
	else
	{
		j->parent = nullptr;
	}

	{
		const std::scoped_lock<std::mutex> lock{ctx->jobs_mutex};
		ctx->jobs.push(j);
	}

	ctx->work_available.notify_one();
	return j;
}

}
	
