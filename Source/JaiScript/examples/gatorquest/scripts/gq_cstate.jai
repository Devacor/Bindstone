// Gator Quest - client-side live state (camera, particles, floating text, UI
// modes). Like gq_state.jai this file is never re-run by hot reload.

import "gq_state.jai";

// ---- camera (world pixels)
float CAM_X = 0.0;
float CAM_Y = 0.0;
bool CAM_SNAP = true;
int SHAKE_F = 0;
int SHAKE_P = 0;
int FRAME = 0;
int NOW_MS = 0;
int PRNG = 12345;
int LIGHTNING_F = 0;
int SHADE_WORLD = -1;
int SHADE_GHOST = -1;
int VIEW_TX0 = 0;
int VIEW_TY0 = 0;
int TITLE_X = 0;
int TITLE_Y = 0;

// ---- particles (tile-unit positions)
array<float> P_X;
array<float> P_Y;
array<float> P_VX;
array<float> P_VY;
array<int> P_LIFE;
array<int> P_MAX;
array<int> P_COL;
array<int> P_FLAGS;

// ---- floating text
array<float> FT_X;
array<float> FT_Y;
array<string> FT_S;
array<int> FT_COL;
array<int> FT_T;
array<int> FT_SCALE;

// ---- projectiles
array<float> PR_X0;
array<float> PR_Y0;
array<float> PR_X1;
array<float> PR_Y1;
array<int> PR_T;
array<int> PR_DUR;
array<int> PR_KIND;

// ---- weather flakes
array<float> WX_X;
array<float> WX_Y;

// ---- per-mobile presentation
map<int, int> FLASH;          // serial -> frame the hit flash ends
map<int, float> VIS_X;        // serial -> last drawn position (for smooth motion)
map<int, float> VIS_Y;

// ---- banner
string BANNER = "";
int BANNER_F = 0;
int BANNER_COL = 0;
string BANNER_SUB = "";

// ---- UI
string UI_MODE = "play";       // play, pack, skills, journal, spells, craft, dialog, shop_buy, shop_sell, bank, chat, help, map, menu, worn
int UI_SEL = 0;
int UI_SCROLL = 0;
string CHAT_BUF = "";
string UI_TOAST = "";
int UI_TOAST_F = 0;
int TILE_ZOOM = 0;             // 0 auto, else forced tile size
int PANE_WIDTH = 34;
int SCREEN_COLS = 120;
int SCREEN_ROWS = 34;
bool MUSIC_ON = true;
bool SFX_ON = true;
int LAST_MOVE_SENT = -2;
