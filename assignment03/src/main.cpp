#include <iostream>
#include <string>
#include <list>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include "configreader.h"
#include "process.h"

// Shared data for all cores
typedef struct SchedulerData {
    std::mutex mutex;
    std::condition_variable condition;
    ScheduleAlgorithm algorithm;
    uint32_t context_switch;
    uint32_t time_slice;
    std::list<Process*> ready_queue;
    bool all_terminated;
    std::list<Process*> io_queue;
} SchedulerData;

void coreRunProcesses(uint8_t core_id, SchedulerData *data);
int printProcessOutput(std::vector<Process*>& processes, std::mutex& mutex);
void clearOutput(int num_lines);
uint32_t currentTime();
std::string processStateToString(Process::State state);

int main(int argc, char **argv)
{
    // ensure user entered a command line parameter for configuration file name
    if (argc < 2)
    {
        std::cerr << "Error: must specify configuration file" << std::endl;
        exit(1);
    }

    // declare variables used throughout main
    int i;
    SchedulerData *shared_data;
    std::vector<Process*> processes;

    // read configuration file for scheduling simulation
    SchedulerConfig *config = readConfigFile(argv[1]);

    // store configuration parameters in shared data object
    uint8_t num_cores = config->cores;
    shared_data = new SchedulerData();
    shared_data->algorithm = config->algorithm;
    shared_data->context_switch = config->context_switch;
    shared_data->time_slice = config->time_slice;
    shared_data->all_terminated = false;

    // create processes
    uint32_t start = currentTime();
    for (i = 0; i < config->num_processes; i++)
    {
        Process *p = new Process(config->processes[i], start);
        processes.push_back(p);
        if (p->getState() == Process::State::Ready)
        {
            shared_data->ready_queue.push_back(p);
        }
    }

    // free configuration data from memory
    deleteConfig(config);

    // launch 1 scheduling thread per cpu core
    std::thread *schedule_threads = new std::thread[num_cores];
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i] = std::thread(coreRunProcesses, i, shared_data);
    }

    // main thread work goes here:
    int num_lines = 0;
    while (!(shared_data->all_terminated))
    {
        // clear output from previous iteration
        clearOutput(num_lines);

        // start new processes at their appropriate start time

        // determine when an I/O burst finishes and put the process back in the ready queue
        shared_data->mutex.lock();
        for(std::list<Process*>::iterator it= shared_data->io_queue.begin(); it != shared_data->io_queue.end(); ++it)
        {
        	std::cout << *it << std::endl;
        	int index = (*it)->getCurrentBurstIndex();
        	//if the io burst has finished, put the process back onto the ready queue
        	if(currentTime() - (*it)->getIOQueueStart() >= (*it)->getBurstTime(index))
        	{
				shared_data->ready_queue.push_back(*it);
				(*it) -> setState(Process::State::Ready,currentTime());
        	}
        }

        // sort the ready queue (if needed - based on scheduling algorithm)
        // shortest job first: sort ready queue by burst time
        if(shared_data->algorithm == ScheduleAlgorithm::SJF)
        {
        	//sort via algo
		//sortSJFMethod();
        }

        //preemptive process:sort ready queue by time remaining
        if(shared_data->algorithm == ScheduleAlgorithm::PP)
        {
        	//sort via algo 
		//sortPreMethod()
        }
        // determine if all processes are in the terminated state
        int count = 0;
        for(int i = 0; i < processes.size(); i ++)
        {
        	if(processes[i]->getState() == Process::State::Terminated)
        	{
        		count = count + 1;
        	}
        }
        //possibly need to lock the mutex when changing the boolean:
        if(count == processes.size())
        {

        		shared_data->all_terminated = true;	
        }

        // output process status table
        num_lines = printProcessOutput(processes, shared_data->mutex);

        // sleep 1/60th of a second
        usleep(16667);
    }


    // wait for threads to finish
    for (i = 0; i < num_cores; i++)
    {
        schedule_threads[i].join();
    }

    // print final statistics
    //  - CPU utilization
    //  - Throughput
    //     - Average for first 50% of processes finished
    //     - Average for second 50% of processes finished
    //     - Overall average
    //  - Average turnaround time
            //total of each process turn_time / #processes
    //  - Average waiting time
            //total of each process turn_time / #processes


    // Clean up before quitting program
    processes.clear();

    return 0;
}

/*
void sortSJFMethod() //if burst time is greater, move to the bottom of the ready queue
{
    //std::list<std::string>::iterator it = shared_data->ready_queue.begin();
    for(int i = 0; i < processes.size()-1; i++)
    {
        for(int j = 0; j < processes[i].num_bursts-1; j++)
        {
            if (config->processes[i].burst_times[j] < config->processes[i].burst_times[j+1])
            {
                //swap(config->processes[i].burst_times[j], config->processes[i].burst_times[j+1]);
                int temp = config->processes[i].burst_times[j];
                config->processes[i].burst_times[j] = config->processes[i].burst_times[j+1];
                config->processes[i].burst_times[j+1] = temp;
            }
        }
    }
    //std::cout<<config->processes[0].burst_times;
    //std::cout<<config->processes[0].burst_times[1];
    //std::cout<<shared_data->ready_queue.push_back();
    //std::advance(it, 1);
}

void sortPreMethod() //if time remaining is smaller, move to front of ready queue
{
    //std::list<std::string>::iterator it = shared_data->ready_queue.begin();
    for(int i = 0; i < config->num_processes; i++)
    {
        for(int j = 0; j < processes[i].num_bursts-1; j++)
        {
            if (config->processes[i].burst_times[j] < config->processes[i].burst_times[j+1])
            {
                //swap(config->processes[i].burst_times[j], config->processes[i].burst_times[j+1]);
                int temp = config->processes[i].burst_times[j];
                config->processes[i].burst_times[j] = config->processes[i].burst_times[j+1];
                config->processes[i].burst_times[j+1] = temp;
            }
        }
    }
}
*/

void coreRunProcesses(uint8_t core_id, SchedulerData *shared_data)
{
    // Work to be done by each core idependent of the other cores
    //  - Get process at front of ready queue
    //  - Simulate the processes running until one of the following:
    //     - CPU burst time has elapsed
    //     - RR time slice has elapsed
    //     - Process preempted by higher priority process
    //  - Place the process back in the appropriate queue
    //     - I/O queue if CPU burst finished (and process not finished)
    //     - Terminated if CPU burst finished and no more bursts remain
    //     - Ready queue if time slice elapsed or process was preempted
    //  - Wait context switching time
    //  * Repeat until all processes in terminated state
	while(!(shared_data->all_terminated))
	{
		uint32_t start_time;
		Process *p;

		//take the first thing off the list:
		shared_data->mutex.lock();
		p = shared_data->ready_queue.front();
		shared_data->ready_queue.pop_front();
		shared_data->mutex.unlock();

		p->setState(Process::State::Running,currentTime());
		p->updateProcess(currentTime());
	
		//determing the time to compute:
		uint32_t slice = 0;
		int index = p->getCurrentBurstIndex();
		if(shared_data->algorithm = ScheduleAlgorithm::RR)
		{
			slice = shared_data->time_slice;
		}
		//otherwise, the time is the burst time of the process popped off the list:
		else
		{		
			slice = uint32_t(p->getBurstTime(index));
		}
	
		//simulate running the process for a set amount of time 
		/*	lock the mutex, check if there is a higher priority in the list:	*/
		start_time = currentTime(); 
		uint8_t p_priority = p->getPriority();
		while(currentTime() - start_time > slice && shared_data->algorithm == ScheduleAlgorithm::PP)
		{
			//lock the mutex, check for a higher priority: 
			shared_data->mutex.lock();
			/*	find a higher priority if Preemptive Priority is running */
			for(std::list<Process*>::iterator it= shared_data->io_queue.begin(); it != shared_data->io_queue.end(); ++it)
	        {
	        	//if the process on the list has a higher priority that the one on the CPU
	        	if((*it)->getPriority() < p_priority )
	        	{
					p->updateBurstTime(index, p->getBurstTime(index) - (currentTime() - start_time));
					shared_data->ready_queue.push_back(p);
	        	}
	        }

			shared_data->mutex.unlock();
		}
	
		/*	Update the BurstTime if greater than the slice:	*/
		if(p->getBurstTime(index) > slice)
		{
			p->updateBurstTime(index,p->getBurstTime(index) - slice);
		}
	
		if(p->getCurrentBurstIndex()%2 == 0)
		{
			//the next burst is IO
			shared_data->mutex.lock();
			shared_data->io_queue.push_back(p);
			p->setState(Process::State::IO,currentTime());
			shared_data->mutex.unlock();
		}

		if(p->getCurrentBurstIndex() == p->getTotalBursts())
		{
			//process is terminated
			shared_data->mutex.lock();
			p->setState(Process::State::Terminated,currentTime());
			shared_data->mutex.unlock();

		}
		/*	Wait the context switching time allotment	*/
		usleep(shared_data->context_switch);
	}
}

int printProcessOutput(std::vector<Process*>& processes, std::mutex& mutex)
{
    int i;
    int num_lines = 2;
    std::lock_guard<std::mutex> lock(mutex);
    printf("|   PID | Priority |      State | Core | Turn Time | Wait Time | CPU Time | Remain Time |\n");
    printf("+-------+----------+------------+------+-----------+-----------+----------+-------------+\n");
    for (i = 0; i < processes.size(); i++)
    {
        if (processes[i]->getState() != Process::State::NotStarted)
        {
            uint16_t pid = processes[i]->getPid();
            uint8_t priority = processes[i]->getPriority();
            std::string process_state = processStateToString(processes[i]->getState());
            int8_t core = processes[i]->getCpuCore();
            std::string cpu_core = (core >= 0) ? std::to_string(core) : "--";
            double turn_time = processes[i]->getTurnaroundTime();
            double wait_time = processes[i]->getWaitTime();
            double cpu_time = processes[i]->getCpuTime();
            double remain_time = processes[i]->getRemainingTime();
            printf("| %5u | %8u | %10s | %4s | %9.1lf | %9.1lf | %8.1lf | %11.1lf |\n", 
                   pid, priority, process_state.c_str(), cpu_core.c_str(), turn_time, 
                   wait_time, cpu_time, remain_time);
            num_lines++;
        }
    }
    return num_lines;
}

void clearOutput(int num_lines)
{
    int i;
    for (i = 0; i < num_lines; i++)
    {
        fputs("\033[A\033[2K", stdout);
    }
    rewind(stdout);
    fflush(stdout);
}

uint32_t currentTime()
{
    uint32_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::system_clock::now().time_since_epoch()).count();
    return ms;
}

std::string processStateToString(Process::State state)
{
    std::string str;
    switch (state)
    {
        case Process::State::NotStarted:
            str = "not started";
            break;
        case Process::State::Ready:
            str = "ready";
            break;
        case Process::State::Running:
            str = "running";
            break;
        case Process::State::IO:
            str = "i/o";
            break;
        case Process::State::Terminated:
            str = "terminated";
            break;
        default:
            str = "unknown";
            break;
    }
    return str;
}
