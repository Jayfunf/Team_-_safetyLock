#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <wiringPi.h>
#include <softTone.h>

int sock;
struct sockaddr_in serv_addr;

#define BUFFER_MAX 3
#define DIRECTION_MAX 45

#define IN 0
#define OUT 1
#define PWM 0

#define LOW 0
#define HIGH 1
#define VALUE_MAX 256

#define POUT 23
#define PIN 24

double distance = 0;

static int GPIOExport(int pin)
{
#define BUFFER_MAX 3
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/export", O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open export for writing!\n");
        return (-1);
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return (0);
}

static int GPIOUnexport(int pin)
{
    char buffer[BUFFER_MAX];
    ssize_t bytes_written;
    int fd;

    fd = open("/sys/class/gpio/unexport", O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open unexport for writing!\n");
        return (-1);
    }

    bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
    write(fd, buffer, bytes_written);
    close(fd);
    return (0);
}

static int GPIODirection(int pin, int dir)
{
    static const char s_directions_str[] = "in\0out";

    //char path[DIRECTION_MAX]="/sys/class/gpio/gpio24/direction";
    char path[DIRECTION_MAX] = "/sys/class/gpio/gpio%d/direction";
    int fd;

    snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);

    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open gpio direction for writing!\n");
        return (-1);
    }

    if (-1 == write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3))
    {
        fprintf(stderr, "Failed to set direction!\n");
        return (-1);
    }

    close(fd);
    return (0);
}

static int GPIORead(int pin)
{
    char path[VALUE_MAX];
    char value_str[3];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_RDONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open gpio value for reading!\n");
        return (-1);
    }

    if (-1 == read(fd, value_str, 3))
    {
        fprintf(stderr, "Failed to read value!\n");
        return (-1);
    }

    close(fd);

    return (atoi(value_str));
}

static int GPIOWrite(int pin, int value)
{
    static const char s_values_str[] = "01";
    char path[VALUE_MAX];
    int fd;

    snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
    fd = open(path, O_WRONLY);
    if (-1 == fd)
    {
        fprintf(stderr, "Failed to open gpio value for writing!\n");
        return (-1);
    }

    if (1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1))
    {
        fprintf(stderr, "Failed to write value!\n");
        return (-1);
    }
    close(fd);
    return (0);
}
void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}
void *ultrawave_thd()
{
    clock_t start_t, end_t;
    double time;
    if (-1 == GPIOExport(POUT) || -1 == GPIOExport(PIN))
    {
        printf("gpio export error\n");
        exit(0);
    }
    usleep(100000);

    if (-1 == GPIODirection(POUT, OUT) || -1 == GPIODirection(PIN, IN))
    {
        printf("gpio direction error\n");
        exit(0);
    }
    GPIOWrite(POUT, 0);
    usleep(10000);
    for (int i = 0; i < 10; i++)
    {
        if (-1 == GPIOWrite(POUT, 1))
        {
            printf("gpio write/trigger error\n");
            exit(0);
        }
        usleep(10);
        GPIOWrite(POUT, 0);
        while (GPIORead(PIN) == 0)
        {
            start_t = clock();
        }
        while (GPIORead(PIN) == 1)
        {
            end_t = clock();
        }
        time = (double)(end_t - start_t) / CLOCKS_PER_SEC;
        distance = time / 2 * 34000;
        if (distance > 900)
            distance = 900;

        printf("time : %.4lf\n", time);
        printf("distance : %.2lfcm\n", distance);
        // 전송
        char dist[4];
        int d = (int)distance;
        sprintf(dist, "%d", d);
        write(sock, dist, sizeof(dist));
        usleep(2000000);
    }
    GPIOUnexport(POUT);
    GPIOUnexport(PIN);
}

void *buzzer()
{
    int pinPiezo = 17;
    char msg[256];
    while (1)
    {
        read(sock, msg, 256);
        if (!strcmp(msg, "1"))
        {
            softToneCreate(pinPiezo);
            softToneWrite(pinPiezo, 1500);
            delay(1000);
            softToneWrite(pinPiezo, 0);
            delay(1000);
        }
    }
}
void *move_thd()
{
    int prev = 0;
    pinMode(26, INPUT);
    while (1)
    {
        if (digitalRead(26))
        {
            ultrawave_thd();
            prev = 1;
        }
        else
        {
            printf("none\n");
            if (prev == 1)
            {
                //잠금
                write(sock, "CCC", 4);
                prev = 0;
            }
        }
        usleep(100000);
    }
}

int main(int argc, char *argv[])
{
    pthread_t p_thread[2];
    int thr_id;
    int status;
    char msg[256];

    if (argc != 3)
    {
        printf("Usage : %s <IP> <port>\n", argv[0]);
        exit(1);
    }
    sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock == -1)
        error_handling("socket() error");

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr(argv[1]);
    serv_addr.sin_port = htons(atoi(argv[2]));

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1)
        error_handling("connect() error");

    wiringPiSetupGpio();

    thr_id = pthread_create(&p_thread[0], NULL, buzzer, NULL);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    thr_id = pthread_create(&p_thread[1], NULL, move_thd, NULL);
    if (thr_id < 0)
    {
        perror("thread create error : ");
        exit(0);
    }
    // move_thd();

    pthread_join(p_thread[0], (void **)&status);
    pthread_join(p_thread[1], (void **)&status);
    if (-1 == GPIOUnexport(POUT) || -1 == GPIOUnexport(PIN))
        return -1;
    return (0);
}