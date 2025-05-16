#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>                 // unsigned short
#include <unistd.h>                 // getopt()
#include <fcntl.h>                  // open(), close()
#include <sys/stat.h>               // stat
#include <sys/mman.h>               // mmap()
#include <string.h>                 // strcmp(), memcpy()
#include <openssl/sha.h>            // SHA1()
#include "nyufile.h"                // BootEntry, DirEntry

#define SHA_DIGEST_LENGTH 20

unsigned char *SHA1(const unsigned char *d, size_t n, unsigned char *md);


void invalid_command_handler() {
    printf("Usage: ./nyufile disk <options>\n"
           "  -i                     Print the file system information.\n"
           "  -l                     List the root directory.\n"
           "  -r filename [-s sha1]  Recover a contiguous file.\n"
           "  -R filename -s sha1    Recover a possibly non-contiguous file.\n");
    exit(1);
}

// Helper function to convert hex string to binary data
// The usage of "hhx" is from https://www.remlab.net/op/integer.shtml
void hex_to_binary(const char *sha1, unsigned char *sha1_binary) {
    for (size_t i = 0; i < SHA_DIGEST_LENGTH; i++) {
        sscanf(sha1 + 2 * i, "%2hhx", &sha1_binary[i]);
    }
}

// Map the disk image into addr
char *disk_mapped(char *arg) {
    const char *disk = arg;

    int fd = open(disk, O_RDWR);
    if (fd == -1) {
        invalid_command_handler();
        return NULL;
    }

    struct stat sb;
    if (fstat(fd, &sb) == -1) {
        perror("Failed to get file size");
        close(fd);
        return NULL;
    }

    char *addr = mmap(NULL, sb.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("Failed to map the file");
        close(fd);
        return NULL;
    }

    return addr;
}

// Helper function that display the file system information
int display_fsinfo(BootEntry *be) {
    printf("Number of FATs = %d\n", be->BPB_NumFATs);
    printf("Number of bytes per sector = %d\n", be->BPB_BytsPerSec);
    printf("Number of sectors per cluster = %d\n", be->BPB_SecPerClus);
    printf("Number of reserved sectors = %d\n", be->BPB_RsvdSecCnt);

    return 0;
}

// Helper function that combine the high 2 bytes and low 2 bytes of the cluster address of the entry
unsigned int get_start_cluster(DirEntry *entry) {
    // 4 * (2 + 2) = 16
    // the idea is from https://wiki.osdev.org/FAT
    return (entry->DIR_FstClusHI << 16) | entry->DIR_FstClusLO;
}

// Helper function that format the filename of the entry into 8.3 format
char *format83(DirEntry *entry) {
    // 11 + 1('.') + 1('\0') = 13
    char *temp_name = malloc(13 * sizeof(char));
    int name_len = 0;

    // transfer filename
    for (int i = 0; i < 8; i++) {
        if (entry->DIR_Name[i] == ' ') break;
        temp_name[name_len] = entry->DIR_Name[i];
        name_len++;
    }

    // check if there is an extension and transfer it
    if (entry->DIR_Name[8] != ' ') {
        temp_name[name_len] = '.';
        name_len++;
        for (int i = 8; i < 11; i++) {
            if (entry->DIR_Name[i] == ' ') break;
            temp_name[name_len] = entry->DIR_Name[i];
            name_len++;
        }
    }

    // set the end of filename to null terminator
    temp_name[name_len] = '\0';
    
    return temp_name;
}

// Helper function that check the filename is in 8.3 format or not
// The filename contains only uppercase letters, numbers, and the following special characters: ! # $ % & ' ( ) - @ ^ _ ` { } ~
int filename_checker(const char *cleaned_filename) {
    int name_len = 0, extension_len = 0, is_dot = 0;
    static const char *valid_char = "!#$%&'()-@^_`{}~";

    for (int i = 0; cleaned_filename[i]; i++) {
        char ch = cleaned_filename[i];

        // check if the current char is a dot
        if (ch == '.' ) {
            // invalid if dot is already exist before
            if (is_dot) return 0;

            is_dot = 1;
            continue;
        }

        // check if the current char is valid
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || strchr(valid_char, ch))) {
            return 0;
        }

        // check if no extension in name
        if (!is_dot) {
            name_len++;

            // invalid if the length of name > 8
            if (name_len > 8) return 0;

        // extension exists in name
        } else {
            extension_len++;

            // invalid if the length of extension > 3
            if (extension_len > 3) return 0;
        }
    }

    return 1;
}

// Helper function that display the entry's information based on entry's attribute and size
int display_entries(DirEntry *entry) {
    char *cleaned_filename = format83(entry);

    // check if filename valid
    if (!filename_checker(cleaned_filename)) {
        free(cleaned_filename);
        return 0;
    }

    // calculate the starting cluster from the entry
    unsigned int current_cluster = get_start_cluster(entry);

    // the entry is a directory
    if (entry->DIR_Attr & 0x10) {
        printf("%s/ (starting cluster = %u)\n", cleaned_filename, current_cluster);

    // the entry is a file
    } else {
        // check if the file is empty or not
        if (entry->DIR_FileSize == 0) {
            printf("%s (size = 0)\n", cleaned_filename);
        } else {
            printf("%s (size = %u, starting cluster = %u)\n", cleaned_filename, entry->DIR_FileSize, current_cluster);
        }
    }

    free(cleaned_filename);
    return 1;
}

// Helper function that find the DirEntry of the specified cluster
DirEntry *cluster_to_dir(BootEntry *be, char *addr, unsigned int current_cluster) {
    unsigned int data_area_start = be->BPB_RsvdSecCnt + (be->BPB_NumFATs * be->BPB_FATSz32);    // calculate the start of data area
    unsigned int dir_sector = data_area_start + (current_cluster - 2) * be->BPB_SecPerClus;     // calculate the current directory sector
    unsigned int dir_offset = dir_sector * be->BPB_BytsPerSec;                                  // calculate the bytes offset

    DirEntry *entry = (DirEntry *)(addr + dir_offset);
    return entry;
}

// Helper function that get the next cluster from the current cluster using FAT
unsigned int get_next_cluster(unsigned int current_cluster, void *addr, BootEntry *be) {
    unsigned int fat_offset = be->BPB_RsvdSecCnt * be->BPB_BytsPerSec;                          // calculate the start of FAT

    unsigned int *fat = (unsigned int *)((char *)addr + fat_offset);
    return fat[current_cluster];
}

// List the root directory
int display_rootinfo(BootEntry *be, char *addr) {
    unsigned int current_cluster = be->BPB_RootClus;
    int total_num_entries = 0;
    int entries_per_cluster = (be->BPB_BytsPerSec * be->BPB_SecPerClus) / sizeof(DirEntry);

    // check all entries in root directory
    while (current_cluster < 0x0FFFFFF8) {
        DirEntry *entry = cluster_to_dir(be, addr, current_cluster);

        for (int i = 0; i < entries_per_cluster; i++) {
            if (entry[i].DIR_Name[0] != 0xE5 && entry[i].DIR_Name[0] != '\0') {
                int is_displayed = display_entries(&entry[i]);
                total_num_entries += is_displayed;
            }
        }
        current_cluster = get_next_cluster(current_cluster, addr, be);
    }
    printf("Total number of entries = %d\n", total_num_entries);
    return 0;
}

// Check if the current_entry is deleted and matchs the file name
int is_recoverable(DirEntry *current_entry, const char *filename) {
    if (current_entry->DIR_Name[0] == 0xE5) {
        char *cleaned_name = format83(current_entry);

        if (strcmp(cleaned_name + 1, filename + 1) == 0) {
            free(cleaned_name);
            return 1;
        }

        free(cleaned_name);
    }

    return 0;
}

// Helper function that calculate how many cluster does the file needs
int file_cluster_counter(DirEntry *current_entry, int cluster_size) {
    return (current_entry->DIR_FileSize + cluster_size - 1) / cluster_size;
}

// Helper function for getting recoverable entries from the root directory with given filename
int get_recoverable_files(BootEntry *be, char *addr, const char *filename, DirEntry **recoverable_entries) {
    unsigned int current_cluster = be->BPB_RootClus;
    int entries_per_cluster = (be->BPB_BytsPerSec * be->BPB_SecPerClus) / sizeof(DirEntry);
    int num_recoverable = 0;

    // check all entries in root directory
    while (current_cluster < 0x0FFFFFF8) {
        DirEntry *entries = cluster_to_dir(be, addr, current_cluster);

        for (int i = 0; i < entries_per_cluster; i++) {
            DirEntry *current_entry = &entries[i];

            if (is_recoverable(current_entry, filename)) {
                recoverable_entries[num_recoverable] = current_entry;
                num_recoverable++;
            }
        }

        current_cluster = get_next_cluster(current_cluster, addr, be);
    }

    return num_recoverable;
}

// Update FAT information for contiguously-allocated file
void fat_updater(unsigned int start_cluster, int cluster_count, BootEntry *be, char *addr){
    unsigned int fat_offset = be->BPB_RsvdSecCnt * be->BPB_BytsPerSec;
    unsigned int fat_size = be->BPB_FATSz32 * be->BPB_BytsPerSec;

    for (unsigned int i = 0; i < be->BPB_NumFATs; ++i) {
        unsigned int *fat = (unsigned int *)(addr + fat_offset + i * fat_size);
        unsigned int current_cluster = start_cluster;
        for (int j = 0; j < cluster_count; j++) {
            if (j == cluster_count - 1) {
                fat[current_cluster] = EOF;
            } else {
                fat[current_cluster] = current_cluster + 1;
            }
            current_cluster++;
        }
    }
}

// Helper function that copy the content of the file for SHA1 use
void copy_entry_content(BootEntry *be, char *addr, DirEntry *current_entry, unsigned int file_size, unsigned char **entry_content) {
    unsigned int entry_start_cluster = get_start_cluster(current_entry);
    unsigned int cluster_size = be->BPB_SecPerClus * be->BPB_BytsPerSec;

    *entry_content = malloc(file_size);
    unsigned int copied_offset = 0;
    unsigned int current_cluster = entry_start_cluster;
    unsigned int rest_copy_size = file_size;

    while (rest_copy_size > 0) {
        unsigned int *cluster_offset = (unsigned int *)cluster_to_dir(be, addr, current_cluster);
        unsigned int bytes_to_copy = rest_copy_size > cluster_size ? cluster_size : rest_copy_size;

        memcpy(*entry_content + copied_offset, cluster_offset, bytes_to_copy);
        copied_offset += bytes_to_copy;
        rest_copy_size -= bytes_to_copy;
        current_cluster++;
    }
}

// Helper function that calculate the SHA1 for specific entry
unsigned char *sha1_calculator(BootEntry *be, char *addr, DirEntry *current_entry) {
    unsigned int file_size = current_entry->DIR_FileSize;
    unsigned char *entry_content = NULL;
    unsigned char *md = malloc(SHA_DIGEST_LENGTH);

    if (file_size == 0) {
        // unsigned char *empty_sha1 = 'da39a3ee5e6b4b0d3255bfef95601890afd80709';
        // the usage of `\x` is from https://stackoverflow.com/questions/7521007/sha1-hash-binary-20-to-string-41
        memcpy(md, "\xda\x39\xa3\xee\x5e\x6b\x4b\x0d\x32\x55\xbf\xef\x95\x60\x18\x90\xaf\xd8\x07\x09", SHA_DIGEST_LENGTH);
        // return md;
    } else {
        copy_entry_content(be, addr, current_entry, file_size, &entry_content);
        SHA1(entry_content, file_size, md);
        free(entry_content);
    }

    return md;
}

// Helper function that check each of the recoverable entry is matched the given SHA1, return NULL if none of them matched
DirEntry *match_entry_sha1(DirEntry **recoverable_entries, int entries_count, BootEntry *be, char *addr, unsigned char *sha1) {
    for (int i = 0; i < entries_count; i++) {
        DirEntry *current_entry = recoverable_entries[i];
        unsigned char *md = sha1_calculator(be, addr, current_entry);

        if (memcmp(md, sha1, SHA_DIGEST_LENGTH) == 0) {
            free(md);
            return current_entry;
        }
        free(md);
    }

    return NULL;
}

// Helper function that recover the filename, reverse the FAT information and flush the changes
void recover_current_entry(DirEntry *current_entry, const char *filename, BootEntry *be, char *addr) {
    int cluster_size = be->BPB_SecPerClus * be->BPB_BytsPerSec;

    unsigned int entry_start_cluster = get_start_cluster(current_entry);
    int cluster_count = file_cluster_counter(current_entry, cluster_size);
    current_entry->DIR_Name[0] = filename[0];
    fat_updater(entry_start_cluster, cluster_count, be, addr);

    msync(addr, be->BPB_BytsPerSec * be->BPB_TotSec32, MS_SYNC);
}

// Main function for recovering contiguously-allocated file
int recover_cont_file(BootEntry *be, char *addr, const char *filename, char *sha1) {
    DirEntry *recoverable_entries[100];
    unsigned char sha1_binary[SHA_DIGEST_LENGTH];

    // get recoverable entries and the number of recoverable entries from root directory
    int num_recoverable = get_recoverable_files(be, addr, filename, recoverable_entries);

    // the number of recoverable files is one
    if (num_recoverable == 1) {
        // if the SHA-1 is not provide
        if (sha1 == NULL) {
            recover_current_entry(recoverable_entries[0], filename, be, addr);
            printf("%s: successfully recovered\n", filename);
        } else {
            hex_to_binary(sha1, sha1_binary);
            DirEntry *matched_entry = match_entry_sha1(recoverable_entries, num_recoverable, be, addr, sha1_binary);

            if (matched_entry) {
                recover_current_entry(matched_entry, filename, be, addr);
                printf("%s: successfully recovered with SHA-1\n", filename);
            } else {
                printf("%s: file not found\n", filename);
            }
        }
    
    // multiple recoverable files exist
    } else if (num_recoverable > 1) {
        // if the SHA-1 is not provide
        if (sha1 == NULL) {
            printf("%s: multiple candidates found\n", filename);
        } else {
            hex_to_binary(sha1, sha1_binary);
            DirEntry *matched_entry = match_entry_sha1(recoverable_entries, num_recoverable, be, addr, sha1_binary);

            if (matched_entry) {
                recover_current_entry(matched_entry, filename, be, addr);
                printf("%s: successfully recovered with SHA-1\n", filename);
            } else {
                printf("%s: file not found\n", filename);
            }
        }
    
    // no recoverable file exists
    } else {
        printf("%s: file not found\n", filename);
    }

    return 0;
}

// Helper function for check given cluster is unallocated or not
int is_unallocated(BootEntry *be, char *addr, unsigned int i) {
    unsigned int fat_offset = be->BPB_RsvdSecCnt * be->BPB_BytsPerSec;
    unsigned int entry_offset = i * 4;  // 4 bytes per FAT32 entry
    unsigned int *fat_table = (unsigned int *)(addr + fat_offset + entry_offset);

    if (*fat_table == 0x00000000) {
        return 1;
    } else {
        return 0;
    }
}

// Helper function for getting unallocated clusters index
int get_unallocated_cluster(BootEntry *be, char *addr, int *unallocated_array) {
    int unallocated_count = 0;

    for (int i = 0; i < 20; i++) {
        if (is_unallocated(be, addr, i)) {
            unallocated_array[unallocated_count] = i;
            unallocated_count++;
        }
    }

    return unallocated_count;
}

// Helper recursive function for brute-force checking unallocated clusters
int cluster_permutation(BootEntry *be, char *addr, DirEntry *entry, unsigned char *file_content, int current_depth, int total_cluster_num, unsigned char *sha1, int *unallocated_array, int num_unallocated, unsigned int *matched_cluster) {
    unsigned int cluster_size = be->BPB_BytsPerSec * be->BPB_SecPerClus;

    // check if the depth is equal to the total number of clusters needed
    if (current_depth == total_cluster_num) {
        unsigned char md[SHA_DIGEST_LENGTH];
        SHA1(file_content, entry->DIR_FileSize, md);

        // check if the content in clusters combination matched with SHA-1
        if (memcmp(md, sha1, SHA_DIGEST_LENGTH) == 0) {
            return 1;
        }
        return 0;
    }

    for (int i = 0; i < num_unallocated; i++) {
        // current cluster is avaiable for search
        if (unallocated_array[i] != -1) {
            int previous_cluster = unallocated_array[i];

            // mark cluster as used
            unallocated_array[i] = -1;

            // copy the content from the cluster based on the rest copy size relative to total file size
            unsigned int rest_copy_size = entry->DIR_FileSize - (current_depth * cluster_size);
            unsigned int bytes_to_copy = (rest_copy_size > cluster_size) ? cluster_size : rest_copy_size;
            unsigned int *cluster_offset = (unsigned int *)cluster_to_dir(be, addr, previous_cluster);
            memcpy(file_content + current_depth * cluster_size, cluster_offset, bytes_to_copy);

            // store cluster index
            matched_cluster[current_depth] = previous_cluster;

            // recursively check
            if (cluster_permutation(be, addr, entry, file_content, current_depth + 1, total_cluster_num, sha1, unallocated_array, num_unallocated, matched_cluster)) {
                // if matched
                return 1;
            }

            // reset cluster as available
            unallocated_array[i] = previous_cluster;
        }
    }
    // no valid combination found at this depth
    return 0;
}

// Helper function for checking if the given entry matched with given SHA-1
int matched_permutation(BootEntry *be, char *addr, DirEntry *entry, unsigned char *sha1, int *unallocated_array, int num_unallocated, unsigned int *matched_cluster) {
    unsigned int file_size = entry->DIR_FileSize;
    unsigned char *file_content = malloc(file_size);
    unsigned char md[SHA_DIGEST_LENGTH];

    if (file_size == 0) {
        memcpy(md, "\xda\x39\xa3\xee\x5e\x6b\x4b\x0d\x32\x55\xbf\xef\x95\x60\x18\x90\xaf\xd8\x07\x09", SHA_DIGEST_LENGTH);
        if (memcmp(md, sha1, SHA_DIGEST_LENGTH) == 0) {
            matched_cluster[0] = get_start_cluster(entry);
            free(file_content);
            return 1;
        } else {
            free(file_content);
            return 0;
        }
    }

    unsigned int cluster_size = be->BPB_SecPerClus * be->BPB_BytsPerSec;
    int cluster_count = file_cluster_counter(entry, cluster_size);

    // run with recursive function
    int is_matched = cluster_permutation(be, addr, entry, file_content, 0, cluster_count, sha1, unallocated_array, num_unallocated, matched_cluster);

    free(file_content);
    return is_matched;
}

// Helper function for return matched entry or NULL
DirEntry *match_non_cont_file(BootEntry *be, char *addr, DirEntry **recoverable_entries, int num_recoverable, int *unallocated_array, int num_unallocated, unsigned int *matched_cluster, unsigned char *sha1) {
    for (int i = 0; i < num_recoverable; i++) {
        DirEntry *current_entry = recoverable_entries[i];

        if (matched_permutation(be, addr, current_entry, sha1, unallocated_array, num_unallocated, matched_cluster)) {
            return current_entry;
        }
    }

    return NULL;
}

// Helper function that update all the fats with given non-contiguous clusters
void update_non_cont_fat(BootEntry *be, char *addr, unsigned int *matched_cluster, int cluster_count) {
    unsigned int fat_offset = be->BPB_RsvdSecCnt * be->BPB_BytsPerSec;
    unsigned int fat_size = be->BPB_FATSz32 * be->BPB_BytsPerSec;
    
    // update all fats with all matched clusters
    for (unsigned int i = 0; i < be->BPB_NumFATs; ++i) {
        unsigned int *fat = (unsigned int *)(addr + fat_offset + i * fat_size);
        for (int j = 0; j < cluster_count; j++) {
            unsigned int current_cluster = matched_cluster[j];
            unsigned int next_cluster = (j == cluster_count - 1) ? EOF : matched_cluster[j + 1];
            fat[current_cluster] = next_cluster;
        }
    }
}

// Helper function for recovering the matched entry with given non-contiguous clusters
void recover_with_clusters(BootEntry *be, char *addr, const char *filename, DirEntry *entry, unsigned int *matched_cluster) {
    unsigned int cluster_size = be->BPB_SecPerClus * be->BPB_BytsPerSec;
    int cluster_count = file_cluster_counter(entry, cluster_size);

    // update all fats with matched clusters
    update_non_cont_fat(be, addr, matched_cluster, cluster_count);

    // update start cluster for the entry
    entry->DIR_FstClusLO = matched_cluster[0] & 0xFFFF;
    entry->DIR_FstClusHI = (matched_cluster[0] >> 16) & 0xFFFF;

    // update the filename
    entry->DIR_Name[0] = filename[0];

    msync(addr, be->BPB_BytsPerSec * be->BPB_TotSec32, MS_SYNC);
}

// Main function for recovering possibly non-contiguous file
int recover_non_cont_file(BootEntry *be, char *addr, const char *filename, char *sha1) {
    DirEntry *recoverable_entries[100];
    unsigned char sha1_binary[SHA_DIGEST_LENGTH];

    // get recoverable entries and the number of recoverable entries from root directory
    int num_recoverable = get_recoverable_files(be, addr, filename, recoverable_entries);

    // get unallocated clusters and the number of clusters
    int unallocated_array[20];
    int num_unallocated = get_unallocated_cluster(be, addr, unallocated_array);

    // no recoverable entries found
    if (num_recoverable == 0) {
        printf("%s: file not found\n", filename);
    
    // recoverable entries found
    } else {
        hex_to_binary(sha1, sha1_binary);
        unsigned int matched_cluster[5];

        DirEntry *matched_entry = match_non_cont_file(be, addr, recoverable_entries, num_recoverable, unallocated_array, num_unallocated, matched_cluster, sha1_binary);

        // matched entry found
        if (matched_entry) {
            recover_with_clusters(be, addr, filename, matched_entry, matched_cluster);
            printf("%s: successfully recovered with SHA-1\n", filename);
        
        // none of the recoverable entries matched with the given SHA-1
        } else {
            printf("%s: file not found\n", filename);
        }
    }

    return 0;
}

/*
Usage: ./nyufile disk <options>
-i                     Print the file system information.
-l                     List the root directory.
-r filename [-s sha1]  Recover a contiguous file.
-R filename -s sha1    Recover a possibly non-contiguous file.
*/
int command_parsing(int argc, char *argv[]){
    // map the disk and construct a BootEntry for later use
    char *addr = disk_mapped(argv[1]);
    BootEntry *be = (BootEntry *)addr;

    int opt;
    int is_r = 0, is_R = 0; //, is_s = 0;
    char *filename_r = NULL;
    char *filename_R = NULL;
    // unsigned char sha1_binary[SHA_DIGEST_LENGTH];
    char *sha1_r = NULL;
    char *sha1_R = NULL;

    while ((opt = getopt(argc, argv, "ilr:R:s:")) != -1) {
        switch (opt) {
            case 'i':
                display_fsinfo(be);
                break;
            
            case 'l':
                display_rootinfo(be, addr);
                break;
            
            case 'r':
                is_r = 1;
                filename_r = optarg;
                // recover_cont_file(be, addr, filename, sha1);
                break;
            
            case 'R':
                is_R = 1;
                filename_R = optarg;
                break;
            
            case 's':
                // is_s = 1;
                // hex_to_binary(optarg, sha1_binary);
                sha1_r = optarg;
                sha1_R = optarg;
                break;
            
            default:
                invalid_command_handler();
        }
    }

    // handle no file after `r:R:s:` and more than one file after `r:R:s:`
    if (optind < argc - 1 || optind >= argc) {
        invalid_command_handler();
    }

    // handle contiguous file
    if (is_r == 1) recover_cont_file(be, addr, filename_r, sha1_r);
    
    // handle possibly non-contiguous file
    if (is_R == 1) recover_non_cont_file(be, addr, filename_R, sha1_R);

    return 0;
}

int main(int argc, char *argv[]){

    // handle argc < 2
    if (argc < 2) {
        invalid_command_handler();
    }

    command_parsing(argc, argv);

    return 0;
}

/*
For more logic ideas of this program and references, please refer to README.md which is also submitted along in the nyufile.zip
*/