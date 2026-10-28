#include "art.h"

static constexpr uint16_t RGB(uint8_t r, uint8_t g, uint8_t b) { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); }

void artFace(Gfx& g, int cx, int cy, int w, Mood mood, uint16_t color) {
    const int ex = w / 4, ey = cy - w / 10, er = max(3, w / 14);
    for (int side = -1; side <= 1; side += 2) {
        const int x = cx + side * ex;
        switch (mood) {
        case Mood::happy:    g.fillArc(x, ey + er, er, er * 2, 180, 360, color); break;   // ^ ^
        case Mood::thinking: g.fillCircle(x + er / 2, ey - er / 2, er, color); break;     // looking up
        default:             g.fillCircle(x, ey, er, color);
        }
    }
    const int my = cy + w / 7, mw = w / 5;
    switch (mood) {
    case Mood::happy:    g.fillArc(cx, my - mw / 2, mw - 3, mw, 20, 160, color); break;   // a smile
    case Mood::oops:     g.drawCircle(cx, my, max(3, w / 22), color); g.drawCircle(cx, my, max(2, w / 22 - 1), color); break;
    case Mood::thinking: g.drawWideLine(cx - mw / 2, my + 2, cx + mw / 2, my - 3, 2, color); break;
    default:             g.drawWideLine(cx - mw / 2, my, cx + mw / 2, my, 2, color);
    }
}

void artOp(Gfx& g, Op op, int cx, int cy, int size, uint16_t color) {
    const int h = size / 2, t = max(3, size / 6);
    switch (op) {
    case Op::add:
        g.fillRoundRect(cx - h, cy - t / 2, size, t, t / 2, color);
        g.fillRoundRect(cx - t / 2, cy - h, t, size, t / 2, color);
        break;
    case Op::sub:
        g.fillRoundRect(cx - h, cy - t / 2, size, t, t / 2, color);
        break;
    case Op::mul: {
        const int d = h * 7 / 10;
        g.drawWideLine(cx - d, cy - d, cx + d, cy + d, t / 2.0f, color);
        g.drawWideLine(cx - d, cy + d, cx + d, cy - d, t / 2.0f, color);
        break;
    }
    case Op::div:
        g.fillRoundRect(cx - h, cy - t / 2, size, t, t / 2, color);
        g.fillCircle(cx, cy - h + t / 2, t * 2 / 3, color);
        g.fillCircle(cx, cy + h - t / 2, t * 2 / 3, color);
        break;
    }
}

static void equals(Gfx& g, int cx, int cy, int size, uint16_t color) {
    const int t = max(3, size / 6);
    g.fillRoundRect(cx - size / 2, cy - t * 3 / 2, size, t, t / 2, color);
    g.fillRoundRect(cx - size / 2, cy + t / 2, size, t, t / 2, color);
}

void artEquation(Gfx& g, const Question& q, int cx, int cy, int shown, uint16_t color, uint16_t answerColor) {
    static constexpr int SIGN = 30, GAP = 10;
    g.setFont(&fonts::FreeSansBold24pt7b);
    g.setTextDatum(middle_left);
    const String a(q.a), b(q.b), r = shown < 0 ? String("?") : String(shown);
    const int wa = g.textWidth(a), wb = g.textWidth(b), wr = g.textWidth(r);
    const int total = wa + wb + wr + 2 * SIGN + 4 * GAP;
    int x = cx - total / 2;
    g.setTextColor(color);
    g.drawString(a, x, cy + 2);
    x += wa + GAP;
    artOp(g, SKILLS[q.skill].op, x + SIGN / 2, cy, SIGN, color);
    x += SIGN + GAP;
    g.drawString(b, x, cy + 2);
    x += wb + GAP;
    equals(g, x + SIGN / 2, cy, SIGN, color);
    x += SIGN + GAP;
    g.setTextColor(shown < 0 ? color : answerColor);
    g.drawString(r, x, cy + 2);
}

void artStar(Gfx& g, int cx, int cy, int r, uint16_t color) {
    float px[10], py[10];
    for (int i = 0; i < 10; i++) {
        const float ang = -PI / 2 + i * PI / 5, rr = i % 2 ? r * 0.45f : r;
        px[i] = cx + rr * cosf(ang);
        py[i] = cy + rr * sinf(ang);
    }
    for (int i = 0; i < 10; i++) g.fillTriangle(cx, cy, px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], color);
}

// ---------------------------------------------------------------- things to count
enum Thing { APPLE, STAR, BALL, FLOWER, THINGS };

static void thing(Gfx& g, Thing t, int cx, int cy, int r) {
    switch (t) {
    case APPLE:
        g.fillCircle(cx, cy + 1, r, RGB(229, 57, 53));
        g.fillCircle(cx - r / 3, cy - r / 3, max(1, r / 4), RGB(255, 138, 128));
        g.fillRect(cx - 1, cy - r - 2, 2, max(3, r / 2), RGB(121, 85, 72));
        g.fillEllipse(cx + r / 3 + 1, cy - r, max(2, r / 3), max(1, r / 5), RGB(67, 160, 71));
        break;
    case STAR:
        artStar(g, cx, cy, r + 1, RGB(255, 193, 7));
        break;
    case BALL:
        g.fillCircle(cx, cy, r, RGB(30, 136, 229));
        g.fillArc(cx, cy, r * 2 / 3, r * 2 / 3 + max(1, r / 4), 200, 340, TFT_WHITE);
        break;
    default:   // flower
        for (int i = 0; i < 5; i++) {
            const float a = i * 2 * PI / 5;
            g.fillCircle(cx + r * 0.55f * cosf(a), cy + r * 0.55f * sinf(a), max(2, r / 2), RGB(236, 64, 122));
        }
        g.fillCircle(cx, cy, max(2, r / 3), RGB(255, 202, 40));
    }
}

// One group of things in a grid of cols columns; the last crossed of them crossed out.
static void group(Gfx& g, Thing t, int count, int crossed, int cols, int x, int y, int cell) {
    const int r = cell * 2 / 5;
    for (int i = 0; i < count; i++) {
        const int cx = x + (i % cols) * cell + cell / 2, cy = y + (i / cols) * cell + cell / 2;
        thing(g, t, cx, cy, r);
        if (i >= count - crossed) {
            g.drawWideLine(cx - r, cy - r, cx + r, cy + r, max(1.5f, r / 4.0f), RGB(66, 66, 66));
            g.drawWideLine(cx - r, cy + r, cx + r, cy - r, max(1.5f, r / 4.0f), RGB(66, 66, 66));
        }
    }
}

bool artPicture(Gfx& g, const Question& q, int x, int y, int w, int h) {
    const Skill& s = SKILLS[q.skill];
    int counts[10], groups = 0, crossed = 0;
    bool plus = false;
    switch (s.op) {
    case Op::add:   // a things + b things
        if (s.n != 0) return false;
        counts[0] = q.a; counts[1] = q.b; groups = 2; plus = true;
        break;
    case Op::sub:   // a things, the last b crossed out
        if (s.n != 0) return false;
        counts[0] = q.a; groups = 1; crossed = q.b;
        break;
    case Op::mul: { // a groups of b (or b groups of a, if that's fewer groups)
        if (q.answer > 30 || q.answer == 0) return false;
        int n = q.a, each = q.b;
        if (n > 6) std::swap(n, each);
        if (n > 6) return false;
        for (int i = 0; i < n; i++) counts[i] = each;
        groups = n;
        break;
    }
    case Op::div:   // a things in groups of b: how many groups?
        if (q.a > 30 || q.answer > 6) return false;
        for (int i = 0; i < q.answer; i++) counts[i] = q.b;
        groups = q.answer;
        break;
    }
    // Rows of up to 5 (like a ten frame) if they fit big enough; otherwise squarer groups.
    int cols[10], best = 0, bestCols[10];
    for (int layout = 0; layout < 2; layout++) {
        int units = 0, rows = 1;
        for (int i = 0; i < groups; i++) {
            cols[i] = layout == 0 ? min(counts[i], 5) : max(1, (int)ceilf(sqrtf(counts[i])));
            units += cols[i];
            rows = max(rows, (counts[i] + cols[i] - 1) / cols[i]);
        }
        const float gapUnits = (groups - 1) * (plus ? 1.6f : 0.7f);
        const int cell = min({ (int)(w / (units + gapUnits)), h / rows, 30 });
        if (cell > best) { best = cell; memcpy(bestCols, cols, sizeof(cols)); }
        if (layout == 0 && cell >= 18) break;
    }
    if (best < 8) return false;
    const int cell = best;
    const int gap = cell * (plus ? 1.6f : 0.7f);
    int total = (groups - 1) * gap;
    for (int i = 0; i < groups; i++) total += bestCols[i] * cell;
    const Thing t = (Thing)((q.a * 7 + q.b) % THINGS);   // the same question, the same things
    int gx = x + (w - total) / 2;
    for (int i = 0; i < groups; i++) {
        const int rows = (counts[i] + bestCols[i] - 1) / bestCols[i];
        group(g, t, counts[i], i == 0 ? crossed : 0, bestCols[i], gx, y + (h - rows * cell) / 2, cell);
        gx += bestCols[i] * cell;
        if (i < groups - 1) {
            if (plus) artOp(g, Op::add, gx + gap / 2, y + h / 2, gap * 2 / 3, RGB(97, 97, 97));
            gx += gap;
        }
    }
    return true;
}
