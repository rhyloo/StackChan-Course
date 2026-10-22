/**
 * Demo 14: the Nezha Pro robotic arm (Elecfreaks case 10), its three motors.
 *
 * micro:bit V2 on the Nezha Pro expansion board, with the three smart motors on
 * M1, M2 and M3. The gripper is M3 (change GRIPPER if yours is on another port).
 *
 *     A (hold)      close the gripper
 *     B (hold)      open it
 *     A + B         stop everything
 *
 * Over the USB serial port (115200 baud), one command per line. <m> is 1, 2 or 3
 * (or m1, m2, m3). Angles are degrees of the motor shaft, counted from where it
 * was at power-on (or at the last "zero"), and can go past 360.
 *     run <m> <speed> [ms]    turn at speed -100..100 for ms (default 500); send it again to keep going
 *     move <m> <degrees> [speed]   turn that many degrees (negative: the other way)
 *     goto <m> <angle> [speed]     go to that angle
 *     stop [m]                stop one motor, or all of them
 *     zero [m]                the current position becomes 0 (it doesn't move)
 *     speed <0-100>           default speed for run (without speed), move and goto
 *     pos                     send the angles now
 *     close [ms] / open [ms]  the gripper, until it grips or reaches the end (at most ms, default 3000)
 *
 * It answers with lines too:
 *     pos <a1> <a2> <a3>      the angles, whenever they change (and every 2 s)
 *     moving <m> run|move
 *     stopped <m> stall|timeout|arrived|command|button
 *     ready / error <text>
 *
 * A motor stops by itself when it stops turning ("stall"): the gripper has
 * gripped something, or a joint reached its end. That way it never keeps pushing
 * against the gears.
 */

const MOTORS = [nezhaV2.MotorPostion.M1, nezhaV2.MotorPostion.M2, nezhaV2.MotorPostion.M3]
const GRIPPER = 2             // index in MOTORS: M3
const CLOSE = 1               // direction that closes the gripper (CW in Elecfreaks' program)
const CHECK_MS = 200          // every this long, read the angles: stall check and "pos"
const STALL_DEG = 2           // turned less than this in CHECK_MS: it is stuck
const START_MS = 400          // the motor needs a moment to start: don't check before this
const ARRIVED_DEG = 3         // move/goto: this close to the target counts as there
const RUN_MS = 500
const GRIP_MS = 3000
const HEARTBEAT_MS = 2000

const IDLE = 0
const RUN = 1
const MOVE = 2

let speed = 40
let mode = [IDLE, IDLE, IDLE]
let runSpeed = [0, 0, 0]
let deadline = [0, 0, 0]      // 0 = no time limit (button held, or move)
let startedAt = [0, 0, 0]
let lastAngle = [0, 0, 0]
let target = [0, 0, 0]
let zero = [0, 0, 0]          // raw angle that counts as 0
let angle = [0, 0, 0]
let sent = [0, 0, 0]          // angles in the last "pos"
let sentAt = 0
let checkedAt = 0
let blocked = 0               // gripper direction that stalled under a button: wait for it to be released
let byButton = 0              // the gripper is moving because a button is held: 1 close, -1 open
let queue: string[] = []      // serial commands, run by the loop (only the loop talks to the board)

function raw(i: number): number {
    return nezhaV2.readAngle(MOTORS[i]) * 0.1
}

function readAngles() {
    for (let i = 0; i < 3; i++) angle[i] = raw(i) - zero[i]
}

function name(i: number): string {
    return "M" + (i + 1)
}

function sendPos() {
    serial.writeLine("pos " + Math.round(angle[0]) + " " + Math.round(angle[1]) + " " + Math.round(angle[2]))
    for (let i = 0; i < 3; i++) sent[i] = Math.round(angle[i])
    sentAt = input.runningTime()
}

function begin(i: number, m: number) {
    mode[i] = m
    startedAt[i] = input.runningTime()
    lastAngle[i] = angle[i]
    serial.writeLine("moving " + name(i) + (m == RUN ? " run" : " move"))
    if (i == GRIPPER) basic.showArrow(m == RUN && runSpeed[i] * CLOSE > 0 ? ArrowNames.West : ArrowNames.East, 0)
}

function halt(i: number, reason: string) {
    if (reason != "arrived") nezhaV2.stop(MOTORS[i])
    if (mode[i] != IDLE) serial.writeLine("stopped " + name(i) + " " + reason)
    mode[i] = IDLE
    deadline[i] = 0
    if (i == GRIPPER) {
        byButton = 0
        basic.showIcon(reason == "stall" ? IconNames.Yes : IconNames.Square, 0)
    }
}

function run(i: number, s: number, ms: number) {
    s = Math.max(-100, Math.min(100, s))
    if (s == 0) {
        halt(i, "command")
        return
    }
    deadline[i] = ms > 0 ? input.runningTime() + ms : 0
    if (mode[i] == RUN && runSpeed[i] == s) return      // the same order again: only renew the time
    runSpeed[i] = s
    nezhaV2.start(MOTORS[i], s)
    if (mode[i] != RUN) begin(i, RUN)
}

function goTo(i: number, to: number, s: number) {
    const delta = to - angle[i]
    if (Math.abs(delta) < ARRIVED_DEG) {
        serial.writeLine("stopped " + name(i) + " arrived")
        return
    }
    target[i] = to
    deadline[i] = 0
    nezhaV2.setServoSpeed(s)
    nezhaV2.__move(MOTORS[i], delta > 0 ? nezhaV2.MovementDirection.CW : nezhaV2.MovementDirection.CCW,
        Math.round(Math.abs(delta)), nezhaV2.SportsMode.Degree)
    begin(i, MOVE)
}

function motorIndex(word: string): number {
    if (word.charAt(0) == "m") word = word.substr(1)
    const n = parseInt(word)
    return n >= 1 && n <= 3 ? n - 1 : -1
}

function num(word: string, otherwise: number): number {
    if (word == null || word == "") return otherwise
    const n = parseFloat(word)
    return isNaN(n) ? otherwise : n
}

function runCommand(line: string) {
    // Opening the port can leave a few garbage bytes before the first command: skip to the first letter.
    let k = 0
    while (k < line.length && (line.charAt(k) < "a" || line.charAt(k) > "z")) k++
    const words = line.substr(k).split(" ").filter(w => w != "")
    if (words.length == 0) return
    const cmd = words[0]
    if (cmd == "close" || cmd == "open") {
        run(GRIPPER, (cmd == "close" ? 1 : -1) * CLOSE * speed, num(words[1], GRIP_MS))
        return
    }
    if (cmd == "stop" && words.length == 1) {
        for (let i = 0; i < 3; i++) halt(i, "command")
        return
    }
    if (cmd == "zero" && words.length == 1) {
        for (let i = 0; i < 3; i++) zero[i] += angle[i]
        readAngles()
        sendPos()
        return
    }
    if (cmd == "speed") {
        speed = Math.max(1, Math.min(100, num(words[1], speed)))
        serial.writeLine("speed " + speed)
        return
    }
    if (cmd == "pos") {
        sendPos()
        return
    }
    const i = words.length > 1 ? motorIndex(words[1]) : -1
    if (i < 0) {
        serial.writeLine("error unknown command or motor: " + line.substr(k))
    } else if (cmd == "run") {
        run(i, num(words[2], speed), num(words[3], RUN_MS))
    } else if (cmd == "move") {
        goTo(i, angle[i] + num(words[2], 0), num(words[3], speed))
    } else if (cmd == "goto") {
        goTo(i, num(words[2], angle[i]), num(words[3], speed))
    } else if (cmd == "stop") {
        halt(i, "command")
    } else if (cmd == "zero") {
        zero[i] += angle[i]
        readAngles()
        sendPos()
    } else {
        serial.writeLine("error unknown command: " + line.substr(k))
    }
}

serial.onDataReceived(serial.delimiters(Delimiters.NewLine), function () {
    queue.push(serial.readUntil(serial.delimiters(Delimiters.NewLine)).trim())
})

function buttons() {
    const a = input.buttonIsPressed(Button.A)
    const b = input.buttonIsPressed(Button.B)
    if (a && b) {
        for (let i = 0; i < 3; i++) if (mode[i] != IDLE) halt(i, "button")
        return
    }
    const held = a ? 1 : b ? -1 : 0
    if (held != blocked) blocked = 0                  // released (or the other button): it can move again
    if (held != 0 && held != byButton && held != blocked) {
        run(GRIPPER, held * CLOSE * speed, 0)
        byButton = held
    } else if (held == 0 && byButton != 0) {
        halt(GRIPPER, "button")                       // button released
    }
}

function check(now: number) {
    for (let i = 0; i < 3; i++) {
        if (mode[i] == IDLE) continue
        if (mode[i] == MOVE && Math.abs(angle[i] - target[i]) < ARRIVED_DEG) {
            halt(i, "arrived")
        } else if (deadline[i] > 0 && now >= deadline[i]) {
            halt(i, "timeout")
        } else if (now - startedAt[i] >= START_MS && Math.abs(angle[i] - lastAngle[i]) < STALL_DEG) {
            if (i == GRIPPER && byButton != 0) blocked = byButton
            halt(i, "stall")
        }
        lastAngle[i] = angle[i]
    }
}

for (let i = 0; i < 3; i++) {
    nezhaV2.stop(MOTORS[i])
    zero[i] = raw(i)
}
readAngles()
basic.showIcon(IconNames.Square, 0)
serial.writeLine("ready: M1 M2 M3 (run, move, goto, stop, zero, speed, pos, close, open)")
sendPos()

basic.forever(function () {
    while (queue.length > 0) runCommand(queue.shift())
    buttons()
    const now = input.runningTime()
    if (now - checkedAt >= CHECK_MS) {
        checkedAt = now
        readAngles()
        check(now)
        let changed = now - sentAt >= HEARTBEAT_MS
        for (let i = 0; i < 3; i++) if (Math.round(angle[i]) != sent[i]) changed = true
        if (changed) sendPos()
    } else {
        // Deadlines without waiting for the next angle check.
        for (let i = 0; i < 3; i++) if (mode[i] == RUN && deadline[i] > 0 && now >= deadline[i]) halt(i, "timeout")
    }
    basic.pause(20)
})
