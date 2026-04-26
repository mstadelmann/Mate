#include "gui.h"

#include <memory>
#include <string>

std::unique_ptr<ChessGui> create_chess_gui(std::string &error_message)
{
    error_message = "GUI support is unavailable in this build. Install SDL2, FreeType, and Fontconfig development files and rebuild Mate.";
    return nullptr;
}
