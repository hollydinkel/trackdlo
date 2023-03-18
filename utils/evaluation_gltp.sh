#!/bin/bash
# Loop

for bag in 0
do
    for pct in 0 10 20 30 40 50
    do
        for trial in 0 1 2 3 4 5 6 7 8 9
        do
            for alg in gltp
            do
                terminator -e "cd rmdlo_tracking && source devel/setup.bash && roslaunch trackdlo gltp.launch bag_file:=$bag" &
                first_terminal=$!
                terminator -e "cd rmdlo_tracking && source devel/setup.bash && roslaunch trackdlo evaluation.launch alg:=$alg bag_file:=$bag trial:=$trial pct_occlusion:=$pct --wait" &
                second_teminal=$!
                sleep 80
                rosnode kill -a
                killall -9 rosmaster
                kill $first_terminal
                kill $second_terminal
            done
        done
    done
done

for bag in 1 2
do
    for pct in 0
    do
        for trial in 0 1 2 3 4 5 6 7 8 9
        do
            for alg in gltp
            do
                terminator -e "cd rmdlo_tracking && source devel/setup.bash && roslaunch trackdlo gltp.launch bag_file:=$bag" &
                first_terminal=$!
                terminator -e "cd rmdlo_tracking && source devel/setup.bash && roslaunch trackdlo evaluation.launch alg:=$alg bag_file:=$bag trial:=$trial pct_occlusion:=$pct --wait" &
                second_teminal=$!
                sleep 75
                rosnode kill -a
                killall -9 rosmaster
                kill $first_terminal
                kill $second_terminal
            done
        done
    done
done