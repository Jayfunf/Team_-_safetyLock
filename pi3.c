#include <wiringPiI2C.h>
#include <wiringPi.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h> 
#include <arpa/inet.h> 
#include <sys/socket.h>
#include <pthread.h>

// Define some device parameters
#define I2C_ADDR   0x27 // I2C device address

// Define some device constants
#define LCD_CHR  1 // Mode - Sending data
#define LCD_CMD  0 // Mode - Sending command

#define LINE1  0x80 // 1st line
#define LINE2  0xC0 // 2nd line

#define LCD_BACKLIGHT   0x08  // On
// LCD_BACKLIGHT = 0x00  # Off

#define ENABLE  0b00000100 // Enable bit

void error_handling( char *message){
	fputs(message,stderr);
	fputc( '\n',stderr);
	exit( 1);
}

int onoff;

#define IN  0
#define OUT 1
#define LOW  0
#define HIGH 1
#define PIN  20
#define POUT 22
#define POUT2 21
#define PIN2 23
#define POUT3 24

static int GPIOExport(int pin) {
#define BUFFER_MAX 3
	char buffer[BUFFER_MAX];
	ssize_t bytes_written;
	int fd;

	fd = open("/sys/class/gpio/export", O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open export for writing!\n");
		return(-1);
	}

	bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
	write(fd, buffer, bytes_written);
	close(fd);
	return(0);
}

static int GPIOUnexport(int pin) {
	char buffer[BUFFER_MAX];
	ssize_t bytes_written;
	int fd;

	fd = open("/sys/class/gpio/unexport", O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open unexport for writing!\n");
		return(-1);
	}

	bytes_written = snprintf(buffer, BUFFER_MAX, "%d", pin);
	write(fd, buffer, bytes_written);
	close(fd);
	return(0);
}

static int GPIODirection(int pin, int dir) {
	static const char s_directions_str[]  = "in\0out";

#define DIRECTION_MAX 35
	//char path[DIRECTION_MAX]="/sys/class/gpio/gpio24/direction";
	char path[DIRECTION_MAX]="/sys/class/gpio/gpio%d/direction";
	int fd;

	snprintf(path, DIRECTION_MAX, "/sys/class/gpio/gpio%d/direction", pin);
	
	fd = open(path, O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open gpio direction for writing!\n");
		return(-1);
	}

	if (-1 == write(fd, &s_directions_str[IN == dir ? 0 : 3], IN == dir ? 2 : 3)) {
		fprintf(stderr, "Failed to set direction!\n");
		return(-1);
	}

	close(fd);
	return(0);
}

static int GPIORead(int pin) {
#define VALUE_MAX 30
	char path[VALUE_MAX];
	char value_str[3];
	int fd;

	snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
	fd = open(path, O_RDONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open gpio value for reading!\n");
		return(-1);
	}

	if (-1 == read(fd, value_str, 3)) {
		fprintf(stderr, "Failed to read value!\n");
		return(-1);
	}

	close(fd);

	return(atoi(value_str));
}

static int GPIOWrite(int pin, int value) {
	static const char s_values_str[] = "01";

	char path[VALUE_MAX];
	int fd;
	

	snprintf(path, VALUE_MAX, "/sys/class/gpio/gpio%d/value", pin);
	fd = open(path, O_WRONLY);
	if (-1 == fd) {
		fprintf(stderr, "Failed to open gpio value for writing!\n");
		return(-1);
	}

	if (1 != write(fd, &s_values_str[LOW == value ? 0 : 1], 1)) {
		fprintf(stderr, "Failed to write value!\n");
		return(-1);

	close(fd);
	return(0);
	}
}
void *led_thd()
{
    pid_t pid;
    pthread_t tid;

    if(-1==GPIOExport(POUT)) exit(0);
	
	if(-1==GPIODirection(POUT,OUT)) exit(0);
	while(1){
    if(onoff==1){
      if(-1==GPIOWrite(POUT,1)) exit(0);
    }
    else{
      if(-1==GPIOWrite(POUT,0)) exit(0);
    }
		usleep(500*1000);
  }
		


}

void lcd_init(void);
void lcd_byte(int bits, int mode);
void lcd_toggle_enable(int bits);

// added by Lewis https://www.bristolwatch.com/rpi/i2clcd.htm
void typeInt(int i);
void typeFloat(float myFloat);
void lcdLoc(int line); //move cursor
void ClrLcd(void); // clear LCD return home
void typeln(const char *s);
void typeChar(char val);
int fd;  // seen by all subroutines

int main(int argc,char *argv[])   {
  pthread_t p_thread[1];
  int thr_id1,thr_id2;
  char p1[] = "thread_1";
  int status=1;
  if(argc!=3){
		printf("Usage : %s <IP> <port>\n",argv[0]); 
		exit(1);
	}
  int sock;
	struct sockaddr_in serv_addr; 

  if (wiringPiSetup () == -1) exit (1);

  fd = wiringPiI2CSetup(I2C_ADDR);


  lcd_init(); // setup LCD

  sock = socket(PF_INET, SOCK_STREAM, 0); 
	if(sock == -1)
		error_handling("socket() error");
	memset(&serv_addr, 0, sizeof(serv_addr)); 
	serv_addr.sin_family = AF_INET; 
	serv_addr.sin_addr.s_addr = inet_addr(argv[1]); 
	serv_addr.sin_port = htons(atoi(argv[2]));
	
	if(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr))==-1) 
		error_handling("connect() error");

    lcdLoc(LINE1);
    typeln("Setting");
    int repeat = 100;
	int state = 1;
	int prev_state = 1;
	int light = 0;

	//Enable GPIO pins
	if (-1 == GPIOExport(POUT2) || -1 == GPIOExport(PIN) || -1 == GPIOExport(POUT3) || -1 == GPIOExport(PIN2))
		return(1);

	//Set GPIO directions
	if (-1 == GPIODirection(POUT2, OUT) || -1 == GPIODirection(PIN, IN) || -1 == GPIODirection(POUT3, OUT) || -1 == GPIODirection(PIN2, IN)){
    close(sock);
		return(2);
  }

  int i=40;
    while(1){
      //printf("%d\n",i);
      if ( -1 == GPIOWrite(POUT2,1) || -1 == GPIOWrite(POUT3,1))
			  return(3);
        if(GPIORead(PIN)==1){
            ClrLcd();
            lcdLoc(LINE1);
            typeln("Kid height : ");
            lcdLoc(LINE2);
            
            i+=10;
            if(i>150) i=50;
            typeInt(i);
            typeln("cm");            
        }
        if(GPIORead(PIN2)==1){
            break;
        }
        delay(200);
    }
    int h1=i;

     ClrLcd();
    lcdLoc(LINE1);
    typeln("Setting Success!");

     delay(2000);
    i=190;
    while(1){
       if ( -1 == GPIOWrite(POUT2,1) || -1 == GPIOWrite(POUT3,1))
			  return(3);
        if(GPIORead(PIN)==1){
            ClrLcd();
            lcdLoc(LINE1);
            typeln("Total height : ");
            lcdLoc(LINE2);
            i+=10;
            if(i>350) i=200;
            typeInt(i);
            typeln("cm");
        }
        if(GPIORead(PIN2)==1){
            break;
        }
        delay(200);
    }
    int h2=i;
    if (-1 == GPIOUnexport(PIN)|| -1 == GPIOUnexport(POUT2) || -1 == GPIOUnexport(PIN2)|| -1 == GPIOUnexport(POUT3))
		return(4);

    ClrLcd();
    lcdLoc(LINE1);
    typeln("H1 : ");
    typeInt(h1);
    lcdLoc(LINE2);
    typeln("H2 : ");
    typeInt(h2);

    int limit=h2-h1;
    char msg[4];
    char check[2]="0",on[2]="1";
    sprintf(msg,"%d",limit);


		  write(sock,msg,sizeof(msg));


    printf("send success\n");


    thr_id1 = pthread_create(&p_thread[0], NULL, led_thd, (void *)p1);
    int flag=0;
    while(1){
       int  str_len=read(sock,check,sizeof(check));
  
        if(str_len == -1)
		      error_handling("read() error");
        if(strcmp(on,check)==0 && flag==0){

          onoff=1;
          ClrLcd();
          lcdLoc(LINE1);
          typeln("open!");
          flag=1;
        }
        else if(strcmp(on,check)==1 && flag==1){
  
          onoff=0;
          ClrLcd();
          lcdLoc(LINE1);
          typeln("closed!");
          flag=0;
        }
  
    }

    pthread_join(p_thread[0], (void **)&status);

    	if(-1==GPIOUnexport(POUT)) return 0;
    close(sock);


  return 0;

}


// float to string
void typeFloat(float myFloat)   {
  char buffer[20];
  sprintf(buffer, "%4.2f",  myFloat);
  typeln(buffer);
}

// int to string
void typeInt(int i)   {
  char array1[20];
  sprintf(array1, "%d",  i);
  typeln(array1);
}

// clr lcd go home loc 0x80
void ClrLcd(void)   {
  lcd_byte(0x01, LCD_CMD);
  lcd_byte(0x02, LCD_CMD);
}

// go to location on LCD
void lcdLoc(int line)   {
  lcd_byte(line, LCD_CMD);
}

// out char to LCD at current position
void typeChar(char val)   {

  lcd_byte(val, LCD_CHR);
}


// this allows use of any size string
void typeln(const char *s)   {

  while ( *s ) lcd_byte(*(s++), LCD_CHR);

}

void lcd_byte(int bits, int mode)   {

  //Send byte to data pins
  // bits = the data
  // mode = 1 for data, 0 for command
  int bits_high;
  int bits_low;
  // uses the two half byte writes to LCD
  bits_high = mode | (bits & 0xF0) | LCD_BACKLIGHT ;
  bits_low = mode | ((bits << 4) & 0xF0) | LCD_BACKLIGHT ;

  // High bits
  wiringPiI2CReadReg8(fd, bits_high);
  lcd_toggle_enable(bits_high);

  // Low bits
  wiringPiI2CReadReg8(fd, bits_low);
  lcd_toggle_enable(bits_low);
}

void lcd_toggle_enable(int bits)   {
  // Toggle enable pin on LCD display
  delayMicroseconds(500);
  wiringPiI2CReadReg8(fd, (bits | ENABLE));
  delayMicroseconds(500);
  wiringPiI2CReadReg8(fd, (bits & ~ENABLE));
  delayMicroseconds(500);
}


void lcd_init()   {
  // Initialise display
  lcd_byte(0x33, LCD_CMD); // Initialise
  lcd_byte(0x32, LCD_CMD); // Initialise
  lcd_byte(0x06, LCD_CMD); // Cursor move direction
  lcd_byte(0x0C, LCD_CMD); // 0x0F On, Blink Off
  lcd_byte(0x28, LCD_CMD); // Data length, number of lines, font size
  lcd_byte(0x01, LCD_CMD); // Clear display
  delayMicroseconds(500);
}