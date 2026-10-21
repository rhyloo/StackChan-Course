"""Image filters you turn on with your voice ("turn on night vision").

Gemini calls the set_filter function (voice.py), which changes FILTER.
To add a filter: write the function, add it to FILTERS and Gemini can use it
right away (the list of names is passed to it automatically).
"""
import cv2
import numpy as np

FILTER = "none"

_clahe = cv2.createCLAHE(clipLimit=2.5, tileGridSize=(8, 8))
_faces = cv2.CascadeClassifier(cv2.data.haarcascades + "haarcascade_frontalface_default.xml")


def contrast(img):
    lab = cv2.cvtColor(img, cv2.COLOR_BGR2LAB)
    l, a, b = cv2.split(lab)
    return cv2.cvtColor(cv2.merge((_clahe.apply(l), a, b)), cv2.COLOR_LAB2BGR)


def night(img):
    g = _clahe.apply(cv2.cvtColor(img, cv2.COLOR_BGR2GRAY))
    g = cv2.add(g, np.random.randint(0, 25, g.shape, np.uint8))   # grain
    out = np.zeros_like(img)
    out[:, :, 1] = g
    return out


def edges(img):
    e = cv2.Canny(cv2.GaussianBlur(img, (5, 5), 0), 50, 120)
    out = img // 3
    out[e > 0] = (0, 255, 255)
    return out


def gray(img):
    return cv2.cvtColor(cv2.cvtColor(img, cv2.COLOR_BGR2GRAY), cv2.COLOR_GRAY2BGR)


def cartoon(img):
    color = cv2.bilateralFilter(img, 7, 60, 60)
    edges = cv2.adaptiveThreshold(cv2.medianBlur(cv2.cvtColor(img, cv2.COLOR_BGR2GRAY), 5), 255,
                                  cv2.ADAPTIVE_THRESH_MEAN_C, cv2.THRESH_BINARY, 9, 4)
    return cv2.bitwise_and(color, color, mask=edges)


def thermal(img):
    return cv2.applyColorMap(cv2.cvtColor(img, cv2.COLOR_BGR2GRAY), cv2.COLORMAP_JET)


def faces(img):
    gray = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    for (x, y, w, h) in _faces.detectMultiScale(gray, 1.2, 5, minSize=(30, 30)):
        cv2.rectangle(img, (x, y), (x + w, y + h), (0, 255, 0), 2)
    return img


FILTERS = {
    "none": lambda img: img,
    "contrast": contrast,
    "night": night,
    "edges": edges,
    "gray": gray,
    "cartoon": cartoon,
    "thermal": thermal,
    "faces": faces,
}

last_frame = None   # so Gemini can "see" and to save photos


def process(frame, options):
    global last_frame
    last_frame = frame.copy()
    return FILTERS.get(FILTER, FILTERS["none"])(frame)


def needs_decoding(options):
    return True   # filters can be turned on at any moment
