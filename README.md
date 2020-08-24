# gopher
Terminal Based File Explorer w/ Ncurses

![alt text](/images/screenshot01.png)

### DEPENDENCIES:

This program requires the ncurses library.

To install on Ubuntu: 
```
sudo apt-get install libncurses5-dev libncursesw5-dev
```
To install on Arch/Manjaro: 
```
sudo pacman -S ncurses
```
Or see the offical website: https://invisible-island.net/ncurses/


### INSTALLATION:

In your terminal:
```
git clone https://github.com/StrictTangent/gopher.git
cd gopher
make
sudo make install
```

To run:
```
gopher
```

### USE:

Navigate with arrow keys.

F1        =  Exit\
LEFT      =  Back / Up a level\
RIGHT     =  Enter Directory\
ENTER     =  Options Menu

a-z       =  Jump to next item beginning with letter.

SHIFT + A =  Sort Alphabetically\
SHIFT + S =  Sort by Size\
SHIFT + D =  Sort by Date

SHIFT + C =  Copy File\
SHIFT + X =  Move File\
SHIFT + V =  Paste File\
SHIFT + R = Rename File\
DELETE = Delete File

SHIFT + N =  New File (really this is just Touch)\
SHIFT + M =  Make New Directory\
SHIFT + T =  Launch a terminal session at current directory (Type 'exit' to return to gopher).\
SHIFT + E =  Execute a single command. Drops to terminal to display output. Press any key to return.

When opening a file, you must type in the name of the program you wish to open the file with.\
For example, when opening myvideo.mp4, you might type 'vlc'.


