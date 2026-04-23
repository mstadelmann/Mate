#include "chess.h"

#include <iostream>

bool chess::mlMove()
{
    std::cout << "[ML] ML move support is disabled in this build." << std::endl;
    std::cout << "[ML] Rebuild with -DMATE_ENABLE_ONNX=ON and make ONNX Runtime visible to CMake." << std::endl;
    return false;
}
