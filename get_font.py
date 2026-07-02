import urllib.request
import re
url = 'https://fonts.googleapis.com/css?family=Inconsolata'
req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0 (Windows NT 6.1; WOW64; rv:40.0) Gecko/20100101 Firefox/40.0'})
try:
    css = urllib.request.urlopen(req).read().decode('utf-8')
    links = re.findall(r'url\((.*?)\)', css)
    if links:
        urllib.request.urlretrieve(links[0], 'initrd/font.ttf')
        print("Downloaded TTF!")
    else:
        print("No TTF links found.")
except Exception as e:
    print(e)
