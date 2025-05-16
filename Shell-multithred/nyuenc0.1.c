#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>

#define CHUNK_SIZE 4096
#define MAX_CHAR_LEN 255

pthread_mutex_t mutex;

typedef struct {
    const char *addr;
    FILE *output_file;
    long head;
    long tail;
} ThreadData;

void handle_error(const char *message, int fd) {
    perror(message);
    if (fd != -1) {
        close(fd);
    }
    exit(EXIT_FAILURE);
}

int parsing_j(int argc, char **argv) {
    int opt;
    int num_threads = 0;

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

void encoder(const char *filename, unsigned char *current_char, unsigned char *count, FILE *output_file) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        handle_error("open fd failed", -1);
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        handle_error("get sb failed", fd);
    }

    char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) {
        handle_error("map file failed", fd);
    }

    for (size_t i = 0; i < (size_t)sb.st_size; i++) {
        if (addr[i] == *current_char && *count < MAX_CHAR_LEN) {
            (*count)++;
        } else {
            if (*count > 0) {
                // Write the current character and its count to stdout
                fwrite(current_char, sizeof(char), 1, output_file);
                fwrite(count, sizeof(char), 1, output_file);
            }
            *current_char = addr[i];
            *count = 1;
        }
    }

    munmap(addr, sb.st_size);
    close(fd);
}

void process_sequentially(int argc, char **argv, int start_index, FILE *output_file) {
    unsigned char current_char = 0;
    unsigned char count = 0;

    for (int arg = start_index; arg < argc; arg++) {
        encoder(argv[arg], &current_char, &count, output_file);
    }

    if (count > 0) {
        fwrite(&current_char, sizeof(char), 1, output_file);
        fwrite(&count, sizeof(char), 1, output_file);
    }
}

void *thread_function(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    unsigned char current_char = data->addr[data->head];
    unsigned char count = 1;

    for (long i = data->head; i < data->tail; i++) {
        if (data->addr[i] == current_char && count < MAX_CHAR_LEN) {
            count++;
        } else {
            pthread_mutex_lock(&mutex);
            fwrite(&current_char, sizeof(char), 1, stdout);
            fwrite(&count, sizeof(char), 1, stdout);
            pthread_mutex_unlock(&mutex);

            current_char = data->addr[i];
            count = 1;
        }
    }

    // Handle the last sequence
    pthread_mutex_lock(&mutex);
    if (count > 0) {
        fwrite(&current_char, sizeof(char), 1, stdout);
        fwrite(&count, sizeof(char), 1, stdout);
    }
    pthread_mutex_unlock(&mutex);

    return NULL;
}

int threads_initialize(char **argv, int num_threads) {
    int fd = open(argv[1], O_RDONLY);
    if (fd == -1) {
        handle_error("open fd failed", -1);
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        handle_error("get sb failed", fd);
    }

    char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (addr == MAP_FAILED) {
        handle_error("map file failed", fd);
    }

    // Initialize the array of thread parameters
    pthread_t threads[num_threads];
    ThreadData thread_data[num_threads];

    // Create threads
    for (int i = 0; i < num_threads; i++) {
        thread_data[i].addr = addr;
        thread_data[i].output_file = stdout;
        thread_data[i].head = i * CHUNK_SIZE;
        thread_data[i].tail = (i + 1) * CHUNK_SIZE;
        if (i == num_threads - 1) {
            thread_data[i].tail = sb.st_size;  // Handle the last segment
        }

        if (pthread_create(&threads[i], NULL, thread_function, &thread_data[i]) != 0) {
            handle_error("Failed to create thread", fd);
        }
    }



    // Join threads
    for (int i = 0; i < num_threads; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            handle_error("Failed to join thread", fd);
        }
    }

    munmap(addr, sb.st_size);
    close(fd);
    return EXIT_SUCCESS;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input_file1> [input_file2 ...]\n", argv[0]);
        return -1;
    }

    int num_threads = parsing_j(argc, argv);

    if (optind >= argc) {
        fprintf(stderr, "Expected at least one input file.\n");
        return -1;
    }

    if (num_threads <= 1) {  // Using <= 1 since num_threads might be 0 (default) or 1 (explicit)
        process_sequentially(argc, argv, optind, stdout);
    } else {
        threads_initialize(argv, num_threads);
    }

    pthread_mutex_destroy(&mutex);

    return 0;
}
