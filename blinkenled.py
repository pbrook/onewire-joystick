#! /usr/bin/python

import pygame
import serial
import collections
import os
import sys
import time

ser = serial.Serial("/dev/ttyACM0")

RGB = collections.namedtuple('RGB', 'r g b')

class LEDOff(object):
    period = None
    def get_color(self, addr):
        return RGB(0,0,0)

class LEDFade(object):
    period = 50
    def __init__(self):
        self.count=[0,0,0]
        self.delta=1
        self.n = 0
    def get_color(self, addr):
        return RGB(self.count[0], self.count[1], self.count[2])
    def tick(self):
        n = self.n;
        self.count[n] += self.delta;
        if self.count[n] == 0xf:
            self.delta = -1;
            n = (n + 1) % 3;
        if self.count[n] == 0:
            self.delta = 1;
            n = (n + 1) % 3;
        self.n = n;

ColorTable = [
        RGB(15,15,15),
        RGB(15,0,0),
        RGB(0,15,0),
        RGB(0,0,15),
        RGB(15,15,0),
        RGB(15,0,15),
        RGB(0,15,15)]
class LEDBlinken(object):
    period = 500
    def __init__(self):
        self.n = 0
    def tick(self):
        self.n = (self.n + 1) % len(ColorTable)
    def get_color(self, addr):
        return ColorTable[(self.n + addr) % len(ColorTable)]

class LEDChase(object):
    period = 200
    def __init__(self):
        self.n = 0
        self.delta = 1
    def tick(self):
        if self.n == 0:
            self.delta = 1
        if self.n == 11:
            self.delta = -1
        self.n += self.delta
    def get_color(self, addr):
        if addr == self.n:
            return RGB(15,15,15)
        return RGB(0,0,0)


def set_led(addr, c):
    ser.write("L%d%d%x%x%x\n" % (addr / 6, addr % 6, c.r, c.g, c.b))

def do_exit():
    for j in xrange(0, 2):
        for i in xrange(0, 12):
            set_led(i, RGB(0,0,0))
        time.sleep(1)
    sys.exit(0)

def main_loop():
    magic_state = [0]
    def update_leds():
        for i in xrange(0, 12):
            m = led_modes[current_mode[i]]
            c = m.get_color(i)
            if c != current_color[i]:
                current_color[i] = c
                set_led(i, c)
    def do_timer():
        now = pygame.time.get_ticks()
        for m in led_modes:
            while m.period is not None and now - m.last_tick >= m.period:
                m.last_tick += m.period
                m.tick()
    def do_button(n):
        # We only populate 3 out of each 4 buttons
        n -= n / 4;
        if n >= 12:
            return
        current_mode[n] += 1
        if current_mode[n] == len(led_modes):
            current_mode[n] = 0
    def do_joystick():
        x = js.get_axis(0)
        y = js.get_axis(1)
        print x, y
        s = magic_state[0]
        if (s == 0) and (y < -0.5):
            s = 1
        if (s == 1) and (y > 0.5):
            s = 2
        if (s == 2) and (x < -0.5):
            s = 3
        if (s == 3) and (x > 0.5):
            do_exit()
        print s
        magic_state[0] = s

    current_color = [None] * 12
    current_mode = [0] * 12
    now = pygame.time.get_ticks()
    for m in led_modes:
        m.last_tick = now
    last_tick = [now]*len(led_modes)
    done = False
    while not done:
        update_leds()
        for e in pygame.event.get():
            if e.type == pygame.QUIT:
                done = True
            elif e.type == pygame.JOYBUTTONDOWN:
                do_button(e.button)
            elif e.type == pygame.USEREVENT:
                do_timer()
		do_joystick()


# pygame gets very unhappy if it doesn't have video.  Hack round this.
os.environ["SDL_VIDEODRIVER"] = "dummy"
pygame.init()
js = pygame.joystick.Joystick(0)
js.init()
pygame.time.set_timer(pygame.USEREVENT, 50)

#led_modes=[LEDBlinken(), LEDFade(), LEDChase(), LEDOff()]
led_modes=[LEDBlinken(), LEDFade(), LEDChase()]

main_loop()

