import os
import random
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from stackchan import protocol as P  # noqa: E402


def chunks_of(frame_id, jpeg):
    return [P.parse_video_chunk(c) for c in P.video_chunks(P.VIDEO_CHUNK, frame_id, jpeg, 1234)]


class FrameAssemblerTest(unittest.TestCase):
    def test_big_endian_layout(self):
        pkt = next(P.video_chunks(P.VIDEO_CHUNK, 0x01020304, b"\xff\xd8", 0x0A0B0C0D))
        self.assertEqual(pkt[:16], bytes([0x53, 0x56, 1, 1, 1, 2, 3, 4, 0, 0, 0, 1, 0x0A, 0x0B, 0x0C, 0x0D]))
        self.assertTrue(all(len(c) <= 1200 for c in P.video_chunks(P.VIDEO_CHUNK, 1, bytes(5000), 0)))

    def test_out_of_order_and_duplicates(self):
        jpeg = bytes(random.getrandbits(8) for _ in range(5000))
        cs = chunks_of(7, jpeg)
        random.shuffle(cs)
        a = P.FrameAssembler()
        out = [a.add(*c) for c in cs + cs[:2]]
        done = [o for o in out if o]
        self.assertEqual(len(done), 1)
        self.assertEqual(done[0][1], jpeg)

    def test_missing_chunk_never_emitted(self):
        a = P.FrameAssembler()
        self.assertTrue(all(a.add(*c) is None for c in chunks_of(1, bytes(4000))[1:]))

    def test_newer_evicts_older_and_older_never_emitted(self):
        a = P.FrameAssembler()
        old = chunks_of(1, bytes(3000))
        a.add(*old[0])
        new = chunks_of(2, b"x" * 100)
        self.assertIsNotNone(a.add(*new[0]))
        self.assertEqual(a.pending, {})
        self.assertTrue(all(a.add(*c) is None for c in old))

    def test_wraparound(self):
        a = P.FrameAssembler()
        self.assertIsNotNone(a.add(*chunks_of(0xFFFFFFFF, b"a")[0]))
        self.assertIsNotNone(a.add(*chunks_of(0, b"b")[0]))

    def test_rejections_and_max_pending(self):
        a = P.FrameAssembler()
        self.assertIsNone(a.add(1, 0, 0, 0, b""))
        self.assertIsNone(a.add(1, 0, 1025, 0, b""))
        self.assertIsNone(a.add(1, 3, 3, 0, b""))
        a.add(5, 0, 2, 0, b"")
        self.assertIsNone(a.add(5, 1, 3, 0, b""))   # disagreeing count
        for f in (6, 7, 8):
            a.add(f, 0, 2, 0, b"")
        self.assertEqual(sorted(a.pending), [6, 7, 8])

    def test_parse_rejects_garbage(self):
        for _ in range(2000):
            data = bytes(random.getrandbits(8) for _ in range(random.randint(0, 40)))
            msg = P.parse(data)
            if msg:
                P.load_json(msg[1])
                P.parse_video_chunk(data)
                P.parse_pose(data)
        self.assertIsNone(P.parse(b"SV\x02\x01"))
        self.assertIsNone(P.parse(b"XX\x01\x01"))
        self.assertIsNone(P.parse(b"SV\x01"))

    def test_pose_roundtrip(self):
        seq, pan, tilt = P.parse_pose(P.pose_packet(42, 1.5, -0.25))
        self.assertEqual((seq, pan, tilt), (42, 1.5, -0.25))


if __name__ == "__main__":
    unittest.main()
