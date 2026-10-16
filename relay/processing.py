"""Video processing on the laptop: this is where you put your computer vision.

The relay calls process(frame) for every frame from the robot, BEFORE it is
sent to the phone. `frame` is a BGR numpy image (OpenCV convention), usually
320x240. Return the image to send (any size). Keep it fast: whatever time you
spend here is added to the latency you feel in the headset.

The default only applies the --enhance / --upscale command-line options.
"""
import cv2

_clahe = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8))


def enhance(frame):
    """Local contrast boost (CLAHE on luminance). The GC0308 is dull indoors."""
    lab = cv2.cvtColor(frame, cv2.COLOR_BGR2LAB)
    l, a, b = cv2.split(lab)
    return cv2.cvtColor(cv2.merge((_clahe.apply(l), a, b)), cv2.COLOR_LAB2BGR)


def process(frame, options):
    if options.enhance:
        frame = enhance(frame)
    if options.upscale > 1:
        frame = cv2.resize(frame, None, fx=options.upscale, fy=options.upscale, interpolation=cv2.INTER_CUBIC)
    # Example: face detection. Uncomment to try it.
    # gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    # for (x, y, w, h) in _faces.detectMultiScale(gray, 1.2, 5):
    #     cv2.rectangle(frame, (x, y), (x + w, y + h), (0, 255, 0), 2)
    return frame


def needs_decoding(options):
    """False lets the relay forward the robot's JPEG untouched (lowest latency).
    Return True if you edit process() to do something on every frame."""
    return options.enhance or options.upscale > 1 or options.show or options.record


# _faces = cv2.CascadeClassifier(cv2.data.haarcascades + "haarcascade_frontalface_default.xml")
