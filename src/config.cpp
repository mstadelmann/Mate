#include "config.h"
#include <string>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

// bad placement gives punishments, good placement gives bonuses
// https://www.chessprogramming.org/Simplified_Evaluation_Function

// Top Left[0][0] is A1!
// FIELD_NAMES[8][8] = {
//     {A1, B1, C1, D1, E1, F1, G1, H1},
//     {A2, B2, C2, D2, E2, F2, G2, H2},
//     {A3, B3, C3, D3, E3, F3, G3, H3},
//     {A4, B4, C4, D4, E4, F4, G4, H4},
//     {A5, B5, C5, D5, E5, F5, G5, H5},
//     {A6, B6, C6, D6, E6, F6, G6, H6},
//     {A7, B7, C7, D7, E7, F7, G7, H7},
//     {A8, B8, C8, D8, E8, F8, G8, H8}};

double pawnEvalWhite[8][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {5, 10, 10, -20, -20, 10, 10, 5},
    {5, -5, -10, 0, 0, -10, -5, 5},
    {0, 0, 0, 20, 20, 0, 0, 0},
    {5, 5, 10, 25, 25, 10, 5, 5},
    {10, 10, 20, 30, 30, 20, 10, 10},
    {50, 50, 50, 50, 50, 50, 50, 50},
    {0, 0, 0, 0, 0, 0, 0, 0}};

// double pawnEvalWhite[8][8] = {
//     {0, 0, 0, 0, 0, 0, 0, 0},
//     {0.5, 1, 1, -2, -2, 1, 1, 0.5},
//     {0.5, -0.5, -1, 0, 0, -1, -0.5, 0.5},
//     {0, 0, 0, 2, 2, 0, 0, 0},
//     {0.5, 0.5, 1, 2.5, 2.5, 1, 0.5, 0.5},
//     {1, 1, 2, 3, 3, 2, 1, 1},
//     {5, 5, 5, 5, 5, 5, 5, 5},
//     {0, 0, 0, 0, 0, 0, 0, 0}};

double pawnEvalBlack[8][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {50, 50, 50, 50, 50, 50, 50, 50},
    {10, 10, 20, 30, 30, 20, 10, 10},
    {5, 5, 10, 25, 25, 10, 5, 5},
    {0, 0, 0, 20, 20, 0, 0, 0},
    {5, -5, -10, 0, 0, -10, -5, 5},
    {5, 10, 10, -20, -20, 10, 10, 5},
    {0, 0, 0, 0, 0, 0, 0, 0}};

// double pawnEvalBlack[8][8] = {
//     {0, 0, 0, 0, 0, 0, 0, 0},
//     {5, 5, 5, 5, 5, 5, 5, 5},
//     {1, 1, 2, 3, 3, 2, 1, 1},
//     {0.5, 0.5, 1, 2.5, 2.5, 1, 0.5, 0.5},
//     {0, 0, 0, 2, 2, 0, 0, 0},
//     {0.5, -0.5, -1, 0, 0, -1, -0.5, 0.5},
//     {0.5, 1, 1, -2, -2, 1, 1, 0.5},
//     {0, 0, 0, 0, 0, 0, 0, 0}};

double knightEvalWhite[8][8] = {
    {-50, -40, -30, -30, -30, -30, -40, -50},
    {-40, -20, 0, 5, 5, 0, -20, -40},
    {-30, 5, 10, 15, 15, 10, 5, -30},
    {-30, 0, 15, 20, 20, 15, 0, -30},
    {-30, 5, 15, 20, 20, 15, 5, -30},
    {-30, 0, 10, 15, 15, 10, 0, -30},
    {-40, -20, 0, 0, 0, 0, -20, -40},
    {-50, -40, -30, -30, -30, -30, -40, -50}};

double knightEvalBlack[8][8] = {
    {-50, -40, -30, -30, -30, -30, -40, -50},
    {-40, -20, 0, 0, 0, 0, -20, -40},
    {-30, 0, 10, 15, 15, 10, 0, -30},
    {-30, 5, 15, 20, 20, 15, 5, -30},
    {-30, 0, 15, 20, 20, 15, 0, -30},
    {-30, 5, 10, 15, 15, 10, 5, -30},
    {-40, -20, 0, 5, 5, 0, -20, -40},
    {-50, -40, -30, -30, -30, -30, -40, -50}};

// double knightEval[8][8] = {
//     {-5, -4, -3, -3, -3, -3, -4, -5},
//     {-4, -2, 0, 0, 0, 0, -2, -4},
//     {-3, 0, 1, 1.5, 1.5, 1, 0, -3},
//     {-3, 0.5, 1.5, 2, 2, 1.5, 0.5, -3},
//     {-3, 0, 1.5, 2, 2, 1.5, 0, -3},
//     {-3, 0.5, 1, 1.5, 1.5, 1, 0.5, -3},
//     {-4, -2, 0, 0.5, 0.5, 0, -2, -4},
//     {-5, -4, -3, -3, -3, -3, -4, -5}};

double bishopEvalWhite[8][8] = {
    {-20, -10, -10, -10, -10, -10, -10, -20},
    {-10, 5, 0, 0, 0, 0, 5, -10},
    {-10, 10, 10, 10, 10, 10, 10, -10},
    {-10, 0, 10, 10, 10, 10, 0, -10},
    {-10, 5, 5, 10, 10, 5, 5, -10},
    {-10, 0, 5, 10, 10, 5, 0, -10},
    {-10, 0, 0, 0, 0, 0, 0, -10},
    {-20, -10, -10, -10, -10, -10, -10, -20}};

// double bishopEvalWhite[8][8] = {
//     {-2, -1, -1, -1, -1, -1, -1, -2},
//     {-1, 0.5, 0, 0, 0, 0, 0.5, -1},
//     {-1, 1, 1, 1, 1, 1, 1, -1},
//     {-1, 0, 1, 1, 1, 1, 0, -1},
//     {-1, 0.5, 0.5, 1, 1, 0.5, 0.5, -1},
//     {-1, 0, 0.5, 1, 1, 0.5, 0, -1},
//     {-1, 0, 0, 0, 0, 0, 0, -1},
//     {-2, -1, -1, -1, -1, -1, -1, -2}};

double bishopEvalBlack[8][8] = {
    {-20, -10, -10, -10, -10, -10, -10, -20},
    {-10, 0, 0, 0, 0, 0, 0, -10},
    {-10, 0, 5, 10, 10, 5, 0, -10},
    {-10, 5, 5, 10, 10, 5, 5, -10},
    {-10, 0, 10, 10, 10, 10, 0, -10},
    {-10, 10, 10, 10, 10, 10, 10, -10},
    {-10, 5, 0, 0, 0, 0, 5, -10},
    {-20, -10, -10, -10, -10, -10, -10, -20}};

// double bishopEvalBlack[8][8] = {
//     {-2, -1, -1, -1, -1, -1, -1, -2},
//     {-1, 0, 0, 0, 0, 0, 0, -1},
//     {-1, 0, 0.5, 1, 1, 0.5, 0, -1},
//     {-1, 0.5, 0.5, 1, 1, 0.5, 0.5, -1},
//     {-1, 0, 1, 1, 1, 1, 0, -1},
//     {-1, 1, 1, 1, 1, 1, 1, -1},
//     {-1, 0.5, 0, 0, 0, 0, 0.5, -1},
//     {-2, -1, -1, -1, -1, -1, -1, -2}};

double rookEvalWhite[8][8] = {
    {0, 0, 0, 5, 5, 0, 0, 0},
    {-5, 0, 0, 0, 0, 0, 0, -5},
    {-5, 0, 0, 0, 0, 0, 0, -5},
    {-5, 0, 0, 0, 0, 0, 0, -5},
    {-5, 0, 0, 0, 0, 0, 0, -5},
    {-5, 0, 0, 0, 0, 0, 0, -5},
    {5, 10, 10, 10, 10, 10, 10, 5},
    {0, 0, 0, 0, 0, 0, 0, 0}};

// double rookEvalWhite[8][8] = {
//     {0, 0, 0, 0.5, 0.5, 0, 0, 0},
//     {-0.5, 0, 0, 0, 0, 0, 0, -0.5},
//     {-0.5, 0, 0, 0, 0, 0, 0, -0.5},
//     {-0.5, 0, 0, 0, 0, 0, 0, -0.5},
//     {-0.5, 0, 0, 0, 0, 0, 0, -0.5},
//     {-0.5, 0, 0, 0, 0, 0, 0, -0.5},
//     {0.5, 1, 1, 1, 1, 1, 1, 0.5},
//     {0, 0, 0, 0, 0, 0, 0, 0}};

double rookEvalBlack[8][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {5, 10, 10, 10, 10, 10, 10, 5},
    {-5, 0, 0, 0, 0, 0, 0, -5},
    {-5, 0, 0, 0, 0, 0, 0, -5},
    {-5, 0, 0, 0, 0, 0, 0, -5},
    {-5, 0, 0, 0, 0, 0, 0, -5},
    {-5, 0, 0, 0, 0, 0, 0, -5},
    {0, 0, 0, 5, 5, 0, 0, 0}};

// double rookEvalBlack[8][8] = {
//     {0, 0, 0, 0, 0, 0, 0, 0},
//     {0.5, 1, 1, 1, 1, 1, 1, 0.5},
//     {-0.5, 0, 0, 0, 0, 0, 0, -0.5},
//     {-0.5, 0, 0, 0, 0, 0, 0, -0.5},
//     {-0.5, 0, 0, 0, 0, 0, 0, -0.5},
//     {-0.5, 0, 0, 0, 0, 0, 0, -0.5},
//     {-0.5, 0, 0, 0, 0, 0, 0, -0.5},
//     {0, 0, 0, 0.5, 0.5, 0, 0, 0}};

double evalQueenWhite[8][8] = {
    {-20, -10, -10, -5, -5, -10, -10, -20},
    {-10, 0, 5, 0, 0, 0, 0, -10},
    {-10, 5, 5, 5, 5, 5, 0, -10},
    {0, 0, 5, 5, 5, 5, 0, -5},
    {-5, 0, 5, 5, 5, 5, 0, -5},
    {-10, 0, 5, 5, 5, 5, 0, -10},
    {-10, 0, 0, 0, 0, 0, 0, -10},
    {-20, -10, -10, -5, -5, -10, -10, -20}};

// double evalQueenWhite[8][8] = {
//     {-2, -1, -1, -0.5, -0.5, -1, -1, -2},
//     {-1, 0, 0, 0, 0, 0, 0, -1},
//     {-1, 0, 0.5, 0.5, 0.5, 0.5, 0, -1},
//     {-0.5, 0, 0.5, 0.5, 0.5, 0.5, 0, -0.5},
//     {0, 0, 0.5, 0.5, 0.5, 0.5, 0, -0.5},
//     {-1, 0.5, 0.5, 0.5, 0.5, 0.5, 0, -1},
//     {-1, 0, 0.5, 0, 0, 0, 0, -1},
//     {-2, -1, -1, -0.5, -0.5, -1, -1, -2}};

double evalQueenBlack[8][8] = {
    {-20, -10, -10, -5, -5, -10, -10, -20},
    {-10, 0, 0, 0, 0, 0, 0, -10},
    {-10, 0, 5, 5, 5, 5, 0, -10},
    {-5, 0, 5, 5, 5, 5, 0, -5},
    {0, 0, 5, 5, 5, 5, 0, -5},
    {-10, 5, 5, 5, 5, 5, 0, -10},
    {-10, 0, 5, 0, 0, 0, 0, -10},
    {-20, -10, -10, -5, -5, -10, -10, -20}};

// // king middle game
// -30,-40,-40,-50,-50,-40,-40,-30,
// -30,-40,-40,-50,-50,-40,-40,-30,
// -30,-40,-40,-50,-50,-40,-40,-30,
// -30,-40,-40,-50,-50,-40,-40,-30,
// -20,-30,-30,-40,-40,-30,-30,-20,
// -10,-20,-20,-20,-20,-20,-20,-10,
//  20, 20,  0,  0,  0,  0, 20, 20,
//  20, 30, 10,  0,  0, 10, 30, 20

double kingEvalWhite[8][8] = {
    {20, 30, 10, 0, 0, 10, 30, 20},
    {20, 20, 0, 0, 0, 0, 20, 20},
    {-10, -20, -20, -20, -20, -20, -20, -10},
    {-20, -30, -30, -40, -40, -30, -30, -20},
    {-30, -40, -40, -50, -50, -40, -40, -30},
    {-30, -40, -40, -50, -50, -40, -40, -30},
    {-30, -40, -40, -50, -50, -40, -40, -30},
    {-30, -40, -40, -50, -50, -40, -40, -30}};

// double kingEvalWhite[8][8] = {
//     {2, 3, 1, 0, 0, 1, 3, 2},
//     {2, 2, 0, 0, 0, 0, 2, 2},
//     {-1, -2, -2, -2, -2, -2, -2, -1},
//     {-2, -3, -3, -4, -4, -3, -3, -2},
//     {-3, -4, -4, -5, -5, -4, -4, -3},
//     {-3, -4, -4, -5, -5, -4, -4, -3},
//     {-3, -4, -4, -5, -5, -4, -4, -3},
//     {-3, -4, -4, -5, -5, -4, -4, -3}};

double kingEvalBlack[8][8] = {
    {-30, -40, -40, -50, -50, -40, -40, -30},
    {-30, -40, -40, -50, -50, -40, -40, -30},
    {-30, -40, -40, -50, -50, -40, -40, -30},
    {-30, -40, -40, -50, -50, -40, -40, -30},
    {-20, -30, -30, -40, -40, -30, -30, -20},
    {-10, -20, -20, -20, -20, -20, -20, -10},
    {20, 20, 0, 0, 0, 0, 20, 20},
    {20, 30, 10, 0, 0, 10, 30, 20}};

// double kingEvalBlack[8][8] = {
//     {-3, -4, -4, -5, -5, -4, -4, -3},
//     {-3, -4, -4, -5, -5, -4, -4, -3},
//     {-3, -4, -4, -5, -5, -4, -4, -3},
//     {-3, -4, -4, -5, -5, -4, -4, -3},
//     {-2, -3, -3, -4, -4, -3, -3, -2},
//     {-1, -2, -2, -2, -2, -2, -2, -1},
//     {2, 2, 0, 0, 0, 0, 2, 2},
//     {2, 3, 1, 0, 0, 1, 3, 2}};

//  // king end game
// -50,-40,-30,-20,-20,-30,-40,-50,
// -30,-20,-10,  0,  0,-10,-20,-30,
// -30,-10, 20, 30, 30, 20,-10,-30,
// -30,-10, 30, 40, 40, 30,-10,-30,
// -30,-10, 30, 40, 40, 30,-10,-30,
// -30,-10, 20, 30, 30, 20,-10,-30,
// -30,-30,  0,  0,  0,  0,-30,-30,
// -50,-30,-30,-30,-30,-30,-30,-50

int pawnValue = 10;
int rookValue = 50;
int knightValue = 30;
int bishopValue = 30;
int queenValue = 90;
int kingValue = 900;
double position_gamma = 1.0;
int earlyMattVal = 30000;
int finalMattVal = 20000;
int finalPattVal = 10000;
int maxValStart = -100000;
int minValStart = 100000;
int minMaxDepth = 3;
bool use_AB_pruning = true;
bool enable_debug_messages = true;

static std::string getBinaryDir()
{
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0)
        return std::string(".");
    buf[len] = '\0';
    std::string path(buf);
    auto pos = path.find_last_of('/');
    if (pos == std::string::npos)
        return std::string(".");
    return path.substr(0, pos);
}

static bool fileExists(const std::string &path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

void init_config_defaults()
{
    // Defaults already set above
}

bool save_config_to_json()
{
    std::string path = getBinaryDir() + "/config.json";
    std::ofstream out(path);
    if (!out)
        return false;
    out << "{\n";
    out << "  \"pawnValue\": " << pawnValue << ",\n";
    out << "  \"rookValue\": " << rookValue << ",\n";
    out << "  \"knightValue\": " << knightValue << ",\n";
    out << "  \"bishopValue\": " << bishopValue << ",\n";
    out << "  \"queenValue\": " << queenValue << ",\n";
    out << "  \"kingValue\": " << kingValue << ",\n";
    out << "  \"position_gamma\": " << position_gamma << ",\n";
    out << "  \"earlyMattVal\": " << earlyMattVal << ",\n";
    out << "  \"finalMattVal\": " << finalMattVal << ",\n";
    out << "  \"finalPattVal\": " << finalPattVal << ",\n";
    out << "  \"maxValStart\": " << maxValStart << ",\n";
    out << "  \"minValStart\": " << minValStart << ",\n";
    out << "  \"minMaxDepth\": " << minMaxDepth << ",\n";
    out << "  \"use_AB_pruning\": " << (use_AB_pruning ? "true" : "false") << ",\n";
    out << "  \"enable_debug_messages\": " << (enable_debug_messages ? "true" : "false") << ",\n";
    auto writeArray = [&out](const char *name, double a[8][8])
    {
        out << "  \"" << name << "\": [\n";
        for (int r = 0; r < 8; ++r)
        {
            out << "    [";
            for (int c = 0; c < 8; ++c)
            {
                out << a[r][c];
                if (c < 7)
                    out << ", ";
            }
            out << "]" << (r < 7 ? ",\n" : "\n");
        }
        out << "  ]";
    };
    writeArray("pawnEvalWhite", pawnEvalWhite);
    out << ",\n";
    writeArray("pawnEvalBlack", pawnEvalBlack);
    out << ",\n";
    writeArray("knightEvalWhite", knightEvalWhite);
    out << ",\n";
    writeArray("knightEvalBlack", knightEvalBlack);
    out << ",\n";
    writeArray("bishopEvalWhite", bishopEvalWhite);
    out << ",\n";
    writeArray("bishopEvalBlack", bishopEvalBlack);
    out << ",\n";
    writeArray("rookEvalWhite", rookEvalWhite);
    out << ",\n";
    writeArray("rookEvalBlack", rookEvalBlack);
    out << ",\n";
    writeArray("evalQueenWhite", evalQueenWhite);
    out << ",\n";
    writeArray("evalQueenBlack", evalQueenBlack);
    out << ",\n";
    writeArray("kingEvalWhite", kingEvalWhite);
    out << ",\n";
    writeArray("kingEvalBlack", kingEvalBlack);
    out << "\n";
    out << "}\n";
    return true;
}

static bool parseArray(const std::string &content, const char *name, double a[8][8])
{
    std::string key = std::string("\"") + name + "\"";
    auto pos = content.find(key);
    if (pos == std::string::npos)
        return false;
    pos = content.find('[', pos);
    if (pos == std::string::npos)
        return false;
    int r = 0, c = 0;
    for (size_t i = pos; i < content.size() && r < 8; ++i)
    {
        if (content[i] == '[')
        {
            c = 0;
        }
        else if ((content[i] >= '0' && content[i] <= '9') || content[i] == '-' || content[i] == '.')
        {
            char *endp = nullptr;
            double val = strtod(&content[i], &endp);
            a[r][c++] = val;
            i = endp - &content[0] - 1;
        }
        else if (content[i] == ']')
        {
            if (c == 8)
                r++;
        }
    }
    return r == 8;
}

bool load_config_from_json()
{
    std::string path = getBinaryDir() + "/config.json";
    if (!fileExists(path))
        return false;
    std::ifstream in(path);
    if (!in)
        return false;
    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();
    auto findNum = [&](const char *key, double &out)
    {
        std::string k = std::string("\"") + key + "\"";
        auto p = content.find(k);
        if (p == std::string::npos)
            return false;
        p = content.find(':', p);
        if (p == std::string::npos)
            return false;
        char *endp = nullptr;
        out = strtod(content.c_str() + p + 1, &endp);
        return true;
    };
    auto findInt = [&](const char *key, int &out)
    {
        double d;
        if (!findNum(key, d))
            return false;
        out = (int)d;
        return true;
    };
    auto findBool = [&](const char *key, bool &out)
    {
        std::string k = std::string("\"") + key + "\"";
        auto p = content.find(k);
        if (p == std::string::npos)
            return false;
        p = content.find(':', p);
        if (p == std::string::npos)
            return false;
        auto valStart = content.find_first_not_of(" \t\n", p + 1);
        if (valStart == std::string::npos)
            return false;
        if (content.compare(valStart, 4, "true") == 0)
        {
            out = true;
            return true;
        }
        else if (content.compare(valStart, 5, "false") == 0)
        {
            out = false;
            return true;
        }
        return false;
    };
    findInt("pawnValue", pawnValue);
    findInt("rookValue", rookValue);
    findInt("knightValue", knightValue);
    findInt("bishopValue", bishopValue);
    findInt("queenValue", queenValue);
    findInt("kingValue", kingValue);
    findNum("position_gamma", position_gamma);
    findInt("earlyMattVal", earlyMattVal);
    findInt("finalMattVal", finalMattVal);
    findInt("finalPattVal", finalPattVal);
    findInt("maxValStart", maxValStart);
    findInt("minValStart", minValStart);
    findInt("minMaxDepth", minMaxDepth);
    findBool("use_AB_pruning", use_AB_pruning);
    findBool("enable_debug_messages", enable_debug_messages);

    parseArray(content, "pawnEvalWhite", pawnEvalWhite);
    parseArray(content, "pawnEvalBlack", pawnEvalBlack);
    parseArray(content, "knightEvalWhite", knightEvalWhite);
    parseArray(content, "knightEvalBlack", knightEvalBlack);
    parseArray(content, "bishopEvalWhite", bishopEvalWhite);
    parseArray(content, "bishopEvalBlack", bishopEvalBlack);
    parseArray(content, "rookEvalWhite", rookEvalWhite);
    parseArray(content, "rookEvalBlack", rookEvalBlack);
    parseArray(content, "evalQueenWhite", evalQueenWhite);
    parseArray(content, "evalQueenBlack", evalQueenBlack);
    parseArray(content, "kingEvalWhite", kingEvalWhite);
    parseArray(content, "kingEvalBlack", kingEvalBlack);
    return true;
}
