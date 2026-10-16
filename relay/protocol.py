"""Stack-chan VR wire protocol (see ../PROTOCOL.md).

Every datagram: b"SV", version 1, type byte, then the payload. Big-endian.
Phone <-> laptop on UDP 5601 is fixed by the Android app (section 3 of the
app spec). Robot <-> laptop on UDP 5600 is ours and mirrors firmware/src/link.cpp.
"""
import json
import struct

MAGIC = b"SV"
VERSION = 1
PHONE_PORT = 5601
ROBOT_PORT = 5600
MAX_DATAGRAM = 1200
VIDEO_HEADER = 16
VIDEO_PAYLOAD = MAX_DATAGRAM - VIDEO_HEADER
MAX_CHUNKS = 1024

# Laptop -> phone
VIDEO_CHUNK, TELEMETRY, PONG, ANNOUNCE, SUBSCRIBE, ACTUATE = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06
# Phone -> laptop
HELLO, CONTROL, PING, DISCOVER, BUTTON, COMMAND = 0x10, 0x11, 0x12, 0x13, 0x14, 0x15
SENSOR_DATA, DEVICE_INFO, EVENT, BLE_SCAN = 0x16, 0x17, 0x18, 0x19
# Robot <-> laptop
R_HELLO, R_ACK, R_VIDEO, R_TELEMETRY, R_POSE, R_CMD = 0x20, 0x21, 0x22, 0x23, 0x24, 0x25

DEFAULT_LIMITS = {"pan": [-2.2689, 2.7925], "tilt": [-0.050, 0.480]}

_VIDEO = struct.Struct("!2sBBIHHI")
_POSE = struct.Struct("!2sBBIff")


def header(msg_type: int) -> bytes:
    return MAGIC + bytes((VERSION, msg_type))


def parse(data: bytes):
    """Returns (type, payload) or None for anything that isn't ours."""
    if len(data) < 4 or data[:2] != MAGIC or data[2] != VERSION:
        return None
    return data[3], data[4:]


def json_packet(msg_type: int, obj) -> bytes:
    return header(msg_type) + json.dumps(obj, separators=(",", ":")).encode()


def load_json(payload: bytes):
    """Decoded JSON object, or None if malformed."""
    try:
        obj = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, ValueError):
        return None
    return obj if isinstance(obj, dict) else None


def video_chunks(msg_type: int, frame_id: int, jpeg: bytes, capture_ms: int):
    """Splits a JPEG into VIDEO_CHUNK (phone) or R_VIDEO (robot) datagrams."""
    count = max(1, -(-len(jpeg) // VIDEO_PAYLOAD))
    if count > MAX_CHUNKS:
        raise ValueError(f"frame too large: {len(jpeg)} bytes")
    for i in range(count):
        head = _VIDEO.pack(MAGIC, VERSION, msg_type, frame_id & 0xFFFFFFFF, i, count, capture_ms & 0xFFFFFFFF)
        yield head + jpeg[i * VIDEO_PAYLOAD:(i + 1) * VIDEO_PAYLOAD]


def parse_video_chunk(data: bytes):
    """(frame_id, index, count, capture_ms, payload) or None."""
    if len(data) < VIDEO_HEADER:
        return None
    _, _, _, frame_id, index, count, capture_ms = _VIDEO.unpack_from(data)
    return frame_id, index, count, capture_ms, data[VIDEO_HEADER:]


def pose_packet(seq: int, pan: float, tilt: float) -> bytes:
    return _POSE.pack(MAGIC, VERSION, R_POSE, seq & 0xFFFFFFFF, pan, tilt)


def parse_pose(data: bytes):
    if len(data) < _POSE.size:
        return None
    _, _, _, seq, pan, tilt = _POSE.unpack_from(data)
    return seq, pan, tilt


def newer(a: int, b: int) -> bool:
    """True if uint32 frame id a is newer than b (serial number arithmetic)."""
    return a != b and ((a - b) & 0xFFFFFFFF) < 0x80000000


class FrameAssembler:
    """Reassembles chunked JPEG frames. Same rules as the app's FrameAssembler:
    out-of-order and duplicate chunks are fine, at most `max_pending`
    incomplete frames are kept, and a frame older than the last one emitted
    is never returned."""

    def __init__(self, max_pending: int = 3):
        self.max_pending = max_pending
        self.pending = {}           # frame_id -> [count, {index: payload}, capture_ms]
        self.last_emitted = None
        self.dropped = 0            # incomplete frames discarded

    def add(self, frame_id, index, count, capture_ms, payload):
        """Returns (frame_id, jpeg, capture_ms) when a frame completes, else None."""
        if count == 0 or count > MAX_CHUNKS or index >= count:
            return None
        if self.last_emitted is not None and not newer(frame_id, self.last_emitted):
            return None
        entry = self.pending.get(frame_id)
        if entry is None:
            if len(self.pending) >= self.max_pending:
                oldest = min(self.pending, key=lambda f: (f - frame_id + 0x80000000) & 0xFFFFFFFF)
                del self.pending[oldest]
                self.dropped += 1
            entry = self.pending[frame_id] = [count, {}, capture_ms]
        elif entry[0] != count:
            return None
        entry[1][index] = payload
        if len(entry[1]) < count:
            return None
        del self.pending[frame_id]
        for f in [f for f in self.pending if newer(frame_id, f)]:
            del self.pending[f]
            self.dropped += 1
        self.last_emitted = frame_id
        parts = entry[1]
        return frame_id, b"".join(parts[i] for i in range(count)), entry[2]
