# Project Threads Design Document

### ---- GROUP ----

黄元敏 17302010002 

### ---- PRELIMINARIES ----

>> If you have any preliminary comments on your submission, notes for the TAs, or extra credit, please give them here.

>> Please cite any offline or online sources you consulted while preparing your submission, other than the Pintos documentation, course text, lecture notes, and course staff.



## I. ALARM CLOCK

### ---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct` or `struct` member, global or static variable, `typedef`, or enumeration.  Identify the purpose of each in 25 words or less.

#### 1. struct `thread`

   ```c
   struct thread
     {
       /* Owned by thread.c. */
       tid_t tid;                          /* Thread identifier. */
       enum thread_status status;          /* Thread state. */
       char name[16];                      /* Name (for debugging purposes). */
       uint8_t *stack;                     /* Saved stack pointer. */
       int priority;                       /* Priority. */
       struct list_elem allelem;           /* List element for all threads list. */
   
       /* Shared between thread.c and synch.c. */
       struct list_elem elem;              /* List element. */
   
   #ifdef USERPROG
       /* Owned by userprog/process.c. */
       uint32_t *pagedir;                  /* Page directory. */
   #endif
   
       /* Owned by thread.c. */
       unsigned magic;                     /* Detects stack overflow. */
   
       /* Newly added */
       int64_t ticks_to_sleep;             /* Ticks to sleep until waked up. */
     };
   ```

在`thread`结构体的最后添加了`ticks_to_sleep`成员，标识了该线程需要睡眠多久后被唤醒。


### ---- ALGORITHMS ----

>> A2: Briefly describe what happens in a call to timer_sleep(), including the effects of the timer interrupt handler.

**`void timer_sleep (int64_t ticks)`**

重构前的`timer_sleep`函数是通过busy waiting的方式实现的线程“睡眠”效果，即线程每次被唤醒的时候会通过while循环来确认睡眠时间是否已经过去，如果没有过去，则通过`thread_yield`函数把自己切换出去，并重新`schedule`一个新的`ready_list`中的线程进来run；如果已经过去，则正常执行。这样的实现使得CPU不断切换到一个应该在sleep的线程上来检查睡眠的ticks是否过去，而大部分情况下是没有过去的，导致CPU在睡眠线程和其它线程之间不断切换，耗费资源。

重构后的`timer_sleep`函数在调用后会在当前运行的线程结构体上标记一个`ticks_to_sleep`，该值即传入的需要线程睡眠的`ticks`参数。之后调用`thread_block`函数来阻塞当前线程。race condition保护相关的部分会在SYNCHRONIZATION中提及。

**`static void timer_interrupt (struct intr_frame *args UNUSED)`**

重构后的`timer_interrupt`函数在原先每个`tick`更新`ticks`属性和线程统计信息（以及根据当前线程的执行时间来决定是否强制被强占）的功能之外，还通过调用`thread_foreach`函数实现了对每个`BLOCKED`且`ticks_to_sleep` > 0的线程的该属性自减的更新操作，并对`ticks_to_sleep`减到0的线程（睡眠已结束）进行`thread_unblock`，该函数将会将该线程重新放入`ready_list`中。这些是通过`thread.c`中新增的`thread_ticks_to_sleep_check`函数封装的。

>> A3: What steps are taken to minimize the amount of time spent in the timer interrupt handler?

目前的实现是通过`timer_interrupt`中调用`thread_foreach`实现的，`thread_foreach`中又是通过对`all_list`线程链表做遍历实现的，对一个链表的遍历复杂度是O(n)，应该已经是很优的解法了。

### ---- SYNCHRONIZATION ----

>> A4: How are race conditions avoided when multiple threads call timer_sleep() simultaneously?

多个线程同时调用`timer_sleep`的情况我认为是被整个CPU的`schedule`机制来控制避免的。所谓的simultaneously其实不是真正的“同时”，还是被调度切换所产生的illusion。

例如，在`thread.c`中的`schedule`函数完成了将一个已经被切换出`RUNNING`状态的线程切换出CPU的工作，并将下一个`ready_list`中即将运行的线程切换进来运行。而`schedule`函数会被很多操作thread的函数调用，例如`thread_block`阻塞一个线程后就会调用，完善了一整个线程切换的过程。

而一个线程真正要做的任务是在进入`RUNNING`状态后才进行的，此时CPU是没有同时运行两个线程的。`timer_sleep`的调用就发生在这个过程中，因此不会发生多个线程竞争的情况。

>> A5: How are race conditions avoided when a timer interrupt occurs during a call to timer_sleep()?

在`timer_sleep`函数中核心代码块如下所示：

```c
  // disable interrupts and get old interrupt level
  enum intr_level old_level = intr_disable ();

  // get current thread
  struct thread *current_thread = thread_current ();

  // record ticks to sleep for current thread
  current_thread->ticks_to_sleep = ticks;

  // block current thread 
  thread_block ();

  // set interrupt level back
  intr_set_level (old_level);
```

在代码中的第2行和第14行分别进行了`intr_disable`和`intr_set_level`的调用。其中第2行的调用，查看其实现是通过清除interrupt flag的方式来disable了中断，并返回了disable之前的中断响应级别（也就是INTR_ON或者INTR_OFF）。而在`intr_set_level`中通过之前保存的`old_level`变量将中断响应设置回了原来的级别，恢复了中断响应。

这里其实是模仿了`timer_ticks`函数中的实现，通过第2行和第14行进行包裹，保证了中间的核心步骤是一个原子操作，不被外部中断打断，也就不会被timer interrupt打断。

### ---- RATIONALE ----

>> A6: Why did you choose this design?  In what ways is it superior to another design you considered?

这样的设计其实在避免busy waiting上是一个挺直接的想法，直接通过每个`tick`会触发的`timer_interrupt`来更新因睡眠而阻塞的线程中的`ticks_to_sleep`属性值，直到线程被唤醒。如此设计在理解上也比较容易，也达到了避免busy waiting的目的。



## II. PRIORITY SCHEDULING

### ---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct` or `struct` member, global or static variable, `typedef`, or enumeration.  Identify the purpose of each in 25 words or less.

>> B2: Explain the data structure used to track priority donation. Use ASCII art to diagram a nested donation.  (Alternately, submit a .png file.)

### ---- ALGORITHMS ----

>> B3: How do you ensure that the highest priority thread waiting for a lock, semaphore, or condition variable wakes up first?

>> B4: Describe the sequence of events when a call to lock_acquire() causes a priority donation.  How is nested donation handled?

>> B5: Describe the sequence of events when lock_release() is called on a lock that a higher-priority thread is waiting for.

### ---- SYNCHRONIZATION ----

>> B6: Describe a potential race in thread_set_priority() and explain how your implementation avoids it.  Can you use a lock to avoid this race?

### ---- RATIONALE ----

>> B7: Why did you choose this design?  In what ways is it superior to another design you considered?



## III. ADVANCED SCHEDULER

### ---- DATA STRUCTURES ----

>> C1: Copy here the declaration of each new or changed `struct` or `struct` member, global or static variable, `typedef`, or enumeration.  Identify the purpose of each in 25 words or less.

### ---- ALGORITHMS ----

>> C2: Suppose threads A, B, and C have nice values 0, 1, and 2.  Each has a recent_cpu value of 0.  Fill in the table below showing the scheduling decision and the priority and recent_cpu values for each thread after each given number of timer ticks:

timer  recent_cpu    priority   thread
ticks   A   B   C   A   B   C   to run
-----  --  --  --  --  --  --   ------
 0
 4
 8
12
16
20
24
28
32
36

>> C3: Did any ambiguities in the scheduler specification make values in the table uncertain?  If so, what rule did you use to resolve them?  Does this match the behavior of your scheduler?

>> C4: How is the way you divided the cost of scheduling between code inside and outside interrupt context likely to affect performance?

### ---- RATIONALE ----

>> C5: Briefly critique your design, pointing out advantages and disadvantages in your design choices.  If you were to have extra time to work on this part of the project, how might you choose to refine or improve your design?

>> C6: The assignment explains arithmetic for fixed-point math in detail, but it leaves it open to you to implement it.  Why did you decide to implement it the way you did?  If you created an abstraction layer for fixed-point math, that is, an abstract data type and/or a set of functions or macros to manipulate fixed-point numbers, why did you do so?  If not, why not?
