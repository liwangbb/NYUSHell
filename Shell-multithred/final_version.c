#include <stdio.h>          // stderr, stdout, perror, fwrite, fprintf
#include <stdlib.h>         // exit, EXIT_FAILURE, EXIT_SUCCESS, malloc
#include <stdbool.h>        // used for IS_ALL_PROCESSED
#include <unistd.h>

#include <semaphore.h>      // sem_init, sem_post, sem_wait
#include <string.h>         // memcpy
#include <limits.h>         // UINT_MAX
#include <sys/mman.h>       // mmap
#include <sys/stat.h>       // fstat
#include <fcntl.h>          // open, O_*
#include <pthread.h>        // pthread_t, pthread_create, phthread_exit, pthread_join


#define CHUNK_SIZE 4096     // Chunk size set to 4KB (4096 Bytes)
#define MAX_CHAR_LEN 255    // no character will appear more than 255 times in a row
#define MAX_FILE_NUM 100    // no more than 100 files
#define MAX_TASK_NUM 250000 // total size is less than 1GB, 1e6/4 = 250000



// Task struct for mapped address, start index, end index and id
typedef struct {
    const char *addr;
    size_t start;
    size_t end;
    size_t id;
} Task;

// Result struct for collecting result for each task in buffer with its length
typedef struct {
    unsigned char *buffer;              // Result char buffer (e.g. "a1b2c3" char[])
    size_t len;                         // Length of buffer ("a255b255c255" = 6, )
} Result;


Task* TASK_QUEUE[MAX_TASK_NUM];         // Task queue
Result* RESULT_QUEUE[MAX_TASK_NUM];     // Result queue
bool IS_ALL_PROCESSED = false;          // Tracking for if all of tasks are processed
int SUBMITTED_TASKS = 0;                // Tracking how many tasks are in the task queue
int PROCESSED_TASKS = 0;                // Tracking task id that has been processed

pthread_mutex_t MUTEX_QUEUE;            // Mutex for queue
pthread_cond_t COND_QUEUE;              // Condition variable for task queue
sem_t READY_QUEUE[MAX_TASK_NUM];        // Semaphore for tracking the result is ready to write or not with related id


// Semaphore helper function `down`
void down(sem_t *sem) {
    sem_wait(sem);
}

// Semaphore helper function `up`
void up(sem_t *sem) {
    sem_post(sem);
}

// Error handler helper function
void handle_error(const char *message, int fd) {
    perror(message);
    if (fd != -1) {
        close(fd);
    }
    exit(EXIT_FAILURE);
}



// Parsing function that update the number of threads based on `-j jobs`
int parsing_j(int argc, char **argv) {
    int opt;
    int num_threads = 1;    // for single thread process

    while ((opt = getopt(argc, argv, "j:")) != -1) {
        switch (opt) {
            case 'j':
                num_threads = atoi(optarg);
                if (num_threads <= 0) {
                    return EXIT_FAILURE;
                }
                break;
            default: 
                return EXIT_FAILURE;
        }
    }

    return num_threads;
}

// Encoder function for each task and return the related result
Result* encoder(Task *task) {
    unsigned char current_char = task->addr[task->start];
    unsigned int count = 0;
    size_t len = 0;

    Result *result = malloc(sizeof(Result));

    // create a temporary buffer for later copying to the related result buffer
    unsigned char temp_buffer[(task->end - task->start) * 2];

    for (size_t i = task->start; i < task->end; i++) {
        if (task->addr[i] == current_char && count < MAX_CHAR_LEN) {
            count++;
        } else {
            temp_buffer[len] = current_char;
            temp_buffer[len + 1] = (char)count;
            len +=2;
            current_char = task->addr[i];
            count = 1;
        }
    }
    
    // handle last char in the task
    temp_buffer[len] = current_char;
    temp_buffer[len + 1] = (char)count;
    len +=2;

    result->buffer = malloc(len);
    result->len = len;

    // copy temp_buffer to result buffer
    memcpy(result->buffer, temp_buffer,len);

    return result;
}

// Write and merge results in RESULT_QUEUE parallelly using READY_QUEUE to the `stdout` 
void write_result() {
    unsigned char current_char = '\0';
    unsigned int current_count = 0;
    int i = 0;

    while (1) {
        // wait for the result to be ready
        down(&READY_QUEUE[i]);

        // handle the termination signal
        if (RESULT_QUEUE[i] == NULL || RESULT_QUEUE[i]->len == UINT_MAX) {
            break;
        }

        Result *result = RESULT_QUEUE[i];

        for (size_t j = 0; j < result->len; j += 2) {
            unsigned char result_char = result->buffer[j];
            unsigned char result_count = result->buffer[j + 1];

            if (result_char == current_char) {
                current_count += result_count;

                // split it into multiple entries if the count exceeds MAX_CHAR_LEN
                while (current_count > MAX_CHAR_LEN) {
                    fwrite(&current_char, sizeof(unsigned char), 1, stdout);
                    unsigned char max_count = MAX_CHAR_LEN;
                    fwrite(&max_count, sizeof(unsigned char), 1, stdout);
                    current_count -= MAX_CHAR_LEN;
                }
            } else {

                // write the previous char and count, update current char and count
                if (current_count > 0) {
                    fwrite(&current_char, sizeof(unsigned char), 1, stdout);
                    fwrite(&current_count, sizeof(unsigned char), 1, stdout);
                }
                current_char = result_char;
                current_count = result_count;
            }
        }

        i++;
    }

    // write any remaining char and count
    if (current_count > 0) {
        fwrite(&current_char, sizeof(unsigned char), 1, stdout);
        fwrite(&current_count, sizeof(unsigned char), 1, stdout);
    }
}



/* The idea of creating threads pool is from: [Thread Pools in C (using the PTHREAD API)](https://www.youtube.com/watch?v=_n2hE2gyPxU) */

// Submit tasks to TASK_QUEUE, update the SUBMITTED_TASKS and signal wake up threads to take task from TASK_QUEUE
void task_submission(Task* task) {
    pthread_mutex_lock(&MUTEX_QUEUE);
    TASK_QUEUE[SUBMITTED_TASKS] = task;
    SUBMITTED_TASKS++;
    pthread_cond_signal(&COND_QUEUE);
    pthread_mutex_unlock(&MUTEX_QUEUE);
}

// Worker threads process
void *thread_process(void *args) {
    (void)args;

    while (1) {
        // lock the TASK_QUEUE
        pthread_mutex_lock(&MUTEX_QUEUE);

        // wait for task when there is no unprocessed task in TASK_QUEUE and all the tasks are not processed yet
        while (SUBMITTED_TASKS <= PROCESSED_TASKS && !IS_ALL_PROCESSED) {
            pthread_cond_wait(&COND_QUEUE, &MUTEX_QUEUE);
        }

        Task task = *TASK_QUEUE[PROCESSED_TASKS];        

        // detect if the task is a poison task
        if (task.start == UINT_MAX && task.end == UINT_MAX) {
            // create poison results
            Result *result = malloc(sizeof(Result));
            result->buffer = NULL;
            result->len = UINT_MAX;
            RESULT_QUEUE[task.id] = result;     // at this time PROCESSED_TASKS = SUBMITTED_TASKS
            IS_ALL_PROCESSED = true;            // inform all the tasks are processed
            pthread_cond_signal(&COND_QUEUE);
            up(&READY_QUEUE[task.id]);          // inform this task is ready to write
            pthread_mutex_unlock(&MUTEX_QUEUE);
            break;
        }

        // update the PROCESSED_TASKS and unlock the TASK_QUEUE
        PROCESSED_TASKS++;
        pthread_mutex_unlock(&MUTEX_QUEUE);

        Result* result = encoder(&task);
        RESULT_QUEUE[task.id] = result;
        up(&READY_QUEUE[task.id]);              // inform this task is ready to write
    }

    return NULL;
}

// Read files and submit tasks
void create_tasks_from_file(int argc, char **argv) {
    size_t id = 0;
    for (int arg = optind; arg < argc; arg++) {
        int fd = open(argv[arg], O_RDONLY);
        if (fd == -1) {
            handle_error("open fd failed", -1);
        }

        struct stat sb;
        if (fstat(fd, &sb) == -1) {
            handle_error("get size failed", fd);
        }

        char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED) { 
            handle_error("map file failed", fd);
        }

        // for each file we segment by CHUNK_SIZE(4KB) and assign it to tasks
        for (size_t size = 0; size < (size_t)sb.st_size; size+=CHUNK_SIZE) {
            Task *task = malloc(sizeof(Task));
            task->addr = addr;
            task->start = size; 
            task->end = (size + CHUNK_SIZE > (size_t)sb.st_size) ? (size_t)sb.st_size : size + CHUNK_SIZE;
            task->id = id;
            id++;

            task_submission(task);
        }

        // munmap(addr, sb.st_size);
        close(fd);
    }

    // create a poison task at the end of task queue to inform there is no task and will not be more tasks in TASK_QUEUE
    Task *poison_pill = malloc(sizeof(Task));
    poison_pill->addr = NULL;
    poison_pill->start = UINT_MAX;
    poison_pill->end = UINT_MAX;
    poison_pill->id = id++;
    task_submission(poison_pill);
}

int main(int argc, char **argv) {

    int num_threads = parsing_j(argc, argv);

    // initialize mutex and condition variable for task queue
    pthread_mutex_init(&MUTEX_QUEUE, NULL);
    pthread_cond_init(&COND_QUEUE, NULL);

    // initialize each sem in READY_QUEUE
    for (int i = 0; i < MAX_TASK_NUM; i++) {
        sem_init(&READY_QUEUE[i], 0, 0);
    }

    // initialize threads pool and create threads
    pthread_t threads[num_threads];
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, thread_process, NULL) != 0) {
            handle_error("Failed to create thread", -1);
        }
    }

    create_tasks_from_file(argc, argv);

    write_result();

    // join all worker threads
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    // clean mutex, condition variable and semaphores 
    pthread_mutex_destroy(&MUTEX_QUEUE);
    pthread_cond_destroy(&COND_QUEUE);
    for (int i = 0; i < MAX_TASK_NUM; i++) {
        sem_destroy(&READY_QUEUE[i]);
    }

    // free every task in TASK_QUEUE, every buffer with related result and results in RESULT_QUEUE
    for (int i = 0; i < MAX_TASK_NUM; i++) {
        if (TASK_QUEUE[i] != NULL) {
            free(TASK_QUEUE[i]);
        }
        if (RESULT_QUEUE[i] != NULL) {
            if (RESULT_QUEUE[i]->buffer != NULL) {
                free(RESULT_QUEUE[i]->buffer);
            }
            free(RESULT_QUEUE[i]);
        }
    }

    return 0;
}



/*
For more details and references, please refer to README.md which is also submitted in `nyuenc.zip`
*/