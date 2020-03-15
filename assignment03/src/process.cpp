#include "process.h"

// Process class methods
Process::Process(ProcessDetails details, uint32_t current_time)
{
    int i;
    pid = details.pid;
    start_time = details.start_time;
    num_bursts = details.num_bursts;
    current_burst = 0;
    burst_times = new uint32_t[num_bursts];
    start_wait = 0;
    start_cpu = 0;
    io_queue_start = 0;

    for (i = 0; i < num_bursts; i++)
    {
        burst_times[i] = details.burst_times[i];
    }
    priority = details.priority;
    state = (start_time == 0) ? State::Ready : State::NotStarted;
    if (state == State::Ready)
    {
        launch_time = current_time;
    }
    core = -1;
    turn_time = 0;
    wait_time = 0;
    cpu_time = 0;
    remain_time = 0;
    for (i = 0; i < num_bursts; i+=2)
    {
        remain_time += burst_times[i];
    }
}

Process::~Process()
{
    delete[] burst_times;
}

uint16_t Process::getPid() const
{
    return pid;
}

uint32_t Process::getStartTime() const
{
    return start_time;
}

uint8_t Process::getPriority() const
{
    return priority;
}

Process::State Process::getState() const
{
    return state;
}

int8_t Process::getCpuCore() const
{
    return core;
}

double Process::getTurnaroundTime() const
{
    return (double)turn_time / 1000.0;
}

double Process::getWaitTime() const
{
    return (double)wait_time / 1000.0;
}

double Process::getCpuTime() const
{
    return (double)cpu_time / 1000.0;
}

double Process::getRemainingTime() const
{
    return (double)remain_time / 1000.0;
}
int Process::getCurrentBurstIndex() const
{
    return current_burst;
}
double Process::getBurstTime(int burst_idx) const
{
	return burst_times[burst_idx];
}

uint32_t Process::getIOQueueStart() const
{
	return io_queue_start;
}

uint32_t Process::getTotalBursts() const
{
	return num_bursts;
}

void Process::setState(State new_state, uint32_t current_time)
{
    if (state == State::NotStarted && new_state == State::Ready)
    {
        launch_time = current_time;
    }
    state = new_state;
}

void Process::setCpuCore(int8_t core_num)
{
    core = core_num;
}

void Process::setIOQueue_Time(uint32_t time)
{
	io_queue_start = time;
}

void Process::updateProcess(uint32_t current_time)
{
    // use `current_time` to update, wait time, burst times, 
    // cpu time

    //If the state is terminated, turnaround time = current time - launchtime
    if(getState() == State::Terminated)
    {
        turn_time = (current_time - launch_time);
    }

    //if we haven't started running anything: 
    if(getState() == State::Ready && getCurrentBurstIndex() == 0)
    {
    	start_wait = current_time;
    }

    /*	if the process is running, update the wait time, set the start_cpu 
     	time to the current time */
    if(getState() == State::Running)
    {
    	wait_time = wait_time + (current_time - start_wait);
    	start_cpu = current_time;
    }



    //calculate the remaining time:
    int sum = 0; 
    for(int i = current_burst; i < num_bursts; i ++)
    {
        sum = sum + burst_times[i];
    }
    remain_time = sum;
}

void Process::updateBurstTime(int burst_idx, uint32_t new_time)
{
    burst_times[burst_idx] = new_time;
}

// Comparator methods: used in std::list sort() method
// No comparator needed for FCFS or RR (ready queue never sorted)

// SJF - comparator for sorting read queue based on shortest remaining CPU time
bool SjfComparator::operator ()(const Process *p1, const Process *p2)
{
    bool p1_greater = false;
    if(p1->getPid() > p2->getPid())
    {
    	p1_greater = true;
    }

    return p1_greater; 
}

// PP - comparator for sorting read queue based on priority
bool PpComparator::operator ()(const Process *p1, const Process *p2)
{
    bool p1_greater = false;
    if(p1->getPriority() > p2->getPriority())
    {
        p1_greater = true;
    }
    return p1_greater;
}
