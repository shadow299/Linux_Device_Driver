#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

int main(void)
{
    int fd;
    char buffer[32];
    ssize_t count;

    fd = open("/dev/bmp280", O_RDWR);
    if (fd < 0)
    {
        perror("open /dev/bmp280");
        return EXIT_FAILURE;
    }

    /* Trigger a fresh temperature read and print the returned value. */
    if (write(fd, "read", 4) < 0)
    {
        perror("write");
        close(fd);
        return EXIT_FAILURE;
    }

    count = read(fd, buffer, sizeof(buffer) - 1);
    if (count < 0)
    {
        perror("read");
        close(fd);
        return EXIT_FAILURE;
    }

    buffer[count] = '\0';
    printf("Temperature: %s", buffer);
    close(fd);
    return EXIT_SUCCESS;
}
