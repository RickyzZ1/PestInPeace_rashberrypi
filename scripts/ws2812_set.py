#!/usr/bin/env python3
import argparse
import os
import time

from rpi_ws281x import Color, PixelStrip


def parse_args():
    p = argparse.ArgumentParser()
    p.add_argument("--pin", type=int, default=18)
    p.add_argument("--count", type=int, default=int(os.getenv("WS2812_COUNT", "8")))
    p.add_argument("--brightness", type=int, default=int(os.getenv("WS2812_BRIGHTNESS", "80")))
    p.add_argument("--on", action="store_true")
    p.add_argument("--off", action="store_true")
    return p.parse_args()


def main():
    args = parse_args()
    if args.on == args.off:
        raise SystemExit("Specify exactly one of --on or --off")

    strip = PixelStrip(
        num=max(1, args.count),
        pin=args.pin,
        freq_hz=800000,
        dma=10,
        invert=False,
        brightness=max(0, min(255, args.brightness)),
        channel=0,
    )
    strip.begin()

    color = Color(255, 255, 255) if args.on else Color(0, 0, 0)
    for i in range(strip.numPixels()):
        strip.setPixelColor(i, color)
    strip.show()
    time.sleep(0.02)


if __name__ == "__main__":
    main()
