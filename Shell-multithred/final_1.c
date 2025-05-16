#include <stdio.h>          // stderr, stdout, perror, fwrite, fprintf
#include <stdlib.h>         // exit, EXIT_FAILURE, EXIT_SUCCESS, malloc
#include <unistd.h>

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
    unsigned char count = 0;
    unsigned char len = 0;

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

    result->buffer = (unsigned char *)malloc(len);
    result->len = len;
    // Copy temp_buffer to result buffer
    memcpy(result->buffer, temp_buffer,len);

    // Allocate temporary non-const buffer for manipulation
    // char *temp_result_buffer = malloc((task->end - task->start) * 2);
    // // Copy encoded data to the temporary result buffer
    // memcpy(temp_result_buffer, temp_buffer, len);

    // // Assign the temporary buffer to the Result struct
    // result->buffer = temp_result_buffer; // Assign the non-const buffer to the const char * field
    // result->len = len;

    // free(temp_result_buffer);

    return result;
}

Task* TASK_QUEUE[MAX_TASK_NUM];
Result* RESULT_QUEUE[MAX_TASK_NUM]; // init to null
int SUBMITTED_TASKS = 0;
int PROCESSED_TASKS = 0;

pthread_mutex_t MUTEX_QUEUE;
pthread_cond_t COND_QUEUE;

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
    /*
    printf("task id %ld\n", task->id);
    for (size_t j = task->start; j < task->end; j += 2) {
            printf("%c", task->addr[j]);
        }
    printf("\n");
    */
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

        // if (task.start == UINT_MAX) {
        //     // poison result
        //     Result *result = malloc(sizeof(Result));
        //     result->buffer = NULL;
        //     result->len = UINT_MAX;
        //     RESULT_QUEUE[PROCESSED_TASKS] = result; // at this time PROCESSED_TASKS = SUBMITTED_TASKS
        //     pthread_mutex_unlock(&MUTEX_QUEUE);
        //     return NULL;;
        // }

        PROCESSED_TASKS ++;

        if (task.start == UINT_MAX) {
            Result* result = malloc(sizeof(Result));
            result->buffer = NULL;
            result->len = UINT_MAX;
            RESULT_QUEUE[task.id] = result;
            pthread_mutex_unlock(&MUTEX_QUEUE);
            //pthread_cond_signal(&COND_QUEUE); // todo: so maybe one poison is enough
            break;
        }

        pthread_mutex_unlock(&MUTEX_QUEUE);

        Result* result = encoder(&task);
        RESULT_QUEUE[task.id] = result;
    }

    return NULL;
}


// void signal_workers_to_finish(int num_threads) {
//     pthread_mutex_lock(&MUTEX_QUEUE);
//     SUBMITTED_TASKS = -1; // Use -1 or another special value to indicate shutdown
//     pthread_cond_broadcast(&COND_QUEUE); // Wake up all worker threads
//     pthread_mutex_unlock(&MUTEX_QUEUE);
// }

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

            //printf("task cut id %ld start %ld end %ld\n", task->id, task->start, task->end);

            submit_task(task);
        }

        // TODO: put them in the end
        // munmap(addr, sb.st_size);
        close(fd);
    }

    // poison tasks
    // Task *task = malloc(sizeof(Task));
    // task->addr = NULL;
    // task->start = UINT_MAX; 
    // task->end = UINT_MAX;
    // task->id = id;
    // id++;
    // submit_task(task);

    for (int i = 0; i < num_threads; i++) {
        Task *poison_pill = malloc(sizeof(Task));
        poison_pill->addr = NULL;
        poison_pill->start = UINT_MAX;
        poison_pill->end = UINT_MAX;
        poison_pill->id = id++;
        submit_task(poison_pill);
    }
}

// int thread_init(int num_threads){

//     // initialize mutex and condition variable for task queue
//     pthread_mutex_init(&MUTEX_QUEUE, NULL);
//     pthread_cond_init(&COND_QUEUE, NULL);

//     // Create threads
//     pthread_t threads[num_threads];
//     for (int i = 0; i < num_threads; i++) {
//         if (pthread_create(&threads[i], NULL, thread_process, NULL) != 0) {
//             handle_error("Failed to create thread", -1);
//         }
//     }
// }
// a2b3c3
// c2e2d3
// b3

// 'a' (97)  NAK (21)
// a2c2

// b1a2
// a3 -> a5
// a4 -> a9

// 1 need mutex ?
// 2 how to merge
// 3 how to know all result is processed - poison
void write_result() {

    while (RESULT_QUEUE[0] == NULL);
    int i = 0;
    unsigned char prev = RESULT_QUEUE[0]->buffer[0]; // a
    unsigned int count = 0;
    while(1) {
        if (RESULT_QUEUE[i]->len == UINT_MAX && RESULT_QUEUE[i]->buffer == NULL) {
        break;
        } 
        //printf("\ni = %d\n", i);
        Result* result = RESULT_QUEUE[i];

        // printf("result len : %ld\n", result->len);
        // for (unsigned int temp = 0; temp < result->len; temp++) {
        // printf("%d", result->buffer[temp]);
        // }
        // printf("\n");
        
        if (result->buffer[0] == prev) {
        //count += (unsigned int)result->buffer[1]; // count = 5
        result->buffer[1] += count; // 5
        //printf("count = %d\n", result->buffer[1]);            
        }

        if (result->len > 2) {
            printf("out1 %c%d", result->buffer[0], result->buffer[1]);
        }

        for (unsigned int temp = 2; temp < result->len - 2; temp = temp + 2) {
            printf("out2 %c%d", result->buffer[temp], result->buffer[temp+1]);
            }

        prev = result->buffer[result->len - 2]; // a
        count = (unsigned int)result->buffer[result->len - 1]; // 2

        i++;
    }
    if (prev == '\n') {
        printf("out3 %c%d", prev, count);
    } else {
        printf("out4 %c%d", prev, count);
    }
}

// void write_result() {
//     if (RESULT_QUEUE[0] == NULL) {
//         return;  // No results to process.
//     }

//     unsigned char current_char = RESULT_QUEUE[0]->buffer[0];
//     int current_count = 0;
//     int i = 0;

//     while (1) {
//         if (RESULT_QUEUE[i] == NULL || RESULT_QUEUE[i]->len == UINT_MAX) {
//             break;
//         }

//         Result* result = RESULT_QUEUE[i];

//         for (size_t j = 0; j < result->len; j += 2) {
//             if (current_char == result->buffer[j]) {
//                 // If the character is the same, accumulate the count.
//                 current_count += result->buffer[j + 1];
//             } else {
//                 // Different character, write the previous character and count, then update.
//                 fwrite(&current_char, sizeof(unsigned char), 1, stdout);
//                 fprintf(stdout, "%d", current_count);  // Convert int count to string.
                
//                 current_char = result->buffer[j];
//                 current_count = result->buffer[j + 1];
//             }
//         }

//         i++;
//     }

//     // Write the last character and count.
//     fwrite(&current_char, sizeof(unsigned char), 1, stdout);
//     fprintf(stdout, "%d", current_count);  // Convert int count to string.
// }

// void write_result() {
//     if (RESULT_QUEUE[0] == NULL) {
//         return;  // No results to process.
//     }

//     unsigned char current_char = RESULT_QUEUE[0]->buffer[0];
//     unsigned int current_count = 0;  // Use an int to accumulate the count.

//     for (int i = 0; i < MAX_TASK_NUM; i++) {
//         if (RESULT_QUEUE[i] == NULL || RESULT_QUEUE[i]->len == UINT_MAX) {
//             break;  // End of results.
//         }

//         Result *result = RESULT_QUEUE[i];
        
//         for (size_t j = 0; j < result->len; j += 2) {
//             unsigned char result_char = result->buffer[j];
//             unsigned char result_count = result->buffer[j + 1];

//             if (result_char == current_char) {
//                 // Accumulate count, being careful to split counts if they exceed 255.
//                 current_count += result_count;
//                 if (current_count > 255) {
//                     fwrite(&current_char, sizeof(unsigned char), 1, stdout);
//                     unsigned char max_count = 255;
//                     fwrite(&max_count, sizeof(unsigned char), 1, stdout);
//                     current_count -= 255;
//                 }
//             } else {
//                 if (current_count > 0) {
//                     fwrite(&current_char, sizeof(unsigned char), 1, stdout);
//                     unsigned char count_to_write = (unsigned char)current_count;
//                     fwrite(&count_to_write, sizeof(unsigned char), 1, stdout);
//                 }
//                 current_char = result_char;
//                 current_count = result_count;
//             }
//         }
//     }

//     if (current_count > 0) {
//         // Write the final character and count.
//         fwrite(&current_char, sizeof(unsigned char), 1, stdout);
//         unsigned char count_to_write = (unsigned char)current_count;
//         fwrite(&count_to_write, sizeof(unsigned char), 1, stdout);
//     }
// }

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
    // write_result();

    // Join all worker threads
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    write_result();

    print_result();

    // Clean resource 
    // free malloc
    pthread_mutex_destroy(&MUTEX_QUEUE);
    pthread_cond_destroy(&COND_QUEUE);

    // for (int i = 0; i < MAX_TASK_NUM; i++) {
    //         free(TASK_QUEUE[i]);
    //         free(RESULT_QUEUE[i]);
    // }
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
