#include <iostream>
#include <string>
#include <cstdio>
#include <vector>
#include <cstring>
#include <cstdlib>

using namespace std;

struct dm_pcb {
    int pid;
    int period;
    int deadline;
    int duration;
};

vector<dm_pcb*> task_list;

// TC: O(n)
struct dm_pcb* pcb_search(int pid) {

    for (auto pcb : task_list) {
        if (pcb->pid == pid) return pcb;
    }
    return nullptr;
}

void pcb_insert(struct dm_pcb *data) {

    int i;
    for (i=0; i<task_list.size(); i++) {
        if (task_list[i]->deadline >= data->deadline) break;
    }
    task_list.insert(task_list.begin()+i, data);
}

int compute_interference(int i, int t) {
	int I = 0;
	struct dm_pcb *pcb;
	int j = 0;
        for (auto pcb : task_list) {
	    if (j++ >= i) break;
	    I += ((t+pcb->period-1)/pcb->period)*pcb->duration;
        }
	return I;
}

int is_schedulable() {
	
    struct dm_pcb *pcb;
    int running_t = 0;
    int interference = 0;

    int i=0;
    for (auto pcb : task_list) {
        int t = running_t + pcb->duration;
        int cont = 1;
        printf("Checking task %d (PID %d)\n", i, pcb->pid);
        while (cont) {
            interference = compute_interference(i, t);
            printf("\tt=%d\tI=%d\n", t, interference);

            if (interference + pcb->duration <= t) cont = false;
            else t = interference + pcb->duration;

            if (t > pcb->deadline) goto unschedulable;
        }
        i++;
        running_t += pcb->duration;
    }
    return 1;
    
unschedulable:
    return 0;
}

void __log() {

    for (auto pcb : task_list) {
        printf("[%u (%u,%u,%u)]\n", pcb->pid, pcb->period, pcb->deadline, pcb->duration);
        printf("       v\n");
    }
}

int __remove(int pid) {

    int i;
    for (i=0; i<task_list.size(); i++) {
        if (task_list[i]->pid == pid) break;
    }
    if (i == task_list.size()) return -22;

    dm_pcb* pcb_struct = task_list[i];
    task_list.erase(task_list.begin()+i);

    delete pcb_struct;
    printf("Removed pid %d\n", pid);
    return 0;
}

int __register_dm(int pid, int period, int deadline, int duration) {

	dm_pcb* new_pcb = new dm_pcb;

	new_pcb->period = period;
	new_pcb->deadline = deadline;
	new_pcb->duration = duration;
	new_pcb->pid = pid;

	// we don't need the task_struct to do the schedulability check
	pcb_insert(new_pcb);
	if (!is_schedulable()) {
		printf("List of tasks is not schedulable\n");
	        __remove(pid);
		return -22;
	}

	// TODO timer and callback
	printf("Successfully scheduled task with pid %d\n", pid);
	return 0;
}

int __list() {

        for (auto pcb : task_list) {
		printf("PID: %u\tperiod: %u\tdeadline: %u\t%u\n",
			pcb->pid, pcb->period, pcb->deadline, pcb->duration);
	}
        return 0;
}

void reg_dm() {
        int pid, period, deadline, exec_time;
        cin >> pid >> period >> deadline >> exec_time;

	if (__register_dm(pid, period, deadline, exec_time) == 0) {
		printf("Could register periodic task properly\n");
	}
	else {
		printf("ERROR: could not register periodic task\n");
	}
}

void del() {
	int pid;
        cin >> pid;

	if (__remove(pid) == 0) {
		printf("Could remove periodic task properly\n");
	}
	else {
		printf("ERROR: could not remove periodic task\n");
	}
}

void list() {
	if (__list() == 0) {
		printf("Listed periodic tasks. See kernel logs for info\n");
	}
	else {
		printf("ERROR: could not list periodic tasks\n");
	}
}

int main(int argc, char** argv) {

    string input;
    while (true) {
        cin >> input;
        if (input == "reg") {
            reg_dm();
        }
        if (input == "del") {
            del();
        }
        if (input == "list") {
            list();
        }
        if (input == "exit") {
            break;
        }
    }

    return 0;
}
