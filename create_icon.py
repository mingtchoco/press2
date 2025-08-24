from PIL import Image, ImageDraw, ImageFont
import os

# Create a 32x32 image with transparent background
sizes = [(16, 16), (32, 32), (48, 48), (64, 64)]
images = []

for size in sizes:
    # Create image with transparent background
    img = Image.new('RGBA', size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    # Draw green circle
    margin = size[0] // 8
    draw.ellipse([margin, margin, size[0]-margin, size[1]-margin], 
                 fill=(0, 200, 0, 255), outline=(0, 150, 0, 255))
    
    # Draw white 'E' in the center
    # Try to use a font, fallback to basic text if not available
    try:
        font_size = int(size[0] * 0.5)
        # Try to use Arial font
        try:
            font = ImageFont.truetype("arial.ttf", font_size)
        except:
            # Fallback to default font
            font = ImageFont.load_default()
    except:
        font = ImageFont.load_default()
    
    # Get text bounding box for centering
    text = "E"
    bbox = draw.textbbox((0, 0), text, font=font)
    text_width = bbox[2] - bbox[0]
    text_height = bbox[3] - bbox[1]
    
    # Calculate position to center the text
    x = (size[0] - text_width) // 2
    y = (size[1] - text_height) // 2 - bbox[1]
    
    # Draw the text
    draw.text((x, y), text, fill=(255, 255, 255, 255), font=font)
    
    images.append(img)

# Save the first size as PNG for reference
images[1].save('icon.png')

# Save all sizes as ICO
images[1].save('icon.ico', format='ICO', sizes=[(16, 16), (32, 32)])

print("Icon created successfully!")