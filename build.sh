#!/bin/bash
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
cd /home/daniel/aoe2_dsi
make "$@"
