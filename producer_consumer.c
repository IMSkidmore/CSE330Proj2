#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched/signal.h>
#include <linux/semaphore.h>
#include <linux/kthread.h>
#include <linux/timekeeping.h>
#include <linux/slab.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Hamlin");
MODULE_DESCRIPTION("Producer and Consumer");

int uid;
int buff_size;
int p;
int c;

module_param(uid, int, 0);
module_param(buff_size, int, 0);
module_param(p, int, 0);
module_param(c, int, 0);

struct task_struct_list {
	struct task_struct* task;
	struct task_struct_list* next;
};

struct buffer {
	int capacity;
	struct task_struct_list* list;
};


//Global Variables:
struct buffer *buf = NULL;
struct semaphore mutex;
struct semaphore full;
struct semaphore empty;

//Global task structs for kernel threads
struct task_struct *ts1;
struct task_struct *ts2;

static int producer(void *arg) {
	printk(KERN_INFO "PRODUCER: %s PID = %d\n", current->comm, current->pid);
	struct task_struct *ptr;
	int process_counter = 0;

	//Adds processes into buffer
	for_each_process(ptr) {
		if (ptr->cred->uid.val == uid) {
			process_counter++;
			
			if (down_interruptible(&empty))
				break;
			if (down_interruptible(&mutex))
				break;	

			//Critical section		
			if (buf->list == NULL) {
				struct task_struct_list *new;
				new = kmalloc(sizeof(struct task_struct_list), GFP_KERNEL);
				new->task = ptr;
				buf->list = new;
				buf->capacity++;
				printk(KERN_INFO "[%s] Produced Item#-%d at buffer index:%d for PID:%d", current->comm, process_counter, buf->capacity, new->task->pid);
			}
			else {
				struct task_struct_list *temp = buf->list;
				while (temp->next != NULL) {
					temp = temp->next;
				}
				struct task_struct_list *new;
				new = kmalloc(sizeof(struct task_struct_list), GFP_KERNEL);
				new->task = ptr;
				temp->next = new;
				buf->capacity++;
				printk(KERN_INFO "[%s] Produced Item#-%d at buffer index:%d for PID:%d\n", current->comm, process_counter, buf->capacity, new->task->pid);
			}


			up(&mutex);
			up(&full);

			
		}
	}
	printk(KERN_INFO "Number of process: %d\n", process_counter);

	return 0;
}

static int consumer(void *arg) {
	printk(KERN_INFO "CONSUMER: %s PID = %d\n", current->comm, current->pid);
	int consumedCount = 0;

	//Start infinite loop until stop
	while (!kthread_should_stop()) {
		if (down_interruptible(&full))
			break;
		if (down_interruptible(&mutex))
			break;

		//Critical section
		struct task_struct_list *temp = buf->list;
		buf->list = buf->list->next;
		int time_elapsed = ktime_get_ns() - temp->task->start_time;
		int total_seconds = time_elapsed/(10^9);
		consumedCount++;
		printk(KERN_INFO "[%s] Consumed Item#-%d on buffer index:0 PID:%d Elapsed Time- %d\n", current->comm, buf->capacity, temp->task->pid, total_seconds);
		
		up(&mutex);
		up(&empty);
	}

	return 0;
}

static int __init init_func(void) {
	int err;

	//Initialize Semaphores
	sema_init(&mutex, 1);
	sema_init(&full, 0);
	sema_init(&empty, buff_size);

	buf = kmalloc(sizeof(struct buffer), GFP_KERNEL);

	//Start the threads
	printk(KERN_INFO "Starting producer_consumer module\n");

	ts1 = kthread_run(producer, NULL, "producer_thread");
	if (IS_ERR(ts1)) {
		printk(KERN_INFO "ERROR: Cannot create producer thread\n");
		err = PTR_ERR(ts1);
		ts1 = NULL;
		return err;
	}

	ts2 = kthread_run(consumer, NULL, "consumer_thread");
	if (IS_ERR(ts2)) {
		printk(KERN_INFO "ERROR: Cannot create consumer thread\n");
		err = PTR_ERR(ts2);
		ts2 = NULL;
		return err;
	}

	return 0;
}


static void __exit exit_func(void) {
	printk(KERN_INFO "Exiting producer_consumer module\n");
	struct task_struct_list *temp = buf->list;
	while (temp != NULL) {
		struct task_struct_list *tbr = temp;
		temp = temp->next;
		kfree(tbr);
	}
	kfree(buf);

	kthread_stop(ts1);
	kthread_stop(ts2);
}


module_init(init_func);
module_exit(exit_func);