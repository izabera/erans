#!/bin/awk -f

BEGIN {
    printf "%-15s | %15s | %15s\n", "region", "rthroughput", "ipc"
    print "----------------+-----------------+----------------"
}

/Code Region/       { region      = $NF }
/IPC/               { ipc         = $NF }
/Block RThroughput/ { rthroughput = $NF
                      printf "%-15s | %15s | %15s\n",
                             region, rthroughput, ipc }


