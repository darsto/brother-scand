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

# The following is an example script, that receives RLENGTH encoded monochrome
# scans, creates a tiff file out of them and then runs tesseract to perform OCR.

set -x -e

if [ ! -z "$SCANNER_FILENAME" ]; then
  mv $SCANNER_FILENAME tmp_${SCANNER_SCANID}_${SCANNER_PAGE}.rle
  exit 0
fi

# Every time we received a set of pages, record a unique destination filename.
: "${DEST_FILENAME:=$(date "+%Y-%m-%d_%H%M%S")}"

# Convert to TIFF
python3 ../rle_to_tiff.py $SCANNER_XDPI $SCANNER_YDPI $SCANNER_WIDTH \
    $(for i in $(seq 1 $SCANNER_PAGE); do echo tmp_${SCANNER_SCANID}_$i.rle; done) \
    > tmp_$DEST_FILENAME.tiff

# For black & white scans, nothing beats FAX group4 compression.
mogrify -compress group4 tmp_$DEST_FILENAME.tiff

# Store final results in a separate output directory. Make sure it exists.
mkdir -p scans

# Run tesseract for OCR. Update the language for better results.
TESSERACT_LANGUAGE=deu
tesseract -l $TESSERACT_LANGUAGE tmp_$DEST_FILENAME.tiff scans/$DEST_FILENAME pdf
rm tmp_$DEST_FILENAME.tiff

notify-send "Received $SCANNER_PAGE page(s) from $SCANNER_IP (${SCANNER_WIDTH}x${SCANNER_HEIGHT} px; ${SCANNER_XDPI}x${SCANNER_YDPI} DPI)"
