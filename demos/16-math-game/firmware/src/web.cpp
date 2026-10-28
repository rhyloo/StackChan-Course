// Demo 16 - The statistics page, served by the robot itself (no laptop program).
//   /            the page: what to practise, every skill, the last answers
//   /stats.json  the same data
//   /reset       POST: forget every statistic
// Arduino's WebServer, run from loop() (webPoll): the page only reads the statistics,
// and the game reads and writes them in the same task.
#include "web.h"
#include "game.h"
#include <WebServer.h>

static WebServer s_server(80);

static const char PAGE[] = R"HTML(<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Stack-chan math progress</title>
<style>
body{font:15px/1.4 system-ui,sans-serif;max-width:46em;margin:0 auto;padding:12px 16px;background:#fff8e7;color:#28283c}
h1{font-size:22px;margin:.2em 0}h2{font-size:17px;margin:1.2em 0 .4em}
table{border-collapse:collapse;width:100%}td,th{padding:4px 6px;text-align:left;border-bottom:1px solid #eadfc6}
.bar{display:inline-block;width:110px;height:10px;background:#eadfc6;border-radius:5px;overflow:hidden;vertical-align:middle}
.bar i{display:block;height:100%}.dim{color:#8a8470}.ok{color:#2e7d32}.no{color:#c62828}
#focus{background:#fff;border:2px solid #ff9800;border-radius:10px;padding:8px 12px}
button{font:inherit;padding:6px 12px;border-radius:6px;border:1px solid #c9bfa6;background:#fff;cursor:pointer}
</style>
<h1>Stack-chan math: progress</h1>
<div id="focus">Loading...</div>
<h2>Skills</h2><table id="skills"></table>
<h2>Last answers</h2><table id="log"></table>
<p><button onclick="reset()">Forget all the statistics</button></p>
<script>
const OPS = ['+', '−', '×', '÷'];
const color = p => p === null ? '#bdb6a3' : p >= 80 ? '#43a047' : p >= 50 ? '#ff9800' : '#e53935';
async function load() {
  const d = await (await fetch('/stats.json')).json();
  const weak = d.skills.filter(s => s.unlocked && s.recent_n >= 5 && s.recent_right / s.recent_n < 0.8)
                       .sort((a, b) => a.recent_right / a.recent_n - b.recent_right / b.recent_n);
  document.getElementById('focus').innerHTML = weak.length
    ? '<b>To practise:</b> ' + weak.slice(0, 3).map(s => `${s.name} (${Math.round(100 * s.recent_right / s.recent_n)} % right lately)`).join(', ')
    : (d.skills.some(s => s.right + s.wrong) ? '<b>Nothing to reinforce:</b> every skill played lately is above 80 %.' : 'No answers yet: play a round on the robot.');
  let html = '<tr><th>Skill<th>Lately<th>Right<th>Wrong<th>Average time';
  for (const s of d.skills) {
    const n = s.right + s.wrong, p = s.recent_n ? Math.round(100 * s.recent_right / s.recent_n) : null;
    html += `<tr class="${s.unlocked ? '' : 'dim'}"><td>${OPS[s.op]} ${s.name}${s.mastered ? ' ★' : ''}${s.unlocked ? '' : ' (locked)'}`
          + `<td><span class="bar"><i style="width:${p ?? 0}%;background:${color(p)}"></i></span> ${p === null ? '' : p + ' %'}`
          + `<td>${s.right}<td>${s.wrong}<td>${n ? (s.time_ms / n / 1000).toFixed(1) + ' s' : ''}`;
  }
  document.getElementById('skills').innerHTML = html;
  html = '<tr><th>Question<th>Answer<th>Time';
  for (const a of d.log) {
    const ok = a.given === a.answer;
    html += `<tr><td>${a.a} ${OPS[d.skills[a.skill].op]} ${a.b} = ${a.answer}<td class="${ok ? 'ok' : 'no'}">${a.given} ${ok ? '✔' : '✘'}<td>${(a.ms / 1000).toFixed(1)} s`;
  }
  document.getElementById('log').innerHTML = d.log.length ? html : '<tr><td class="dim">None since the robot started.';
}
async function reset() {
  if (!confirm('Forget every answer and lock the levels again?')) return;
  await fetch('/reset', { method: 'POST' });
  load();
}
load(); setInterval(load, 3000);
</script></html>
)HTML";

static void sendJson() {
    String j;
    j.reserve(4096);
    j = "{\"skills\":[";
    for (int i = 0; i < SKILL_COUNT; i++) {
        const SkillStats& s = skillStats(i);
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "%s{\"name\":\"%s\",\"op\":%d,\"unlocked\":%d,\"mastered\":%d,\"right\":%u,\"wrong\":%u,"
                 "\"time_ms\":%lu,\"recent_right\":%d,\"recent_n\":%u}",
                 i ? "," : "", SKILLS[i].name, (int)SKILLS[i].op, skillUnlocked(i), s.mastered, s.right, s.wrong,
                 (unsigned long)s.timeMs, recentRight(i), s.recentN);
        j += buf;
    }
    j += "],\"log\":[";
    const AnswerLog* log;
    const int n = gameLog(&log);
    for (int i = 0; i < n; i++) {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s{\"skill\":%d,\"a\":%d,\"b\":%d,\"given\":%d,\"answer\":%d,\"ms\":%lu}", i ? "," : "",
                 log[i].skill, log[i].a, log[i].b, log[i].given, log[i].answer, (unsigned long)log[i].ms);
        j += buf;
    }
    j += "]}";
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(200, "application/json", j);
}

void webStart() {
    s_server.on("/", HTTP_GET, [] { s_server.send_P(200, "text/html", PAGE); });
    s_server.on("/stats.json", HTTP_GET, sendJson);
    s_server.on("/reset", HTTP_POST, [] {
        gameReset();
        Serial.println("[math] statistics reset from the web page");
        s_server.send(200, "text/plain", "ok");
    });
    s_server.begin();
}

void webPoll() { s_server.handleClient(); }
