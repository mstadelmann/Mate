#ifndef UTILS_H
#define UTILS_H

#include <stdio.h>
#include <iostream>
#include <tuple>
#include <vector>
#include <array>
#include <string>
#include <cstdlib>
#include <time.h>

using std::cout;
using std::endl;
using std::string;
using std::tuple;

// FUNCTION PROTOTYPES
void printLogo(void);
void MainMenu(void);
void printGameMenu(void);
void debugMessage(string message, int msg_level);

#endif /* UTILS_H */
