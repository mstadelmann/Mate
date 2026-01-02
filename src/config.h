#ifndef CONF_H
#define CONF_H

#include <string>

extern double pawnEvalWhite[8][8];
extern double pawnEvalBlack[8][8];
extern double knightEvalWhite[8][8];
extern double knightEvalBlack[8][8];
extern double bishopEvalWhite[8][8];
extern double bishopEvalBlack[8][8];
extern double rookEvalWhite[8][8];
extern double rookEvalBlack[8][8];
extern double evalQueenWhite[8][8];
extern double evalQueenBlack[8][8];
extern double kingEvalWhite[8][8];
extern double kingEvalBlack[8][8];

extern int pawnValue;
extern int rookValue;
extern int knightValue;
extern int bishopValue;
extern int queenValue;
extern int kingValue;

extern double position_gamma; // 0: no position factor, 1: full position factor

extern int earlyMattVal;
extern int finalMattVal;
extern int finalPattVal;

extern int maxValStart;
extern int minValStart;

extern int minMaxDepth;
extern bool use_AB_pruning;
extern bool enable_debug_messages;

extern std::string db_path;
extern int network_port; // TCP port for network games

// Load/save config.json next to the binary
bool load_config_from_json();
bool save_config_to_json();
void init_config_defaults();

#endif /* CONF_H */