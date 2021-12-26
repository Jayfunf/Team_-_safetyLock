/*
Major: Software
Student_id: 201920816
Name: 조민현
Class: System Programming and Practice(F062-2)
*/
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <wiringPi.h> // 와이어링파이 사용
#include <softPwm.h>  // 소프트Pwm 사용

#define servo_pin 18 //서보모터 핀 설정
#define CLIENT_NUM 3 // 클라이언트 수 설정
int flag = 0;

int client[CLIENT_NUM]; // 접속 가능한 인원
char distance[10];      // 거리를 측정한 파이에게 넘겨받는 거리 정보 저장용
char on[2] = "0";       // on/off 비교용

void error_handling(char *message)
{
    fputs(message, stderr);
    fputc('\n', stderr);
    exit(1);
}

// 전달받은 거리 정보와, 설정 값을 통해 사용자의 창문 개방 권한을 식별하고 잠금 장치 동작 및 클라이언트들에게 on/off 정보 전달.
int check_Open(char H[], char sonic[])
{
    int H_int;
    int sonic_int;
    if (sonic[0] == 'C')
    {
        printf("Close\n");
        printf("%c\n", sonic[0]);

        pwmWrite(servo_pin, 600); //닫힘
        on[0] = '0';
        int iAccept;
        iAccept = client[0];
        write(iAccept, on, sizeof(on));
        iAccept = client[1];
        write(iAccept, on, sizeof(on));

        // flag = 1;
        return 0;
    }
    H_int = atoi(H);
    sonic_int = atoi(sonic);
    // sonic_int = atoi(sonicValues);

    printf("%d\n", H_int);
    printf("%d\n", sonic_int);

    if (H_int > sonic_int)
    {
        printf("Open\n");
        printf("H_%d\n", H_int);
        printf("H_%d\n", sonic_int);
        pwmWrite(servo_pin, 1500); //열림
        on[0] = '1';
        return 0;
    }
    else if (H_int < sonic_int && flag == 1)
    {
        pwmWrite(servo_pin, 1500); //열림
    }
    else if (H_int < sonic_int && flag == 0)
    {
        pwmWrite(servo_pin, 600); //닫힘
    }
}

int main(int argc, char *argv[])
{
    printf("Server_ON! Enjoy\n");

    int clnt_sock;
    int file_dsc; // 파일디스크립터 설정을 위한 변수
    int sock_accept;
    fd_set sock_status;
    int clnt_num = 0; // 접속자 수(client number)
    int clnt_addr_size;
    int sock_bind;
    double str_len;
    double str_len2;
    struct sockaddr_in serv_addr, clnt_addr;

    int count = 0;
    char open[2] = "1";
    char H_setting[4]; //키 셋팅값
    char H_sonic[4];   //키 측정값

    wiringPiSetupGpio();
    pinMode(servo_pin, PWM_OUTPUT);
    pwmSetClock(19);
    pwmSetMode(PWM_MODE_MS);
    pwmSetRange(20000);
    pwmWrite(servo_pin, 600);

    clnt_addr_size = sizeof(struct sockaddr_in);
    bzero(&serv_addr, sizeof(serv_addr));
    clnt_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    // 만약 소켓 생성에 실패했을 때의 예외 처리이다.
    if (clnt_sock < 0)
    {
        perror("socket() failed");
        close(clnt_sock); // 열었던 소켓을 닫는다.
        return -10;
    }
    serv_addr.sin_family = AF_INET; // socket의 첫번째 인자와 같다.
    /*
    해당 ""부분에 사용하는 와이파이 주소를 작성한다. (파이에서 사용한 ipTime 공유기가 아닌 인터넷이 가능한 개인 와이파이를 사용하여 구현하였다.)
    테스트 시 해당 부분에 파이_1,2,3을 묶을 인터넷이 가능한 와이파이 주소를 할당하여 테스트를 진행하여야 한다.
    */
    sock_bind = inet_pton(AF_INET, "172.30.1.37", &serv_addr.sin_addr.s_addr);
    if (sock_bind == 0)
    {
        printf("inet_pton() failed", "invalid address string");
        close(clnt_sock); // 열었던 소켓을 닫는다.
        return -100;
    }
    else if (sock_bind < 0)
    {
        perror("inet_pton() failded");
        close(clnt_sock); // 열었던 소켓을 닫는다.
        return -100;
    }
    // ip 출력
    printf("IP : %s\n", inet_ntoa(serv_addr.sin_addr));
    // 서버 포트(포트 문을 열어준다.)
    serv_addr.sin_port = htons(8888); // *포트번호 설정*

    if (0 > bind(clnt_sock, (struct sockaddr *)&serv_addr, clnt_addr_size))
    {
        perror("bind 실패");
        close(clnt_sock); // 열었던 소켓을 닫는다.
        return -100;
    }
    // 소켓이 들어오는 요청을 처리할 수 있도록 설정한다.
    if (0 > listen(clnt_sock, 5))
    {
        perror("listen 실패");
        close(clnt_sock);
        return -100;
    }

    file_dsc = clnt_sock + 1;
    while (1)
    {
        printf("현재 접속자 수는 %d명 입니다.\n", clnt_num);

        // FD_ZERO를 사용하여 소켓 식별용 sock_status를 '0'으로 초기화, 서버 소켓이 사용하도록 설정한다.
        FD_ZERO(&sock_status); // select가 호출되면 매번 '0'으로 초기화한다.
        FD_SET(clnt_sock, &sock_status);

        for (int i = 0; clnt_num - 1 > i; ++i)
        {
            FD_SET(client[i], &sock_status); // 각 클라이언트의 소켓을 식별하기 위해 할당한다.
        }
        if (0 > select(file_dsc, &sock_status, NULL, NULL, NULL))
        {
            perror("select 오류");
            close(clnt_sock);
            return -100;
        }
        printf("Select End\n");
        // 실제 통신을 시작하는 부분이다, accept는 소캣을 새로 만들어 소켓이 늘어난다. 그리고 각 소켓에 번호(파일디스크립터)가 할당된다.
        sock_accept = accept(clnt_sock, (struct sockaddr *)&clnt_addr, &clnt_addr_size);

        if (0 > sock_accept)
        {
            perror("accept 오류");
            close(clnt_sock);
            // 들어온 접속자 만큼 닫는다.- 접속자가 없으면 돌지 않는다.
            for (int i = 0; clnt_num > i; ++i)
            {
                close(client[i]);
            }
            return -100;
        }
        // 접속 제한 인원보다 많이 들어 왔을 때의 에러 처리부이다.
        if (CLIENT_NUM <= clnt_num)
        {
            write(sock_accept, "접속 가능 인원을 초과하였습니다.\n", sizeof("접속 가능 인원을 초과하였습니다.\n"));
            close(sock_accept);
            continue;
        }
        // 중간에 빈 파일디스크립터로 생성되었을 경우에는 더하면 안되므로 이를 체크한다.
        if (sock_accept == file_dsc)
        {
            file_dsc = sock_accept + 1;
            client[clnt_num] = sock_accept;
            ++clnt_num;
        }

        // 실제로 서버에 접속을 해보면, 클라이언트의 IP가 표시되는 것을 확인할 수 있다.
        printf("Client IP : [%s]\n", inet_ntoa(clnt_addr.sin_addr));

        if (count == 0)
        {
            str_len = read(sock_accept, H_setting, sizeof(H_setting));
            printf("%s\n", H_setting);
        }

        if (count == 1)
        {
            while (1)
            {
                str_len = read(sock_accept, H_sonic, sizeof(H_sonic));
                check_Open(H_setting, H_sonic);
                if (strncmp(on, open, 1) == 0 && flag == 0)
                { // on과 open이 각각 1일경우 즉, 문이 열렸으면 보내줌.
                    // printf("Test_1\n");
                    write(sock_accept, open, sizeof(open));
                    sock_accept = client[0];
                    write(sock_accept, open, sizeof(open));
                    sock_accept = client[1];
                    flag = 1;
                }

                if (strncmp(on, open, 1) != 0 && flag == 1)
                {
                    // printf("Test_2\n");
                    flag = 0;
                }
            }
        }
        count++;
    }
    close(clnt_sock);
    close(sock_accept);
    return 0;
}