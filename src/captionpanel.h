// Draws the VR caption panel into an RGBA image on the CPU: a soft, see-through "hologram" panel
// with the newest lines at the bottom. Only runs when the text or the style changes, so it costs
// nothing while nothing is said. Fonts are loaded on first use; fonts for other writing systems
// (Japanese, Korean, ...) only when such text actually shows up.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct PanelLine {
    std::string text;   // what to read (the translation)
    std::string sub;    // smaller line under it (the original words), may be empty
    bool me = false;    // something you said
    int speaker = -1;   // "Voice 1/2" inside one caption: coloured marker
};

struct PanelStyle {
    int width = 1024;          // pixels; the panel's height follows from maxLines and the text size
    float textPx = 40;
    int maxLines = 3;          // rows of text (wrapped lines and original-word rows count too)
    uint32_t textColor = 0xF2F5FA;    // 0xRRGGBB
    uint32_t accentColor = 0x7FE3FF;  // border, glow, speaker markers
    uint32_t meColor = 0xF2B04A;      // your own lines
    float panelOpacity = 0.55f;       // 0 = text only, floating
    bool glow = true;
    bool showSub = false;
    std::string tag;           // small note at the top right ("Talking to: a girl"), empty = none
};

// A row of big buttons for the SteamVR dashboard: a title, the buttons, a line under them.
// The buttons sit in the vertical middle of the image, so clicks land right whichever way up the
// pointer's y runs.
// The CIMA Box tab in the SteamVR menu: rows of buttons, each row with a name on the left.
struct MenuButton {
    std::string label;
    int id = -1;       // what pressing it does (the app's numbering)
    bool on = false;   // drawn selected / switched on
    bool operator==(const MenuButton& o) const { return label == o.label && id == o.id && on == o.on; }
};
struct MenuRow {
    std::string label;
    std::vector<MenuButton> buttons;
    bool operator==(const MenuRow& o) const { return label == o.label && buttons == o.buttons; }
};
struct MenuSpec {
    int width = 1500;
    std::string title, footer;
    std::vector<MenuRow> rows;
    int hover = -1;    // id of the button under the pointer
    uint32_t accentColor = 0x7FE3FF;
};
int menuHeight(const MenuSpec& m);  // follows from the rows
// The id of the button under pixel x, y (y from the top), or -1.
int menuButtonAt(const MenuSpec& m, float x, float y);

struct PanelImage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;  // straight (not premultiplied) alpha, rows top to bottom
    bool empty = true;          // nothing to show
};

class CaptionPainter {
public:
    CaptionPainter();
    ~CaptionPainter();
    CaptionPainter(const CaptionPainter&) = delete;
    CaptionPainter& operator=(const CaptionPainter&) = delete;

    // Font files to use, best first (the first is the main font). Defaults to the system's fonts.
    void setFontFiles(const std::vector<std::string>& files);
    bool hasFont();  // false if no font file could be read

    void paint(const std::vector<PanelLine>& lines, const PanelStyle& style, PanelImage& out);
    void paintMenu(const MenuSpec& menu, PanelImage& out);

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};

// The panel's height in pixels for a style (fixed, so the panel doesn't jump as lines come and go).
int panelHeight(const PanelStyle& style);
