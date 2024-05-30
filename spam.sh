#!/bin/bash
NUM_TIMES=$1
for ((i = 1; i <= NUM_TIMES; i++)); do
     ./udp_test/udp_test 3 127.0.0.1 53480 54321
done

wait