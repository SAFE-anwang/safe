
Debian
====================
This directory contains files used to package safed/safe-qt
for Debian-based Linux systems. If you compile safed/safe-qt yourself, there are some useful files here.

## safe: URI support ##


safe-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install safe-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your safe-qt binary to `/usr/bin`
and the `../../share/pixmaps/safe128.png` to `/usr/share/pixmaps`

safe-qt.protocol (KDE)

