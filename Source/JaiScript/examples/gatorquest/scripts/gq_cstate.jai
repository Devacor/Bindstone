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
string TITLE_LAST = "";        // the last gator played (the title's default name)

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
int MUSIC_CUR = -99;           // the theme playing (-1 title, -2 death, else a world), -99 none

// ---- networking: the host side (one NetServer, clients by id)
shared_ptr<NetServer> NET_SRV = null;
int NET_PORT = 28777;
map<int, int> NET_SLOT_OF;     // client id -> player slot (after HELLO)
map<int, int> NET_CID_OF;      // player slot -> client id
array<int> NH_KIND;            // host inbox: 0 connect, 1 packet, 2 disconnect
array<int> NH_ID;
array<string> NH_TXT;
int NET_BYTES_OUT = 0;
int NET_SNAPS_OUT = 0;

// ---- delta snapshots: per player slot, the last record sent for each visible mobile and item
array<map<int, string>> SNAP_MOBS;
array<map<int, string>> SNAP_ITEMS;
array<int> SNAP_WORLD;
bool SNAP_DELTAS = true;

// ---- networking: client sessions (the game uses one; the net test drives several)
array<shared_ptr<NetChannel>> NCS_CH;
array<array<string>> NCS_INBOX;
array<shared_ptr<ClientView>> NCS_VIEW;
array<string> NCS_STATUS;      // connecting, hello, joined, refused: why, closed: why
array<int> NCS_SLOT;
array<int> NCS_SEED;
array<string> NCS_DROP;
int NET_SESSION = -1;          // the session the interactive client plays through
int NET_CLOSE_F = 0;           // frames left before a closed client session quits

// ---- the network simulator (off by default): added round trip, jitter and TCP-style loss
int NET_SIM_LAG = 0;           // round-trip ms added (half each way)
int NET_SIM_JITTER = 0;        // +- ms per message
int NET_SIM_LOSS = 0;          // percent of messages that need a retransmission (+250 ms, order kept)
int NET_SIM_SEED = 777;
array<array<int>> NCS_OUT_DUE; // per session: delayed outgoing messages
array<array<string>> NCS_OUT_MSG;
array<array<int>> NCS_IN_DUE;  // per session: delayed incoming messages
array<array<string>> NCS_IN_MSG;
array<int> NCS_OUT_LAST;       // TCP keeps order: a message never overtakes the one before it
array<int> NCS_IN_LAST;

// ---- client-side prediction of the local gator
bool PRED_ON = true;
int PRED_SEQ = 0;              // the last step sequence number sent
array<int> PEND_SEQ;           // steps sent but not yet processed by the host
array<int> PEND_DIR;
int PRED_W = -1;               // the predicted tile (-1 = not started)
int PRED_X = 0;
int PRED_Y = 0;
int PRED_FX = 0;               // the tile the current step started from
int PRED_FY = 0;
int PRED_T0 = 0;               // when the current step started (ms)
int PRED_DUR = 1;              // how long it takes (ms)
int PRED_READY = 0;            // when the next step may start (ms)
int PRED_DIR = 4;              // facing, updated on the key press
int PRED_LUNGE_T = -1000;      // a bump attack's lunge (ms)
float CORR_X = 0.0;            // visual correction offset (tiles), decays to zero
float CORR_Y = 0.0;
int PRED_CORRECTIONS = 0;      // measured: reconciliations that moved the gator
int PRED_SNAPS = 0;            // ... of which snapped (too far to glide)
float PRED_CORR_MAX = 0.0;     // the largest correction (tiles)
int PRED_STEPS = 0;
// the smoothed server clock (ticks); others are drawn INTERP_DELAY ticks behind it
float CLOCK_TICK = 0.0;
int CLOCK_MS = 0;
bool CLOCK_SET = false;
int CLOCK_SEEN = -1;
float INTERP_DELAY = 1.0;
int LAST_MOVE_SENT = -2;
