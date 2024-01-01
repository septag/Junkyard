from PIL import Image, ImageDraw, ImageFont
import os

IMAGE_W = 512
IMAGE_H = 512
STROKE_COLOR = (255, 255, 255, 255)
BG_COLOR = (0, 0, 0, 255)
MAX_DATA_SIZE = 4*1024*1024*1024
IMAGE_FORMAT = "tga"

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
        format: "bc7"
    }
}
"""

if not os.path.exists('images'): os.mkdir('images')
os.chdir('images')

print('Generating images ...')
while True:
    text = str(index)
    text_left, text_top, text_right, text_bottom = canvas.textbbox((0, 0), text, font=font)
    text_width = text_right - text_left
    text_height = text_bottom - text_top

    canvas.rectangle([(0, 0), (IMAGE_W, IMAGE_H)], outline=STROKE_COLOR, fill=BG_COLOR, width=50)
    canvas.text(((IMAGE_W - text_width)/2, (IMAGE_H - text_height*1.5)/2), text, font=font, fill=STROKE_COLOR)

    filename = text + "." + IMAGE_FORMAT
    image.save(filename, bitmap_format=IMAGE_FORMAT)
    print(filename)

    with open(text + '.asset', 'w') as meta_file:
        meta_file.write(meta_template)
        meta_file.close()

    index = index + 1
    total_size = total_size + image_size
    if total_size >= MAX_DATA_SIZE:
        break

os.chdir('..')
print('Generated total {0} mb of image data'.format(total_size/(1024*1024)))

