#!/bin/bash
################################################################################
#
# The following environment variables will be set:
#
#   SCANNER_XDPI
#   SCANNER_YDPI
#   SCANNER_HEIGHT
#   SCANNER_WIDTH
#   SCANNER_PAGE (starts from 1)
#   SCANNER_IP
#   SCANNER_SCANID (to group a set of scanned pages)
#   SCANNER_HOSTNAME (as selected on the device)
#   SCANNER_FUNC (as selected on the device)
#   SCANNER_FILENAME (where the received data was stored), e.g. scan123.jpg
#
# The script is also called after all pages of a set have been received,
# in this case SCANNER_FILENAME will not be set.
# For a set of pages, you can expect the variables to stay the same (except for
# SCANNER_PAGE and SCANNER_FILENAME).
#
################################################################################

# This is just an example script. It doesn't actually save any data.

if [ ! -z "$SCANNER_FILENAME" ]; then
  "Received page $SCANNER_PAGE page(s) from $SCANNER_IP: $SCANNER_FILENAME"
  exit 0
fi

notify-send "Received $SCANNER_PAGE page(s) from $SCANNER_IP (${SCANNER_WIDTH}x${SCANNER_HEIGHT} px; ${SCANNER_XDPI}x${SCANNER_YDPI} DPI)"
