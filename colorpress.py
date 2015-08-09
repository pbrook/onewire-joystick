#! /usr/bin/env python
import pygame
import serial
import collections
import os
import sys
import time
import random

DT = 100

try:
    ser = serial.Serial("/dev/ttyACM0")
except:
    raise
    class DummySerial(object):
        def write(self, data):
            pass
    ser = DummySerial()

RGB = collections.namedtuple('RGB', 'r g b')

class Node(object):
    def __init__(self):
        self.level = [0, 0, 0]
        self.new_level = [0, 0, 0]
    def post_tick(self):
        for i in range(3):
            if self.new_level[i] > 0:
                self.new_level[i] -= 1
            self.level[i] = self.new_level[i]

class Pipe(object):
    def __init__(self, a, b):
        self.a = a
        self.b = b
    def tick(self):
        for i in range(3):
            val = max(self.a.level[i], self.b.level[i])
            if self.a.new_level[i] < val:
                self.a.new_level[i] = val
            if self.b.new_level[i] < val:
                self.b.new_level[i] = val
        

class Model(object):
    def connect(self, a, b):
        self.pipes.append(Pipe(self.nodes[a], self.nodes[b]))

    def __init__(self):
        self.nodes = [Node() for _ in range(12)]
        self.pipes = []
        self.tick_count = 0
        for x in range(2):
            self.connect(x, x+1)
            self.connect(x+3, x+4)
            self.connect(x+6, x+7)
            self.connect(x+9, x+10)
        for x in range(3):
            self.connect(x, x+3)
            self.connect(x+6, x+9)


    def tick(self):
        if self.tick_count == 0:
            self.tick_count = 3
            for p in self.pipes:
                p.tick()
        #self.dump()
        self.tick_count -= 1
        for n in self.nodes:
            n.post_tick()

    def dump(self):
        print(["%0.2f" % n.level[0] for n in self.nodes[0:6]])
        print(["%0.2f" % n.level[0] for n in self.nodes[6:12]])
        print

def set_led(addr, c):
    ser.write("L%d%d%x%x%x\n" % (addr / 6, addr % 6, c.r, c.g, c.b))

def do_exit():
    for j in xrange(0, 2):
        for i in xrange(0, 12):
            set_led(i, RGB(0,0,0))
        time.sleep(1)
    sys.exit(0)

class Controller():
    def __init__(self):
        self.model = Model()
        self.magic_state = [0]
        self.current_color = [None] * 12
        self.next_color = 0
        self.last_tick = pygame.time.get_ticks()
        self.last_button = self.last_tick
        try:
            self.js = pygame.joystick.Joystick(0)
            self.js.init()
        except:
            raise
            class DummyJS(object):
                def get_axis(self, n):
                    return 0
            self.js = DummyJS()

    def update_leds(self):
        for i, n in enumerate(self.model.nodes):
            c = RGB(n.level[0], n.level[1], n.level[2])
            if c != self.current_color[i]:
                self.current_color[i] = c
                set_led(i, c)
    def do_timer(self):
        now = pygame.time.get_ticks()
        if now - self.last_button >= 1000:
            self.do_button(random.randrange(12), True)
        while now - self.last_tick >= DT:
            self.last_tick += DT
            self.model.tick()
    def do_button(self, n, press):
        # We only populate 3 out of each 4 buttons
        n -= n / 4;
        if n >= 12:
            return
        if press:
            c = self.next_color
            self.next_color = (c + 1) % 3
            self.model.nodes[n].new_level[c] = 15
            self.model.nodes[n].level[c] = 15
            self.last_button = self.last_tick
    def do_joystick(self):
        #x = self.js.get_axis(0)
        #y = self.js.get_axis(1)
        x = 0
        y = 0
        s = self.magic_state[0]
        if (s == 0) and (y < -0.5):
            s = 1
        if (s == 1) and (y > 0.5):
            s = 2
        if (s == 2) and (x < -0.5):
            s = 3
        if (s == 3) and (x > 0.5):
            do_exit()
        self.magic_state[0] = s

    def run(self):
        done = False
        while not done:
            self.update_leds()
            for e in pygame.event.get():
                if e.type == pygame.QUIT:
                    done = True
                elif e.type == pygame.JOYBUTTONDOWN:
                    self.do_button(e.button, True)
                elif e.type == pygame.JOYBUTTONUP:
                    self.do_button(e.button, False)
                elif e.type == pygame.USEREVENT:
                    self.do_timer()
                    self.do_joystick()


def main():
    # pygame gets very unhappy if it doesn't have video.  Hack round this.
    os.environ["SDL_VIDEODRIVER"] = "dummy"
    pygame.init()
    pygame.time.set_timer(pygame.USEREVENT, 50)
    Controller().run()

if __name__ == '__main__':
    main()
