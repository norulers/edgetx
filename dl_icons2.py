import urllib.request, os

base = 'https://www.skyraccoon.com/assets/images/airicons/opentx/x10png/'
dest = '/mnt/c/Users/norulers/Desktop/model_icons'
os.makedirs(dest, exist_ok=True)
count = 0

# Already have 5500-5551, now get 1-100
for i in range(1, 100):
    fid = '%05d' % i
    url = base + fid + '_x10.png'
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req, timeout=3) as resp:
            data = resp.read()
            if len(data) > 500:
                path = os.path.join(dest, fid + '.png')
                with open(path, 'wb') as f:
                    f.write(data)
                count += 1
    except:
        pass

print('Done:', count, 'icons in', dest)
