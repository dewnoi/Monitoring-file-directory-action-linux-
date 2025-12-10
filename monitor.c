#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h> 
#include <time.h>

#define DEVICE_PATH "/dev/project_monitor"

int main(int argc, char *argv[]) {
    int fd;
    char buffer[256];
    struct stat st;
    char time_buffer[64]; 
    time_t now;
    struct tm *t;

    if (argc < 2) {
        printf("Please add directory path to monitor");
        return -1;
    }

    if (stat(argv[1], &st) < 0) {
        perror("Failed to get inode");
        return -1;
    }

    printf("Target Directory: %s\n", argv[1]);

    fd = open(DEVICE_PATH, O_RDWR); 
    if (fd < 0) {
        perror("Failed to open device");
        return -1;
    }

    sprintf(buffer, "%lu", st.st_ino);
    write(fd, buffer, strlen(buffer)); 
    printf("--- Configured Kernel to watch Inode %s ---\n", buffer);

    printf("Waiting for events in '%s'...\n", argv[1]);

    while (1) {
        int ret = read(fd, buffer, 256);
        if (ret > 0) {
            buffer[ret] = '\0';
            
            now = time(NULL);
            t = localtime(&now);

            strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", t);

            printf("[%s] %s", time_buffer, buffer);
            
            fflush(stdout);
        }
    }

    close(fd);
    return 0;
}
