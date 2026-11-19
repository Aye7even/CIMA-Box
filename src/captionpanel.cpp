#include "captionpanel.h"

#include "bidi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>

// Dear ImGui's copy of stb_truetype, compiled privately here (static functions).
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

namespace {

std::vector<std::string> systemFonts() {
#if defined(_WIN32)
    const std::string d = "C:\\Windows\\Fonts\\";
    return {d + "segoeui.ttf", d + "arial.ttf",  d + "YuGothM.ttc", d + "meiryo.ttc", d + "malgun.ttf",
            d + "msyh.ttc",    d + "simsun.ttc", d + "Nirmala.ttf", d + "LeelawUI.ttf", d + "ebrima.ttf", d + "mmrtext.ttf"};
#elif defined(__APPLE__)
    const std::string d = "/System/Library/Fonts/";
    return {d + "SFNS.ttf", d + "Helvetica.ttc", d + "Hiragino Sans GB.ttc", d + "AppleSDGothicNeo.ttc", d + "PingFang.ttc"};
#else
    return {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc"};
#endif
}

void appendUtf8(std::string& out, unsigned cp) {
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
    else { out += char(0xF0 | (cp >> 18)); out += char(0x80 | ((cp >> 12) & 0x3F)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
}

std::vector<unsigned> decodeUtf8(const std::string& s) {
    std::vector<unsigned> out;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned cp = c, n = 0;
        if (c >= 0xF0) { cp = c & 0x07; n = 3; }
        else if (c >= 0xE0) { cp = c & 0x0F; n = 2; }
        else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }
        ++i;
        for (unsigned k = 0; k < n && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
        out.push_back(cp);
    }
    return out;
}

// Scripts written without spaces: a line may break between any two characters.
bool breaksAnywhere(unsigned cp) { return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFF00 && cp <= 0xFFEF); }

struct Rgb { float r, g, b; };
Rgb rgbOf(uint32_t c) { return {((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f}; }

}  // namespace

static float tagRoom(const PanelStyle& st) { return st.tag.empty() ? 0.f : st.textPx * 0.62f; }

int panelHeight(const PanelStyle& st) {
    const float line = st.textPx * 1.32f;
    const float pad = st.textPx * 0.55f;
    return int(std::ceil(pad * 2 + tagRoom(st) + line * float(std::max(1, st.maxLines)) + st.textPx * 0.5f));  // + glow
}

// Menu layout: buttons centred vertically, evenly spread across.
static void menuLayout(const MenuSpec& m, std::vector<std::array<float, 4>>& rects) {
    rects.clear();
    const size_t n = m.buttons.size();
    if (n == 0) return;
    const float margin = 30.f, gap = 18.f, bh = float(m.height) * 0.36f;
    const float bw = (float(m.width) - 2 * margin - gap * float(n - 1)) / float(n);
    const float top = (float(m.height) - bh) * 0.5f;
    for (size_t i = 0; i < n; ++i) {
        const float l = margin + float(i) * (bw + gap);
        rects.push_back({l, top, l + bw, top + bh});
    }
}

int menuButtonAt(const MenuSpec& m, float x, float y) {
    std::vector<std::array<float, 4>> r;
    menuLayout(m, r);
    for (size_t i = 0; i < r.size(); ++i)
        if (x >= r[i][0] && x <= r[i][2] && y >= r[i][1] && y <= r[i][3]) return int(i);
    return -1;
}

struct CaptionPainter::Impl {
    struct Font {
        std::string path;
        bool tried = false, ok = false;
        std::vector<unsigned char> data;
        stbtt_fontinfo info;
    };
    struct Glyph {
        int w = 0, h = 0, x0 = 0, y0 = 0;
        float advance = 0;
        std::vector<uint8_t> bitmap;
    };
    std::mutex mtx;
    std::vector<Font> fonts;
    std::map<uint64_t, Glyph> cache;  // (font, size, codepoint) -> glyph

    bool load(Font& f) {
        if (f.tried) return f.ok;
        f.tried = true;
        std::ifstream in(f.path, std::ios::binary);
        if (!in) return false;
        f.data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        const int offset = stbtt_GetFontOffsetForIndex(f.data.data(), 0);  // first font of a collection
        f.ok = offset >= 0 && stbtt_InitFont(&f.info, f.data.data(), offset) != 0;
        if (!f.ok) f.data.clear();
        return f.ok;
    }

    // The first font (loading more only when needed) that has this character.
    int fontFor(unsigned cp) {
        int fallback = -1;
        for (size_t i = 0; i < fonts.size(); ++i) {
            if (!load(fonts[i])) continue;
            if (fallback < 0) fallback = int(i);
            if (stbtt_FindGlyphIndex(&fonts[i].info, int(cp)) != 0) return int(i);
        }
        return fallback;
    }

    const Glyph* glyph(unsigned cp, float px) {
        const int fi = fontFor(cp);
        if (fi < 0) return nullptr;
        const uint64_t key = (uint64_t(fi) << 48) | (uint64_t(px * 4.f) << 32) | cp;
        auto it = cache.find(key);
        if (it != cache.end()) return &it->second;
        if (cache.size() > 3000) cache.clear();  // stays small however long the session
        Font& f = fonts[size_t(fi)];
        // Every font is scaled to the same line height, so mixed scripts sit together evenly.
        int asc = 0, desc = 0, gap = 0;
        stbtt_GetFontVMetrics(&f.info, &asc, &desc, &gap);
        const float scale = px / float(std::max(1, asc - desc));
        Glyph g;
        int adv = 0, lsb = 0;
        stbtt_GetCodepointHMetrics(&f.info, int(cp), &adv, &lsb);
        g.advance = float(adv) * scale;
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&f.info, int(cp), scale, scale, &x0, &y0, &x1, &y1);
        g.w = std::max(0, x1 - x0);
        g.h = std::max(0, y1 - y0);
        g.x0 = x0;
        g.y0 = y0 + int(std::lround(float(asc) * scale));  // relative to the top of the line
        if (g.w > 0 && g.h > 0) {
            g.bitmap.assign(size_t(g.w) * size_t(g.h), 0);
            stbtt_MakeCodepointBitmap(&f.info, g.bitmap.data(), g.w, g.h, g.w, scale, scale, int(cp));
        }
        return &cache.emplace(key, std::move(g)).first->second;
    }

    float widthOf(const std::vector<unsigned>& cps, size_t a, size_t b, float px) {
        float w = 0;
        for (size_t i = a; i < b; ++i)
            if (const Glyph* g = glyph(cps[i], px)) w += g->advance;
        return w;
    }

    // Word wrap: break at spaces, or anywhere in Chinese / Japanese text. Arabic is joined first (the
    // joined letters are narrower), and each row is then put in drawing order (right to left for
    // Arabic and Hebrew); `rtl` says which rows read right to left.
    std::vector<std::vector<unsigned>> wrap(const std::string& text, float px, float maxW, std::vector<bool>* rtl = nullptr) {
        std::vector<std::vector<unsigned>> rows;
        const bool anyRtl = bidi::hasRtl(text);
        const std::vector<unsigned> cps = decodeUtf8(anyRtl ? bidi::shape(text) : text);
        size_t start = 0;
        while (start < cps.size()) {
            while (start < cps.size() && cps[start] == ' ') ++start;
            if (start >= cps.size()) break;
            float w = 0;
            size_t i = start, lastBreak = 0;
            bool haveBreak = false;
            for (; i < cps.size(); ++i) {
                const Glyph* g = glyph(cps[i], px);
                const float adv = g ? g->advance : 0;
                if (w + adv > maxW && i > start) break;
                w += adv;
                if (cps[i] == ' ' || breaksAnywhere(cps[i])) { lastBreak = cps[i] == ' ' ? i : i + 1; haveBreak = true; }
            }
            size_t end = i;
            if (i < cps.size() && haveBreak && lastBreak > start) end = lastBreak;
            rows.emplace_back(cps.begin() + long(start), cps.begin() + long(end));
            bool rowRtl = false;
            if (anyRtl) {
                std::string u;
                for (unsigned cp : rows.back()) appendUtf8(u, cp);
                rows.back() = decodeUtf8(bidi::visualLine(u, &rowRtl));
            }
            if (rtl) rtl->push_back(rowRtl);
            start = end;
        }
        return rows;
    }
};

CaptionPainter::CaptionPainter() : p_(new Impl) { setFontFiles(systemFonts()); }
CaptionPainter::~CaptionPainter() = default;

void CaptionPainter::setFontFiles(const std::vector<std::string>& files) {
    std::lock_guard<std::mutex> lk(p_->mtx);
    p_->fonts.clear();
    p_->cache.clear();
    for (const std::string& f : files) {
        Impl::Font font;
        font.path = f;
        p_->fonts.push_back(std::move(font));
    }
}

bool CaptionPainter::hasFont() {
    std::lock_guard<std::mutex> lk(p_->mtx);
    for (Impl::Font& f : p_->fonts)
        if (p_->load(f)) return true;
    return false;
}

void CaptionPainter::paint(const std::vector<PanelLine>& lines, const PanelStyle& st, PanelImage& out) {
    std::lock_guard<std::mutex> lk(p_->mtx);
    const int W = std::max(64, st.width), H = panelHeight(st);
    const float px = st.textPx, subPx = st.textPx * 0.72f, lineH = px * 1.32f, subH = subPx * 1.3f;
    const float pad = px * 0.55f, glowRoom = px * 0.25f;
    const float maxTextW = float(W) - 2 * pad - 2 * glowRoom - px * 0.4f;  // room for the speaker marker

    // ---- the rows that fit, newest at the bottom
    struct Row { std::vector<unsigned> cps; float px; Rgb color; int speaker; bool first; bool rtl; };
    std::vector<Row> rows;  // collected bottom-up
    float used = 0;
    const float budget = lineH * float(std::max(1, st.maxLines));
    for (auto it = lines.rbegin(); it != lines.rend() && used < budget - 1; ++it) {
        std::vector<Row> mine;
        const Rgb main = rgbOf(it->me ? st.meColor : st.textColor);
        std::vector<bool> rtl;
        auto wrapped = p_->wrap(it->text, px, maxTextW, &rtl);
        for (size_t k = 0; k < wrapped.size(); ++k) mine.push_back({std::move(wrapped[k]), px, main, it->speaker, false, rtl[k]});
        if (!mine.empty()) mine.front().first = true;
        if (st.showSub && !it->sub.empty() && it->sub != it->text) {
            Rgb muted = rgbOf(it->me ? st.meColor : st.textColor);
            muted = {muted.r * 0.62f, muted.g * 0.62f, muted.b * 0.62f};
            rtl.clear();
            auto sub = p_->wrap(it->sub, subPx, maxTextW, &rtl);
            for (size_t k = 0; k < sub.size(); ++k) mine.push_back({std::move(sub[k]), subPx, muted, -1, false, rtl[k]});
        }
        float total = 0;
        for (const Row& r : mine) total += r.px == px ? lineH : subH;
        // An older line that doesn't fit whole is left out; only the newest line may lose its start.
        if (!rows.empty() && used + total > budget + 0.5f) break;
        for (auto r = mine.rbegin(); r != mine.rend(); ++r) {
            const float h = r->px == px ? lineH : subH;
            if (used + h > budget + 0.5f) break;  // the start of a very long newest line drops off the top
            used += h;
            rows.push_back(std::move(*r));
        }
    }
    std::reverse(rows.begin(), rows.end());

    out.width = W;
    out.height = H;
    out.empty = rows.empty();
    out.rgba.assign(size_t(W) * size_t(H) * 4, 0);
    if (rows.empty()) return;

    // Premultiplied float canvas.
    std::vector<float> c(size_t(W) * size_t(H) * 4, 0.f);
    auto blend = [&](int x, int y, Rgb col, float a) {
        if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
        float* d = &c[(size_t(y) * size_t(W) + size_t(x)) * 4];
        const float k = 1.f - a;
        d[0] = col.r * a + d[0] * k;
        d[1] = col.g * a + d[1] * k;
        d[2] = col.b * a + d[2] * k;
        d[3] = a + d[3] * k;
    };

    // ---- panel: rounded, slightly tinted with the accent, thin glowing edge
    const Rgb accent = rgbOf(st.accentColor);
    const float tagH = tagRoom(st);
    const float panelH = used + 2 * pad + tagH;
    const float left = glowRoom, right = float(W) - glowRoom, bottom = float(H) - glowRoom, top = bottom - panelH;
    const float radius = std::min(px * 0.7f, panelH * 0.5f);
    if (st.panelOpacity > 0.01f) {
        const Rgb fill = {0.035f + accent.r * 0.05f, 0.05f + accent.g * 0.05f, 0.08f + accent.b * 0.05f};
        const int y0 = std::max(0, int(top - glowRoom)), y1 = std::min(H, int(bottom + glowRoom) + 1);
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < W; ++x) {
                // signed distance to the rounded rectangle
                const float cx = std::max(left + radius - x - 0.5f, std::max(0.f, x + 0.5f - (right - radius)));
                const float cy = std::max(top + radius - y - 0.5f, std::max(0.f, y + 0.5f - (bottom - radius)));
                const float qx = std::max(cx, 0.f), qy = std::max(cy, 0.f);
                const float sd = std::sqrt(qx * qx + qy * qy) + std::min(std::max(cx, cy), 0.f) - radius;
                if (sd < 0.5f) {
                    // slightly lighter towards the top, like light catching glass
                    const float shade = 1.f + 0.35f * std::max(0.f, 1.f - (y - top) / std::max(1.f, panelH * 0.6f));
                    blend(x, y, {fill.r * shade, fill.g * shade, fill.b * shade}, st.panelOpacity * std::min(1.f, 0.5f - sd));
                }
                const float edge = std::fabs(sd + 1.0f);
                if (edge < 1.2f) blend(x, y, accent, 0.5f * st.panelOpacity * (1.2f - edge) / 1.2f + 0.12f * (1.2f - edge));
                if (sd > 0 && sd < glowRoom && st.glow) blend(x, y, accent, 0.10f * std::min(1.f, st.panelOpacity * 2) * std::exp(-sd / (glowRoom * 0.35f)));
            }
    }

    // ---- text into a coverage mask per row colour, then glow + text
    std::vector<float> mask(size_t(W) * size_t(H), 0.f);
    std::vector<Rgb> colorAt(size_t(W) * size_t(H), Rgb{1, 1, 1});
    float y = top + pad + tagH;
    if (!st.tag.empty()) {  // small accent-coloured note, right-aligned in the top margin
        const float tpx = st.textPx * 0.5f;
        const std::vector<unsigned> cps = decodeUtf8(bidi::visualLine(st.tag));
        float x = right - pad - p_->widthOf(cps, 0, cps.size(), tpx);
        const float base = top + pad * 0.55f;
        for (unsigned cp : cps) {
            const Impl::Glyph* g = p_->glyph(cp, tpx);
            if (!g) continue;
            for (int j = 0; j < g->h; ++j)
                for (int i = 0; i < g->w; ++i)
                    blend(int(std::lround(x)) + g->x0 + i, int(std::lround(base)) + g->y0 + j, accent,
                          0.9f * g->bitmap[size_t(j) * size_t(g->w) + size_t(i)] / 255.f);
            x += g->advance;
        }
    }
    for (const Row& r : rows) {
        const float h = r.px == px ? lineH : subH;
        float x = left + pad + px * 0.4f;
        if (r.speaker >= 0 && r.first) {  // a short coloured bar marks who is talking
            static const uint32_t kVoices[] = {0x7FE3FF, 0xF2B04A, 0x7FD6A4, 0xE59AD0};
            const Rgb vc = rgbOf(kVoices[r.speaker % 4]);
            for (int yy = int(y + h * 0.18f); yy < int(y + h * 0.82f); ++yy)
                for (int xx = int(left + pad); xx < int(left + pad + px * 0.12f); ++xx) blend(xx, yy, vc, 0.9f);
        }
        const float baseline = y + (h - r.px * 1.0f) * 0.5f;
        if (r.rtl) x = std::max(x, right - pad - p_->widthOf(r.cps, 0, r.cps.size(), r.px));  // Arabic, Hebrew: on the right
        for (unsigned cp : r.cps) {
            const Impl::Glyph* g = p_->glyph(cp, r.px);
            if (!g) continue;
            const int gx = int(std::lround(x)) + g->x0, gy = int(std::lround(baseline)) + g->y0;
            for (int j = 0; j < g->h; ++j)
                for (int i = 0; i < g->w; ++i) {
                    const int X = gx + i, Y = gy + j;
                    if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
                    const float a = g->bitmap[size_t(j) * size_t(g->w) + size_t(i)] / 255.f;
                    float& m = mask[size_t(Y) * size_t(W) + size_t(X)];
                    if (a > m) {
                        m = a;
                        colorAt[size_t(Y) * size_t(W) + size_t(X)] = r.color;
                    }
                }
            x += g->advance;
        }
        y += h;
    }
    if (st.glow) {  // soft halo: the text mask blurred twice with a box filter
        std::vector<float> a = mask, b(mask.size());
        const int rad = std::max(1, int(px * 0.11f));
        for (int pass = 0; pass < 2; ++pass) {
            for (int yy = 0; yy < H; ++yy) {  // horizontal
                float sum = 0;
                for (int xx = -rad; xx <= rad; ++xx) sum += a[size_t(yy) * size_t(W) + size_t(std::clamp(xx, 0, W - 1))];
                for (int xx = 0; xx < W; ++xx) {
                    b[size_t(yy) * size_t(W) + size_t(xx)] = sum / float(2 * rad + 1);
                    sum += a[size_t(yy) * size_t(W) + size_t(std::min(W - 1, xx + rad + 1))] -
                           a[size_t(yy) * size_t(W) + size_t(std::max(0, xx - rad))];
                }
            }
            for (int xx = 0; xx < W; ++xx) {  // vertical
                float sum = 0;
                for (int yy = -rad; yy <= rad; ++yy) sum += b[size_t(std::clamp(yy, 0, H - 1)) * size_t(W) + size_t(xx)];
                for (int yy = 0; yy < H; ++yy) {
                    a[size_t(yy) * size_t(W) + size_t(xx)] = sum / float(2 * rad + 1);
                    sum += b[size_t(std::min(H - 1, yy + rad + 1)) * size_t(W) + size_t(xx)] -
                           b[size_t(std::max(0, yy - rad)) * size_t(W) + size_t(xx)];
                }
            }
        }
        for (int yy = 0; yy < H; ++yy)
            for (int xx = 0; xx < W; ++xx) blend(xx, yy, accent, std::min(1.f, a[size_t(yy) * size_t(W) + size_t(xx)] * 0.75f));
    }
    for (int yy = 0; yy < H; ++yy)
        for (int xx = 0; xx < W; ++xx) {
            const size_t i = size_t(yy) * size_t(W) + size_t(xx);
            if (mask[i] > 0) blend(xx, yy, colorAt[i], mask[i]);
        }

    // ---- to 8-bit straight alpha
    for (size_t i = 0; i < size_t(W) * size_t(H); ++i) {
        const float a = std::min(1.f, c[i * 4 + 3]);
        uint8_t* o = &out.rgba[i * 4];
        if (a <= 0.f) continue;
        for (int k = 0; k < 3; ++k) o[k] = uint8_t(std::lround(std::min(1.f, c[i * 4 + size_t(k)] / a) * 255.f));
        o[3] = uint8_t(std::lround(a * 255.f));
    }
}

void CaptionPainter::paintMenu(const MenuSpec& m, PanelImage& out) {
    std::lock_guard<std::mutex> lk(p_->mtx);
    const int W = std::max(64, m.width), H = std::max(64, m.height);
    std::vector<float> c(size_t(W) * size_t(H) * 4, 0.f);
    auto blend = [&](int x, int y, Rgb col, float a) {
        if (x < 0 || y < 0 || x >= W || y >= H || a <= 0) return;
        float* d = &c[(size_t(y) * size_t(W) + size_t(x)) * 4];
        const float k = 1.f - a;
        d[0] = col.r * a + d[0] * k;
        d[1] = col.g * a + d[1] * k;
        d[2] = col.b * a + d[2] * k;
        d[3] = a + d[3] * k;
    };
    auto roundRect = [&](float l, float t, float r, float b, float rad, Rgb fill, float alpha, Rgb edge, float edgeA) {
        for (int y = std::max(0, int(t) - 1); y < std::min(H, int(b) + 2); ++y)
            for (int x = std::max(0, int(l) - 1); x < std::min(W, int(r) + 2); ++x) {
                const float cx = std::max(l + rad - x - 0.5f, std::max(0.f, x + 0.5f - (r - rad)));
                const float cy = std::max(t + rad - y - 0.5f, std::max(0.f, y + 0.5f - (b - rad)));
                const float qx = std::max(cx, 0.f), qy = std::max(cy, 0.f);
                const float sd = std::sqrt(qx * qx + qy * qy) + std::min(std::max(cx, cy), 0.f) - rad;
                if (sd < 0.5f) blend(x, y, fill, alpha * std::min(1.f, 0.5f - sd));
                const float e = std::fabs(sd + 1.f);
                if (edgeA > 0 && e < 1.5f) blend(x, y, edge, edgeA * (1.5f - e) / 1.5f);
            }
    };
    // Text centred on cx (or left-aligned at x when cx < 0), shrunk to fit maxW.
    auto text = [&](const std::string& s, float px, float cx, float x, float top, float maxW, Rgb col, float a) {
        const std::vector<unsigned> cps = decodeUtf8(bidi::visualLine(s));
        float w = p_->widthOf(cps, 0, cps.size(), px);
        if (w > maxW && w > 0) {
            px *= maxW / w;
            w = p_->widthOf(cps, 0, cps.size(), px);
        }
        float pen = cx >= 0 ? cx - w * 0.5f : x;
        for (unsigned cp : cps) {
            const Impl::Glyph* g = p_->glyph(cp, px);
            if (!g) continue;
            for (int j = 0; j < g->h; ++j)
                for (int i = 0; i < g->w; ++i)
                    blend(int(std::lround(pen)) + g->x0 + i, int(std::lround(top)) + g->y0 + j, col,
                          a * g->bitmap[size_t(j) * size_t(g->w) + size_t(i)] / 255.f);
            pen += g->advance;
        }
    };

    const Rgb accent = rgbOf(m.accentColor), white = {0.95f, 0.96f, 0.98f}, dim = {0.62f, 0.66f, 0.72f};
    const Rgb bg = {0.06f + accent.r * 0.04f, 0.075f + accent.g * 0.04f, 0.11f + accent.b * 0.04f};
    roundRect(2, 2, float(W) - 2, float(H) - 2, 26, bg, 0.94f, accent, 0.35f);
    text(m.title, 38, -1, 34, 22, float(W) - 68, white, 1.f);

    std::vector<std::array<float, 4>> rects;
    menuLayout(m, rects);
    for (size_t i = 0; i < rects.size(); ++i) {
        const auto& r = rects[i];
        const bool sel = int(i) == m.selected, hov = int(i) == m.hover;
        const Rgb fill = sel ? accent : Rgb{0.16f, 0.19f, 0.25f};
        roundRect(r[0], r[1], r[2], r[3], 18, fill, sel ? 0.95f : (hov ? 0.95f : 0.8f), accent, hov && !sel ? 0.9f : 0.f);
        const Rgb ink = sel ? Rgb{0.05f, 0.07f, 0.1f} : white;
        const float px = 40;
        text(m.buttons[i], px, (r[0] + r[2]) * 0.5f, 0, (r[1] + r[3]) * 0.5f - px * 0.5f, r[2] - r[0] - 20, ink, 1.f);
    }
    if (!m.footer.empty()) text(m.footer, 32, -1, 34, float(H) - 66, float(W) - 68, dim, 1.f);

    out.width = W;
    out.height = H;
    out.empty = false;
    out.rgba.assign(size_t(W) * size_t(H) * 4, 0);
    for (size_t i = 0; i < size_t(W) * size_t(H); ++i) {
        const float a = std::min(1.f, c[i * 4 + 3]);
        uint8_t* o = &out.rgba[i * 4];
        if (a <= 0.f) continue;
        for (int k = 0; k < 3; ++k) o[k] = uint8_t(std::lround(std::min(1.f, c[i * 4 + size_t(k)] / a) * 255.f));
        o[3] = uint8_t(std::lround(a * 255.f));
    }
}
