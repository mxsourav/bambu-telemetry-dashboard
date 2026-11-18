import urllib.request

pixels = bytearray(160 * 128 * 2)
# Make a gradient or solid pattern
for i in range(160 * 128):
    pixels[i*2] = 0x00
    pixels[i*2+1] = 0xF8

boundary = '----WebKitFormBoundary7MA4YWxkTrZu0gW'
body = bytearray()
body.extend(f'--{boundary}\r\n'.encode('utf-8'))
body.extend(b'Content-Disposition: form-data; name="file"; filename="screen.bin"\r\n')
body.extend(b'Content-Type: application/octet-stream\r\n\r\n')
body.extend(pixels)
body.extend(f'\r\n--{boundary}--\r\n'.encode('utf-8'))

req = urllib.request.Request('http://192.168.0.154/api/display/image',
                            data=body,
                            headers={'Content-Type': f'multipart/form-data; boundary={boundary}'})
try:
    with urllib.request.urlopen(req, timeout=5) as resp:
        print('Upload status:', resp.status, resp.read().decode())
except Exception as e:
    print('Upload error:', e)
