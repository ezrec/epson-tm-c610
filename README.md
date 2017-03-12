# `ep_tmc6xx` - A printer driver for CUPS, for the EPSON TM-C610 printer

To install:

```
$ make
$ sudo make install
```

This copies `rastertotmc6xx` to `/usr/lib/cups/filters`, and `ppd/ep_tmc*.ppd` to
`/usr/share/ppd/tmc6xx`

After the `make install` step, you can plug in the printer via USB, and the correct
driver should be automatically loaded when you add the printer via CUPS's web interface
(http://localhost:631) or via the GNOME Settings panel.

For those that wish to directly print to the printer from Python, see the `tmc600.py`
example, which take in an image of arbitrary size, and renders a Floyd-Steinberg
dithered print at 360x180 resolution, for up to 12" of roll length.
