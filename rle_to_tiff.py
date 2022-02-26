#!/usr/bin/env python3
"""Creates a standard tiff image from RLENGTH data sent by Brother scanners."""
import struct
import sys


def rle_decode(data):
    """Decodes PackBits encoded data."""
    i = 0
    output = bytearray()
    while i < len(data):
        val = data[i]
        i += 1
        if val == 0x80:
            continue
        if val > 0x80:
            repeats = 0x101 - val
            output += data[i:i + 1] * repeats
            i += 1
        else:
            output += data[i:i + val + 1]
            i += val + 1
    return output


EXIF_TAGS = {
    0x100: "ImageWidth",
    0x101: "ImageLength",
    0x102: "BitsPerSample",
    0x103: "Compression",
    0x106: "PhotometricInterpretation",
    0x10A: "FillOrder",
    0x10D: "DocumentName",
    0x10E: "ImageDescription",
    0x10F: "Make",
    0x110: "Model",
    0x111: "StripOffsets",
    0x112: "Orientation",
    0x115: "SamplesPerPixel",
    0x116: "RowsPerStrip",
    0x117: "StripByteCounts",
    0x11A: "XResolution",
    0x11B: "YResolution",
    0x11C: "PlanarConfiguration",
    0x128: "ResolutionUnit",
    0x129: "PageNumber",
    0x12D: "TransferFunction",
    0x131: "Software",
    0x132: "DateTime",
    0x13B: "Artist",
    0x13E: "WhitePoint",
    0x13F: "PrimaryChromaticities",
    0x156: "TransferRange",
    0x200: "JPEGProc",
    0x201: "JPEGInterchangeFormat",
    0x202: "JPEGInterchangeFormatLength",
    0x211: "YCbCrCoefficients",
    0x212: "YCbCrSubSampling",
    0x213: "YCbCrPositioning",
    0x214: "ReferenceBlackWhite",
    0x828F: "BatteryLevel",
    0x8298: "Copyright",
    0x829A: "ExposureTime",
    0x829D: "FNumber",
    0x83BB: "IPTC/NAA",
    0x8769: "ExifIFDPointer",
    0x8773: "InterColorProfile",
    0x8822: "ExposureProgram",
    0x8824: "SpectralSensitivity",
    0x8825: "GPSInfoIFDPointer",
    0x8827: "ISOSpeedRatings",
    0x8828: "OECF",
    0x9000: "ExifVersion",
    0x9003: "DateTimeOriginal",
    0x9004: "DateTimeDigitized",
    0x9101: "ComponentsConfiguration",
    0x9102: "CompressedBitsPerPixel",
    0x9201: "ShutterSpeedValue",
    0x9202: "ApertureValue",
    0x9203: "BrightnessValue",
    0x9204: "ExposureBiasValue",
    0x9205: "MaxApertureValue",
    0x9206: "SubjectDistance",
    0x9207: "MeteringMode",
    0x9208: "LightSource",
    0x9209: "Flash",
    0x920A: "FocalLength",
    0x9214: "SubjectArea",
    0x927C: "MakerNote",
    0x9286: "UserComment",
    0x9290: "SubSecTime",
    0x9291: "SubSecTimeOriginal",
    0x9292: "SubSecTimeDigitized",
    0xA000: "FlashPixVersion",
    0xA001: "ColorSpace",
    0xA002: "PixelXDimension",
    0xA003: "PixelYDimension",
    0xA004: "RelatedSoundFile",
    0xA005: "InteroperabilityIFDPointer",
    0xA20B: "FlashEnergy",  # 0x920B in TIFF/EP
    0xA20C: "SpatialFrequencyResponse",  # 0x920C    -  -
    0xA20E: "FocalPlaneXResolution",  # 0x920E    -  -
    0xA20F: "FocalPlaneYResolution",  # 0x920F    -  -
    0xA210: "FocalPlaneResolutionUnit",  # 0x9210    -  -
    0xA214: "SubjectLocation",  # 0x9214    -  -
    0xA215: "ExposureIndex",  # 0x9215    -  -
    0xA217: "SensingMethod",  # 0x9217    -  -
    0xA300: "FileSource",
    0xA301: "SceneType",
    0xA302: "CFAPattern",  # 0x828E in TIFF/EP
    0xA401: "CustomRendered",
    0xA402: "ExposureMode",
    0xA403: "WhiteBalance",
    0xA404: "DigitalZoomRatio",
    0xA405: "FocalLengthIn35mmFilm",
    0xA406: "SceneCaptureType",
    0xA407: "GainControl",
    0xA408: "Contrast",
    0xA409: "Saturation",
    0xA40A: "Sharpness",
    0xA40B: "DeviceSettingDescription",
    0xA40C: "SubjectDistanceRange",
    0xA420: "ImageUniqueID",
}
TIFF_TAGS = {v: k for k, v in EXIF_TAGS.items()}
SHORT = 3
INT = 4


def tiff_tag(name, datatype, *values):
    """Packs a tiff_tag header"""
    n = len(values)
    dt = 'I' if n == 1 else 'H'
    assert n <= 2, "this stub assumes that values fit into the for byte payload"
    return struct.pack('<HHI%d%s' % (n, dt), TIFF_TAGS[name], datatype, len(values), *values)


class TiffWriter(object):
    def __init__(self, outfile, pages):
        self.outfile = outfile
        outfile.write(b'II\x2a\x00')  # TIFF header
        self.pages = pages
        self.page = 0
        self.offset = 4

    def addPage(self, data, width, xdpi, ydpi):
        """Writes the page and image file directory for the page."""
        width = int(width) & ~7  # round down to multiples of 8
        ifd = [
            tiff_tag('ImageWidth', INT, width),
            tiff_tag('ImageLength', INT, len(data) * 8 // width),  # compute the height based on actual data
            tiff_tag('BitsPerSample', SHORT, 1),
            tiff_tag('Compression', SHORT, 1),  # raw data, no compression
            tiff_tag('PhotometricInterpretation', SHORT, 0),  # 0 is white
            tiff_tag('StripOffsets', INT, self.offset + 4),  # image data right after tiff header
            tiff_tag('SamplesPerPixel', SHORT, 1),
            tiff_tag('RowsPerStrip', INT, len(data) * 8 // width),
            tiff_tag('StripByteCounts', INT, len(data)),
            tiff_tag('XResolution', SHORT, int(xdpi)),
            tiff_tag('YResolution', SHORT, int(ydpi)),
            tiff_tag('PageNumber', SHORT, i, len(filenames)),
        ]
        offset_ifd = self.offset + 4 + len(data)
        data = struct.pack('<I', offset_ifd) + data + struct.pack('<H', len(ifd)) + b''.join(ifd)
        self.offset += len(data)
        self.outfile.write(data)
        self.page += 1


if __name__ == '__main__':
    if len(sys.argv) < 5:
        print('Usage: ./%s xdpi ydpi width filenames > out.tiff' % sys.argv[0], file=sys.stderr)
        exit(1)
    xdpi, ydpi, width = map(int, sys.argv[1:4])
    filenames = sys.argv[4:]
    tiff_writer = TiffWriter(sys.stdout.buffer, pages=len(filenames))
    for i, filename in enumerate(filenames):
        rle_data = open(filename, 'rb').read()
        tiff_writer.addPage(rle_decode(rle_data), width, xdpi, ydpi)
