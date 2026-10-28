#pragma once
// Demo 16 - The questions, the skills and their statistics. No screen here.
//
// Each question belongs to a skill: "add up to 10", "the 7 times table"... A skill
// unlocks when the one before it in its operation is mastered (MASTERY_OF of the last
// RECENT answers right). The next skill to ask is drawn at random, weighted towards
// the unlocked ones that fail most: that is the reinforcement.
#include <Arduino.h>

enum class Op : uint8_t { add, sub, mul, div };
static constexpr int OPS = 4;

struct Skill {
    Op op;
    const char* name;          // shown on the screen and the web page
    uint8_t n;                 // the times table for mul / div; the kind for add / sub
};

struct SkillStats {
    uint16_t right, wrong;
    uint32_t timeMs;           // total answer time, for the average
    uint16_t recent;           // last RECENT answers, bit 0 = the newest, 1 = right
    uint8_t recentN;           // how many of those bits are real
    uint8_t mastered;          // reached once: the next skill stays unlocked
};

struct Question {
    int skill;
    int a, b, answer;
    int options[4];            // the four buttons, one of them = answer
};

static constexpr int RECENT = 10, MASTERY_OF = 8;

extern const Skill SKILLS[];
extern const int SKILL_COUNT;

void gameBegin();                              // loads the statistics from the flash
const SkillStats& skillStats(int skill);
bool skillUnlocked(int skill);
int recentRight(int skill);                    // right answers among the last recentN
// A new question of that operation (or of any unlocked one with mix = true).
Question gameNext(Op op, bool mix);
// Records an answer and saves it. Returns true if this answer just mastered the skill.
bool gameAnswer(const Question& q, int given, uint32_t ms);
// The unlocked skill that needs the most practice (-1 if none has enough answers yet).
int gameWeakest(bool anyOp, Op op);
void gameReset();                              // forget every statistic

// The last answers, newest first (only in memory: they go with a restart).
struct AnswerLog { int skill, a, b, given, answer; uint32_t ms; };
int gameLog(const AnswerLog** log);            // returns how many there are
