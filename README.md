# Brother scanner network driver

This repository is no longer maintained. Please check out the @rumpeltux fork at https://github.com/rumpeltux/brother-scand

![Status](https://img.shields.io/badge/status-discontinued-red.svg)
[![License](https://img.shields.io/github/license/darsto/brother-scanner-driver.svg)](LICENSE.md)

Functional userspace driver for Brother scanners.

This is a server for brother click-to-scan feature.
To scan, simply press a button on the scanner.

This is a cross-platform, open-source and headless equivalent of Brother's Control Center 4.

Written in C11. Does not use any external dependencies.

## Advantages over the official Linux driver

Brother released a Linux driver for their scanners, however... 

The official Linux driver uses click-to-scan Brother protocol only to notify about the button press event.
After receiving such event, the driver closes the connection and starts up separate SANE application
that will establish connection with the same scanner (again!) and will request single page scan.

As for DCP-J105 scanner model, the connection establishment + handshake takes around 3 seconds.
To scan a single page, one would have to wait at least 6 seconds before the actual scanning starts.

The press-to-scan protocol offers much more than. It can be used to receive entire
image data within the same (original) connection. But only on Windows... Well, not anymore!

## Features

 * All the Windows Control Center 4 features, including:
   * Scan to IMAGE, OCR, EMAIL, FILE
   * Configurable scan params (DPI, Dimensions, Brightness, Contrast, Color, Compression)
   * Scan multiple pages with almost no interval (~ 0 sec)
   * Multiple scanners support
 * Password-protected hosts (a feature apparently not implemented in Control Center 4)
 * External scan hooks
 * Minimal resource usage when idle
 * Configurable hostname :)

# Installation
```
git clone https://github.com/darsto/brother-scanner-driver.git
cd brother-scanner-driver
make
cd out
vi ./brother.config
chmod +x ./scanhook.sh
../build/brother-scand
```

The driver **should** work for the most of Brother devices. 
However, it has only been tested on the DCP-J105.

If you have successfully run this driver with a different model,
please open a github issue.
