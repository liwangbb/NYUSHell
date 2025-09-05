# OS lab2 references

 



1. [How to (safely) read user input with the getline function | Opensource.com](https://opensource.com/article/22/5/safely-read-user-input-getline)
2. [getcwd() — Get path name of the working directory - IBM Documentation](https://www.ibm.com/docs/en/zos/2.3.0?topic=functions-getcwd-get-path-name-working-directory)
3. [execv (qnx.com)](https://www.qnx.com/developers/docs/6.5.0SP1.update/com.qnx.doc.neutrino_lib_ref/e/execv.html)
4. [execl(3) - Linux man page](https://linux.die.net/man/3/execl)
5. [Parsing data with strtok in C](https://opensource.com/article/22/4/parsing-data-strtok-c#:~:text=The%20basic%20call%20to%20strtok,pointer%20to%20the%20first%20token.)
6. [C shell built-in commands list](https://www.ibm.com/docs/en/aix/7.2?topic=shell-c-built-in-commands-list)
7. [chdir() in C language with Examples](https://www.geeksforgeeks.org/chdir-in-c-language-with-examples/)
8. [strtok() and strtok_r() functions in C with examples](https://www.geeksforgeeks.org/strtok-strtok_r-functions-c-examples/)
9. [C standard library: String.h](https://www.runoob.com/cprogramming/c-standard-library-string-h.html)
10. [Implement shell in C and need help handling input/output redirection](https://stackoverflow.com/questions/11515399/implementing-shell-in-c-and-need-help-handling-input-output-redirection)
11. [open(2) — Linux manual page](https://man7.org/linux/man-pages/man2/open.2.html)
12. [dup(2) — Linux manual page](https://www.man7.org/linux/man-pages/man2/dup.2.html)
13. [Signals in C language](https://www.geeksforgeeks.org/signals-c-language/)
14. [Ignore SIGINT signal in child process](https://stackoverflow.com/questions/12953350/ignore-sigint-signal-in-child-process)
15. [6.2.2 Creating Pipes in C](https://tldp.org/LDP/lpg/node11.html)
16. [Simulating the pipe "|" operator in C](https://www.youtube.com/watch?v=6xbLgZpOBi8)
17. [Working with multiple pipes](https://www.youtube.com/watch?v=NkfIUo_Qq4c)
18. [Communicating between processes (using pipes) in C](https://www.youtube.com/watch?v=Mqb2dVRe0uo)
19. [28.5 Implementing a Job Control Shell](https://www.gnu.org/software/libc/manual/html_node/Implementing-a-Shell.html)
20. [7.1 Job Control Basics](https://www.gnu.org/software/bash/manual/html_node/Job-Control-Basics.html)
21. [‘fg’ Linux Command: Your Complete Guide to Job Control](https://ioflood.com/blog/fg-linux-command/)



`open() flags:` 

* `O_WRONLY`: the file is opened for write
* `O_CREAT`: create such a file if it doesn't exist
* `O_TRUNC`: truncate the file to zero length if it already exists
* `O_APPEND`: enable append mode for the output file
* `O_RDONLY`: for read only permission

`open() mode_t:`

* `S_IRUSR`: user has read permission
* `S_IWUSR`: user has write permission
* `S_IRGRP`: group has read permission
* `S_IROTH`: other has read permission
