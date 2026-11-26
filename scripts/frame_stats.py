import sys
from PIL import Image, ImageStat

image = Image.open(sys.argv[1]).convert("RGB")
stat = ImageStat.Stat(image)
mean = sum(stat.mean) / 3.0
std = max(stat.stddev)
print(f"{mean:.3f} {std:.3f}")
