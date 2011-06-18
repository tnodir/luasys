
strace -r -T -x -e raw=poll,select -o trace.log ./echosrvr.lua
