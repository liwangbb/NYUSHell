# Lab 3: Encoder
Author: Li Wang

Student# N15155142

## SYNOPSIS
```
int getopt_long(int argc, char *argv[],
                  const char *optstring,
                  const struct option *longopts, int *longindex);
int getopt_long_only(int argc, char *argv[],
                  const char *optstring,
                  const struct option *longopts, int *longindex);

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream);

void *memcpy(void *to, const void *from, size_t numBytes);
```
## Idea for sturcts
* `TASK`: assign each task with a related file address with its start and end index, id as well. `addr`, `start` and `end` for encoding, `id` for tracking.
* `RESULT`: assign each result with a `buffer` that save the result from encoder, e.g.`a3b9a1`. Also, `len` length of the buffer, for writing and merging result use.

## Ideas for critical section
* `MUTEX_QUEUE`: ensure only current thread can process task from `TASK_QUEUE` and update some global variables.
* `COND_QUEUE`: tell threads wait for valid task or take and process any task from `TASK_QUEUE`.
* `READY_QUEUE`: the main thread should work parallelly that collect and write results right after the result is added to the `RESULT_QUEUE`. also, next result should be written to `stdout` if previous is ready, or wait for previous result. the semaphore here is set to tracking which result is available to write and merge.

## Idea for termination signal
* poison task and poison result are perfect for multithreads here. it ends the threads when detecting the poison task.
* `IS_ALL_PROCESSED_` is set up for creating only one poison task. multiple poison tasks creation is also working well. but considering the global arrays, one poison task is a better choice.

## Three parts for Threads
* Thread initialization: 
  1. initialize mutex, condition variable and semaphore.
  2. map each file.
  3. segment each file into smaller pieces by chunk size and assign tasks.
  4. submit each task.
  5. create a poison task at the end of `TASK_QUEUE` to inform there is no task and will not be more tasks in TASK_QUEUE
* Thread process: 
  1. using mutex to ensure only one task is taken from queue at the same time.
  2. call `pthread_cond_wait` when there is no unprocessed task in TASK_QUEUE when all the tasks are not processed yet.
  3. using encoder to get the task-related result and assign it to `RESULT_QUEUE`.
  4. check if current task is a poison task, create a poison result and add it to `RESULT_QUEUE` if so.
  5. call `up(&READYU_QUEUE[i])` to inform this task is ready to write.
* Thread submission:
  1. using mutex to ensure only one task is submitted to task queue at the same time.
  2. call `pthread_cond_signal` to tell task queue has an unprocessed task for threads to take.

## REFERENCE
[getopt(3) — Linux manual page](https://man7.org/linux/man-pages/man3/getopt.3.html)

[Run Length Encoding and Decoding](https://www.geeksforgeeks.org/run-length-encoding/)

[Read/Write Structure From/to a File in C](https://www.geeksforgeeks.org/read-write-structure-from-to-a-file-in-c/)

[Example of Parsing Arguments with getopt](https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html)

[Thread Pools in C (using the PTHREAD API)](https://www.youtube.com/watch?v=_n2hE2gyPxU)

[pthread: pthread_cond_signal() from within critical section](https://stackoverflow.com/questions/1640389/pthreads-pthread-cond-signal-from-within-critical-section)

[Multithreading in C](https://www.geeksforgeeks.org/multithreading-in-c/)

[memcpy() in C/C++](https://www.geeksforgeeks.org/memcpy-in-cc/)

[unsigned char in C with Examples](https://www.geeksforgeeks.org/unsigned-char-in-c-with-examples/)

[Producer-Consumer. Consumer wait while all producers are done, poison pill](https://stackoverflow.com/questions/52525656/producer-consumer-consumer-wait-while-all-producers-is-done-poison-pill)

