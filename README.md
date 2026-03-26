# hitsound_stream
This program was made for the [ctb](https://osu.ppy.sh/wiki/en/Game_mode/osu%21catch) game mode in [osu!](https://osu.ppy.sh/) where hit sounds are independet of user input and therefore are at the mercy of the sound latency of the computer. 
This program tries to fix this problem by putting the hit sounds in a sound buffer and playing them separately from osu!. The user can then manually adjust the offset while the program is 
running in the terminal. 

## Running the program
### Linux
Download the latest release and make the file executable

Make the program executable: `chmod +x h_stream`

Execute the program: `h_stream`

### Windows
There is currently not a version for windows

## Controls
Use the arrow keys to adjust the offset for the hit sounds.
Up    = +5ms
Down  = -5ms
Left  = -1ms
Right = +1ms

The volume of the hit sounds can be changed using the '+' and '-' keys.

Quit the program by pressing 'q'. This saves the volume and offset settings in a file called user_data.bin

## Dependencies
Add the miniaudio.h file to the src directory
[miniaudio.h](https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h)
## Building
### Linux
You can compile with: `gcc -lm src/main.c -o h_stream` 

Run the program with (process_vm_readv requires sudo): `sudo ./h_stream`

### Windows
There is currently not a version for windows
## Acknowledgements
This is an early version and will therefore be very buggy. The program has not been tested thoroughly. This is a personal project and is therefore subject to being dropped at any time.

Most of the heavy work in reverse engineering osu! and reading memory has been done before in projects such as
[gosumemory](https://github.com/l3lackShark/gosumemory) 
