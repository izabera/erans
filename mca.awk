#!/bin/awk -f

BEGIN {
    printf "%-15s | %15s | %15s\n", "region", "rthroughput", "ipc"
    print "----------------+-----------------+----------------"
}

/Code Region/       { region      = $NF }
/IPC/               { ipc         = $NF }
/Block RThroughput/ { rthroughput = $NF }

/Block RThroughput/ && region ~ FILTER {
    printf "%-15s | %15s | %15s\n", region, rthroughput, ipc
    result[0][region] = rthroughput
    result[1][region] = ipc
}

END {
    print "----------------+-----------------+----------------"

    PROCINFO["sorted_in"] = "@val_type_asc"
    best = 10000000000
    for (region in result[0]) {
        if (result[0][region] <= best) {
            best = result[0][region]
            printf "%-15s | %15s | %15s\n", region, result[0][region], result[1][region]
        }
    }
}

