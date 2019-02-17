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
#
# Do the heavy lifting of scan processing remotely.
# Invokes a remote script and passes the environment params as HTTP headers.
#
# If SCANNER_FILENAME is set, sends a POST request with the files’ contents
# as binary data. Otherwise (for the final invocation), sends a GET request.
#

URL=$1

if [ ! -z "$SCANNER_FILENAME" ]; then
  UPLOAD_CMD="--data-binary @$SCANNER_FILENAME"
fi

curl -v $UPLOAD_CMD $URL \
    -H "SCANNER_XDPI: $SCANNER_XDPI" \
    -H "SCANNER_YDPI: $SCANNER_YDPI" \
    -H "SCANNER_HEIGHT: $SCANNER_HEIGHT" \
    -H "SCANNER_WIDTH: $SCANNER_WIDTH" \
    -H "SCANNER_PAGE: $SCANNER_PAGE" \
    -H "SCANNER_IP: $SCANNER_IP" \
    -H "SCANNER_HOSTNAME: $SCANNER_HOSTNAME" \
    -H "SCANNER_FUNC: $SCANNER_FUNC" \
    -H 'Expect:'

if [ ! -z "$SCANNER_FILENAME" ]; then
  rm $SCANNER_FILENAME
fi
