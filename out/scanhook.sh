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

# The following is an example script, that receives RLENGTH encoded monochrome
# scans, creates a tiff file out of them and then runs tesseract to perform OCR.
env | tee /tmp/scanhook.env

set -x

if [ ! -z "$SCANNER_FILENAME" ]; then
  cp $SCANNER_FILENAME tmp_${SCANNER_IP}_${SCANNER_PAGE}.rle
  exit 0
fi

# Every time we received a set of pages, record a unique destination filename.
# Also keep the previous one, in case we need to merge it for fake two-page ADF
# (see below).
#[ -e destfilename ] && cp destfilename prevfilename
#echo $(date "+%Y-%m-%d_%H%M%S") > destfilename
DEST_FILENAME=$(cat destfilename)

python3 ../rle_to_tiff.py $SCANNER_XDPI $SCANNER_YDPI $SCANNER_WIDTH \
    $(for i in $(seq 1 $SCANNER_PAGE); do echo tmp_${SCANNER_IP}_${SCANNER_PAGE}.rle; done) \
    > tmp_$DEST_FILENAME.tiff
# For black & white scans, nothing beats FAX group4 compression.
mogrify -compress group4 tmp_$DEST_FILENAME.tiff
# Run tesseract for OCR. Update the language for better results.
TESSERACT_LANGUAGE=deu
tesseract -l $TESSERACT_LANGUAGE tmp_$DEST_FILENAME.tiff scans/$DEST_FILENAME pdf

# Fake ADF support.
if [ -e prevfilename ]; then
  PREV_FILENAME=$(cat prevfilename)
  if [ -e scans/$PREV_FILENAME ]; then
    PREV_PAGES=$(pdftk scans/$PREV_FILENAME dump_data | awk '/NumberOfPages/{print $2}')
    # Heuristic: if there were multiple pages scanned and the previous doc has the
    # same number of pages: assume it's front and back of an ADF scan and merge them
    if [ $SCANNER_PAGE -eq $PREV_PAGES ] && [ $SCANNER_PAGE -gt 1 ]; then
      A=scans/$PREV_FILENAME.pdf
      B=scans/$DEST_FILENAME.pdf
      pdftk A=$A B=$B shuffle A Bend-1 output $A-combined.pdf
    fi
  fi
fi

# Want to upload to Google Drive?
# rclone scans/${DEST_FILENAME}.pdf gdrive:${DEST_FILENAME}.pdf
notify-send "Received $SCANNER_PAGE page(s) from $SCANNER_IP (${SCANNER_WIDTH}x${SCANNER_HEIGHT} px; ${SCANNER_XDPI}x${SCANNER_YDPI} DPI)"
