#ifndef SAVESTATE_MENU_H
#define SAVESTATE_MENU_H

#include <stdbool.h>
#include <stdint.h>
#include "Controller.h"
#include "../EmulationLayer/Savestate.h"

// Declared here so the functions below can take pointers to them without including their headers
struct Screen;
struct Overlay;

// The slots are shown in a grid, in reading order: slot 1 top-left, slot 2 to its right, slot 3 below slot 1, and so on
#define SAVESTATE_MENU_COLUMNS 2
#define SAVESTATE_MENU_ROWS ((NES_SAVESTATE_SLOT_COUNT + SAVESTATE_MENU_COLUMNS - 1) / SAVESTATE_MENU_COLUMNS)

// Each slot has up to this many lines of text beside its thumbnail: its name, and the date and time it was saved
#define SAVESTATE_MENU_LABEL_LINES 3

struct SavestateMenu {
    // While the menu is open. The game is paused
    bool active;
    // The highlighted slot. It's kept when the menu closes, so it opens on the same slot next time
    int selected;
    // Whether each slot has a savestate in it. Worked out when the menu opens
    bool slotUsed[NES_SAVESTATE_SLOT_COUNT];

    // Player 1's buttons on the last update, so a press is acted on once rather than on every frame it's held
    bool held[NUMBER_OF_BUTTONS];

    // After the menu closes, the game doesn't see player 1's buttons until they've all been let go. Otherwise the
    // button that loaded a savestate would also be pressed in the game
    bool blockingInput;

    // The overlay elements: the box that dims the game, the box behind the highlighted slot, the line saying which
    // buttons do what, and each slot's thumbnail (or a plain box if it has none) and lines of text
    int dimId;
    int highlightId;
    int hintId;
    int thumbnailIds[NES_SAVESTATE_SLOT_COUNT];
    int labelIds[NES_SAVESTATE_SLOT_COUNT][SAVESTATE_MENU_LABEL_LINES];
};

struct SavestateMenu* createSavestateMenu();
void destroySavestateMenu(struct SavestateMenu* menu);

// Save the screen's current frame as the thumbnail for a slot, next to the slot's savestate file. Call just after
// saving the slot. Returns false if it couldn't be saved
bool saveSavestateThumbnail(struct NES_Console* console, struct Screen* screen, int slot);

// Call every frame while a game is running, unless rewinding. The menu button opens and closes the menu. While it's
// open, the d-pad moves the highlight, A loads the highlighted slot and closes the menu, Select saves to it and
// keeps the menu open, and B closes the menu
void savestateMenuUpdate(struct SavestateMenu* menu, struct Overlay* overlay, struct Screen* screen, struct NES_Console* console, Controller* controller);

// Close the menu without loading anything
void savestateMenuClose(struct SavestateMenu* menu, struct Overlay* overlay);

// Pass player 1's controller byte through this before giving it to the NES. It's 0 while the menu is open, and after
// it closes until every button has been let go
uint8_t savestateMenuFilterInput(struct SavestateMenu* menu, uint8_t state);

#endif
