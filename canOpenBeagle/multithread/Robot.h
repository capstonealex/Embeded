//
// Created by William Campbell on 2019-07-30.
//

#ifndef CANOPENBEAGLE_ROBOT_H
#define CANOPENBEAGLE_ROBOT_H
#include "Joint.h"


class Robot {

//    static const int NUM_JOINTS = 4;
    enum{ NUM_JOINTS = 4};
public:
    Robot();
    Joint joints[NUM_JOINTS];
    void printInfo();
//    CanDevice** canDev[]l;

};


#endif //CAPSTONE_ROBOT_H