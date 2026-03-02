#!/bin/bash
# Grants tsp permissions for real-time scheduling and memory locking
setcap cap_sys_nice,cap_ipc_lock,cap_net_raw=ep ./build/tsp
echo "Permissions granted. Now you can run tsp without sudo:"
echo "./build/tsp -b 20000000 -i 127.0.0.1 -p 1234 -c 0 -f ~/dev/cctvhd.ts"
