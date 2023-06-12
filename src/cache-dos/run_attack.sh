#!/bin/bash

path=$(dirname "$0")
prog=bandwidth_$1
for (( i=1; i<=$2; i++ ))
do
    $path/$prog $(( RANDOM % 4 )) & 
done
$path/latency $(( RANDOM % 4 ))