# OS Project Threads Design Document

### 17302010002 黄元敏



## Test Results

执行测试结果截图如下。

![image-20200628134412909](./README.assets/image-20200628134412909.png)



## I. ALARM CLOCK

### ---- DATA STRUCTURES ----

>> A1: Copy here the declaration of each new or changed `struct` or `struct` member, global or static variable, `typedef`, or enumeration.  Identify the purpose of each in 25 words or less.

#### 1. struct `thread`

   ```c
   struct thread
     {
	   // ... other members
   
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

多个线程同时调用`timer_sleep`的情况在我的设计中不会产生冲突，因为在`timer_sleep`中仅仅对当前正在执行的线程做了`ticks_to_sleep`的赋值，并不会涉及对公共变量的修改。因此，也就避免了race condition。

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

在代码中的第2行和第14行分别进行了`intr_disable`和`intr_set_level`的调用。其中第2行的调用，查看其实现是通过清除interrupt flag的方式来disable了中断，并返回了disable之前的中断响应级别（也就是`INTR_ON`或者`INTR_OFF`）。而在`intr_set_level`中通过之前保存的`old_level`变量将中断响应设置回了原来的级别，恢复了中断响应。

这里其实是模仿了`timer_ticks`函数中的实现，通过第2行和第14行进行包裹，保证了中间的核心步骤是一个原子操作，不被外部中断打断，也就不会被timer interrupt打断。

### ---- RATIONALE ----

>> A6: Why did you choose this design?  In what ways is it superior to another design you considered?

这样的设计其实在避免busy waiting上是一个挺直接的想法，直接通过每个`tick`会触发的`timer_interrupt`来更新因睡眠而阻塞的线程中的`ticks_to_sleep`属性值，直到线程被唤醒。如此设计在理解上和实现上也比较容易，也达到了避免busy waiting的目的。



## II. PRIORITY SCHEDULING

### ---- DATA STRUCTURES ----

>> B1: Copy here the declaration of each new or changed `struct` or `struct` member, global or static variable, `typedef`, or enumeration.  Identify the purpose of each in 25 words or less.

#### 1. struct `thread`

```c
struct thread
  {
    // ... other members

    /* Newly added for priority scheduling */
    int original_priority;              /* Stores the original priority when being donated. */
    struct lock *waiting_lock;          /* Lock the thread is waiting for. */
    struct list holding_locks;          /* Locks the thread is holding. */
  };
```

在结构体中添加了：

a. `original_priority`: 用来存储一个线程被捐赠优先级之前的原始优先级；

b. `waiting_lock`: 用来存储一个线程正在等待的锁的指针；

c. `holding_locks`: 用来存储一个线程当前持有的锁的列表；

#### 2. struct `lock`

```c
struct lock 
  {
    // ... other members
  
    /* Newly added for priority scheduling */
    struct list_elem elem;      /* List element for sorting. */
    int max_priority;           /* The max priority of all threads holding/waiting for the lock. */
  };
```

在结构体中添加了：

a. `elem`: 用来使得`lock`的结构体可以被放入`list`结构中，能够调用`list`中封装好的函数；

b. `max_priority`: 用来存储当前持有和等待该锁的所有线程中最高的优先级；

>> B2: Explain the data structure used to track priority donation. Use ASCII art to diagram a nested donation.  (Alternately, submit a .png file.)

在目前的设计中没有使用特殊单独的数据结构来实现priority donation的追踪，而是通过锁的`max_priority`与线程的`holding_locks`共同实现的。即：

a. 在一个线程想要获取锁但锁当前已被持有的时候，用等待线程的`priority`尝试更新锁的`max_priority`，若线程优先级更高则采用线程优先级，之后进行`priority_donation`：持有该锁的线程排序`holding_locks`，取最高优先级的锁尝试更新该线程的`priority`，若锁优先级更高则采用锁优先级。以此完成了捐赠优先级的过程；

b. 在一个线程成功获取锁之后，用该线程的`priority`更新锁的`max_priority`，并将锁放入该线程的`holding_locks`中，此时不必更新线程`priority`，因为其`priority`一定是最高的（不然怎么会轮到它执行呢），也就没有捐赠的过程；

c. 在一个线程释放锁的时候，需要将锁从该线程的`holding_locks`中移除，而后进行priority donation的更新：对该线程的`holding_locks`按照`max_priority`排序，取最高优先级的锁更新该线程的`priority`，如果此时`holding_locks`已经为空，则会用线程的`original_priority`更新`priority`。以此完成了捐赠优先级的逐步恢复过程；

以下通过表格的方式示意`priority-donate-nest`测试中各线程、锁的状态逐步更新情况。

![image-20200628151248768](README.assets/image-20200628151248768.png)

### ---- ALGORITHMS ----

>> B3: How do you ensure that the highest priority thread waiting for a lock, semaphore, or condition variable wakes up first?

#### 1. lock

决定哪个等待锁的线程先醒来，是线程在`ready_list`的顺序决定的（因为`next_thread_to_run`函数中使用了`list_pop_front`来获取下一个要执行的线程）。因此，只需要使`ready_list`成为一个优先级队列即可，让`priority`高的线程排在前面，自然就会更早被唤醒。

而`ready_list`的顺序是在插入的过程中维护的。在thread.c文件中搜索`ready_list`，可以发现一共有两处执行了插入操作，分别是在`thread_unblock`和`thread_yield`中。原始的插入操作可以发现都是通过`list_push_back`函数简单地放到列表末尾来实现的，只需要将其修改为按`priority`降序插入即可实现一个优先级队列，修改后的插入代码如下：

```c
  list_insert_ordered (&ready_list, &t->elem, thread_list_higher_priority_func, NULL);
```

其中`thread_list_higher_priority_func`是自己实现的一个比较函数，实现了将`thread`按`priority`降序排列的效果：

```c
/* Compares the priority of two threads. */
bool 
thread_list_higher_priority_func (const struct list_elem *a,
                                  const struct list_elem *b,
                                  void *aux UNUSED)
{
  return list_entry(a, struct thread, elem)->priority
       > list_entry(b, struct thread, elem)->priority;
}
```

#### 2. semaphore

决定那个等待信号量的线程先醒来更加直观，也与等待锁的情况类似，即有一个`waiters`队列，其中存放了等待该信号量的线程。这里的实现比较直接，就是在`sema_up`函数中，执行`thread_unblock`之前对整个`waiters`队列进行按`priority`降序排序，实现了取出其中最高优先级队列的效果：

```c
    // sort waiting threads and find the first priority thread to unblock
    list_sort (&sema->waiters, thread_list_higher_priority_func, NULL); 

    thread_unblock (list_entry (list_pop_front (&sema->waiters),
                                struct thread, elem));
```

这里使用的排序函数就是之前定义的。

在实现的过程中我尝试通过对`sema_down`中插入waiters队列时按序插入来维护优先级队列，但是不能成功，最后还是采取了比较暴力的直接排序方式。

#### 3. condition variable

这个的实现与信号量的实现几乎一致，也是通过`cond_signal`函数中，执行`sema_up`之前对`cond->waiters`进行排序实现的，不再赘述。

>> B4: Describe the sequence of events when a call to lock_acquire() causes a priority donation.  How is nested donation handled?

主要步骤有：

1. 判断发现想要获取的锁已经被其它线程持有：`lock->holder != NULL`；

2. 记录当前线程的`waiting_lock`：`current_thread->waiting_lock = lock;`；

3. 判断等待锁的`max_priority`是否低于当前线程的`priority`，若是，则更新锁的`max_priority`，并进行`thread_donate_priority`：

   a. 被捐赠的线程通过排序自己的`holding_locks`，找到最高的`max_priority`来更新自己的`priority`；

   b. 如果被捐赠的线程是`READY`的线程，还需要更新自己在`ready_list`中的排位；

4. 继续递归至当前锁的持有线程的`waiting_lock`，执行第3步，直至当前线程的`priority`不再高于锁的`max_priority`时退出循环。这一步实现了对nested donation的处理：

   a. 即如果当前线程`TA`等待的锁`LA`的持有者`TB`，也在等待另一个锁`LB`，则锁`LB`的持有者`TC`会在锁`LB`的`max_priority`被判断不如`TA`的`priority`高的时候，通过锁`LB`的`holder`指针找到线程`TC`进行`thread_donate_priority`。更加实例化的过程描述可以参见B2问的回答；

主要步骤位于sych.c文件的`lock_acquire`函数中，源码如下：

```c
  // lock want to acquire is holding by other thread
  if (lock->holder != NULL && !thread_mlfqs)
  {
    current_thread->waiting_lock = lock;
    
    // recursively donate priority if current thread's priority
    // is higher than thread holding the lock (chain)
    struct lock *recursive_lock = lock;
    while (recursive_lock != NULL 
    && current_thread->priority > recursive_lock->max_priority)
    {
      // current thread which has highest priority waiting for the lock
      recursive_lock->max_priority = thread_get_priority ();
      thread_donate_priority (recursive_lock->holder);

      // go to next thread's waiting lock if there is one
      recursive_lock = recursive_lock->holder->waiting_lock;
    }
  }
```

>> B5: Describe the sequence of events when lock_release() is called on a lock that a higher-priority thread is waiting for.

主要步骤有：

1. 标记锁的`holder`为`NULL`；

2. 从当前线程的`holding_locks`中移除将要释放的锁；

3. 重新更新线程的`priority`，即：

   a. 通过排序自己的`holding_locks`，找到剩下最高的`max_priority`来更新自己的`priority`；

   b. 如果当前线程已经没有`holding_locks`了，则回归到`original_priority`：在线程创建时记录的线程原始优先级；

   c. 由于有更高级别的线程在等待当前即将释放的锁，因此移除该锁之后更新后的线程`priority`一定会降低；

4. 执行`sema_up`释放该锁；

   a. 该步骤会从当前锁的`waiters`中取出下一个优先级最高的线程进行`thread_unblock`；

   b. 而后进行`thread_yield`，将CPU切换给优先级更高的线程，可能是正在等待该锁的线程；

主要步骤位于sych.c文件的`lock_release`函数中，源码如下：

```c
  lock->holder = NULL;

  if (!thread_mlfqs)
  {
    // disable interrupts and get old interrupt level
    enum intr_level old_level = intr_disable ();
    
    // remove the lock from current thread's holding locks list
    list_remove (&lock->elem);

    // current thread's priority may have changed because of the release
    thread_update_priority (thread_current ());

    // set interrupt level back
    intr_set_level (old_level);
  }

  sema_up (&lock->semaphore);
```

### ---- SYNCHRONIZATION ----

>> B6: Describe a potential race in thread_set_priority() and explain how your implementation avoids it.  Can you use a lock to avoid this race?

该函数的主要工作是更新当前线程的`original_priority`，使得线程在释放所有持有的锁之后、不被捐赠的情况下的默认优先级改变。因此，只有在`new_priority` > 当前`priority`的时候、或者当前线程不持有锁的情况下需要同步更新`priority`值，并且调用`thread_yield`切换线程。

潜在的竞争情况会出现在一个线程在`thread_yield`的过程中另一个线程更新了它的`priority`，这会导致调度可能与线程优先级发生不一致。

目前的解决方案是通过类似ALARM部分中的`intr_disable ()` + `intr_set_level (old_level)`方式以屏蔽中断响应来实现的。

理论上这样的情况也是可以通过加锁来避免的，即所有线程共享一个对`thread_set_priority`进行控制的锁，保证同一时刻只有一个线程能够修改`priority`并且调用`thread_yield`即可。只是这样的实现需要修改的部分比较多，不如直接使用屏蔽中断来得方便。

### ---- RATIONALE ----

>> B7: Why did you choose this design?  In what ways is it superior to another design you considered?

目前的这种实现结构上比较精巧，巧妙地利用了存储一个`max_priority`值在锁结构中的方式，来记录一个锁当前等待线程和持有线程中的最高优先级，能够在持有线程更新`priority`的时候直接通过`holding_locks`中的锁来查找到应当提升到的优先级。且在释放锁之后也可以复用同样的方式对线程的优先级进行更新，避免了在`thread`中存储一系列被捐赠的优先级，而后再释放锁之后删除的困扰。

之前还思考过通过在`thread`中通过列表的形式记录被捐赠的优先级的方式来实现释放锁之后的优先级回溯，但是这样的更新函数实现起来十分复杂，既要考虑锁和捐赠优先级的对应关系，还要考虑在持有和释放锁期间捐赠优先级发生的变化。不如直接将捐赠优先级和锁挂钩，而将持有的锁与线程挂钩，这样的方式比较符合逻辑上的关系，更新起来也比较实现简单。



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
