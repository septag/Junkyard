from PIL import Image, ImageDraw, ImageFont
import os

image_w = 1024
image_h = 1024
stroke_color = (255, 255, 255, 255)
bg_color = (0, 0, 0, 255)
max_size = 256*1024*1024

image = Image.new('RGBA', (image_w, image_h))
canvas = ImageDraw.Draw(image)
font = ImageFont.truetype("Impact.ttf", 512)

index:int = 1
total_size:int = 0
image_size = image_w*image_h*4

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

    canvas.rectangle([(0, 0), (image_w, image_h)], outline=stroke_color, fill=bg_color, width=50)
    canvas.text(((image_w - text_width)/2, (image_h - text_height*1.5)/2), text, font=font, fill=stroke_color)

    filename = text + ".png"
    image.save(filename, bitmap_format='png')
    print(filename)

    with open(text + '.asset', 'w') as meta_file:
        meta_file.write(meta_template)
        meta_file.close()

    index = index + 1
    total_size = total_size + image_size
    if total_size >= max_size:
        break

os.chdir('..')
print('Generated total {0} mb of image data'.format(total_size/(1024*1024)))

