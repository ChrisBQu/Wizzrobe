#define _CRT_SECURE_NO_WARNINGS

#include "SavestateMenu.h"
#include "Overlay.h"
#include "Screen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

// A slot's thumbnail is a BMP next to its savestate, e.g. Savestates/<checksum>/1.bmp. SDL reads and writes BMPs itself
#define SAVESTATE_FILE_EXTENSION "savestate"
#define THUMBNAIL_FILE_EXTENSION "bmp"
#define SLOT_PATH_LENGTH 128
#define LABEL_TEXT_LENGTH 32

// Layout of the grid, in NES pixels. The cells share out the picture between the margins and gaps, leaving a line at
// the bottom for the hint. Each cell has its thumbnail on the left, as tall as the cell allows, and its text to the right
#define MENU_MARGIN 8.0f
#define MENU_GAP 4.0f
#define MENU_CELL_PADDING 3.0f
#define MENU_TEXT_GAP 4.0f
#define MENU_TEXT_HEIGHT 10.0f
#define MENU_LINE_SPACING 11.0f
#define MENU_HINT_TEXT "A: Load      Select: Save      B: Close"
#define MENU_HINT_SPACE (MENU_TEXT_HEIGHT + MENU_GAP)
#define MENU_CELL_WIDTH ((DEFAULT_SCREEN_WIDTH - 2 * MENU_MARGIN - (SAVESTATE_MENU_COLUMNS - 1) * MENU_GAP) / SAVESTATE_MENU_COLUMNS)
#define MENU_CELL_HEIGHT ((DEFAULT_SCREEN_HEIGHT - 2 * MENU_MARGIN - MENU_HINT_SPACE - (SAVESTATE_MENU_ROWS - 1) * MENU_GAP) / SAVESTATE_MENU_ROWS)

// The game is dimmed behind the menu. The highlighted slot sits on a blue box with a white border. Slots with no
// thumbnail show a grey box in its place
#define MENU_DIM_COLOR 0.0f, 0.0f, 0.0f, 0.75f
#define MENU_HIGHLIGHT_COLOR 0.2f, 0.4f, 0.8f, 0.9f
#define MENU_HIGHLIGHT_BORDER 1.0f
#define MENU_NO_THUMBNAIL_COLOR 0.25f, 0.25f, 0.25f, 1.0f

// Constructor
struct SavestateMenu* createSavestateMenu() {
    struct SavestateMenu* menu = (struct SavestateMenu*)calloc(1, sizeof(struct SavestateMenu));
    if (menu == NULL) { return NULL; }
    menu->dimId = OVERLAY_INVALID_ID;
    menu->highlightId = OVERLAY_INVALID_ID;
    menu->hintId = OVERLAY_INVALID_ID;
    for (int slot = 0; slot < NES_SAVESTATE_SLOT_COUNT; slot++) {
        menu->thumbnailIds[slot] = OVERLAY_INVALID_ID;
        for (int line = 0; line < SAVESTATE_MENU_LABEL_LINES; line++) { menu->labelIds[slot][line] = OVERLAY_INVALID_ID; }
    }
    return menu;
}

void destroySavestateMenu(struct SavestateMenu* menu) {
    free(menu);
}

bool saveSavestateThumbnail(struct NES_Console* console, struct Screen* screen, int slot) {
    if (console == NULL || screen == NULL) { return false; }
    char path[SLOT_PATH_LENGTH];
    if (NES_getSavestateSlotPath(console, slot, THUMBNAIL_FILE_EXTENSION, path, sizeof(path)) != 0) { return false; }

    // The surface only wraps the framebuffer, so freeing it leaves the framebuffer alone
    SDL_Surface* frame = SDL_CreateRGBSurfaceWithFormatFrom(screen->framebuffer, screen->width, screen->height, 32,
        screen->width * (int)sizeof(uint32_t), SDL_PIXELFORMAT_ARGB8888);
    if (frame == NULL) { return false; }
    bool saved = SDL_SaveBMP(frame, path) == 0;
    if (!saved) { printf("Warning: Could not save the thumbnail '%s'. SDL Error: %s\n", path, SDL_GetError()); }
    SDL_FreeSurface(frame);
    return saved;
}

// Helper function: the top-left corner of a slot's cell, in NES pixels
static void getCellPosition(int slot, float* x, float* y) {
    int column = slot % SAVESTATE_MENU_COLUMNS;
    int row = slot / SAVESTATE_MENU_COLUMNS;
    *x = MENU_MARGIN + column * (MENU_CELL_WIDTH + MENU_GAP);
    *y = MENU_MARGIN + row * (MENU_CELL_HEIGHT + MENU_GAP);
}

// Helper function: when a slot was saved, from its savestate file's last change. Returns false if the slot is empty
static bool getSlotTime(struct NES_Console* console, int slot, struct tm* local) {
    char path[SLOT_PATH_LENGTH];
    if (NES_getSavestateSlotPath(console, slot, SAVESTATE_FILE_EXTENSION, path, sizeof(path)) != 0) { return false; }
    struct _stat64 info;
    if (_stat64(path, &info) != 0) { return false; }
    time_t modified = (time_t)info.st_mtime;
    return localtime_s(local, &modified) == 0;
}

// Helper function: add a slot's thumbnail to the overlay, or a plain box if it doesn't have one (like slots saved
// before thumbnails were added). Returns the element's id
static int addThumbnail(struct Overlay* overlay, struct NES_Console* console, int slot, float x, float y, float width, float height) {
    int id = OVERLAY_INVALID_ID;
    char path[SLOT_PATH_LENGTH];
    SDL_Surface* loaded = NULL;
    if (NES_getSavestateSlotPath(console, slot, THUMBNAIL_FILE_EXTENSION, path, sizeof(path)) == 0) { loaded = SDL_LoadBMP(path); }
    if (loaded != NULL) {
        SDL_Surface* argb = SDL_ConvertSurfaceFormat(loaded, SDL_PIXELFORMAT_ARGB8888, 0);
        SDL_FreeSurface(loaded);
        // overlayAddImageARGB takes rows with no gap between them
        if (argb != NULL && argb->pitch == argb->w * (int)sizeof(Uint32)) {
            id = overlayAddImageARGB(overlay, (const Uint32*)argb->pixels, argb->w, argb->h, x, y, width, height, OVERLAY_NO_EXPIRY);
        }
        SDL_FreeSurface(argb);
    }
    if (id == OVERLAY_INVALID_ID) { id = overlayAddBox(overlay, x, y, width, height, MENU_NO_THUMBNAIL_COLOR, OVERLAY_NO_EXPIRY); }
    return id;
}

// Helper function: add one line of a slot's text to the overlay
static int addLabel(struct Overlay* overlay, const char* text, float x, float y) {
    int id = overlayAddText(overlay, text, x, y, OVERLAY_NO_EXPIRY);
    overlaySetTextStyle(overlay, id, MENU_TEXT_HEIGHT, false);
    overlaySetLayer(overlay, id, OVERLAY_LAYER_FRONT);
    return id;
}

// Helper function: move the highlight box to the selected slot
static void moveHighlight(struct SavestateMenu* menu, struct Overlay* overlay) {
    float x, y;
    getCellPosition(menu->selected, &x, &y);
    overlaySetPosition(overlay, menu->highlightId, x, y);
}

// Helper function: find out whether a slot is used, and add its thumbnail and text to the overlay
static void showSlot(struct SavestateMenu* menu, struct Overlay* overlay, struct NES_Console* console, int slot) {
    // Thumbnails keep the picture's shape
    float thumbnailHeight = MENU_CELL_HEIGHT - 2 * MENU_CELL_PADDING;
    float thumbnailWidth = thumbnailHeight * DEFAULT_SCREEN_WIDTH / DEFAULT_SCREEN_HEIGHT;
    float cellX, cellY;
    getCellPosition(slot, &cellX, &cellY);
    float thumbnailX = cellX + MENU_CELL_PADDING;
    float thumbnailY = cellY + MENU_CELL_PADDING;
    float textX = thumbnailX + thumbnailWidth + MENU_TEXT_GAP;

    struct tm saved;
    menu->slotUsed[slot] = getSlotTime(console, slot, &saved);

    menu->thumbnailIds[slot] = addThumbnail(overlay, console, slot, thumbnailX, thumbnailY, thumbnailWidth, thumbnailHeight);
    overlaySetLayer(overlay, menu->thumbnailIds[slot], OVERLAY_LAYER_FRONT);

    char lines[SAVESTATE_MENU_LABEL_LINES][LABEL_TEXT_LENGTH];
    snprintf(lines[0], LABEL_TEXT_LENGTH, "Slot %d", slot + 1);
    if (menu->slotUsed[slot]) {
        strftime(lines[1], LABEL_TEXT_LENGTH, "%d %b %Y", &saved);
        strftime(lines[2], LABEL_TEXT_LENGTH, "%H:%M:%S", &saved);
    }
    else {
        snprintf(lines[1], LABEL_TEXT_LENGTH, "Empty");
        lines[2][0] = '\0';
    }
    for (int line = 0; line < SAVESTATE_MENU_LABEL_LINES; line++) {
        if (lines[line][0] == '\0') { continue; }
        menu->labelIds[slot][line] = addLabel(overlay, lines[line], textX, thumbnailY + line * MENU_LINE_SPACING);
    }
}

// Helper function: remove a slot's thumbnail and text from the overlay
static void hideSlot(struct SavestateMenu* menu, struct Overlay* overlay, int slot) {
    overlayRemove(overlay, menu->thumbnailIds[slot]);
    menu->thumbnailIds[slot] = OVERLAY_INVALID_ID;
    for (int line = 0; line < SAVESTATE_MENU_LABEL_LINES; line++) {
        overlayRemove(overlay, menu->labelIds[slot][line]);
        menu->labelIds[slot][line] = OVERLAY_INVALID_ID;
    }
}

// Helper function: add the dimming, highlight, hint and slots to the overlay
static void openMenu(struct SavestateMenu* menu, struct Overlay* overlay, struct NES_Console* console) {
    menu->active = true;

    // The dimming goes at the back and the highlight above it, so both are under the thumbnails and text
    menu->dimId = overlayAddBox(overlay, 0, 0, DEFAULT_SCREEN_WIDTH, DEFAULT_SCREEN_HEIGHT, MENU_DIM_COLOR, OVERLAY_NO_EXPIRY);
    overlaySetLayer(overlay, menu->dimId, OVERLAY_LAYER_BACK);
    menu->highlightId = overlayAddBox(overlay, 0, 0, MENU_CELL_WIDTH, MENU_CELL_HEIGHT, MENU_HIGHLIGHT_COLOR, OVERLAY_NO_EXPIRY);
    overlaySetBorder(overlay, menu->highlightId, MENU_HIGHLIGHT_BORDER);
    overlaySetLayer(overlay, menu->highlightId, OVERLAY_LAYER_MIDDLE);
    moveHighlight(menu, overlay);

    // The hint goes below the grid
    menu->hintId = addLabel(overlay, MENU_HINT_TEXT, MENU_MARGIN, DEFAULT_SCREEN_HEIGHT - MENU_MARGIN - MENU_TEXT_HEIGHT);

    for (int slot = 0; slot < NES_SAVESTATE_SLOT_COUNT; slot++) { showSlot(menu, overlay, console, slot); }
}

void savestateMenuClose(struct SavestateMenu* menu, struct Overlay* overlay) {
    if (menu == NULL || !menu->active) { return; }
    menu->active = false;
    menu->blockingInput = true;

    overlayRemove(overlay, menu->dimId);
    overlayRemove(overlay, menu->highlightId);
    overlayRemove(overlay, menu->hintId);
    menu->dimId = OVERLAY_INVALID_ID;
    menu->highlightId = OVERLAY_INVALID_ID;
    menu->hintId = OVERLAY_INVALID_ID;
    for (int slot = 0; slot < NES_SAVESTATE_SLOT_COUNT; slot++) { hideSlot(menu, overlay, slot); }
}

void savestateMenuUpdate(struct SavestateMenu* menu, struct Overlay* overlay, struct Screen* screen, struct NES_Console* console, Controller* controller) {
    if (menu == NULL) { return; }

    // Work out which buttons have just been pressed
    bool pressed[NUMBER_OF_BUTTONS];
    for (int b = 0; b < NUMBER_OF_BUTTONS; b++) {
        bool down = controller_get_pressed(controller, (CONTROLLER_BUTTON)b);
        pressed[b] = down && !menu->held[b];
        menu->held[b] = down;
    }

    if (!menu->active) {
        if (pressed[BUTTON_SAVESTATE_MENU]) { openMenu(menu, overlay, console); }
        return;
    }

    if (pressed[BUTTON_SAVESTATE_MENU] || pressed[BUTTON_B]) {
        savestateMenuClose(menu, overlay);
        return;
    }

    // Empty slots can be highlighted, but there's nothing to load from them
    if (pressed[BUTTON_A] && menu->slotUsed[menu->selected]) {
        int slot = menu->selected;
        savestateMenuClose(menu, overlay);
        char message[MAX_OVERLAY_TEXT_LENGTH];
        if (NES_loadSavestateSlot(console, slot) == 0) { SDL_snprintf(message, sizeof(message), "Loaded slot %d", slot + 1); }
        else { SDL_snprintf(message, sizeof(message), "Failed to load slot %d", slot + 1); }
        overlayShowMessage(overlay, message);
        return;
    }

    // The game is paused on the frame the menu opened on, and the menu is only in the overlay, so the screen's
    // framebuffer still holds that frame for the thumbnail. The menu stays open, and the slot's new thumbnail and time
    // show that it saved
    if (pressed[BUTTON_SELECT]) {
        int slot = menu->selected;
        if (NES_saveSavestateSlot(console, slot) == 0) {
            // The savestate is fine without its thumbnail, so a failure here doesn't count as a failed save
            saveSavestateThumbnail(console, screen, slot);
            hideSlot(menu, overlay, slot);
            showSlot(menu, overlay, console, slot);
        }
        else {
            // Messages go under the menu normally, so bring this one to the front to be seen
            char message[MAX_OVERLAY_TEXT_LENGTH];
            SDL_snprintf(message, sizeof(message), "Failed to save to slot %d", slot + 1);
            overlayShowMessage(overlay, message);
            overlaySetLayer(overlay, overlay->messageId, OVERLAY_LAYER_FRONT);
        }
        return;
    }

    // Move the highlight around the grid, stopping at its edges
    int column = menu->selected % SAVESTATE_MENU_COLUMNS;
    int row = menu->selected / SAVESTATE_MENU_COLUMNS;
    if (pressed[BUTTON_LEFT] && column > 0) { column--; }
    if (pressed[BUTTON_RIGHT] && column < SAVESTATE_MENU_COLUMNS - 1) { column++; }
    if (pressed[BUTTON_UP] && row > 0) { row--; }
    if (pressed[BUTTON_DOWN] && row < SAVESTATE_MENU_ROWS - 1) { row++; }
    int selected = row * SAVESTATE_MENU_COLUMNS + column;
    if (selected >= NES_SAVESTATE_SLOT_COUNT || selected == menu->selected) { return; }
    menu->selected = selected;
    moveHighlight(menu, overlay);
}

uint8_t savestateMenuFilterInput(struct SavestateMenu* menu, uint8_t state) {
    if (menu == NULL) { return state; }
    if (menu->blockingInput && state == 0) { menu->blockingInput = false; }
    return (menu->active || menu->blockingInput) ? 0 : state;
}
