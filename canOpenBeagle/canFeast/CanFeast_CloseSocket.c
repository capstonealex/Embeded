/*
 * ALEX Exoskeleton.
 * Program for sit stand mode on the Fourier X2 exoskeleton.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <string.h>

//Buffer for socket
#ifndef BUF_SIZE
#define BUF_SIZE 100000
#endif
//String Length for defining fixed sized char array
#define STRING_LENGTH 50
//Base for int to str conversion
#define DECIMAL 10
//Exo skeleton user buttons
#define BUTTON_ONE 1
#define BUTTON_TWO 2
#define BUTTON_THREE 3
#define BUTTON_FOUR 4
//Node ID for the 4 joints
#define LHIP 1
#define LKNEE 2
#define RHIP 3
#define RKNEE 4
//Clearance used when doing point to point motion
#define POSCLEARANCE 5000
//Velocity and acceleration for position mode move
#define PROFILEVELOCITY 200000
#define PROFILEACCELERATION 40000
//Knee motor reading and corresponding angle. Used for mapping between degree and motor values.
#define KNEE_MOTOR_POS1 250880
#define KNEE_MOTOR_DEG1 90
#define KNEE_MOTOR_POS2 0
#define KNEE_MOTOR_DEG2 0
//Hip motor reading and corresponding angle. Used for mapping between degree and motor values.
#define HIP_MOTOR_POS1 250880
#define HIP_MOTOR_DEG1 90
#define HIP_MOTOR_POS2 0
#define HIP_MOTOR_DEG2 180
//standing or sitting state
//Arbitrarily using 1 and 2 here. The actual sitstate is automatically calculated in sitStand()
#define STANDING 1
#define SITTING 2

/*
 Most functions defined here use canReturnMessage as a pass-by-reference string.
 The return message from canopencomm is stored in this.
 In functions like getPos(), this is parsed to obtain the position value.
 However, this string can contain error messages as well.
 Therefore, it can be used by the calling function for error-handling.
 */

//State machine with sit-stand logic
void sitStand(int state);
//For sending socket commands
static void sendCommand(int fd, char *command, size_t commandLength, char *canReturnMessage);
//Sets up sockets and calls sendCommand()
void canFeast (char *buf, char *canReturnMessage);
//Used to read button status. Returns 1 if button is pressed
int getButton(int button, char *canReturnMessage);
//Reads position of specified node
long getPos(int nodeid, char *canReturnMessage);
//Sets target position of node and moves it to that position.
void setAbsPosSmart(int nodeide, int position, char *canReturnMessage);
//Converts integer to string
void itoa(int value, char* str, int base);
//Reverses a string. Used by itoa()
void strreverse(char* begin, char* end);
//Extracts string a specified pos and stores it in extractStr
void stringExtract(char *origStr, char **extractStr, int pos);
//Converts strings to integer and returns it.
long strToInt(char str[]);
//Sets specified node to preop mode
void preop(int nodeid);
//Sets node to start mode and sets it to position move mode.
void initMotorPos(int nodeid);
//Checks for 4 joints are within +-POSCLEARANCE of the hipTarget and kneeTarget values. Returns 1 if true.
int checkPos(long hipTarget, long kneeTarget);
//Sets profile velocity for position mode motion.
void setProfileVelocity(int nodeid, long velocity);
//Sets profile acceleration and deceleration for position mode motion.
void setProfileAcceleration(int nodeid, long acceleration);
//Used to convert position array from degrees to motors counts as used in CANopen
void motorPosArrayConverter(const double origArr[], long newArr[], int arrSize, int nodeid);
//calculate A and B in the formula y=Ax+B. Use by motorPosArrayConverter()
void calcAB(long y1, long x1, long y2, long x2, double *A, double *B);

int main (){
    printf("Welcome to CANfeast!\n");
    sitStand(SITTING);
    return 0;
}

//State machine with sit-stand logic
void sitStand(int state){

    //Array of trajectory points from R&D team
    //smallest index is standing
    //IMPORTANT: Update state arg passed to sitstand() from main.
    double sitStandArrHip_degrees[]={
            171.5932928,
            170.6247195,
            165.4071417,
            155.2170037,
            141.6186214,
            126.7503007,
            111.8500781,
            98.17421981,
            87.90148218,
            82.63339525,
            81.65449369
    };
    double sitStandArrKnee_degrees[]={
            18.18910485,
            19.56831896,
            26.67960225,
            39.57206594,
            54.74348215,
            68.05336053,
            77.4266519,
            83.03778535,
            85.73771647,
            86.61634762,
            86.7210792
    };

    int arrSize = sizeof(sitStandArrHip_degrees)/ sizeof(sitStandArrHip_degrees[0]);
    //These arrays store the converted values of joint. These can be sent to the motor.
    long sitStandArrayHip[arrSize];
    long sitStandArrayKnee[arrSize];

    //Converting from degrees to motor drive compatible values.
    motorPosArrayConverter(sitStandArrHip_degrees, sitStandArrayHip, arrSize, LHIP);
    motorPosArrayConverter(sitStandArrKnee_degrees, sitStandArrayKnee, arrSize, LKNEE);

    //The sitstate value should be 1 position outside array index (ie -1 or 11 for a 11 item array).
    int sitstate = 1;
    if(state==SITTING)
        sitstate=arrSize;
    if(state==STANDING)
        sitstate=-1;

    //Used to store the canReturnMessage. Not used currently, hence called junk.
    //Should pass this to calling function for possible error handling.
    char junk[STRING_LENGTH];

    while(getButton(BUTTON_FOUR, junk)==0){
        printf("LHIP: %ld, LKNEE: %ld, RHIP: %ld, RKNEE: %ld\n", getPos(LHIP, junk), getPos(LKNEE, junk), getPos(RHIP, junk), getPos(RKNEE, junk));
    }

    //Initialise 4 joints
    initMotorPos(LHIP);
    initMotorPos(LKNEE);
    initMotorPos(RHIP);
    initMotorPos(RKNEE);

    //Sets profile velocity, acceleration & deceleration for the joints.
    setProfileAcceleration(LHIP, PROFILEACCELERATION);
    setProfileVelocity(LHIP,PROFILEVELOCITY);
    setProfileAcceleration(LKNEE, PROFILEACCELERATION);
    setProfileVelocity(LKNEE,PROFILEVELOCITY);
    setProfileAcceleration(RHIP, PROFILEACCELERATION);
    setProfileVelocity(RHIP,PROFILEVELOCITY);
    setProfileAcceleration(RKNEE, PROFILEACCELERATION);
    setProfileVelocity(RKNEE,PROFILEVELOCITY);

    //Use to maintain states.
    //sitstate goes from 0 to 10, indicating the 11 indices of the sitstandArrays
    //sitstate is obtained as argument to this function.
    //movestate is 1 if moving, else 0
    int movestate=0;

    //Used to check if button is pressed.
    int button1Status=0;
    int button2Status=0;
    int button3Status=0;


    //Statemachine loop.
    //Exits when button 3 is pressed.
    //Button 1 sits more, button 2 stands more.
    while(1){

        //read button state
        button1Status=getButton(BUTTON_ONE, junk);
        button2Status=getButton(BUTTON_TWO, junk);
        button3Status=getButton(BUTTON_THREE, junk);

        //Button has to be pressed & Exo not moving & array not at end. If true, execute move.
        if(button1Status==1 && movestate==0 && sitstate<(arrSize-1)){
            movestate=1;
            printf("Sitting down\n");
            setAbsPosSmart(LHIP, sitStandArrayHip[sitstate+1], junk);
            setAbsPosSmart(LKNEE, sitStandArrayKnee[sitstate+1], junk);
            setAbsPosSmart(RHIP, sitStandArrayHip[sitstate+1], junk);
            setAbsPosSmart(RKNEE, sitStandArrayKnee[sitstate+1], junk);
        }

        //If target position is reached, then increment sitstate and set movestate to 0.
        if(sitstate<10 && movestate==1){
            if(checkPos(sitStandArrayHip[sitstate+1], sitStandArrayKnee[sitstate+1])==1){
                printf("Position reached.\n");
                sitstate++;
                movestate=0;
            }
        }

        //Button has to be pressed & Exo not moving & array not at end. If true, execute move.
        if(button2Status==1 && movestate==0 && sitstate>0){
            movestate=1;
            printf("Standing up\n");
            setAbsPosSmart(LHIP, sitStandArrayHip[sitstate-1], junk);
            setAbsPosSmart(LKNEE, sitStandArrayKnee[sitstate-1], junk);
            setAbsPosSmart(RHIP, sitStandArrayHip[sitstate-1], junk);
            setAbsPosSmart(RKNEE, sitStandArrayKnee[sitstate-1], junk);
        }

        //If target position is reached, then decrease sitstate and set movestate to 0.
        if(sitstate>0 && movestate==1){
            if(checkPos(sitStandArrayHip[sitstate-1], sitStandArrayKnee[sitstate-1])==1){
                printf("Position reached.\n");
                sitstate--;
                movestate=0;
            }
        }

        //if button 3 pressed, then set to preop and exit.
        if(button3Status==1){
            preop(1);
            preop(2);
            preop(3);
            preop(4);
            break;
        }
    }
}

//Used to read button status. Returns 1 if button is pressed
int getButton(int button, char *canReturnMessage){
    //char canReturnMessage[STRING_LENGTH];
    char *buttonMessage;
    char *buttonPressed = "0x3F800000";

    char buttons[][STRING_LENGTH]=
            {
                    "[1] 9 read 0x0101 1 u32", //button 1
                    "[1] 9 read 0x0102 1 u32",//button 2
                    "[1] 9 read 0x0103 1 u32", //button 3
                    "[1] 9 read 0x0104 1 u32"//button 4
            };
    canFeast(buttons[button-1], canReturnMessage);

    //printf("CAN return on button press is: %s", canReturnMessage);
    //Button pressed returns "[1] 0x3F800000\n". Extracting 2nd string to compare.
    stringExtract(canReturnMessage,&buttonMessage,2);

    //printf("strcmp result is: %d\n", strcmp(canReturnMessage, "[1] 0x3F800000\r"));

    if(strcmp(buttonMessage, buttonPressed)==0)
        return 1;
    else
        return 0;
}

//Reads position of specified node
long getPos(int nodeid, char *canReturnMessage){
    char node[STRING_LENGTH], getpos[STRING_LENGTH], dataType[STRING_LENGTH], buffer[STRING_LENGTH];
    char positionMessage[STRING_LENGTH];
    char *positionStr;
    long position;

    //Create a message to be sent using canFeast. "[1] <nodeid> read 0x6063 0 i32"
    //Return should be "[1] <position value>\r"
    itoa(nodeid,buffer,DECIMAL);
    strcpy(getpos, "[1] ");
    strcpy(node, buffer);
    strcpy(dataType," read 0x6063 0 i32");
    //concatenate message
    strcat(getpos, node);
    strcat(getpos, dataType);
    //Send message
    canFeast(getpos, positionMessage);

    //printf("Position Message for node %d: %s",nodeid, positionMessage);

    //Extracting 2 position of return string. This should contain position value (if no errors).
    stringExtract(positionMessage,&positionStr,2);
    // printf("Extracted Message for node %d: %s\n",nodeid, positionStr);

    //Converting string to int
    position=strToInt(positionStr);
    // printf("Position of node %d: %ld\n", nodeid, position);

    return position;

}

//Sets target position of node and moves it to that position.
void setAbsPosSmart(int nodeid, int position, char *canReturnMessage){
    char pos[STRING_LENGTH], movePos[STRING_LENGTH], buffer[STRING_LENGTH], nodeStr[STRING_LENGTH];
    char cntrWordH[STRING_LENGTH], cntrWordL[STRING_LENGTH];

    //Create a message to be sent using canFeast. "[1] <nodeid> write 0x607A 0 i32 <position>"
    strcpy(movePos, "[1] "); //Start message with "[1] "
    itoa(nodeid,buffer,DECIMAL); //convert nodeid to string
    strcpy(nodeStr, buffer); //Copy  nodeid to string
    strcat(movePos, nodeStr); //Concat node to movepox
    strcat(movePos, " write 0x607A 0 i32 "); //concat remaining message to movepos
    itoa(position,buffer,DECIMAL); //convert position to string
    strcpy(pos, buffer); //copy position to string
    strcat(movePos, pos); // concat position string to movepos message.

    //printf("Move pose comm: %s\n",movePos);

    //Create a message to be sent using canFeast. "[1] <nodeid> write 0x6040 0 i16 47". This is control word low.
    strcpy(cntrWordL, "[1] ");
    strcat(cntrWordL, nodeStr);
    strcat(cntrWordL, " write 0x6040 0 i16 47");

    //printf("Control Word comm: %s\n",cntrWordL);

    //Create a message to be sent using canFeast. "[1] <nodeid> write 0x6040 0 i16 63". This is control word high.
    strcpy(cntrWordH, "[1] ");
    strcat(cntrWordH, nodeStr);
    strcat(cntrWordH, " write 0x6040 0 i16 63");

    //printf("Control Word Comm%s\n",cntrWordH);

    //Creating array of messages.
    char* commList[]= {
            movePos, //move to this position (absolute)
            cntrWordL, //control word low
            cntrWordH //control word high
    };

    //Sending array of messages.
    int Num_of_Strings = sizeof(commList)/ sizeof(commList[0]);
    for(int i=0; i<Num_of_Strings; ++i)
        canFeast(commList[i],canReturnMessage);
}

//Sets up sockets and calls sendCommand()
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

//For sending socket commands
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
    //printf("%s", buf);
    strcpy(canReturnMessage,buf);
}

//Definitionof itoa(int to string conversion) and helper Kernighan & Ritchie's Ansi C.
//char *  itoa ( int value, char * str, int base );
//value - Value to be converted to a string.
//str - Array in memory where to store the resulting null-terminated string.
//base - Numerical base used to represent the value as a string, between 2 and 36, where 10 means decimal base, 16 hexadecimal, 8 octal, and 2 binary.
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

//Reverses a string. Used by itoa()
void strreverse(char* begin, char* end) {
    char aux;
    while(end>begin)
        aux=*end, *end--=*begin, *begin++=aux;
}

//Extracts a string at position pos of origStr and stores it in extractStr.
//Index 0 is position 1.
// IMPORTANT: The origStr passed into the function gets modified. So pass a copy if needed.
void stringExtract(char *origStr, char **extractStr, int pos){
    //using both space, nextline and carriage return as delimiter
    char delim[] = " \n\r";
    char *ptr = strtok(origStr, delim);

    //strtok extracts each string using delimiter specified earlier.
    for(int i=0; ptr != NULL && i<pos; i++){
        *extractStr=ptr;
        ptr = strtok(NULL, delim);
    }
}

//Converts strings to integer and returns it.
long strToInt(char str[]){
    int len = strlen(str);
    long num=0;

    //If there is - in the beginning, then just convert the remaining chars and return -ve of value.
    if(str[0]=='-'){
        for(int i=0, j=1; i<(len-1); i++, j*=10)
            num += ((str[len-(i+1)]-'0')*j); //ASCII subtraction followed by multiplying factor of 10
        return -num;
    }

    //if no -ve, then just convert convert char to int.
    for(int i=0, j=1; i<len; i++, j*=10)
        num += ((str[len-(i+1)]-'0')*j); //ASCII subtraction followed by multiplying factor of 10
    return num;
}

//set node to preop mode
void preop(int nodeid){
    char junk[STRING_LENGTH];
    char node[STRING_LENGTH], preop[STRING_LENGTH], dataTail[STRING_LENGTH], buffer[STRING_LENGTH];

    //Create a message to be sent using canFeast. "[1] <nodeid> preop".
    itoa(nodeid,buffer,DECIMAL);
    strcpy(preop, "[1] ");
    strcpy(node, buffer);
    strcpy(dataTail," preop");
    //concatenate message
    strcat(preop, node);
    strcat(preop, dataTail);
    //printf("\nNode %d is now in preop state\n",nodeid);
    canFeast(preop,junk);
}

//start motor and set to position mode.
void initMotorPos(int nodeid){
    char node[STRING_LENGTH], comm[STRING_LENGTH], dataTail[STRING_LENGTH], buffer[STRING_LENGTH];
    itoa(nodeid,buffer,DECIMAL);

    char canMessage[STRING_LENGTH];

    //Create a message to be sent using canFeast. "[1] <nodeid> start".
    strcpy(comm, "[1] ");
    strcpy(node, buffer);
    strcpy(dataTail," start");
    //concatenate message
    strcat(comm, node);
    strcat(comm, dataTail);
    //printf("Start motor, node %d, message: %s\n", nodeid, comm);
    canFeast(comm,canMessage);

    //Create a message to be sent using canFeast. "[1] <nodeid> write 0x6060 0 i8 1".
    strcpy(comm, "[1] ");
    strcpy(node, buffer);
    strcpy(dataTail," write 0x6060 0 i8 1");
    //concatenate message
    strcat(comm, node);
    strcat(comm, dataTail);
    //printf("Position mode motor, node %d, message: %s\n", nodeid, comm);
    canFeast(comm,canMessage);
}

//Checks for 4 joints are within +-POSCLEARANCE of the hipTarget and kneeTarget values. Returns 1 if true.
int checkPos(long hipTarget, long kneeTarget){

    //The positions could be polled and stored earlier for more readable code. But getpos uses canfeast which has overhead.
    //Since C support short circuit evaluation, using getPos in if statement is more efficient.
    char junk[STRING_LENGTH];
    if(getPos(LHIP, junk) > (hipTarget-POSCLEARANCE) && getPos(LHIP, junk) < (hipTarget+POSCLEARANCE) &&
       getPos(RHIP, junk) > (hipTarget-POSCLEARANCE) && getPos(RHIP, junk) < (hipTarget+POSCLEARANCE)){
        if(getPos(LKNEE, junk) > (kneeTarget-POSCLEARANCE) && getPos(LKNEE, junk) < (kneeTarget+POSCLEARANCE) &&
           getPos(RKNEE, junk) > (kneeTarget-POSCLEARANCE) && getPos(RKNEE, junk) < (kneeTarget+POSCLEARANCE)) {
            return 1;
        }
        return 0;
    }
    return 0;
}

//Sets profile velocity for position mode motion.
void setProfileVelocity(int nodeid, long velocity){
    char node[STRING_LENGTH], comm[STRING_LENGTH], buffer[STRING_LENGTH], velMessage[STRING_LENGTH];
    char junk[STRING_LENGTH];

    //Create a message to be sent using canFeast. "[1] <nodeid> write 0x6081 0 i32 <velocity>".
    strcpy(comm, "[1] ");
    itoa(nodeid,buffer,DECIMAL);
    strcpy(node, buffer);
    strcat(comm, node);
    strcat(comm, " write 0x6081 0 i32 ");
    itoa(velocity,buffer,DECIMAL);
    strcpy(velMessage, buffer);
    strcat(comm, velMessage);

    //printf("Velocity message %s\n", comm);

    canFeast(comm, junk);
}

//Sets profile acceleration and deceleration for position mode motion.
//Using same value for acceleration and deceleration.
void setProfileAcceleration(int nodeid, long acceleration){
    char node[STRING_LENGTH], commAcc[STRING_LENGTH], commDec[STRING_LENGTH], buffer[STRING_LENGTH], accelMessage[STRING_LENGTH];
    char junk[STRING_LENGTH];

    //Create a message to be sent using canFeast.
    // "[1] <nodeid> write 0x6083 0 i32 <acceleration>"
    // "[1] <nodeid> write 0x6084 0 i32 <deceleration>"
    strcpy(commAcc, "[1] ");
    strcpy(commDec, "[1] ");

    itoa(nodeid,buffer,DECIMAL);
    strcpy(node, buffer);

    strcat(commAcc, node);
    strcat(commDec, node);

    strcat(commAcc, " write 0x6083 0 i32 ");
    strcat(commDec, " write 0x6084 0 i32 ");

    itoa(acceleration,buffer,DECIMAL);
    strcpy(accelMessage, buffer);

    strcat(commAcc, accelMessage);
    strcat(commDec, accelMessage);

    //printf("Acceleration message %s\n%s\n", commAcc, commDec);

    canFeast(commAcc, junk);
    canFeast(commDec, junk);
}

//Used to convert position array from degrees to motors counts as used in CANopen
void motorPosArrayConverter(const double origArr[], long newArr[], int arrSize, int nodeid){
    double A=0;
    double B=0;

    if(nodeid==1 || nodeid==3)
        calcAB(HIP_MOTOR_POS1, HIP_MOTOR_DEG1, HIP_MOTOR_POS2, HIP_MOTOR_DEG2, &A, &B);

    if(nodeid==2 || nodeid==4)
        calcAB(KNEE_MOTOR_POS1, KNEE_MOTOR_DEG1, KNEE_MOTOR_POS2, KNEE_MOTOR_DEG2, &A, &B);

    for(int i=0; i<arrSize; i++){
        newArr[i]=(long)(A*origArr[i]+B);
    }
}

//calculate A and B in the formula y=Ax+B. Use by motorPosArrayConverter()
void calcAB(long y1, long x1, long y2, long x2, double *A, double *B){
    *A=1.0*(y2-y1)/(x2-x1);
    //printf("A is %f\n", *A);
    *B=1.0*(y1*x2-y2*x1)/(x2-x1);
    //printf("B is %f\n", *B);
}