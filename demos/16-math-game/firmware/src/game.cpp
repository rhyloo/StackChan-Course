#include "game.h"
#include <Preferences.h>

// In unlock order inside each operation. For add and sub, n is the kind of question
// (see make()); for mul and div, the times table. The tables go in the order schools
// usually teach them: 2, 10, 5, then the rest.
const Skill SKILLS[] = {
    { Op::add, "Add up to 10", 0 },
    { Op::add, "Add across 10", 1 },
    { Op::add, "Add tens, no carrying", 2 },
    { Op::add, "Add with carrying", 3 },
    { Op::sub, "Take away within 10", 0 },
    { Op::sub, "Take away across 10", 1 },
    { Op::sub, "Subtract, no borrowing", 2 },
    { Op::sub, "Subtract with borrowing", 3 },
    { Op::mul, "2 times table", 2 },  { Op::mul, "10 times table", 10 }, { Op::mul, "5 times table", 5 },
    { Op::mul, "3 times table", 3 },  { Op::mul, "4 times table", 4 },   { Op::mul, "6 times table", 6 },
    { Op::mul, "7 times table", 7 },  { Op::mul, "8 times table", 8 },   { Op::mul, "9 times table", 9 },
    { Op::div, "Divide by 2", 2 },    { Op::div, "Divide by 10", 10 },   { Op::div, "Divide by 5", 5 },
    { Op::div, "Divide by 3", 3 },    { Op::div, "Divide by 4", 4 },     { Op::div, "Divide by 6", 6 },
    { Op::div, "Divide by 7", 7 },    { Op::div, "Divide by 8", 8 },     { Op::div, "Divide by 9", 9 },
};
static constexpr int COUNT = sizeof(SKILLS) / sizeof(SKILLS[0]);
const int SKILL_COUNT = COUNT;

static constexpr uint32_t STATS_VERSION = 1;    // change it if SKILLS or SkillStats change: old data is dropped
static constexpr int RETRY_AFTER = 3;           // a wrong question comes back after this many others
static constexpr int LOG_SIZE = 30;

static Preferences s_prefs;
static SkillStats s_stats[COUNT];
static Question s_retry;
static int s_retryIn;                           // 0 = nothing to retry
static Question s_last;
static AnswerLog s_log[LOG_SIZE];
static int s_logN;

static int rnd(int lo, int hi) { return lo + esp_random() % (hi - lo + 1); }

static void save() {
    s_prefs.putBytes("skills", s_stats, sizeof(s_stats));
    s_prefs.putUInt("version", STATS_VERSION);
}

void gameBegin() {
    s_prefs.begin("mathgame", false);
    memset(s_stats, 0, sizeof(s_stats));
    if (s_prefs.getUInt("version", 0) == STATS_VERSION && s_prefs.getBytesLength("skills") == sizeof(s_stats))
        s_prefs.getBytes("skills", s_stats, sizeof(s_stats));
}

void gameReset() {
    memset(s_stats, 0, sizeof(s_stats));
    s_retryIn = 0;
    s_logN = 0;
    save();
}

const SkillStats& skillStats(int skill) { return s_stats[skill]; }

int recentRight(int skill) {
    const SkillStats& s = s_stats[skill];
    return __builtin_popcount(s.recent & ((1u << s.recentN) - 1));
}

bool skillUnlocked(int skill) {
    return skill == 0 || SKILLS[skill - 1].op != SKILLS[skill].op || s_stats[skill - 1].mastered;
}

// The two numbers and the answer of a question of this skill.
static void make(const Skill& s, int* a, int* b, int* answer) {
    switch (s.op) {
    case Op::add:
        switch (s.n) {
        case 0: *a = rnd(1, 9); *b = rnd(1, 10 - *a); break;
        case 1: *a = rnd(2, 9); *b = rnd(11 - *a, 9); break;                               // 11-18: past ten
        case 2: do { *a = rnd(10, 89); *b = rnd(2, 89); } while (*a % 10 + *b % 10 > 9 || *a + *b > 99); break;
        default: do { *a = rnd(11, 89); *b = rnd(2, 89); } while (*a % 10 + *b % 10 < 10 || *a + *b > 99);
        }
        *answer = *a + *b;
        break;
    case Op::sub:
        switch (s.n) {
        case 0: *a = rnd(2, 10); *b = rnd(1, *a); break;
        case 1: do { *a = rnd(11, 18); *b = rnd(2, 9); } while (*b <= *a % 10); break;  // borrows from the ten
        case 2: do { *a = rnd(20, 99); *b = rnd(2, *a - 1); } while (*b % 10 > *a % 10); break;
        default: do { *a = rnd(21, 99); *b = rnd(2, *a - 1); } while (*b % 10 <= *a % 10);
        }
        *answer = *a - *b;
        break;
    case Op::mul: {
        const int k = rnd(1, 10);
        if (rnd(0, 1)) { *a = s.n; *b = k; } else { *a = k; *b = s.n; }
        *answer = *a * *b;
        break;
    }
    case Op::div: {
        const int k = rnd(1, 10);
        *a = s.n * k;
        *b = s.n;
        *answer = k;
        break;
    }
    }
}

// Three wrong answers that look right: the mistakes children really make
// (off by one, the ten forgotten, one group more or less, the wrong operation).
static void makeOptions(Question& q) {
    const Op op = SKILLS[q.skill].op;
    const int x = q.answer, a = q.a, b = q.b;
    int cand[8], n = 0;
    switch (op) {
    case Op::add: { const int c[] = { x + 1, x - 1, x + 10, x - 10, x + 2, x - 2 }; for (int v : c) cand[n++] = v; break; }
    case Op::sub: { const int c[] = { x + 1, x - 1, x + 10, a + b, x + 2, x - 2 }; for (int v : c) cand[n++] = v; break; }
    case Op::mul: { const int c[] = { x + b, x - b, x + a, x - a, a + b, x + 1 }; for (int v : c) cand[n++] = v; break; }
    case Op::div: { const int c[] = { x + 1, x - 1, x + 2, x - 2, b, x * 2 }; for (int v : c) cand[n++] = v; break; }
    }
    for (int i = n - 1; i > 0; i--) std::swap(cand[i], cand[rnd(0, i)]);
    int opts[4], k = 0;
    auto add = [&](int v) {
        if (k >= 3 || v < 0 || v == x) return;
        for (int i = 0; i < k; i++) if (opts[i] == v) return;
        opts[k++] = v;
    };
    for (int i = 0; i < n; i++) add(cand[i]);
    for (int extra = 3; k < 3; extra++) add(x + extra);
    opts[3] = x;
    for (int i = 3; i > 0; i--) std::swap(opts[i], opts[rnd(0, i)]);
    memcpy(q.options, opts, sizeof(opts));
}

Question gameNext(Op op, bool mix) {
    if (s_retryIn && --s_retryIn == 0 && (mix || SKILLS[s_retry.skill].op == op)) {
        makeOptions(s_retry);                   // the same question, the buttons in another order
        return s_last = s_retry;
    }
    // Weight: 1 for a skill that goes well, up to 4 for one that always fails, +2 while
    // it has few answers (it's new). Mastered skills still come up, less often.
    float w[COUNT], total = 0;
    for (int i = 0; i < COUNT; i++) {
        w[i] = 0;
        if (!skillUnlocked(i) || (!mix && SKILLS[i].op != op)) continue;
        const SkillStats& s = s_stats[i];
        const float acc = s.recentN ? (float)recentRight(i) / s.recentN : 0.5f;
        w[i] = 1 + 3 * (1 - acc) + (s.recentN < 5 ? 2 : 0);
        total += w[i];
    }
    float r = total * (esp_random() / 4294967296.0f);
    int skill = 0;
    for (int i = 0; i < COUNT; i++) {
        if (w[i] <= 0) continue;
        skill = i;
        if ((r -= w[i]) < 0) break;
    }
    Question q;
    q.skill = skill;
    for (int tries = 0; tries < 5; tries++) {   // not the same question twice in a row
        make(SKILLS[skill], &q.a, &q.b, &q.answer);
        if (q.skill != s_last.skill || q.a != s_last.a || q.b != s_last.b) break;
    }
    makeOptions(q);
    return s_last = q;
}

bool gameAnswer(const Question& q, int given, uint32_t ms) {
    SkillStats& s = s_stats[q.skill];
    const bool ok = given == q.answer;
    ok ? s.right++ : s.wrong++;
    s.timeMs += min<uint32_t>(ms, 60000);       // a child who left for a while doesn't spoil the average
    s.recent = ((s.recent << 1) | ok) & ((1u << RECENT) - 1);
    if (s.recentN < RECENT) s.recentN++;
    bool mastered = false;
    if (!s.mastered && recentRight(q.skill) >= MASTERY_OF) mastered = s.mastered = true;
    if (!ok) { s_retry = q; s_retryIn = RETRY_AFTER; }
    save();

    memmove(s_log + 1, s_log, sizeof(AnswerLog) * (LOG_SIZE - 1));
    s_log[0] = { q.skill, q.a, q.b, given, q.answer, ms };
    if (s_logN < LOG_SIZE) s_logN++;
    return mastered;
}

int gameWeakest(bool anyOp, Op op) {
    int best = -1;
    float bestAcc = 0.8f;                       // from 80 % right it doesn't need reinforcing
    for (int i = 0; i < COUNT; i++) {
        const SkillStats& s = s_stats[i];
        if (!skillUnlocked(i) || s.recentN < 5 || (!anyOp && SKILLS[i].op != op)) continue;
        const float acc = (float)recentRight(i) / s.recentN;
        if (acc < bestAcc) { bestAcc = acc; best = i; }
    }
    return best;
}

int gameLog(const AnswerLog** log) {
    *log = s_log;
    return s_logN;
}
