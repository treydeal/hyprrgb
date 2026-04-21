# hyprrgb
Allows you to specify RGB values to adjust white balance of displays without controls (like laptops).

Requirements:
```
sudo pacman -S wayland wayland-protocols
```
Building from source - first build the headers:
```
wayland-scanner client-header \
  hyprland-ctm-control-v1.xml \
  hyprland-ctm-control-v1-client-protocol.h

wayland-scanner private-code \
  hyprland-ctm-control-v1.xml \
  hyprland-ctm-control-v1-client-protocol.c
```
Then compile the interactive version with:
```
cc -O2 -Wall -Wextra \
  hyprrgb.c \
  hyprland-ctm-control-v1-client-protocol.c \
  -lwayland-client -lm \
  -o hyprrgb
```
Compile the non-interactive version with:
```
cc -O2 -Wall -Wextra \
  hyprrgbd.c \
  hyprland-ctm-control-v1-client-protocol.c \
  -lwayland-client -lm \
  -o hyprrgbd
```
Usage:

Run the interactive version with no arguments to assume eDP-1, or with your monitor's identifier to specify another display:
```
./hyprrgb eDP-1
```
Run the non-interactive version with your monitor's identifier, and the RGB values you want for the display. As an example, to reduce green:
```
./hyprrgbd eDP-1 100 50 100
```
You can add the line to your hyprland.conf as an exec-once entry once you've determined the desired values.
