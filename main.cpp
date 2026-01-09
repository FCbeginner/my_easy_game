//my_roguelike.cpp
//a simple roguelike demo using rlutil
//chang from a c program

#include "rlutil.h"
#include <stdlib.h> // for srand() / rand()
#include <stdio.h>
#include "math.h"
#include <chrono>
#include <ctime>
#include <queue>
#include <vector>
#include <algorithm>
#include <deque>
#include <string>
#include <cstdarg>

using namespace rlutil;

#ifndef min
#define min(a,b) (((a)<(b))?(a):(b))
#endif // min

/// Tiles
#define FLOOR 0
#define WALL 1
#define COIN (1 << 1)
#define STAIRS_DOWN (1 << 2)
#define TORCH (1 << 4)
#define POTION (1 << 5)
#define SWORD_ITEM (1 << 6)

#define MAPSIZE 15

/// Globals
int x, y;
int coins = 0, moves = 0, torch = 30, level = 1;
int lvl[MAPSIZE][MAPSIZE];

// Combat & items
int potions = 0;           // current potion count
int potions_used = 0;      // for summary
int swordDamage = 2;      // base attack
int max_hp = 20;
int hp = 20;
int kills = 0; // total enemies defeated
bool player_defending = false;

// End game reason
std::string game_end_reason = "";

struct Enemy {
	int x, y;
	int hp;
	int max_hp;
	int damage;
	bool active;
	bool defending;
	bool alive;
	// predefined drops shown in HUD and applied on death
	int coins_drop;
	int potions_drop;
	int torch_drop;
	int hp_drop;
};

std::vector<Enemy> enemies;

// Helper forward declarations
int enemy_at(int px, int py); // returns index in enemies or -1
int adjacent_enemy_index();
void process_enemies_turn();
void drop_loot(int enemy_index);

// Message log (newest on top), limited to 14 entries
std::deque<std::string> msglog;
void push_msg(const char *fmt, ...) {
	char buf[128];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	msglog.push_front(std::string(buf));
	if (msglog.size() > 14) msglog.pop_back();
}

// Helper: check whether a tile is walkable (not a wall)
bool is_walkable(int px, int py) {
	if (px < 0 || py < 0 || px >= MAPSIZE || py >= MAPSIZE) return false;
	return !(lvl[px][py] & WALL);
}

// Remove simple dead-ends by opening adjacent walls
void remove_dead_ends() {
	bool changed = true;
	int iter = 0;
	while (changed && iter < 1000) {
		changed = false;
		iter++;
		for (int j = 1; j < MAPSIZE-1; j++) {
			for (int i = 1; i < MAPSIZE-1; i++) {
				if (!is_walkable(i, j)) continue;
				int walls = 0;
				if (!is_walkable(i+1, j)) walls++;
				if (!is_walkable(i-1, j)) walls++;
				if (!is_walkable(i, j+1)) walls++;
				if (!is_walkable(i, j-1)) walls++;
				if (walls >= 3) {
					// open one adjacent wall (try random order)
					int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
					for (int d = 0; d < 4; d++) {
						int r = rand() % 4;
						int tx = i + dirs[r][0];
						int ty = j + dirs[r][1];
						if (tx > 0 && tx < MAPSIZE-1 && ty > 0 && ty < MAPSIZE-1 && !is_walkable(tx, ty)) {
							lvl[tx][ty] = 0; // carve to floor
							changed = true;
							break;
						}
					}
				}
			}
		}
	}
}

// Return index of enemy at position or -1
int enemy_at(int px, int py) {
	for (size_t i = 0; i < enemies.size(); i++) {
		if (enemies[i].alive && enemies[i].x == px && enemies[i].y == py) return (int)i;
	}
	return -1;
}

// Return index of an enemy adjacent to player (-1 if none)
int adjacent_enemy_index() {
	int dirs[4][2] = {{1,0},{-1,0},{0,1},{0,-1}};
	for (int d = 0; d < 4; d++) {
		int tx = x + dirs[d][0];
		int ty = y + dirs[d][1];
		int ei = enemy_at(tx, ty);
		if (ei != -1) return ei;
	}
	return -1;
}

// Loot drop on enemy death (use enemy's predefined drops)
void drop_loot(int enemy_index) {
	if (enemy_index < 0 || enemy_index >= (int)enemies.size()) return;
	Enemy &e = enemies[enemy_index];
	if (e.coins_drop > 0) { coins += e.coins_drop; push_msg("You gain %d coins.", e.coins_drop); }
	if (e.potions_drop > 0) { potions += e.potions_drop; push_msg("You gain %d potion(s).", e.potions_drop); }
	if (e.torch_drop > 0) { torch += e.torch_drop; push_msg("You gain %d torch(es).", e.torch_drop); }
	if (e.hp_drop > 0) { hp += e.hp_drop; if (hp > max_hp) hp = max_hp; push_msg("You recovered %d HP.", e.hp_drop); }
	// track kills and increase max HP every 10 kills
	kills++;
	if (kills % 10 == 0) {
		max_hp += 1;
		push_msg("Max HP increased to %d!", max_hp);
	}
}

// Process all enemies' turns (after player acts)
void process_enemies_turn() {
	int activation_distance = 4 + level/2; // when player gets closer, enemies become active
	for (size_t i = 0; i < enemies.size(); i++) {
		Enemy &e = enemies[i];
		if (!e.alive) continue;
		int dist = abs(e.x - x) + abs(e.y - y);
		if (!e.active && dist <= activation_distance) { e.active = true; push_msg("An enemy notices you!"); }
			if (!e.active) continue;
			// reset defending flag from previous turn
			e.defending = false;
			// If adjacent to player -> attack or defend
			if (dist == 1) {
				int act = rand() % 100;
				if (act < 70) {
					// attack
					int raw = e.damage + rand() % (e.damage + 1);
					int reduction = 0;
					if (player_defending) reduction = rand() % (swordDamage + 1);
					int dmg = raw - reduction;
					if (dmg < 0) dmg = 0;
					hp -= dmg;
					if (reduction > 0) push_msg("Your defense reduced damage by %d.", reduction);
					push_msg("Enemy hits you for %d damage.", dmg);
				} else {
					// defend this turn (reduces next player's damage)
					e.defending = true;
					push_msg("Enemy defends.");
				}
			} else {
			// move towards player one tile (try x then y)
			int dx = (x > e.x) ? 1 : (x < e.x ? -1 : 0);
			int dy = (y > e.y) ? 1 : (y < e.y ? -1 : 0);
			int nx = e.x + dx, ny = e.y;
			if (dx != 0 && nx > 0 && nx < MAPSIZE-1 && ny > 0 && ny < MAPSIZE-1 && is_walkable(nx, ny) && enemy_at(nx, ny) == -1 && !(nx==x && ny==y)) {
				e.x = nx; e.y = ny;
			} else {
				int nx2 = e.x, ny2 = e.y + dy;
				if (dy != 0 && nx2 > 0 && nx2 < MAPSIZE-1 && ny2 > 0 && ny2 < MAPSIZE-1 && is_walkable(nx2, ny2) && enemy_at(nx2, ny2) == -1 && !(nx2==x && ny2==y)) {
					e.x = nx2; e.y = ny2;
				}
			}
		}
	}
	// clear player's defending after enemies acted
	player_defending = false;
}

/// Generates the dungeon map
void gen(int seed) {
	// Seed RNG using high-resolution time combined with provided seed
	unsigned int s = (unsigned int)(std::chrono::high_resolution_clock::now().time_since_epoch().count() ^ (unsigned long long)seed);
	srand(s);
	// Message: entering level
	push_msg("Entering level %d.", seed);

	int i, j;
	// Initialize map: outer walls and random interior walls
	for (j = 0; j < MAPSIZE; j++) {
		for (i = 0; i < MAPSIZE; i++) {
			if (i == 0 || i == MAPSIZE-1 || j == 0 || j == MAPSIZE-1) lvl[i][j] = WALL;
			else lvl[i][j] = (rand() % 10 == 0) ? WALL : 0;
		}
	}

	// Scatter coins, torches, potions and swords on empty floor tiles (no overlap with walls/items yet)
	for (int tries = 0; tries < MAPSIZE*MAPSIZE; tries++) {
		int rx = 1 + rand() % (MAPSIZE-2);
		int ry = 1 + rand() % (MAPSIZE-2);
		if (lvl[rx][ry] == 0) {
			int r = rand() % 100;
			if (r < 5) lvl[rx][ry] = COIN;            // ~5%
			else if (r < 8) lvl[rx][ry] = TORCH;     // ~3%
			else if (r < 10) lvl[rx][ry] = POTION;   // ~2% (reduced)
			else if (r < 12) lvl[rx][ry] = SWORD_ITEM; // ~2% (reduced)
		}
	}

	// Choose player start on a non-wall tile (carve if unlucky)
	int tries = 0;
	do {
		x = 1 + rand() % (MAPSIZE-2);
		y = 1 + rand() % (MAPSIZE-2);
		tries++;
	} while ((lvl[x][y] & WALL) && tries < 1000);
	if (lvl[x][y] & WALL) lvl[x][y] = 0;

	// Choose stairs on a non-wall tile and not overlapping start
	int sx, sy;
	tries = 0;
	do {
		sx = 1 + rand() % (MAPSIZE-2);
		sy = 1 + rand() % (MAPSIZE-2);
		tries++;
	} while (((lvl[sx][sy] & WALL) || (sx == x && sy == y)) && tries < 1000);
	if (lvl[sx][sy] & WALL) lvl[sx][sy] = 0;
	// Note: do NOT set STAIRS_DOWN yet; carving may overwrite and we'll set it after cleanup

	// Ensure connectivity between player and stairs by carving a simple Manhattan path
	int cx = x, cy = y;
	while (cx != sx) {
		if (sx > cx) cx++; else cx--;
		lvl[cx][cy] = 0;
	}
	while (cy != sy) {
		if (sy > cy) cy++; else cy--;
		lvl[cx][cy] = 0;
	}

	// Remove simple dead-ends to reduce isolated corridors
	remove_dead_ends();

	// Ensure items do not overlap with start or walls
	for (j = 1; j < MAPSIZE-1; j++) {
		for (i = 1; i < MAPSIZE-1; i++) {
			if (lvl[i][j] & (COIN | TORCH | POTION | SWORD_ITEM)) {
				if ((lvl[i][j] & WALL) || (i == x && j == y)) {
					lvl[i][j] &= ~(COIN | TORCH | POTION | SWORD_ITEM);
				}
			}
		}
	}

	// Place stairs after carving/dead-end removal and ensure no overlap
	// Clear any item that might overlap the chosen stairs tile, force it to floor, then set the stairs flag
	lvl[sx][sy] &= ~(COIN | TORCH | POTION | SWORD_ITEM);
	if (lvl[sx][sy] & WALL) lvl[sx][sy] = 0;
	lvl[sx][sy] |= STAIRS_DOWN;

	// Spawn enemies for this level
	enemies.clear();
	int enemy_count = rand() % (1 + level); // may be zero
	for (int e = 0; e < enemy_count; e++) {
		int ex = 0, ey = 0, etries = 0;
		do {
			ex = 1 + rand() % (MAPSIZE-2);
			ey = 1 + rand() % (MAPSIZE-2);
			etries++;
		} while ((lvl[ex][ey] != 0) || (ex == x && ey == y) || (ex == sx && ey == sy) || (enemy_at(ex, ey) != -1 && etries < 200));
		if (etries >= 200) continue;
		Enemy ne;
		ne.x = ex; ne.y = ey;
		// scale enemy HP/damage with level and add variability
		ne.hp = 2 + rand() % (3 + level);
		ne.max_hp = ne.hp;
		ne.damage = 1 + rand() % (1 + (level/2));
		ne.active = false; ne.defending = false; ne.alive = true;
		// Precompute drops to show in HUD and give on death
		ne.coins_drop = 1 + rand() % (1 + level/2 + 1); // 1..(1+level/2+1)
		ne.potions_drop = (rand() % 10 == 0) ? 1 : 0; // ~10% chance
		ne.torch_drop = rand() % (1 + level/2 + 2);
		ne.hp_drop = 1 + rand() % (1 + level/2);
		enemies.push_back(ne);
	}
}

/// Draws the screen
void draw() {
	cls();
	locate(1, 1);
	int i, j;
	for (j = 0; j < MAPSIZE; j++) {
		for (i = 0; i < MAPSIZE; i++) {
			int ei = enemy_at(i, j);
			if (0); //(i == x && j == y) printf("@");
			else if (abs(x-i)+abs(y-j)>min(10,torch/2)) printf(" ");
			else if (ei != -1 && enemies[ei].alive) { setColor(RED); printf("E"); }
			else if (lvl[i][j] == 0) { setColor(BLUE); printf("."); }
			else if (lvl[i][j] & WALL) { setColor(CYAN); printf("#"); }
			else if (lvl[i][j] & COIN) { setColor(YELLOW); printf("o"); }
			else if (lvl[i][j] & STAIRS_DOWN) { setColor(GREEN); printf("<"); }
			else if (lvl[i][j] & TORCH) { setColor(LIGHTRED); printf("f"); }
			else if (lvl[i][j] & POTION) { setColor(MAGENTA); printf("P"); }
			else if (lvl[i][j] & SWORD_ITEM) { setColor(LIGHTCYAN); printf("S"); }
		}
		printf("\n");
	}
	locate(x+1, y+1);
	setColor(WHITE);
	printf("@");
	fflush(stdout);

	// HUD below the map
	locate(1, MAPSIZE + 2);
	setColor(LIGHTMAGENTA);
	printf("Level: %d\n", level);
	setColor(CYAN);
	printf("%-20s %20s\n", "me", "Enemies");
	int ae = adjacent_enemy_index();
	char lbuf[64], rbuf[64];
	// HP
	sprintf(lbuf, "HP: %d/%d", hp, max_hp);
	if (ae != -1) sprintf(rbuf, "HP: %d/%d", enemies[ae].hp, enemies[ae].max_hp); else sprintf(rbuf, "HP: -/-");
	setColor(GREEN);
	printf("%-20s %20s\n", lbuf, rbuf);
	// Sword
	sprintf(lbuf, "Sword: %d", swordDamage);
	if (ae != -1) sprintf(rbuf, "Sword: %d", enemies[ae].damage); else sprintf(rbuf, "Sword: -");
	setColor(LIGHTCYAN);
	printf("%-20s %20s\n", lbuf, rbuf);
	// Moves
	sprintf(lbuf, "Moves: %d", moves);
	sprintf(rbuf, "Moves: -");
	setColor(GREY);
	printf("%-20s %20s\n", lbuf, rbuf);
	// Coins
	sprintf(lbuf, "Coins: %d", coins);
	if (ae != -1) sprintf(rbuf, "Coins: %d", enemies[ae].coins_drop); else sprintf(rbuf, "Coins: 0");
	setColor(YELLOW);	
	printf("%-20s %20s\n", lbuf, rbuf);
	// Torch
	sprintf(lbuf, "Torch: %d", torch);
	if (ae != -1) sprintf(rbuf, "Torch: %d", enemies[ae].torch_drop); else sprintf(rbuf, "Torch: 0");
	setColor(LIGHTRED);
	printf("%-20s %20s\n", lbuf, rbuf);
	// Potions
	sprintf(lbuf, "Potions: %d", potions);
	if (ae != -1) sprintf(rbuf, "Potions: %d", enemies[ae].potions_drop); else sprintf(rbuf, "Potions: 0");
	setColor(MAGENTA);	
	printf("%-20s %20s\n", lbuf, rbuf);
	// Kills
	sprintf(lbuf, "Kills: %d", kills);
	sprintf(rbuf, "");
	setColor(BLUE);
	printf("%-20s %20s\n", lbuf, rbuf);
	setColor(WHITE);

	// Message log (max 14 lines), newest messages on top
	locate(MAPSIZE + 5, 1);
	setColor(GREY);
	printf("~~~Message Log:~~~");
	for (size_t m = 0; m < 14; m++) {
		locate(MAPSIZE + 5, 2 + (int)m);
		if (m < msglog.size()) {
			//setColor(WHITE);
			printf("%-80s", msglog[m].c_str());
		} else {
			printf("%-80s", "");
		}
		printf("\n");
	}
}

// Show help screen and wait for any key to return
void show_help() {
	cls();
	setColor(LIGHTMAGENTA);
	locate(1,1);
	printf("HELP - Controls and Symbols\n\n");
	setColor(WHITE);
	printf("Movement: WASD\n");
	printf("Attack: WASD\n");
	printf("Use potion: p\n");
	printf("Defend: e\n");
	printf("Help: h\n");
	printf("Quit: ESC\n\n");
	printf("Symbols:\n");
	setColor(BLUE); printf(" . "); setColor(WHITE); printf("= floor\n");
	setColor(CYAN); printf(" # "); setColor(WHITE); printf("= wall\n");
	setColor(YELLOW); printf(" o "); setColor(WHITE); printf("= coin\n");
	setColor(GREEN); printf(" < "); setColor(WHITE); printf("= stairs down\n");
	setColor(LIGHTRED); printf(" f "); setColor(WHITE); printf("= torch\n");
	setColor(MAGENTA); printf(" P "); setColor(WHITE); printf("= potion\n");
	setColor(LIGHTCYAN); printf(" S "); setColor(WHITE); printf("= sword (increases attack)\n");
	setColor(RED); printf(" E "); setColor(WHITE); printf("= enemy\n\n");
	setColor(GREY);
	printf("Press any key to return...\n");
	anykey("");
}

// Show beginning text at the beginning of the game
void show_begining() {
	cls();
	setColor(WHITE);

	locate(1,1);
	std::cout <<
R"(==================================================

    ######  ##    ## ######## ## ##    ## ######  
      ##    ###   ## ##       ## ###   ## ##      
      ##    ####  ## ######   ## ####  ## ######  
      ##    ## ## ## ##       ## ## ## ## ##      
    ######  ##  #### ##       ## ##  #### ######  

     ######  ##       ######   ######  ######  
     ##      ##      ##    ## ##    ## ##    ## 
     ######  ##      ##    ## ##    ## ######  
     ##      ##      ##    ## ##    ## ##    ## 
     ##      ######   ######   ######  ##    ## 

                 v   v   v   v
                ==============
                 [ ] [ ] [ ] 
                ==============
                 [ ] [ ] [ ]
                ==============
                 [ ] [ ] [ ]
                ==============
                 [ ] [ ] [ ]
                ==============
                 [ ] [ ] [ ] 
                ==============
                 [ ] [ ] [ ]
                ==============    @
                 [ ] [ ] [ ]     /|\
                ==============   / \
                  (INFINITE)

           PRESS ANY KEY TO DESCEND

==================================================)"
    << std::endl;
	anykey("");

	cls();
	setColor(LIGHTCYAN);
	std::cout <<
R"(
Welcome, adventurer!  o /
                     /|
                     / \
!!! Please use the English keyboard. !!!
Use WASD to move, H for help Menu, and ESC to quit.
)" 
	<< std::endl;
	anykey("Hit any key to start.\n");

	cls();
	setColor(LIGHTCYAN);
    std::cout <<
R"(================================================================================

       The Primal Sun is gone, leaving the world to the Eternal Whisper.
    
                   In this abyss, darkness devours the soul.

               Listen... that is the sound of your sanity fraying.


                             Here, Fire is God.

 The Sacred Flame is all that separates your reason from the madness of beasts.

                       Take up your torch, adventurer.

                    Slay the shadows and reclaim the light.

                Keep the fire burning - or perish in the silence.

================================================================================)"
	<< std::endl;
	anykey("\nHit any key to continue...\n");
}

/// Main loop and input handling
int main() {
	hidecursor();
	saveDefaultColor();
	gen(level);

	show_begining();

	push_msg("Game started.");
	draw();
	bool running = true;
	while (running) {
		// Input
		if (kbhit()) {
			char k = getkey();

			int oldx = x, oldy = y;
			bool player_acted = false;
			if (k == 'a' || k == 'd' || k == 'w' || k == 's') {
				int tx = x, ty = y;
				if (k == 'a') tx = x-1;
				else if (k == 'd') tx = x+1;
				else if (k == 'w') ty = y-1;
				else if (k == 's') ty = y+1;
				// Attack if enemy is there
				int ei = enemy_at(tx, ty);
				if (ei != -1) {
					// attack enemy
					int raw = swordDamage + rand() % (swordDamage + 1);
					int reduction = 0;
					if (enemies[ei].defending) reduction = rand() % (enemies[ei].damage + 1);
					int dmg = raw - reduction; if (dmg < 0) dmg = 0;
					enemies[ei].hp -= dmg;
					player_acted = true;
					push_msg("You hit the enemy for %d damage.", dmg);
					if (enemies[ei].hp <= 0) {
						enemies[ei].alive = false;
						push_msg("Victory! You have defeated the enemy.");
						drop_loot((int)ei);
					}
				} else {
					// attempt move
					x = tx; y = ty; ++moves; player_acted = true;
					if (lvl[x][y] & WALL) { x = oldx; y = oldy; }
					else if (lvl[x][y] & COIN) { coins++; lvl[x][y] ^= COIN; push_msg("You gain %d coins.", 1); }
					else if (lvl[x][y] & TORCH) { torch+=20; lvl[x][y] ^= TORCH; push_msg("You found torch +%d.", 20); }
					else if (lvl[x][y] & POTION) { ++potions; lvl[x][y] &= ~POTION; push_msg("You found a potion."); }
					else if (lvl[x][y] & SWORD_ITEM) { int inc = 1 + rand()%2; swordDamage += inc; lvl[x][y] &= ~SWORD_ITEM; push_msg("You found a sword (+%d attack).", inc); }
					else if (lvl[x][y] & STAIRS_DOWN) gen(++level);
				}
			}
			else if (k == 'p') {
				// use potion
				if (potions > 0) {
					int heal = 5 + rand()%6; hp += heal; if (hp > max_hp) hp = max_hp; potions--; potions_used++; player_acted = true;
					push_msg("You used a potion and recovered %d HP.", heal);
				}
			}
			else if (k == 'e') {
				player_defending = true; player_acted = true;
			}
			else if (k == 'h') {
				show_help();
				draw();
			}
			else if (k == KEY_ESCAPE) { game_end_reason = "Player quit the game."; running = false; }

			// After player action, enemies take their turns
			if (player_acted) {
				process_enemies_turn();
				draw();
				if (--torch <= 0) { game_end_reason = "Your torch ran out."; running = false; break; }
				if (hp <= 0) { game_end_reason = "You were killed."; running = false; break; }
			}
		}
	}

	// Final summary and achievements
	cls();
	// display final summary content will be shown then exit
	setColor(LIGHTMAGENTA);
	locate(1,1);
	printf("=== Game Summary ===\n\n");
	setColor(WHITE);
	if (game_end_reason.size()) printf("Reason: %s\n\n", game_end_reason.c_str());
	printf("Level reached: %d\n", level);
	printf("Sword: %d\n", swordDamage);
	printf("Moves: %d\n", moves);
	printf("Coins: %d\n", coins);
	printf("Torch: %d\n", torch);
	printf("Potions (left): %d  (used: %d)\n", potions, potions_used);
	printf("Kills: %d\n", kills);
	printf("HP: %d/%d\n\n", hp, max_hp);

	printf("Achievements:\n");
	int ach = 0;
	if (kills >= 1000) { printf(" - Slayer (1000 kills)\n"); ach += 8; }
	else if (kills >= 500) { printf(" - One-Man Army (500 kills)\n"); ach += 7; }
	else if (kills >= 250) { printf(" - Executioner (250 kills)\n"); ach += 6; }
	else if (kills >= 100) { printf(" - Butcher (100 kills)\n"); ach += 5; }
	else if (kills >= 50) { printf(" - Merciless (50 kills)\n"); ach += 4; }
	else if (kills >= 25) { printf(" - Reckless (25 kills)\n"); ach += 3; }
	else if (kills >= 10) { printf(" - Skirmisher (10 kills)\n"); ach += 2; }
	else if (kills >= 1) { printf(" - First Blood\n"); ach++; }
	if (coins >= 1000) { printf(" - Filthy Rich (1000 coins)\n"); ach += 6; }
	else if (coins >= 750) { printf(" - Tycoon (750 coins)\n"); ach += 5; }
	else if (coins >= 500) { printf(" - Deep Pockets (500 coins)\n"); ach += 4; }
	else if (coins >= 250) { printf(" - Entrepreneur (250 coins)\n"); ach += 3; }
	else if (coins >= 100) { printf(" - Well-to-do (100 coins)\n"); ach += 2; }
	else if (coins >= 50) { printf(" - Pocket Change (50 coins)\n"); ach++; }
	if (potions >= 200) { printf(" - The Human Flask (200 potions)\n"); ach += 5; }
	else if (potions >= 100) { printf(" - Apothecary's Friend (100 potions)\n"); ach += 4; }
	else if (potions >= 50) { printf(" - Lifeline (50 potions)\n"); ach += 3; }
	else if (potions >= 25) { printf(" - Stockpiler (25 potions)\n"); ach += 2; }
	else if (potions >= 5) { printf(" - Taste Tester (5 potions)\n"); ach++; }
	if (level >= 100) { printf(" - The Human Flask (100 levels)\n"); ach += 7; }
	else if (level >= 75) { printf(" - Labyrinth Master (50 levels)\n"); ach += 6; }
	else if (level >= 50) { printf(" - Abyssal Voyager (50 levels)\n"); ach += 5; }
	else if (level >= 25) { printf(" - Deep Diver (25 levels)\n"); ach += 4; }
	else if (level >= 10) { printf(" - Spelunker (10 levels)\n"); ach += 3; }
	else if (level >= 5) { printf(" - Taste Tester (5 levels)\n"); ach += 2; }
	else if (level > 1) { printf(" - First Step (more than 1 level)\n"); ach++; }
	if (ach == 0) printf(" none\n");

	// Score calculation
	long score = 0;
	score += (long)kills * 100;
	score += (long)coins * 2;
	score += (long)level * 500;
	score += (long)swordDamage * 50;
	score += (long)hp * 10;
	score += (long)potions_used * 20;
	score += (long)ach * 500;
	score -= (long)moves; // penalty for too many moves
	printf("\nFinal Score: %ld\n", score);

	anykey("\nPress any key to exit...\n");

	cls();
	resetColor();
	showcursor();

	return 0;
}