# voxelcopter

![voxelcopter screenshot](http://lew-palm.de/voxelcopter.jpg)

Multi-threaded and multi-platform helicopter simulator with CPU-only rendering (raycasting) using terrain from the 1992's game Comache.

This project was heavily inspired by https://github.com/s-macke/VoxelSpace. Read that page if you want to learn something about the ingenious simple rendering algorithm.

## Building
The program is written in C++20 (needed for the type atomic int). You need GCC 10 (or later) to compile it. It is also possible to compile it with clang, but this results in less FPS.

You need two libraries: libsdl2 and libsdl2-image. Maybe the packages are called libsdl2-dev und libsdl2-image-dev in your distribution.

## Control
WASD and mouse.
Use ESC to quit the game.

## License
(copied from s-macke/VoxelSpace)
The software part of the repository is under the MIT license. Please read the license file for more information. Please keep in mind, that the Voxel Space technology might be still patented in some countries. The color and height maps are reverse engineered from the game Comanche and are therefore excluded from the license.
