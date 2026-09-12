#!/bin/sh
printf 'started\n' >> "$1"
sleep 2
cat "$2"
