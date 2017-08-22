# Brother scanner network driver

![Status](https://img.shields.io/badge/status-unstable-orange.svg)
[![License](https://img.shields.io/github/license/darsto/brother-scanner-driver.svg)](LICENSE.md)

Functional userspace driver for Brother scanners.

This is a server for brother click-to-scan feature.
To scan, simply press a button on the scanner.

Written in C11. Does not use any external dependencies.

# Installation
```
git clone https://github.com/darsto/brother-scanner-driver.git
cd brother-scanner-driver
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
cp ../out/* ./
$EDITOR ./brother.config
chmod +x ./scanhook.sh
./brother
```

The driver **should** work for the most of Brother devices. 
However, it has only been tested on the DCP-J105.

If you have successfully run this driver with a different model,
please open a github issue.
