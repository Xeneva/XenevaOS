# Dock and clock artifacts during window movement

Moving Controls across the bottom of the desktop could erase sections of the
dock and leave the clock fragmented or ghosted. The window-movement path restores
wallpaper at the old window position. The always-on-top composition path then
drew only the intersection with the window's **new** position, leaving other
erased overlay pixels unrepaired.

That path also appended compositor-generated intersections to the dock client's
shared dirty-rectangle queue. The client owns those pending updates; compositor
movement damage must not be mixed into them.

Always-on-top windows now redraw their entire visible rectangle when movement
invalidates the top layer. Underlying normal windows do not clip this redraw.
Glass uses its stable blurred backdrop, and alpha overlays use the wallpaper
backdrop consistently. The client dirty queue is not modified by this path.

## Regression

```sh
bash Tests/test_deodhai_overlay.sh
```

The sanitized host regression links the real compositor and blending code. It
checks every pixel as a normal window moves across and beyond a synthetic dock,
including opaque clock pixels outside the new overlap. It also checks that
pending client rectangles remain intact and repeated glass redraws are stable.
This test fails against the original intersection-only path.

For GTK verification, drag Controls across the dock repeatedly, including moving
it away from the clock. The entire clock, date, dock border, and icons should
remain intact without requiring hover or another client repaint to repair them.

GTK verification reproduced the broken clock/dock before this change. After an
LLVM rebuild, repeated Controls drags across the dock preserved its entire image;
the before/after dock crops differed by at most 1/255 per color channel.
