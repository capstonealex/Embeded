#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <string.h>


#ifndef BUF_SIZE
#define BUF_SIZE 100000
#endif
#define STRING_LENGTH 50
#define DECIMAL 10
#define BUTTON_ONE 1
#define BUTTON_TWO 2
#define BUTTON_THREE 3
#define BUTTON_FOUR 4
#define LHIP 1
#define LKNEE 2
#define RHIP 3
#define RKNEE 4



/* Helper functions */
static void sendCommand(int fd, char *command, size_t commandLength, char *canReturnMessage);
void canFeast (char *buf, char *canReturnMessage);
int getButton(int button, char *canReturnMessage);
// MAKE ME ACCEPT nodeID
long getPos(int nodeid, char *canReturnMessage);
void setAbsPosSmart(int nodeide, int position, char *canReturnMessage);
/* Helpers for int to string conversion */
void itoa(int value, char* str, int base);
void strreverse(char* begin, char* end);
void stringExtract(char *origStr, char **extractStr, int pos);
long strToInt(char str[]);
void sitStand();
void preop(int nodeid);
void initMotorPos(int nodeid);

int main (){
    printf("Welcome to CANfeast!\n");
    sitStand();
    return 0;
}

int getButton(int button, char *canReturnMessage){
    //char canReturnMessage[STRING_LENGTH];

    char buttons[][STRING_LENGTH]=
            {
                    "[1] 9 read 0x0101 1 u32", //button 1
                    "[1] 9 read 0x0102 1 u32",//button 2
                    "[1] 9 read 0x0103 1 u32", //button 3
                    "[1] 9 read 0x0104 1 u32"//button 4
            };
    canFeast(buttons[button-1], canReturnMessage);

    printf("CAN return on button press is: %s", canReturnMessage);

    if(strcmp(canReturnMessage, "[1] 0x3F800000\r")==0)
        return 1;
    else
        return 0;
}

long getPos(int nodeid, char *canReturnMessage){
    char node[STRING_LENGTH], getpos[STRING_LENGTH], dataType[STRING_LENGTH], buffer[STRING_LENGTH];
    char positionMessage[STRING_LENGTH];
    char *positionStr;
    long position;

    itoa(nodeid,buffer,DECIMAL);
    strcpy(getpos, "[1] ");
    strcpy(node, buffer);
    strcpy(dataType," read 0x6063 0 i32");
    //concatenate message
    strcat(getpos, node);
    strcat(getpos, dataType);
    canFeast(getpos, canReturnMessage);

    printf("Position Message for node %d: %s\n",nodeid, positionMessage);

    stringExtract(positionMessage,&positionStr,2);
    printf("Extracted Message for node %d: %s\n",nodeid, positionStr);

    position=strToInt(positionStr);
    printf("Position of node %d: %ld", nodeid, position);

    return position;

}

void setAbsPosSmart(int nodeid, int position, char *canReturnMessage){
    char pos[STRING_LENGTH], movePos[STRING_LENGTH], buffer[STRING_LENGTH], nodeStr[STRING_LENGTH];
    strcpy(movePos, "[1] "); //Start message with "[1] "
    itoa(nodeid,buffer,DECIMAL); //convert nodeid to string
    strcpy(nodeStr, buffer); //Copy  nodeid to string
    strcat(movePos, nodeStr); //Concat node to movepox
    strcat(movePos, " write 0x607A 0 i32 "); //concat remaining message to mocepos
    itoa(position,buffer,DECIMAL); //convert position to string
    strcpy(pos, buffer); //copy position to string
    strcat(movePos, pos); // concat position string to movepos message.

    printf("%s\n",movePos);

    char* commList[]= {
            movePos, //move to this position (absolute)
            "[1] 2 write 0x6040 0 i16 47", //control word low
            "[1] 2 write 0x6040 0 i16 63" //control word high
    };

    int Num_of_Strings = sizeof(commList)/ sizeof(commList[0]);
    for(int i=0; i<Num_of_Strings; ++i)
        canFeast(commList[i],canReturnMessage);
}

void canFeast(char *buf, char *canReturnMessage) {
    char *socketPath = "/tmp/CO_command_socket";  /* Name of the local domain socket, configurable by arguments. */
    int fd;
    struct sockaddr_un addr;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1){
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);
    // Try to make a connection to the local UNIT AF_UNIX SOCKET, quit if unavailable
    if (connect(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1){
        perror("Socket connection failed");
        exit(EXIT_FAILURE);
    }
    sendCommand(fd, buf, strlen(buf),canReturnMessage);
    //close socket
    close(fd);
}

static void sendCommand(int fd, char *command, size_t commandLength, char *canReturnMessage)
{
    size_t n;
    char buf[BUF_SIZE];

    if (write(fd, command, commandLength) != commandLength){
        perror("Socket write failed");
        exit(EXIT_FAILURE);
    }

    n = read(fd, buf, sizeof(buf));
    if (n == -1){
        perror("Socket read failed");
        exit(EXIT_FAILURE);
    }
    printf("%s", buf);
    strcpy(canReturnMessage,buf);
}

////Definitionof itoa(int to string conversion) and helper Kernighan & Ritchie's Ansi C.
////Converts an integer value to a null-terminated string using the specified base and stores the result in the array given by str parameter.
////If base is 10 and value is negative, the resulting string is preceded with a minus sign (-). With any other base, value is always considered unsigned.
////char *  itoa ( int value, char * str, int base );
////value - Value to be converted to a string.
////str - Array in memory where to store the resulting null-terminated string.
////base - Numerical base used to represent the value as a string, between 2 and 36, where 10 means decimal base, 16 hexadecimal, 8 octal, and 2 binary.
void itoa(int value, char* str, int base) {
    static char num[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char* wstr=str;
    int sign;

    // Validate base
    if (base<2 || base>35){
        *wstr='\0';
        return;
    }

    // Take care of sign
    if ((sign=value) < 0)
        value = -value;

    // Conversion. Number is reversed.
    do
        *wstr++ = num[value%base];
    while(value/=base);

    if(sign<0)
        *wstr++='-';

    *wstr='\0';

    // Reverse string
    strreverse(str,wstr-1);
}

void strreverse(char* begin, char* end) {
    char aux;
    while(end>begin)
        aux=*end, *end--=*begin, *begin++=aux;
}

//Extracts a string at position pos of origStr and stores it in extractStr.
//Index 0 is position 1.
// IMPORTANT: The origStr passed into the function gets modified. So pass a copy if needed.
void stringExtract(char *origStr, char **extractStr, int pos){
    //using both space and nextline as delimiter
    char delim[] = " \n\r";
    char *ptr = strtok(origStr, delim);

    for(int i=0; ptr != NULL && i<pos; i++){
        *extractStr=ptr;
        ptr = strtok(NULL, delim);
    }
}

long strToInt(char str[]){
    int len = strlen(str);
    long num=0;

    if(str[0]=='-'){
        for(int i=0, j=1; i<(len-1); i++, j*=10)
            num += ((str[len-(i+1)]-'0')*j);
        return -num;
    }

    for(int i=0, j=1; i<len; i++, j*=10)
        num += ((str[len-(i+1)]-'0')*j);
    return num;
}

void sitStand(){
    char junk[STRING_LENGTH];

    char positionMesNode1[STRING_LENGTH];
    char positionMesNode2[STRING_LENGTH];
    char positionMesNode3[STRING_LENGTH];
    char positionMesNode4[STRING_LENGTH];

    char *positionStrLHip; //Node 1
    char *positionStrLknee; //Node 2
    char *positionStrRHip; //Node 3
    char *positionStrRKnee; //Node 4

    long posLHip;
    long posLKnee;
    long posRHip;
    long posRKnee;


    initMotorPos(1);
    initMotorPos(2);
    initMotorPos(3);
    initMotorPos(4);

    posLHip=getPos(1,positionMesNode1);
    posLKnee=getPos(2,positionMesNode2);
    posRHip=getPos(3,positionMesNode3);
    posRKnee=getPos(4,positionMesNode4);

/*    printf("Position Message is (node 1): %s\n",positionMesNode1);
    printf("Position Message is (node 2): %s\n",positionMesNode2);
    printf("Position Message is (node 3): %s\n",positionMesNode3);
    printf("Position Message is (node 4): %s\n",positionMesNode4);

    stringExtract(positionMesNode1,&positionStrLHip,2);
    stringExtract(positionMesNode2,&positionStrLknee,2);
    stringExtract(positionMesNode3,&positionStrRHip,2);
    stringExtract(positionMesNode4,&positionStrRKnee,2);

    printf("Extracted Message is (node 1): %s\n",positionStrLHip);
    printf("Extracted Message is (node 2): %s\n",positionStrLknee);
    printf("Extracted Message is (node 3): %s\n",positionStrRHip);
    printf("Extracted Message is (node 4): %s\n",positionStrRKnee);

    posLHip=strToInt(positionStrLHip);
    posLKnee=strToInt(positionStrLknee);
    posRHip=strToInt(positionStrRHip);
    posRKnee=strToInt(positionStrRKnee);

*/

    printf("Left Hip (node 1) positions is: %ld\n", posLHip);
    printf("Left Knee (node 2) positions is: %ld\n", posLKnee);
    printf("Right Hip (node 3) positions is: %ld\n", posRHip);
    printf("Right Knee (node 4) positions is: %ld\n", posRKnee);

    int button1Status=getButton(BUTTON_ONE, junk);
    printf("Button 1 states is %d\n",button1Status);

    sleep(1);

    preop(LHIP);
    preop(2);
    preop(3);
    preop(4);

    /***************************************************************/


/*

    long sitStandArrayHip[]={23351, 26042, 40535, 68841, 106614, 147915, 189305, 227293, 255829, 270462, 273182 };
    long sitStandArrayKnee[]={50525, 54356, 74110, 109922, 152065, 189037, 215074, 230660, 238160, 240601, 240891};

    int sitstate=0;
    int movestate=0;

    int button1Status=0;
    int button2Status=0;
    int button3Status=0;

    char junk[STRING_LENGTH];

    while(1){
        button1Status=getButton(1, junk);
        button2Status=getButton(2, junk);
        button3Status=getButton(3, junk);

        if(button1Status==1 && movestate==0 && sitstate<10){
            movestate=1;
            setAbsPosSmart(LHIP, sitStandArrayHip[sitstate+1], junk);
        }

        if(sitstate<10 && movestate==1){
            if(getPos(LHIP, junk)==sitStandArrayHip[sitstate+1]){
                sitstate++;
                movestate=0;
            }
        }


        if(button2Status==1 && movestate==0 && sitstate>10){
            movestate=1;
            setAbsPosSmart(LHIP, sitStandArrayHip[sitstate-1], junk);
        }

        if(sitstate>0 && movestate==1){
            if(getPos(LHIP, junk)==sitStandArrayHip[sitstate-1]){
                sitstate--;
                movestate=0;
            }
        }


        if(button3Status==1){
            preop(1);
            preop(2);
            preop(3);
            preop(4);
            break;
        }
    }
*/

    /***************************************************************/
}

//set node to preop mode
void preop(int nodeid){
    char junk[STRING_LENGTH];
    char node[STRING_LENGTH], preop[STRING_LENGTH], dataTail[STRING_LENGTH], buffer[STRING_LENGTH];
    itoa(nodeid,buffer,DECIMAL);
    strcpy(preop, "[1] ");
    strcpy(node, buffer);
    strcpy(dataTail," preop");
    //concatenate message
    strcat(preop, node);
    strcat(preop, dataTail);
    printf("\nNode %d is now in preop state\n",nodeid);
    canFeast(preop,junk);
}

//start motor and set to position mode.
void initMotorPos(int nodeid){
    char node[STRING_LENGTH], comm[STRING_LENGTH], dataTail[STRING_LENGTH], buffer[STRING_LENGTH];
    itoa(nodeid,buffer,DECIMAL);

    char canMessage[STRING_LENGTH];

    //creating message for start mode
    strcpy(comm, "[1] ");
    strcpy(node, buffer);
    strcpy(dataTail," start");
    //concatenate message
    strcat(comm, node);
    strcat(comm, dataTail);
    canFeast(comm,canMessage);

    //creating message for position mode
    strcpy(comm, "[1] ");
    strcpy(node, buffer);
    strcpy(dataTail," write 0x6060 0 i8 1");
    //concatenate message
    strcat(comm, node);
    strcat(comm, dataTail);
    canFeast(comm,canMessage);
}