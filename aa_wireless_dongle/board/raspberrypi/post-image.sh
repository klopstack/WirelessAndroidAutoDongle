#!/bin/bash

set -e

# Call Buildroot's Raspberry Pi post-image script directly.
# (Don't source it: sourcing would keep $0 as this wrapper, causing it to
# look for genimage.cfg.in in the external tree instead of Buildroot.)
exec board/raspberrypi/post-image.sh
