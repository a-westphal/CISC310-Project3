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
    uint32_t start_time;
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
        shared_data->ready_queue.push_back(p);
        if(p->getStartTime() == 0)
        {
        	p->setState(Process::State::Ready, currentTime());
        	p->updateProcess(currentTime());
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
        std::unique_lock<std::mutex> lock2(shared_data->mutex);
        for(std::list<Process*>::iterator it = shared_data->ready_queue.begin(); it!= shared_data->ready_queue.end(); ++it)
        {
        	if(currentTime() - (*it)->getStartTime()>= currentTime() - start && (*it)->getState() == Process::State::NotStarted)
        	{
        		(*it)->setState(Process::State::Ready,currentTime());
        		(*it) ->updateProcess(currentTime());
        	}
        }
        lock2.unlock();
  
        // determine when an I/O burst finishes and put the process back in the ready queue
      	std::unique_lock<std::mutex> lock1(shared_data->mutex);
        for(std::list<Process*>::iterator it= shared_data->io_queue.begin(); it != shared_data->io_queue.end(); ++it)
        {
        	int index = (*it)->getCurrentBurstIndex();
        	//if the io burst has finished, put the process back onto the ready queue
        	if(currentTime() - (*it)->getIOQueueStart() >= (*it)->getBurstTime(index))
        	{
				shared_data->ready_queue.push_back(*it);
				(*it) -> setState(Process::State::Ready,currentTime());
				(*it) ->updateProcess(currentTime());
        	}
        }
        lock1.unlock();
        // sort the ready queue (if needed - based on scheduling algorithm)
        // shortest job first: sort ready queue by using comparator
        if(shared_data->algorithm == ScheduleAlgorithm::SJF)
        {
        	std::cout << "In SJF if" <<std::endl;
        	//sort via algo
        	std::unique_lock<std::mutex> lock(shared_data->mutex);
				shared_data->ready_queue.sort(SjfComparator());
				for(std::list<Process*>::iterator it = shared_data->ready_queue.begin(); it!= shared_data->ready_queue.end(); ++it)
        		{
		        	std::cout<< (*it) -> getPid() <<std::endl;
		        }
			lock.unlock();
		//sortSJFMethod();
        }

        //preemptive process:sort ready queue by using comparator
        if(shared_data->algorithm == ScheduleAlgorithm::PP)
        {
        	//sort via algo 
        	std::unique_lock<std::mutex> lock(shared_data->mutex);
				shared_data->ready_queue.sort(PpComparator());
			lock.unlock();


		//sortPreMethod()
        }
        // determine if all processes are in the terminated state
        int count = 0;
        std::unique_lock<std::mutex> lock(shared_data->mutex);
        for(std::list<Process*>::iterator it = shared_data->ready_queue.begin(); it!= shared_data->ready_queue.end(); ++it)
        {
        	if((*it)->getState() == Process::State::Terminated)
        	{
        		count += 1;
        	}
		}
        lock.unlock();
        //possibly need to lock the mutex when changing the boolean:
        if(count == processes.size())
        {
        	std::unique_lock<std::mutex> lock(shared_data->mutex);
        		shared_data->all_terminated = true;	
        	lock.unlock();
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
            //percent of the time the CPU is being used
    //  - Throughput
            //number of processes completed per unit of time
    //     - Average for first 50% of processes finished
    //     - Average for second 50% of processes finished
    //     - Overall average
    //  - Average turnaround time
            //total of each process turn_time / #processes
    //  - Average waiting time
            //total of each process turn_time / #processes
    double avg_turnTime = 0.0;
    double avg_waitTime = 0.0;
    
    for (i = 0; i < processes.size(); i++)
    {
        avg_turnTime = avg_turnTime + (double)processes[i]->getTurnaroundTime();
        avg_waitTime = avg_waitTime + (double)processes[i]->getWaitTime();
    }
    
    avg_turnTime = avg_turnTime/config->num_processes;
    avg_waitTime = avg_waitTime/config->num_processes;


    // Clean up before quitting program
    processes.clear();

    return 0;
}


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

    std::cout << "In coreRunProcesses" <<std::endl;
	while(!(shared_data->all_terminated))
	{
		uint32_t start_time;
		Process *p;

		bool found = false;
		//take the first thing off the list:
		while(!found)
		{
			std::unique_lock<std::mutex> lock(shared_data->mutex);
			if(shared_data->ready_queue.front()->getState() == Process::State::Ready)
			{
				p = shared_data->ready_queue.front();
				shared_data->ready_queue.pop_front();
				found = true;
			}
			
			lock.unlock();
		}

		std::cout << "process being computed: " << p->getPid()<< std::endl;
		p->setState(Process::State::Running,currentTime());
		p->updateProcess(currentTime());
	
		//determing the time to compute:
		uint32_t slice = 0;
		int index = p->getCurrentBurstIndex();
		uint32_t original_burst = p->getBurstTime(index);

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

		std::cout << "Process: "<< p->getPid() << " and Slice: " << slice << " versus the time burst: " << p->getBurstTime(p->getCurrentBurstIndex()) <<std::endl;
		/*	lock the mutex, check if there is a higher priority in the list:	*/
		start_time = currentTime(); 
		uint8_t p_priority = p->getPriority();
		
		std::unique_lock<std::mutex> lock(shared_data->mutex);
			std::cout << "time slice: " << slice << std::endl;
		lock.unlock();
		while((currentTime() - start_time < slice))
		{
			//std::cout << "In the burst time while" <<std::endl;
			//lock the mutex, check for a higher priority: 
			if(shared_data->algorithm == ScheduleAlgorithm::PP)
			{	
				std::cout << "In the PP if" <<std::endl;
				std::unique_lock<std::mutex> lock(shared_data->mutex);
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

				lock.unlock();
			}
		}
		
		/*	Update the BurstTime if greater than the slice:	*/
		if(original_burst > slice)
		{
			std::unique_lock<std::mutex> lock(shared_data->mutex);
				std::cout<<"Burst time is greater than slice" <<std::endl;
				p->updateBurstTime(index,p->getBurstTime(index) - slice);
				p->setState(Process::State::Ready,currentTime());
				p->updateProcess(currentTime());
				shared_data->ready_queue.push_back(p);
			lock.unlock();
		}

 		if(p->getCurrentBurstIndex() == p->getTotalBursts())
		{
			//process is terminated
			std::unique_lock<std::mutex> lock(shared_data->mutex);
				std::cout << "burst == total burst" <<std::endl;
				p->setState(Process::State::Terminated,currentTime());
				p->updateProcess(currentTime());
			lock.unlock();

		}
		if(p->getCurrentBurstIndex()%2 == 0 && (original_burst <= slice))
		{
			//the next burst is IO
			std::unique_lock<std::mutex> lock(shared_data->mutex);
				std::cout << "Entering IO" <<std::endl;
				p->setState(Process::State::IO,currentTime());
				p->updateProcess(currentTime());
				p->setCurrentBurstIndex(p->getCurrentBurstIndex() + 1);
				shared_data->io_queue.push_back(p);
			lock.unlock();
		}
		std::cout << "Before sleep" <<std::endl;
		/*	Wait the context switching time allotment	*/
		usleep(shared_data->context_switch * 1000);
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
