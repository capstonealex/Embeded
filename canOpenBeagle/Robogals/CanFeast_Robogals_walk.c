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
#include <iostream>
#include "GPIO/GPIOManager.h"
#include "GPIO/GPIOConst.h"

//Buffer for socket
#ifndef BUF_SIZE
#define BUF_SIZE 100000
#endif
//String Length for defining fixed sized char array
#define STRING_LENGTH 50
#define MAX_RECONNECTS 10
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
#define POSCLEARANCE 10000
//Velocity and acceleration for position mode move
#define PROFILEVELOCITY 900000
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
#define STANDING 111
#define SITTING 222
#define STATESTANDING 1
#define STATESITTING 2
#define STATEIMMOBILE 0
#define WALKINGFORWARD 1
#define WALKINGBACK 2

/*
 Most functions defined here use canReturnMessage as a pass-by-reference string.
 The return message from canopencomm is stored in this.
 In functions like getPos(), this is parsed to obtain the position value.
 However, this string can contain error messages as well.
 Therefore, it can be used by the calling function for error-handling.
 */

//State machine with sit-stand logic
void sitStand(int *socket, int initState);
//For sending socket commands
void canFeastUp(int *canSocket);
void canFeast(int *canSocket, char *command, char *canReturnMessage);
void canFeastDown(int *canSocket);
void canFeastErrorHandler(int *canSocket, char *command, char *canReturnMessage);
//Used to read button status. Returns 1 if button is pressed
int getButton(int *canSocket, int button, char *canReturnMessage);
//Reads position of specified node
long getPos(int *canSocket, int nodeid, char *canReturnMessage);
//Sets target position of node and moves it to that position.
void setAbsPosSmart(int *canSocket, int nodeide, int position, char *canReturnMessage);
//Converts integer to string
void itoa(int value, char *str, int base);
//Reverses a string. Used by itoa()
void strreverse(char *begin, char *end);
//Extracts string a specified pos and stores it in extractStr
void stringExtract(char *origStr, char **extractStr, int pos);
//Converts strings to integer and returns it.
long strToInt(char str[]);
//Sets specified node to preop mode
void preop(int *canSocket, int nodeid);
//Sets node to start mode and sets it to position move mode.
void initMotorPos(int *canSocket, int nodeid);
//Checks for 4 joints are within +-POSCLEARANCE of the hipTarget and kneeTarget values. Returns 1 if true.
int checkPos(int *canSocket, long lhipTarget, long lkneeTarget, long rhipTarget, long rkneeTarget);
//Sets profile velocity for position mode motion.
void setProfileVelocity(int *canSocket, int nodeid, long velocity);
//Sets profile acceleration and deceleration for position mode motion.
void setProfileAcceleration(int *canSocket, int nodeid, long acceleration);
//Used to convert position array from degrees to motors counts as used in CANopen
void motorPosArrayConverter(const double origArr[], long newArr[], int arrSize, int nodeid);
//calculate A and B in the formula y=Ax+B. Use by motorPosArrayConverter()
void calcAB(long y1, long x1, long y2, long x2, double *A, double *B);
//Function to set motors to start mode and set accelerations/velocities.
void initExo(int *socket);
//Function to walk
void walkMode(int *socket);
//Function to put motors to preop.
void stopExo(int *socket);
void changeVel(int *socket, long newVelocity);

int main()
{
    printf("Welcome to CANfeast!\n");
    int socket;
    char junk[STRING_LENGTH];
    int on = 1;
    canFeastUp(&socket);
    // GREEN BUTTON
    int button4=1;

    while (button4 == 1)
    {
        printf("LHIP: %ld, LKNEE: %ld, RHIP: %ld, RKNEE: %ld\n", getPos(&socket, LHIP, junk), getPos(&socket, LKNEE, junk), getPos(&socket, RHIP, junk), getPos(&socket, RKNEE, junk));
        std::cout<<"PRESS GREEN BUTTON TO START: ";
        GPIO::GPIOManager* gp = GPIO::GPIOManager::getInstance();
	    int pin = GPIO::GPIOConst::getInstance()->getGpioByKey("P8_9");
	    gp->setDirection(pin, GPIO::INPUT);
	    button4 = gp->getValue(pin);
        printf("Button 4: %d\n",button4);
    }

    initExo(&socket);
    sitStand(&socket, SITTING);
    changeVel(&socket, 700000);
    walkMode(&socket);
    changeVel(&socket, PROFILEVELOCITY);
    sitStand(&socket, STANDING);
    stopExo(&socket);

    canFeastDown(&socket);

    return 0;
}

//State machine with sit-stand logic
void sitStand(int *socket, int initState)
{
    printf("Sit Stand Mode\n");
    //Used to store the canReturnMessage. Not used currently, hence called junk.
    //Should pass this to calling function for possible error handling.
    char junk[STRING_LENGTH];

    //Array of trajectory points from R&D team
    //smallest index is standing
    //IMPORTANT: Update initState arg passed to sitstand() from main.

    double sitStandArrHip_degrees[] = {
            180.00,
            96.23
    };
    double sitStandArrKnee_degrees[] = {
            0.00,
            84.42
    };

    int arrSize = sizeof(sitStandArrHip_degrees) / sizeof(sitStandArrHip_degrees[0]);
    //These arrays store the converted values of joint. These can be sent to the motor.
    long sitStandArrayHip[arrSize];
    long sitStandArrayKnee[arrSize];

    //Converting from degrees to motor drive compatible values.
    motorPosArrayConverter(sitStandArrHip_degrees, sitStandArrayHip, arrSize, LHIP);
    motorPosArrayConverter(sitStandArrKnee_degrees, sitStandArrayKnee, arrSize, LKNEE);

    //The sitstate value should be 1 position outside array index (ie -1 or 11 for a 11 item array).
    //This is required since 1st iteration of statemachine goes to array index sitstate+1 or sitstate-1
    int sitstate = 1;
    if (initState == SITTING)
        sitstate = arrSize;
    if (initState == STANDING)
        sitstate = -1;

    //Use to maintain states.
    //sitstate goes from 0 to arrsize-1, indicating the indices of the sitstandArrays
    //movestate can be STATEIMMOBILE, STATESITTING or STATESTANDING
    int movestate = STATEIMMOBILE;

    //Used to check if button is pressed.
    int button1Status = 0;
    int button2Status = 0;
    int button3Status = 0;
    int button4Status = 0;


    //Statemachine loop.
    //Kill motor and end program when button 3 is pressed.
    //Button 4 exits state machine
    //Button 1 sits more, button 2 stands more.
    static char *BUTTONRED = "P8_7";
    static char *BUTTONBLUE = "P8_8";
    static char *BUTTONGREEN = "P8_9";
    static char *BUTTONYELLOW = "P8_10";
    while (1)
    {
    	GPIO::GPIOManager *gp = GPIO::GPIOManager::getInstance();
	    int red = GPIO::GPIOConst::getInstance()->getGpioByKey(BUTTONRED);
	    int blue = GPIO::GPIOConst::getInstance()->getGpioByKey(BUTTONBLUE);
	    int green = GPIO::GPIOConst::getInstance()->getGpioByKey(BUTTONGREEN);
	    int yellow = GPIO::GPIOConst::getInstance()->getGpioByKey(BUTTONYELLOW);
	    gp->setDirection(red, GPIO::INPUT);
	    gp->setDirection(blue, GPIO::INPUT);
	    gp->setDirection(green, GPIO::INPUT);
	    gp->setDirection(yellow, GPIO::INPUT);
	    button1Status = gp->getValue(red);
	    button2Status = gp->getValue(blue);
	    button4Status = gp->getValue(green);
	    button3Status = gp->getValue(yellow);
	    gp->~GPIOManager();

        // //read button state using keyboard
        // std::cout<<"Enter button 1: ";
        // std::cin>>button1Status;
         // printf("Buttons: %d %d %d %d ... %d , %d\n",button1Status,button2Status,button3Status,button4Status, movestate,sitstate);
        /// USING BUTTONS

        //Button has to be pressed & Exo not moving & array not at end. If true, execute move.
        if (button1Status == 0 && movestate == STATEIMMOBILE && sitstate < (arrSize - 1))
        {
            movestate = STATESITTING;
            printf("Sitting down\n");
            setAbsPosSmart(socket, LHIP, sitStandArrayHip[sitstate + 1], junk);
            setAbsPosSmart(socket, LKNEE, sitStandArrayKnee[sitstate + 1], junk);
            setAbsPosSmart(socket, RHIP, sitStandArrayHip[sitstate + 1], junk);
            setAbsPosSmart(socket, RKNEE, sitStandArrayKnee[sitstate + 1], junk);
        }

        //If target position is reached, then increment sitstate and set movestate to 0.
        if (sitstate < (arrSize-1) && movestate == STATESITTING)
        {
            if (checkPos(socket, sitStandArrayHip[sitstate + 1], sitStandArrayKnee[sitstate + 1], sitStandArrayHip[sitstate + 1], sitStandArrayKnee[sitstate + 1]) == 1)
            {
                printf("Position reached.\n");
                sitstate++;
                if(sitstate==(arrSize-1))
                    printf("fully seated position\n");
                movestate = STATEIMMOBILE;
            }
        }

        //Button has to be pressed & Exo not moving & array not at end. If true, execute move.
        if (button2Status == 0 && movestate == STATEIMMOBILE && sitstate > 0)
        {
            movestate = STATESTANDING;
            printf("Standing up\n");
            setAbsPosSmart(socket, LHIP, sitStandArrayHip[sitstate - 1], junk);
            setAbsPosSmart(socket, LKNEE, sitStandArrayKnee[sitstate - 1], junk);
            setAbsPosSmart(socket, RHIP, sitStandArrayHip[sitstate - 1], junk);
            setAbsPosSmart(socket, RKNEE, sitStandArrayKnee[sitstate - 1], junk);
        }

        //If target position is reached, then decrease sitstate and set movestate to 0.
        if (sitstate > 0 && movestate == STATESTANDING)
        {
            if (checkPos(socket, sitStandArrayHip[sitstate - 1], sitStandArrayKnee[sitstate - 1], sitStandArrayHip[sitstate - 1], sitStandArrayKnee[sitstate - 1]) == 1)
            {
                printf("Position reached.\n");
                sitstate--;
                if(sitstate==0)
                    printf("full standing position\n");
                movestate = STATEIMMOBILE;
            }
        }

        //if button 3 pressed, then set to preop and exit.
        if (button3Status == 0)
        {
            printf("Terminating Program (sitstand)\n");
            stopExo(socket);
            canFeastDown(socket);
            exit(EXIT_SUCCESS);
        }

        //Exit statemachine only if button 4 pressed and gone from sitting to standing or vice versa.
        if (button4Status == 0 && ((sitstate==arrSize-1 && initState==STANDING)||(sitstate==0 && initState==SITTING)))
        {
            break;
        }
    }
}


//Walking state machine
void walkMode(int *socket){

    printf("Walk Mode\n");

    //Used to store the canReturnMessage. Not used currently, hence called junk.
    char junk[STRING_LENGTH];

    //Array of trajectory points from R&D team
    double walkArrLHip_degrees[] = {
        170.00,
        170.06,
        168.01,
        157.19,
        130.26,
        110.86,
        155.61,
        156.20,
        159.09,
        164.12,
        170.00,
        177.14,
        184.39,
        183.49,
        177.24,
        160.69,
        130.26,
        110.86,
        155.61,
        156.20,
        159.09,
        164.12,
        170.00,
        177.14,
        184.39,
        181.84,
        169.71,
        150.11,
        130.26,
        130.26,
        170.00 
    };
    double walkArrLKnee_degrees[] = {
          0.00,
          4.89,
         27.01,
         58.79,
         82.60,
         67.44,
          0.00,
          0.00,
          0.00,
          0.00,
          0.00,
          0.00,
          0.00,
          4.89,
         27.01,
         58.79,
         82.60,
         67.44,
          0.00,
          0.00,
          0.00,
          0.00,
          0.00,
          0.00,
          0.00,
          3.31,
         19.79,
         48.64,
         82.60,
         82.60,
          0.00 
    };

    double walkArrRHip_degrees[] = {
        170.00,
        169.63,
        168.31,
        167.63,
        170.00,
        177.14,
        184.39,
        183.49,
        177.24,
        160.69,
        130.26,
        110.86,
        155.61,
        156.20,
        159.09,
        164.12,
        170.00,
        177.14,
        184.39,
        183.49,
        177.24,
        160.69,
        130.26,
        110.86,
        155.61,
        156.56,
        160.77,
        166.49,
        170.00,
        170.00,
        170.00 
    };
    double walkArrRKnee_degrees[] = {
          0.00,
          0.00,
          0.00,
          0.00,
          0.00,
          0.00,
          0.00,
          4.89,
         27.01,
         58.79,
         82.60,
         67.44,
          0.00,
          0.00,
          0.00,
          0.00,
          0.00,
          0.00,
          0.00,
          4.89,
         27.01,
         58.79,
         82.60,
         67.44,
          0.00,
          0.00,
          0.00,
          0.00,
          0.00,
          0.00,
          0.00
    };

    int arrSize = sizeof(walkArrLHip_degrees) / sizeof(walkArrLHip_degrees[0]);
    //These arrays store the converted values of joint. These can be sent to the motor.
    long walkArrLHip[arrSize];
    long walkArrLKnee[arrSize];
    long walkArrRHip[arrSize];
    long walkArrRKnee[arrSize];

    //Converting from degrees to motor drive compatible values.
    motorPosArrayConverter(walkArrLHip_degrees, walkArrLHip, arrSize, LHIP);
    motorPosArrayConverter(walkArrLKnee_degrees, walkArrLKnee, arrSize, LKNEE);
    motorPosArrayConverter(walkArrRHip_degrees, walkArrRHip, arrSize, RHIP);
    motorPosArrayConverter(walkArrRKnee_degrees, walkArrRKnee, arrSize, RKNEE);

    //The walkstate value should be 1 position outside array index (ie -1 or 11 for a 11 item array).
    //This is required because the 1st iteration of statemachine goes to array position walkstate+1
    int walkstate = -1;

    //Use to maintain states.
    //walkstate can be STATEIMMOBILE, WALKINGFORWARD or WALKINGBACK
    int movestate = STATEIMMOBILE;

    //Used to check if button is pressed.
    int button1Status = 0;
    int button2Status = 0;
    int button3Status = 0;
    int button4Status = 0;


    //Statemachine loop.
    //Kill motor and end program when button 3 is pressed.
    //Button 4 exits state machine
    //Button 1 sits more, button 2 stands more.
    static char *BUTTONRED = "P8_7";
    static char *BUTTONBLUE = "P8_8";
    static char *BUTTONGREEN = "P8_9";
    static char *BUTTONYELLOW = "P8_10";
    

    //Statemachine loop.
    //Kill motor and program when button 3 is pressed.
    //Exits state machine when button 4 pressed
    //Button 1 sits more, button 2 stands more.
    while (1)
    {

        //read button state
        GPIO::GPIOManager *gp = GPIO::GPIOManager::getInstance();
        int red = GPIO::GPIOConst::getInstance()->getGpioByKey(BUTTONRED);
        int blue = GPIO::GPIOConst::getInstance()->getGpioByKey(BUTTONBLUE);
        int green = GPIO::GPIOConst::getInstance()->getGpioByKey(BUTTONGREEN);
        int yellow = GPIO::GPIOConst::getInstance()->getGpioByKey(BUTTONYELLOW);
        gp->setDirection(red, GPIO::INPUT);
        gp->setDirection(blue, GPIO::INPUT);
        gp->setDirection(green, GPIO::INPUT);
        gp->setDirection(yellow, GPIO::INPUT);
        button1Status = gp->getValue(red);
        button2Status = gp->getValue(blue);
        button4Status = gp->getValue(green);
        button3Status = gp->getValue(yellow);
        gp->~GPIOManager();
        //Button has to be pressed & Exo not moving & array not at end. If true, execute move.
        if (button1Status == 0 && movestate == STATEIMMOBILE && walkstate < (arrSize - 1))
        {
            movestate = WALKINGFORWARD;
            printf("Walking forward\n");
            setAbsPosSmart(socket, LHIP, walkArrLHip[walkstate + 1], junk);
            setAbsPosSmart(socket, LKNEE, walkArrLKnee[walkstate + 1], junk);
            setAbsPosSmart(socket, RHIP, walkArrRHip[walkstate + 1], junk);
            setAbsPosSmart(socket, RKNEE, walkArrRKnee[walkstate + 1], junk);
        }

        //If target position is reached, then increment walkstate and set movestate to 0.
        if (walkstate < (arrSize-1) && movestate == WALKINGFORWARD)
        {
            if (checkPos(socket, walkArrLHip[walkstate + 1], walkArrLKnee[walkstate + 1], walkArrRHip[walkstate + 1], walkArrRKnee[walkstate + 1]) == 1)
            {
                printf("Position reached.\n");
                walkstate++;
                if(walkstate==(arrSize-1))
                    printf("final array position\n");
                movestate = STATEIMMOBILE;
            }
        }

        //Button has to be pressed & Exo not moving & array not at end. If true, execute move.
        if (button2Status == 0 && movestate == STATEIMMOBILE && walkstate > 0)
        {
            movestate = WALKINGBACK;
            printf("Walking backward\n");
            setAbsPosSmart(socket, LHIP, walkArrLHip[walkstate - 1], junk);
            setAbsPosSmart(socket, LKNEE, walkArrLKnee[walkstate - 1], junk);
            setAbsPosSmart(socket, RHIP, walkArrRHip[walkstate - 1], junk);
            setAbsPosSmart(socket, RKNEE, walkArrRKnee[walkstate - 1], junk);
        }

        //If target position is reached, then decrease walkstate and set movestate to 0.
        if (walkstate > 0 && movestate == WALKINGBACK)
        {
            if (checkPos(socket, walkArrLHip[walkstate - 1], walkArrLKnee[walkstate - 1], walkArrRHip[walkstate - 1], walkArrRKnee[walkstate - 1]) == 1)
            {
                printf("Position reached.\n");
                walkstate--;
                if(walkstate==0)
                    printf("first array position\n");
                movestate = STATEIMMOBILE;
            }
        }

        //if button 3 pressed, then set to preop and exit program.
        if (button3Status == 0)
        {
            printf("Terminating Program (walk mode)\n");
            stopExo(socket);
            canFeastDown(socket);
            exit(EXIT_SUCCESS);
        }

        //Only exit state machine if button 4 pressed and at end of walking array. 
        if(button4Status==0 && walkstate==(arrSize-1)){
            break;
        }
    }
}


//Used to read button status. Returns 1 if button is pressed
int getButton(int *canSocket, int button, char *canReturnMessage)
{
    //char canReturnMessage[STRING_LENGTH];
    char *buttonMessage;
    char *buttonPressed = "0x3F800000";

    char buttons[][STRING_LENGTH] =
            {
                    "[1] 9 read 0x0101 1 u32", //button 1
                    "[1] 9 read 0x0102 1 u32", //button 2
                    "[1] 9 read 0x0103 1 u32", //button 3
                    "[1] 9 read 0x0104 1 u32"  //button 4
            };
    canFeast(canSocket, buttons[button - 1], canReturnMessage);

    //printf("CAN return on button press is: %s", canReturnMessage);
    //Button pressed returns "[1] 0x3F800000\n". Extracting 2nd string to compare.
    stringExtract(canReturnMessage, &buttonMessage, 2);

    //printf("strcmp result is: %d\n", strcmp(canReturnMessage, "[1] 0x3F800000\r"));

    if (strcmp(buttonMessage, buttonPressed) == 0)
        return 1;
    else
        return 0;
}

//Reads position of specified node
long getPos(int *canSocket, int nodeid, char *canReturnMessage)
{
    char node[STRING_LENGTH], getpos[STRING_LENGTH], dataType[STRING_LENGTH], buffer[STRING_LENGTH];
    char positionMessage[STRING_LENGTH];
    char *positionStr;
    long position;

    //Create a message to be sent using canFeast. "[1] <nodeid> read 0x6063 0 i32"
    //Return should be "[1] <position value>\r"
    itoa(nodeid, buffer, DECIMAL);
    strcpy(getpos, "[1] ");
    strcpy(node, buffer);
    strcpy(dataType, " read 0x6063 0 i32");
    //concatenate message
    strcat(getpos, node);
    strcat(getpos, dataType);
    //Send message
    canFeast(canSocket, getpos, positionMessage);

    //printf("Position Message for node %d: %s",nodeid, positionMessage);

    //Extracting 2 position of return string. This should contain position value (if no errors).
    stringExtract(positionMessage, &positionStr, 2);
    // printf("Extracted Message for node %d: %s\n",nodeid, positionStr);

    //Converting string to int
    position = strToInt(positionStr);
    // printf("Position of node %d: %ld\n", nodeid, position);

    return position;
}

//Sets target position of node and moves it to that position.
void setAbsPosSmart(int *canSocket, int nodeid, int position, char *canReturnMessage)
{
    char pos[STRING_LENGTH], movePos[STRING_LENGTH], buffer[STRING_LENGTH], nodeStr[STRING_LENGTH];
    char cntrWordH[STRING_LENGTH], cntrWordL[STRING_LENGTH];

    //Create a message to be sent using canFeast. "[1] <nodeid> write 0x607A 0 i32 <position>"
    strcpy(movePos, "[1] ");                 //Start message with "[1] "
    itoa(nodeid, buffer, DECIMAL);           //convert nodeid to string
    strcpy(nodeStr, buffer);                 //Copy  nodeid to string
    strcat(movePos, nodeStr);                //Concat node to movepox
    strcat(movePos, " write 0x607A 0 i32 "); //concat remaining message to movepos
    itoa(position, buffer, DECIMAL);         //convert position to string
    strcpy(pos, buffer);                     //copy position to string
    strcat(movePos, pos);                    // concat position string to movepos message.

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
    char *commList[] = {
            movePos,   //move to this position (absolute)
            cntrWordL, //control word low
            cntrWordH  //control word high
    };

    //Sending array of messages.
    int Num_of_Strings = sizeof(commList) / sizeof(commList[0]);
    for (int i = 0; i < Num_of_Strings; ++i)
        canFeast(canSocket, commList[i], canReturnMessage);
}

// Creates a socket connection to canopend using a pointer to int socket

void canFeastUp(int *canSocket)
{
    char *socketPath = "/tmp/CO_command_socket"; /* Name of the local domain socket, configurable by arguments. */
    struct sockaddr_un addr;

    *canSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    printf("Socket: %d\n",*canSocket);

    if (*canSocket == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socketPath, sizeof(addr.sun_path) - 1);
    // Try to make a connection to the local UNIT AF_UNIX SOCKET, quit if unavailable
    if (connect(*canSocket, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) == -1)
    {
        perror("Socket connection failed");
        exit(EXIT_FAILURE);
    }
}
void canFeastDown(int *canSocket)
{
    printf("closing socket...\n");
    //close socket
    close(*canSocket);
    printf("socket close\n");
}
void canFeast(int *canSocket, char *command, char *canReturnMessage)
{
    int commandLength = strlen(command);
    size_t n;
    char buf[BUF_SIZE];

    if (write(*canSocket, command, commandLength) != commandLength)
    {
        perror("Socket write failed");
        exit(EXIT_FAILURE);
    }

    n = read(*canSocket, buf, sizeof(buf));
    if (n == -1)
    {
        perror("Socket read failed");
        close(*canSocket);
        exit(EXIT_FAILURE);
    }
    //printf("%s", buf);
    strcpy(canReturnMessage, buf);
}
// Error handling -> reset socket and try again when sockets fail.
void canFeastErrorHandler(int *canSocket, char *command, char *canReturnMessage)
{
    int commandLength = strlen(command);
    int recconects;
    size_t n;
    char buf[BUF_SIZE];

    if (write(*canSocket, command, commandLength) != commandLength)
    {
        perror("Socket write failed, attempting again");
        canFeast(canSocket, command, canReturnMessage);
    }
    while ((write(*canSocket, command, commandLength) != commandLength) && recconects != MAX_RECONNECTS)
    {
        perror("Socket write failed, attempting again");
        //shut down socket
        canFeastDown(canSocket);
        //recreate socket
        canFeastUp(canSocket);
        recconects++;
        // try to send again
    }

    n = read(*canSocket, buf, sizeof(buf));
    if (n == -1)
    {
        perror("Socket read failed, attempting to send command again");
        //shut down socket
        canFeastDown(canSocket);
        //recreate socket
        canFeastUp(canSocket);
        // try to send again
        canFeast(canSocket, command, canReturnMessage);
        exit(EXIT_FAILURE);
    }
    printf("%s", buf);
    strcpy(canReturnMessage, buf);
}

//Definitionof itoa(int to string conversion) and helper Kernighan & Ritchie's Ansi C.
//char *  itoa ( int value, char * str, int base );
//value - Value to be converted to a string.
//str - Array in memory where to store the resulting null-terminated string.
//base - Numerical base used to represent the value as a string, between 2 and 36, where 10 means decimal base, 16 hexadecimal, 8 octal, and 2 binary.
void itoa(int value, char *str, int base)
{
    static char num[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char *wstr = str;
    int sign;

    // Validate base
    if (base < 2 || base > 35)
    {
        *wstr = '\0';
        return;
    }

    // Take care of sign
    if ((sign = value) < 0)
        value = -value;

    // Conversion. Number is reversed.
    do
        *wstr++ = num[value % base];
    while (value /= base);

    if (sign < 0)
        *wstr++ = '-';

    *wstr = '\0';

    // Reverse string
    strreverse(str, wstr - 1);
}

//Reverses a string. Used by itoa()
void strreverse(char *begin, char *end)
{
    char aux;
    while (end > begin)
        aux = *end, *end-- = *begin, *begin++ = aux;
}

//Extracts a string at position pos of origStr and stores it in extractStr.
//Index 0 is position 1.
// IMPORTANT: The origStr passed into the function gets modified. So pass a copy if needed.
void stringExtract(char *origStr, char **extractStr, int pos)
{
    //using both space, nextline and carriage return as delimiter
    char delim[] = " \n\r";
    char *ptr = strtok(origStr, delim);

    //strtok extracts each string using delimiter specified earlier.
    for (int i = 0; ptr != NULL && i < pos; i++)
    {
        *extractStr = ptr;
        ptr = strtok(NULL, delim);
    }
}

//Converts strings to integer and returns it.
long strToInt(char str[])
{
    int len = strlen(str);
    long num = 0;

    //If there is - in the beginning, then just convert the remaining chars and return -ve of value.
    if (str[0] == '-')
    {
        for (int i = 0, j = 1; i < (len - 1); i++, j *= 10)
            num += ((str[len - (i + 1)] - '0') * j); //ASCII subtraction followed by multiplying factor of 10
        return -num;
    }

    //if no -ve, then just convert convert char to int.
    for (int i = 0, j = 1; i < len; i++, j *= 10)
        num += ((str[len - (i + 1)] - '0') * j); //ASCII subtraction followed by multiplying factor of 10
    return num;
}

//set node to preop mode
void preop(int *canSocket, int nodeid)
{
    char junk[STRING_LENGTH];
    char node[STRING_LENGTH], preop[STRING_LENGTH], dataTail[STRING_LENGTH], buffer[STRING_LENGTH];

    //Create a message to be sent using canFeast. "[1] <nodeid> preop".
    itoa(nodeid, buffer, DECIMAL);
    strcpy(preop, "[1] ");
    strcpy(node, buffer);
    strcpy(dataTail, " preop");
    //concatenate message
    strcat(preop, node);
    strcat(preop, dataTail);
    //printf("\nNode %d is now in preop state\n",nodeid);
    canFeast(canSocket, preop, junk);
}

//start motor and set to position mode.
void initMotorPos(int *canSocket, int nodeid)
{
    char node[STRING_LENGTH], comm[STRING_LENGTH], dataTail[STRING_LENGTH], buffer[STRING_LENGTH];
    itoa(nodeid, buffer, DECIMAL);

    char canMessage[STRING_LENGTH];

    //Create a message to be sent using canFeast. "[1] <nodeid> start".
    strcpy(comm, "[1] ");
    strcpy(node, buffer);
    strcpy(dataTail, " start");
    //concatenate message
    strcat(comm, node);
    strcat(comm, dataTail);
    //printf("Start motor, node %d, message: %s\n", nodeid, comm);
    canFeast(canSocket, comm, canMessage);

    //Create a message to be sent using canFeast. "[1] <nodeid> write 0x6060 0 i8 1".
    strcpy(comm, "[1] ");
    strcpy(node, buffer);
    strcpy(dataTail, " write 0x6060 0 i8 1");
    //concatenate message
    strcat(comm, node);
    strcat(comm, dataTail);
    //printf("Position mode motor, node %d, message: %s\n", nodeid, comm);
    canFeast(canSocket, comm, canMessage);
}

//Checks for 4 joints are within +-POSCLEARANCE of the hipTarget and kneeTarget values. Returns 1 if true.
int checkPos(int *canSocket, long lhipTarget, long lkneeTarget, long rhipTarget, long rkneeTarget)
{

    //The positions could be polled and stored earlier for more readable code. But getpos uses canfeast which has overhead.
    //Since C support short circuit evaluation, using getPos in if statement is more efficient.
    char junk[STRING_LENGTH];
    if (getPos(canSocket, LHIP, junk) > (lhipTarget - POSCLEARANCE) && getPos(canSocket, LHIP, junk) < (lhipTarget + POSCLEARANCE) &&
        getPos(canSocket, RHIP, junk) > (rhipTarget - POSCLEARANCE) && getPos(canSocket, RHIP, junk) < (rhipTarget + POSCLEARANCE))
    {
        if (getPos(canSocket, LKNEE, junk) > (lkneeTarget - POSCLEARANCE) && getPos(canSocket, LKNEE, junk) < (lkneeTarget + POSCLEARANCE) &&
            getPos(canSocket, RKNEE, junk) > (rkneeTarget - POSCLEARANCE) && getPos(canSocket, RKNEE, junk) < (rkneeTarget + POSCLEARANCE))
        {
            return 1;
        }
        return 0;
    }
    return 0;
}

//Sets profile velocity for position mode motion.
void setProfileVelocity(int *canSocket, int nodeid, long velocity)
{
    char node[STRING_LENGTH], comm[STRING_LENGTH], buffer[STRING_LENGTH], velMessage[STRING_LENGTH];
    char junk[STRING_LENGTH];

    //Create a message to be sent using canFeast. "[1] <nodeid> write 0x6081 0 i32 <velocity>".
    strcpy(comm, "[1] ");
    itoa(nodeid, buffer, DECIMAL);
    strcpy(node, buffer);
    strcat(comm, node);
    strcat(comm, " write 0x6081 0 i32 ");
    itoa(velocity, buffer, DECIMAL);
    strcpy(velMessage, buffer);
    strcat(comm, velMessage);

    //printf("Velocity message %s\n", comm);

    canFeast(canSocket, comm, junk);
}

//Sets profile acceleration and deceleration for position mode motion.
//Using same value for acceleration and deceleration.
void setProfileAcceleration(int *canSocket, int nodeid, long acceleration)
{
    char node[STRING_LENGTH], commAcc[STRING_LENGTH], commDec[STRING_LENGTH], buffer[STRING_LENGTH], accelMessage[STRING_LENGTH];
    char junk[STRING_LENGTH];

    //Create a message to be sent using canFeast.
    // "[1] <nodeid> write 0x6083 0 i32 <acceleration>"
    // "[1] <nodeid> write 0x6084 0 i32 <deceleration>"
    strcpy(commAcc, "[1] ");
    strcpy(commDec, "[1] ");

    itoa(nodeid, buffer, DECIMAL);
    strcpy(node, buffer);

    strcat(commAcc, node);
    strcat(commDec, node);

    strcat(commAcc, " write 0x6083 0 i32 ");
    strcat(commDec, " write 0x6084 0 i32 ");

    itoa(acceleration, buffer, DECIMAL);
    strcpy(accelMessage, buffer);

    strcat(commAcc, accelMessage);
    strcat(commDec, accelMessage);

    //printf("Acceleration message %s\n%s\n", commAcc, commDec);

    canFeast(canSocket, commAcc, junk);
    canFeast(canSocket, commDec, junk);
}

//Used to convert position array from degrees to motors counts as used in CANopen
void motorPosArrayConverter(const double origArr[], long newArr[], int arrSize, int nodeid)
{
    double A = 0;
    double B = 0;

    if (nodeid == 1 || nodeid == 3)
        calcAB(HIP_MOTOR_POS1, HIP_MOTOR_DEG1, HIP_MOTOR_POS2, HIP_MOTOR_DEG2, &A, &B);

    if (nodeid == 2 || nodeid == 4)
        calcAB(KNEE_MOTOR_POS1, KNEE_MOTOR_DEG1, KNEE_MOTOR_POS2, KNEE_MOTOR_DEG2, &A, &B);

    for (int i = 0; i < arrSize; i++)
    {
        newArr[i] = (long)(A * origArr[i] + B);
    }
}

//calculate A and B in the formula y=Ax+B. Use by motorPosArrayConverter()
void calcAB(long y1, long x1, long y2, long x2, double *A, double *B)
{
    *A = 1.0 * (y2 - y1) / (x2 - x1);
    //printf("A is %f\n", *A);
    *B = 1.0 * (y1 * x2 - y2 * x1) / (x2 - x1);
    //printf("B is %f\n", *B);
}

void initExo(int *socket){

    //Initialise 4 joints
    initMotorPos(socket, LHIP);
    initMotorPos(socket, LKNEE);
    initMotorPos(socket, RHIP);
    initMotorPos(socket, RKNEE);

    //Sets profile velocity, acceleration & deceleration for the joints.
    setProfileAcceleration(socket, LHIP, PROFILEACCELERATION);
    setProfileVelocity(socket, LHIP, PROFILEVELOCITY);
    setProfileAcceleration(socket, LKNEE, PROFILEACCELERATION);
    setProfileVelocity(socket, LKNEE, PROFILEVELOCITY);
    setProfileAcceleration(socket, RHIP, PROFILEACCELERATION);
    setProfileVelocity(socket, RHIP, PROFILEVELOCITY);
    setProfileAcceleration(socket, RKNEE, PROFILEACCELERATION);
    setProfileVelocity(socket, RKNEE, PROFILEVELOCITY);
}

//Function to put motors to preop.
void stopExo(int *socket){
    preop(socket, LHIP);
    preop(socket, LKNEE);
    preop(socket, RHIP);
    preop(socket, RKNEE);
}

void changeVel(int *socket, long newVelocity){
    setProfileVelocity(socket, LHIP, newVelocity);
    setProfileVelocity(socket, LKNEE, newVelocity);
    setProfileVelocity(socket, RHIP, newVelocity);
    setProfileVelocity(socket, RKNEE, newVelocity);
}
