#!/bin/sh
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
#   SCANNER_SCANID (unique per scand instance)
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

# The following is an example script, that builds upon ocr.sh and adds fake
# duplex support. If you scanned a set of >1 pages with the same number in
# succession, assume it's front and back and create a merged file.

set -e -x

if [ -z "$SCANNER_FILENAME" ]; then
  # We need to define the destination filename and remember the previous one.
  [ -e destfilename ] && cp destfilename prevfilename
  echo $(date "+%Y-%m-%d_%H%M%S") > destfilename
  DEST_FILENAME=$(cat destfilename)
fi

# This performs tiff conversion and OCR, exits unless SCANNER_FILENAME is empty.
. ocr.sh

# Fake ADF support.
if [ -e prevfilename ]; then
  PREV_FILENAME=$(cat prevfilename)
  A=scans/$PREV_FILENAME.pdf
  B=scans/$DEST_FILENAME.pdf
  if [ -e $A ]; then
    PREV_PAGES=$(pdftk $A dump_data | awk '/NumberOfPages/{print $2}')
    # Heuristic: if there were multiple pages scanned and the previous doc has the
    # same number of pages: assume it's front and back of an ADF scan and merge them
    if [ $SCANNER_PAGE -eq $PREV_PAGES ] && [ $SCANNER_PAGE -gt 1 ]; then
      pdftk A=$A B=$B shuffle A Bend-1 output $A-combined.pdf
      mv $A-combined.pdf $A
      rm $B
    fi
  fi
fi
