#include <stdio.h>          // stderr, stdout, perror, fwrite, fprintf
#include <stdlib.h>         // exit, EXIT_FAILURE, EXIT_SUCCESS, malloc
#include <unistd.h>
#include <stdbool.h>

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


// Task struct for mapped address, start index and end index
typedef struct {
    const char *addr;
    size_t start;
    size_t end;
    size_t id; // size_t is a special unsigned integer type defined in the standard library of C
} Task;

typedef struct {
    unsigned char *buffer; // result char buffer e.g. "a1b2c3" char[]
    size_t len; // length of "a1b2c3" = 6
} Result;

Result* encoder(Task *task) {
    unsigned char current_char = task->addr[task->start];
    unsigned int count = 0;
    size_t len = 0;

    Result *result = malloc(sizeof(Result));
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
    
    temp_buffer[len] = current_char;
    temp_buffer[len + 1] = (char)count;
    len +=2;

    result->buffer = malloc(len);
    result->len = len;

    // Copy temp_buffer to result buffer
    memcpy(result->buffer, temp_buffer,len);

    return result;
}

Task* TASK_QUEUE[MAX_TASK_NUM];
Result* RESULT_QUEUE[MAX_TASK_NUM]; // init to null
int SUBMITTED_TASKS = 0;
int PROCESSED_TASKS = 0;

pthread_mutex_t MUTEX_QUEUE;
pthread_cond_t COND_QUEUE;
sem_t READY_QUEUE[MAX_TASK_NUM];

void down(sem_t *sem) {
    sem_wait(sem);
}

void up(sem_t *sem) {
    sem_post(sem);
}

// Error handler
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
    // for single-thread processing
    int num_threads = 1;

    while ((opt = getopt(argc, argv, "j:")) != -1) {
        switch (opt) {
            case 'j':
                num_threads = atoi(optarg);
                if (num_threads <= 0) {
                    fprintf(stderr, "Invalid number of threads specified.\n");
                    return EXIT_FAILURE;
                }
                break;
            default:  // '?' case is handled here
                fprintf(stderr, "Usage: %s [-j jobs] <input_file1> [input_file2 ...]\n", argv[0]);
                return EXIT_FAILURE;
        }
    }

    return num_threads;
}


void submit_task(Task* task) {
    pthread_mutex_lock(&MUTEX_QUEUE);
    TASK_QUEUE[SUBMITTED_TASKS] = task;
    SUBMITTED_TASKS++;
    pthread_mutex_unlock(&MUTEX_QUEUE);
    pthread_cond_signal(&COND_QUEUE);
}

void *thread_process(void *args) {
    (void)args;

    while (1) {
        pthread_mutex_lock(&MUTEX_QUEUE);
        while (SUBMITTED_TASKS <= PROCESSED_TASKS) { // SUBMITTED_TASKS > PROCESSED_TASKS
            pthread_cond_wait(&COND_QUEUE, &MUTEX_QUEUE);
        }

        Task task = *TASK_QUEUE[PROCESSED_TASKS];

        // PROCESSED_TASKS++;

        if (task.start == UINT_MAX && task.end == UINT_MAX) {
            // create poison results
            Result *result = malloc(sizeof(Result));
            result->buffer = NULL;
            result->len = UINT_MAX;
            RESULT_QUEUE[task.id] = result; // at this time PROCESSED_TASKS = SUBMITTED_TASKS
            pthread_mutex_unlock(&MUTEX_QUEUE);
            up(&READY_QUEUE[task.id]);
            break;
        }

        PROCESSED_TASKS++;

        pthread_mutex_unlock(&MUTEX_QUEUE);

        Result* result = encoder(&task);
        RESULT_QUEUE[task.id] = result;
        up(&READY_QUEUE[task.id]);
    }

    return NULL;
}

void create_tasks_from_file(int num_threads, int argc, char **argv) {
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

        for (size_t size = 0; size < (size_t)sb.st_size; size+=CHUNK_SIZE) {
            Task *task = malloc(sizeof(Task));
            task->addr = addr;
            task->start = size; 
            task->end = (size + CHUNK_SIZE > (size_t)sb.st_size) ? (size_t)sb.st_size : size + CHUNK_SIZE;
            task->id = id;
            id++;

            submit_task(task);
        }

        // TODO: put them in the end
        // munmap(addr, sb.st_size);
        close(fd);
    }

    // create poison tasks for each thread that break the `threads_process` when all of tasks are finished
    for (int i = 0; i < num_threads; i++) {
        Task *poison_pill = malloc(sizeof(Task));
        poison_pill->addr = NULL;
        poison_pill->start = UINT_MAX;
        poison_pill->end = UINT_MAX;
        poison_pill->id = id++;
        // id++;
        submit_task(poison_pill);
    }
}

/*
a255b255q16
q239v255w16
w239z255
*/

void write_result() {
    unsigned char current_char = '\0';
    unsigned int current_count = 0;

    for (int i = 0; i < SUBMITTED_TASKS; i++) {
        sem_wait(&READY_QUEUE[i]);  // Wait for the result to be ready

        if (RESULT_QUEUE[i] == NULL || RESULT_QUEUE[i]->len == UINT_MAX) {
            // Handle the termination signal
            break;
        }

        Result *result = RESULT_QUEUE[i];

        for (size_t j = 0; j < result->len; j += 2) {
            unsigned char result_char = result->buffer[j];
            unsigned char result_count = result->buffer[j + 1];

            if (result_char == current_char) {
                current_count += result_count;

                // If the count exceeds MAX_CHAR_LEN, split it into multiple entries
                while (current_count > MAX_CHAR_LEN) {
                    fwrite(&current_char, sizeof(unsigned char), 1, stdout);
                    unsigned char max_count = MAX_CHAR_LEN;
                    fwrite(&max_count, sizeof(unsigned char), 1, stdout);
                    current_count -= MAX_CHAR_LEN;
                }
            } else {
                // Write the previous character and count, if any
                if (current_count > 0) {
                    fwrite(&current_char, sizeof(unsigned char), 1, stdout);
                    fwrite(&current_count, sizeof(unsigned char), 1, stdout);
                }
                current_char = result_char;
                current_count = result_count;
            }
        }
    }

    // Write any remaining character and count
    if (current_count > 0) {
        fwrite(&current_char, sizeof(unsigned char), 1, stdout);
        fwrite(&current_count, sizeof(unsigned char), 1, stdout);
    }
}


void print_result() {
    // print all result to check if each task is correct
    printf("Results:\n");
    for (int i = 0; i < MAX_TASK_NUM; i++) {
        Result *result = RESULT_QUEUE[i];
        if (result == NULL || result->buffer == NULL) {
            continue;  // Skip null results
        }
        printf("Result %d: ", i);
        for (size_t j = 0; j < result->len; j += 2) {
            printf("%c%d", result->buffer[j], result->buffer[j + 1]);
        }
        printf("\n");
    }
}

int main(int argc, char **argv) {
    // 1. parse thread number 1 - single-thread , or multi-thread
    int num_threads = parsing_j(argc, argv);

    // 2. init thread pool
    // thread_init(num_threads);

    // initialize mutex and condition variable for task queue
    pthread_mutex_init(&MUTEX_QUEUE, NULL);
    pthread_cond_init(&COND_QUEUE, NULL);

    for (int i = 0; i < MAX_TASK_NUM; i++) {
        sem_init(&READY_QUEUE[i], 0, 0);
    }

    // Create threads
    pthread_t threads[num_threads];
    for (int i = 0; i < num_threads; i++) {
        if (pthread_create(&threads[i], NULL, thread_process, NULL) != 0) {
            handle_error("Failed to create thread", -1);
        }
    }

    // 3. read file and submit tasks
    create_tasks_from_file(num_threads, argc, argv);

    // merge result and write to STDOUT
    write_result();

    // Join all worker threads
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    // print_result();
    // write_result();

    // Clean resource 
    // free malloc
    pthread_mutex_destroy(&MUTEX_QUEUE);
    pthread_cond_destroy(&COND_QUEUE);
    for (int i = 0; i < MAX_TASK_NUM; i++) {
        sem_destroy(&READY_QUEUE[i]);
    }

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
