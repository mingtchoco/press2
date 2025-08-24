from PIL import Image, ImageDraw, ImageFont
import os

# Create a 32x32 image with transparent background
sizes = [(16, 16), (32, 32), (48, 48), (64, 64)]
images = []

for size in sizes:
    # Create image with transparent background
    img = Image.new('RGBA', size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    # Draw darker green square with rounded corners
    margin = size[0] // 12
    # Dark green background
    draw.rounded_rectangle([margin, margin, size[0]-margin, size[1]-margin], 
                           radius=size[0]//8,
                           fill=(0, 100, 0, 255), 
                           outline=(255, 255, 255, 255), 
                           width=1)
    
    # Draw bold white 'E' in the center
    # Try to use a font, fallback to basic text if not available
    try:
        font_size = int(size[0] * 0.6)  # Bigger font
        # Try to use Arial Bold font
        try:
            font = ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf", font_size)  # Arial Bold
        except:
            try:
                font = ImageFont.truetype("C:/Windows/Fonts/arial.ttf", font_size)
            except:
                # Fallback to default font but bigger
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
    
    # Draw the E with thick white color
    # Create a bold effect by drawing multiple times with slight offset
    for dx in [-1, 0, 1]:
        for dy in [-1, 0, 1]:
            draw.text((x+dx, y+dy), text, fill=(255, 255, 255, 255), font=font)
    
    images.append(img)

# Save the first size as PNG for reference
images[1].save('icon.png')

# Save all sizes as ICO
images[1].save('icon.ico', format='ICO', sizes=[(16, 16), (32, 32)])

print("Icon created successfully!")