from PIL import Image, ImageDraw
import os

def generate_icons():
    # 512x512 master
    img512 = Image.new('RGB', (512, 512), (8, 8, 10))
    draw = ImageDraw.Draw(img512)

    top = (256, 108)
    bl = (108, 368)
    br = (404, 368)

    stroke_w = 46
    draw.line([top, br, bl, top], fill=(255, 255, 255), width=stroke_w, joint='curve')
    r = stroke_w // 2
    for pt in [top, bl, br]:
        draw.ellipse([pt[0]-r, pt[1]-r, pt[0]+r, pt[1]+r], fill=(255, 255, 255))

    # Save 512
    img512.save('web/icon-delta-512.png', optimize=True)
    img512.save('web/icon-512.png', optimize=True)

    # Save 192
    img192 = img512.resize((192, 192), Image.Resampling.LANCZOS)
    img192.save('web/icon-delta-192.png', optimize=True)
    img192.save('web/icon-192.png', optimize=True)

    svg = '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="512" height="512">
  <rect width="512" height="512" rx="112" fill="#08080a"/>
  <path d="M256 108 L404 368 L108 368 Z" fill="none" stroke="#FFFFFF" stroke-width="46" stroke-linejoin="round" stroke-linecap="round"/>
</svg>
'''
    with open('web/icon-delta.svg', 'w', encoding='utf-8') as f:
        f.write(svg)
    with open('web/icon.svg', 'w', encoding='utf-8') as f:
        f.write(svg)

    print("Icons generated successfully!")

if __name__ == '__main__':
    generate_icons()
