//
// Created by William Campbell on 2019-08-01.
//

#ifndef CANOPENBEAGLE_CANDEVICE_H
#define CANOPENBEAGLE_CANDEVICE_H
#include<iostream>


class CanDevice {
    int nodeID; // canNode address on the network
    std::string canFeastGet(std::string commandMessage, std::string(* canDevUpdate)(std::string));
    // std:: canDevUpate(std::string data); // implement for each specific can device needing a callback
    std::string jointUpdate(std::string data);

    // objectDictionary should be a hash between specific commands and the correct OD address
    //  std::string objectDictionary;
    // Using the OD hash function and other needed params for a specific command createCanMessage can be called
    // to assemble the correct canFeast message
    // std::string dataType; // i32 i16 i8 ...
    // std::string commands // read, write,
    // std
    //std::string createCanMessage(std::string specific_command);

public:
    // set and get Object dictionary paramaters
    // All dictionary commands
    std::string position = "[1] 2 read 0x6063 0 i32";// for testing purposes just a string of hard set commands set and get pos //display current knee position
    std::string setAbsPosition = "[1] 2 write 0x607A 0 i32 50000"; //move to this position (absolute)
    void setOD(std::string objectAddress);
    void getOD(std::string objectAddress);


};


#endif //CANOPENBEAGLE_CANDEVICE_H