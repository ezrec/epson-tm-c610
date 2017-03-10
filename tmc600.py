#!/usr/bin/env python3
#
#  Copyright (C) 2017, Jason S. McMullan <jason.mcmullan@gmail.com>
#  All rights reserved.
# 
#  Licensed under the MIT License:
# 
#  Permission is hereby granted, free of charge, to any person obtaining
#  a copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  the rights to use, copy, modify, merge, publish, distribute, sublicense,
#  and/or sell copies of the Software, and to permit persons to whom the
#  Software is furnished to do so, subject to the following conditions:
# 
#  The above copyright notice and this permission notice shall be included
#  in all copies or substantial portions of the Software.
# 
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
#  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
#  DEALINGS IN THE SOFTWARE.
#

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import sys
import numpy
import struct
import datetime

from PIL import Image

DPI_X = 360
DPI_Y = 180

BED_X = 55.0

BED_X_MARGIN_LEFT = 0.0 # mm
BED_X_MARGIN_RIGHT = 0.0 # mm

BED_Y_MARGIN_TOP = 1.0 # mm
BED_Y_MARGIN_BOTTOM = 15.0 # mm

class EpsonTMC600(object):
    def __init__(self, fd):
        self.fd = fd

    def mm2in(self, mm):
        return mm / 25.4

    def send(self, comment = None, code = None):
        if code is not None:
            fd.write(code)

    def size_mm(self):
        return (BED_X, BED_Y)

    def send_esc(self, code = None, data = None):
        if data is None:
            data = b''
        self.send(comment = None, code = b'\033' + code + data)
        pass

    def send_escp(self, code = None, data = None):
        self.send(comment = None, code = b'\033(' + code + struct.pack("<H", len(data)) + data)
        pass

    def send_remote1(self, code = None, data = None):
        if data is None:
            data = b''
        else:
            data = b"\000" + data
        self.send(comment = None, code = code + struct.pack("<H", len(data)) + data)
        pass

    def prepare(self, image = None, name = None, config = None, auto_cutter = False):
        size = image.size

        size_x_in = self.mm2in(BED_X)
        new_size = (int(DPI_X*size_x_in), int(size[1]/size[0]*DPI_Y*size_x_in))

        self.image = image.convert(mode="RGB").resize(new_size)

        # Do any start-of-day initialization here
        self.send("Leave packet mode", b'\000\000\000')
        self.send_esc(b'\001', b'@EJL 1284.4\n@EJL     \n')
        self.send_esc(b'@')
        self.send_esc(b'@')

        self.send_escp(b'R', b'\000REMOTE1')
        self.send_remote1(b'EX', struct.pack(">LB", 5, 0)) # Media position
        if auto_cutter:
            cut = b'\001'
        else:
            cut = b'\000'
        self.send_remote1(b'AC', cut) # Enable auto-cutter
        self.send_esc(b'\000', b'\000\000')

        # Enable graphics mode
        self.send_escp(b'G', b'\001')

        # Set resolution
        dots_h, dots_v = self.image.size
        unit = 1440
        page = unit // DPI_Y
        vertical = unit // DPI_Y
        horizontal = unit // DPI_X
        self.send_escp(b'U', struct.pack("<BBBH", page, vertical, horizontal, unit))

        self.margin_left = int(self.mm2in(BED_X_MARGIN_LEFT) * DPI_X)
        self.margin_right = int(self.mm2in(BED_X_MARGIN_RIGHT) * DPI_X)
        self.margin_top = int(self.mm2in(BED_Y_MARGIN_TOP) * DPI_Y)
        self.margin_bottom = int(self.mm2in(BED_Y_MARGIN_BOTTOM) * DPI_Y)

        # Set paper loading/ejection
        self.send_esc(b'\x19', b'1')
        # Set page length
        self.send_escp(b'C', struct.pack("<L", self.margin_top + dots_v + self.margin_bottom))
        # Set page top & bottom
        self.send_escp(b'c', struct.pack("<LL", self.margin_top, self.margin_top + dots_v))
        pass

    def _rastpack2(self, line = []):
        return [(line[x + 0] << 6) |
                (line[x + 1] << 4) |
                (line[x + 2] << 2) |
                (line[x + 3] << 0) for x in range(0, len(line) & ~3, 4)]

    def _rle_compress(self, cmode = 0, array = bytearray([])):
        if cmode == 0:
            return array

        out = bytearray([])
        uncompressed = bytearray([])
        i = 0
        while i < len(array):
            max_same = 1
            while max_same < 129 and (max_same + i) < len(array) and array[max_same + i] == array[i]:
                max_same += 1

            if max_same > 2:
                if len(uncompressed) > 0:
                    out += bytearray([len(uncompressed)-1]) + uncompressed
                    uncompressed = bytearray([])

                out += bytearray([257 - max_same, array[i]])
                i += max_same
            else:
                uncompressed += bytearray([array[i]])
                i += 1
                if len(uncompressed) == 128:
                    out += bytearray([len(uncompressed)-1]) + uncompressed
                    uncompressed = bytearray([])

            pass

        if len(uncompressed) > 0:
            out += bytearray([len(uncompressed)-1]) + uncompressed

        return out


    def _render_lines(self, raster = None):
        lines = len(raster)
        width = len(raster[0])

        if lines == 0:
            return

        cmode = 1
        bpp = 2 # 4 dot size mode

        for index in [1, 0, 2]:
            color = 1 << index
            band = [1, 0, 2][index]

            plane = [bytes(self._rastpack2([raster[l][i][band] for i in range(width)])) for l in range(lines)]

            v_lines = lines // 2
            bitmap = bytearray([]).join([plane[i] for i in range(0, lines, 2)])
            weaved = bytearray([]).join([plane[i] for i in range(1, lines, 2)])

            if self.margin_left > 0:
                self.send_escp(b'$', struct.pack("<L", self.margin_left))

            cmd = struct.pack("<BBBHH", color, cmode, bpp, len(plane[0]), lines - v_lines )
            self.send_esc(b'i', cmd + self._rle_compress(cmode, bitmap))

            # Go back to the left margin for the weave
            self.send_escp(b'$', struct.pack("<L", self.margin_left))

            cmd = struct.pack("<BBBHH", color | 0x40, cmode, bpp, len(plane[0]), v_lines )
            self.send_esc(b'i', cmd + self._rle_compress(cmode, weaved))

            self.send(code = b'\r')
            pass
        pass

    def _fsdither(self, image=None):
        h, v = image.size

        plane = list(image.getdata())
        data = [ [255-c[0], 255-c[1], 255-c[2]] for c in plane ]

        raster = []
        for y in range(v):
            row = []
            for x in range(h):
                here = y * h + x
                dots = []
                pixel = data[here]
                for c in range(3):
                    old = pixel[c]
                    if old < 0:
                        old = 0
                    elif old > 255:
                        old = 255
                    new = (old & 0xc0)
                    error = (old - new)
                    if x < (h - 1):
                        data[here + 1][c] += int(error * 7 / 16)
                    if x > 0 and y < (v - 1):
                        data[here - 1 + h][c] += int(error * 3 / 16)
                    if y < (v - 1):
                        data[here + h][c] += int(error * 5 / 16)
                    if x < (h - 1) and y < (v - 1):
                        data[here + 1 + h][c] += int(error * 1 / 16)
                    dots.append(new >> 6)
                    pass
                row.append(dots)
                pass
            raster.append(row)
            pass

        return raster


    def render(self):
        h_dots, v_dots = self.image.size

        dotplanes = self._fsdither(self.image)

        # Got to the top margin
        self.send_escp(b'v', struct.pack("<L", self.margin_top))

        row_count = 180

        # Render the lines...
        for y in range(0, v_dots, row_count):
            # .. in groups of 180
            if v_dots - y > row_count:
                lines = row_count
            else:
                lines = v_dots - y

            raster = dotplanes[y : y + lines]

            self._render_lines(raster)

            if lines == row_count:
                self.send_escp(b'v', struct.pack("<L", lines))
            pass

        self.send(code = b'\x0c')
        pass

    def finish(self):
        self.send_esc(b'@')
        self.send_esc(b'@')
        self.send_escp(b'R', b'\000' + b'REMOTE1')
        self.send_remote1(b'LD')
        self.send_remote1(b'JE', b'\000')
        self.send_esc(b'\000', b'\000\000')
        pass

if __name__ == "__main__":
    with open("output.prn", "wb") as fd:
        epson = EpsonTMC600(fd=fd)
        config={}

        epson.prepare(Image.open("input.jpg"), "input.jpg", config, auto_cutter=True)
        epson.render()
        epson.finish()
    pass

#  vim: set shiftwidth=4 expandtab: #
