#!/usr/bin/env python

from PIL import Image, ImageDraw, ImageFont
import os
import argparse
import sys

arg_parser = argparse.ArgumentParser(description='')
arg_parser.add_argument('--outputdir', help='RootDir of generated images', default='./images')
arg_parser.add_argument('--datasize', help='Total data size of the images in Gigabytes', default='1')
arg_parser.add_argument('--imagedim', help='Dimension of the generated image in pixels', default='1024')
arg_parser.add_argument('--prefix', help='Prefix of the image names', default='')
args = arg_parser.parse_args(sys.argv[1:])

IMAGE_W = int(args.imagedim)
IMAGE_H = int(args.imagedim)
STROKE_COLOR = (255, 255, 255, 255)
BG_COLOR = (0, 0, 0, 255)
IMAGE_FORMAT = "tga"
OUTPUT_DIR = args.outputdir
MAX_DATA_SIZE = int(args.datasize)*1024*1024*1024

image = Image.new('RGBA', (IMAGE_W, IMAGE_H))
canvas = ImageDraw.Draw(image)
font = ImageFont.truetype("Impact.ttf", 512)

index:int = 1
total_size:int = 0
image_size = IMAGE_W*IMAGE_H*4

meta_template = """
{
    normalMap: false,
    hasAlpha: false,
    sRGB: true,
    isMask: false,
	generateMips: true,
    android: {
        format: "astc_6x6"
    },
    pc: {
        format: "bc1"
    }
}
"""

if not os.path.exists(OUTPUT_DIR): 
    os.makedirs(OUTPUT_DIR)
cur_dir = os.curdir
os.chdir(OUTPUT_DIR)

print('Generating images ...')
while True:
    text = str(index)
    text_left, text_top, text_right, text_bottom = canvas.textbbox((0, 0), text, font=font)
    text_width = text_right - text_left
    text_height = text_bottom - text_top

    canvas.rectangle([(0, 0), (IMAGE_W, IMAGE_H)], outline=STROKE_COLOR, fill=BG_COLOR, width=50)
    canvas.text(((IMAGE_W - text_width)/2, (IMAGE_H - text_height*1.5)/2), text, font=font, fill=STROKE_COLOR)

    filename = args.prefix + text + "." + IMAGE_FORMAT
    metaFilename = filename + '.asset'
    image.save(filename, bitmap_format=IMAGE_FORMAT)
    print(filename)

    with open(metaFilename, 'w') as meta_file:
        meta_file.write(meta_template)
        meta_file.close()

    index = index + 1
    total_size = total_size + image_size
    if total_size >= MAX_DATA_SIZE:
        break

os.chdir(cur_dir)
print('Generated total {0} mb of image data'.format(total_size/(1024*1024)))

