#!/bin/sh -x
set -e
# librsvg2-tools
rsvg-convert -w 1024 -h 1024 --keep-aspect-ratio kutes.drawio.svg -o kutes.png
# from https://github.com/idesis-gmbh/png2icons
png2icons kutes.png kutes -allp -bz -i
cp kutes.ico logo256.ico
cp kutes.ico logo48.ico
cp kutes.icns logo.icns
rm kutes.png kutes.ico kutes.icns
