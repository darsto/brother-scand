if [ ! -z "$SCANNER_FILENAME" ]; then
  mv $SCANNER_FILENAME $(date "+%Y-%m-%d_%H%M%S").jpg
fi
