# File Recovery for Fat32
Author: Li Wang, Student# N15155142

## Ideas for recover contiguous file
1. Create a `is_recoverable` function to check any deleted file matched with given filename in the root directory.
2. Update the matched entry's filename and all fats.

## Ideas for recover contiguous file with SHA-1
1. Using above helper functions to store a list of recoverable entries.
2. Copy each recoverable entries' content and calculate its SHA-1.
3. Check the calculated SHA-1 with the given SHA-1.
4. Using helper functions to return matched entry or NULL for later recovery.
5. Update the matched entry's filename and all fats.

### Contiguous Notes
1. Since it's contiguous, we can easily use `current_cluster++` to deal with clusters.
2. For file content, we can use `file_size`, `rest_copy_size` and `bytes_to_copy` to tracking the copied size for copying content from different clusters.

## Ideas for recover possibly non-contiguous file with SHA-1
1. The main logic of functions are almost the same with the contiguous file.
2. Using recursive function to find the non-contiguous clusters that matched with given SHA-1. Store the cluster index is needed for fats.
3. Update the matched entry's filename and starting cluster and all fats.

## Reference
* [8.3 filename](https://en.wikipedia.org/wiki/8.3_filename)
* [Long filename](https://en.wikipedia.org/wiki/Long_filename)
* [In C - check if a char exists in a char array](https://stackoverflow.com/questions/1071542/in-c-check-if-a-char-exists-in-a-char-array)
* [C strcmp()](https://www.scaler.com/topics/strcmp-in-c/#)
* [How to use SHA1 hashing in C programming](https://stackoverflow.com/questions/9284420/how-to-use-sha1-hashing-in-c-programming)
* [How would I compare two unsigned char arrays](https://stackoverflow.com/questions/30146358/how-would-i-compare-2-unsigned-char-arrays)
* [SHA1 hash binary (20) to string (41)](https://stackoverflow.com/questions/7521007/sha1-hash-binary-20-to-string-41)
* [C integer types](https://www.remlab.net/op/integer.shtml)
* [Design of the FAT file system](https://en.wikipedia.org/wiki/Design_of_the_FAT_file_system#Data_region)
* [FAT32 Boot Sector, Locating Files and Dirs](http://www.cs.fsu.edu/~cop4610t/lectures/project3/Week11/Slides_week11.pdf)
* [FAT](https://wiki.osdev.org/FAT)