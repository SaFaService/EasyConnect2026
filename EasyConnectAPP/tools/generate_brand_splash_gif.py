from __future__ import annotations

import math
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def clamp(value: float, min_value: float = 0.0, max_value: float = 1.0) -> float:
    return max(min_value, min(max_value, value))


def smoothstep(value: float) -> float:
    x = clamp(value)
    return x * x * (3.0 - 2.0 * x)


def phase(progress: float, start: float, end: float) -> float:
    if end <= start:
        return 1.0
    return clamp((progress - start) / (end - start))


def fit(image: Image.Image, max_w: int, max_h: int) -> Image.Image:
    im = image.copy()
    im.thumbnail((max_w, max_h), Image.Resampling.LANCZOS)
    return im


def load_font(size: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = [
        Path("C:/Windows/Fonts/segoesc.ttf"),
        Path("C:/Windows/Fonts/seguisb.ttf"),
        Path("C:/Windows/Fonts/arial.ttf"),
    ]
    for path in candidates:
        if path.exists():
            return ImageFont.truetype(str(path), size=size)
    return ImageFont.load_default()


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    logo_path = root.parent / "WebSiteComunicazione" / "assets" / "img" / "AntraluxLogo.png"
    cloud_path = root.parent / "WebSiteComunicazione" / "assets" / "img" / "AntraluxCloud.png"
    out_path = root / "assets" / "splash_preview.gif"
    out_path.parent.mkdir(parents=True, exist_ok=True)

    logo = Image.open(logo_path).convert("RGBA")
    cloud = Image.open(cloud_path).convert("RGBA")

    width, height = 940, 520
    card = (40, 35, width - 40, height - 35)
    logo_box = (106, 155, width - 106, 373)

    logo_fit = fit(logo, logo_box[2] - logo_box[0], logo_box[3] - logo_box[1])
    cloud_fit = fit(cloud, logo_box[2] - logo_box[0], logo_box[3] - logo_box[1])

    frames: list[Image.Image] = []
    fps = 25
    duration_s = 3.9
    total = int(fps * duration_s)
    font = load_font(52)

    for i in range(total):
        progress = i / float(max(1, total - 1))
        bg_fade = smoothstep(phase(progress, 0.0, 0.10))
        logo_in = smoothstep(phase(progress, 0.05, 0.22))
        cloud_pop = smoothstep(phase(progress, 0.30, 0.50))
        handwriting = smoothstep(phase(progress, 0.45, 0.78))
        final_alpha = smoothstep(phase(progress, 0.62, 0.94))

        frame = Image.new("RGBA", (width, height), (239, 245, 251, int(255 * bg_fade)))
        draw = ImageDraw.Draw(frame, "RGBA")
        draw.rounded_rectangle(card, radius=24, fill=(255, 255, 255, 235), outline=(31, 86, 124, 28), width=1)

        x = logo_box[0] + ((logo_box[2] - logo_box[0]) - logo_fit.width) // 2
        y = logo_box[1] + ((logo_box[3] - logo_box[1]) - logo_fit.height) // 2

        if logo_in > 0:
            temp = logo_fit.copy()
            temp.putalpha(int(255 * logo_in * (1.0 - 0.88 * final_alpha)))
            frame.alpha_composite(temp, (x, y))

        if final_alpha > 0:
            split = int(cloud_fit.width * 0.62)

            left = cloud_fit.crop((0, 0, split, cloud_fit.height))
            left.putalpha(int(255 * final_alpha * 0.92))
            frame.alpha_composite(left, (x, y))

            right = cloud_fit.crop((split, 0, cloud_fit.width, cloud_fit.height))
            reveal = int((cloud_fit.width - split) * handwriting)
            if reveal > 0:
                right = right.crop((0, 0, reveal, right.height))
                right.putalpha(int(255 * final_alpha * 0.96))
                frame.alpha_composite(right, (x + split, y))

        if cloud_pop > 0:
            cx = logo_box[0] + int((logo_box[2] - logo_box[0]) * 0.72)
            cy = logo_box[1] + int((logo_box[3] - logo_box[1]) * 0.33)
            scale = 0.55 + 0.45 * cloud_pop
            circles = [
                (cx - int(35 * scale), cy + int(6 * scale), int(22 * scale)),
                (cx - int(10 * scale), cy - int(6 * scale), int(28 * scale)),
                (cx + int(18 * scale), cy + int(5 * scale), int(21 * scale)),
            ]
            for px, py, radius in circles:
                draw.ellipse(
                    (px - radius, py - radius, px + radius, py + radius),
                    fill=(122, 183, 224, int(210 * cloud_pop)),
                    outline=(255, 255, 255, int(180 * cloud_pop)),
                    width=1,
                )

        if handwriting > 0:
            text_layer = Image.new("RGBA", (width, height), (0, 0, 0, 0))
            text_draw = ImageDraw.Draw(text_layer, "RGBA")
            tx = logo_box[0] + int((logo_box[2] - logo_box[0]) * 0.63)
            ty = logo_box[1] + int((logo_box[3] - logo_box[1]) * 0.56)
            wobble = int(2.0 * (1.0 - handwriting) * math.sin(35.0 * progress))
            text_draw.text((tx, ty + wobble), "cloud", font=font, fill=(30, 111, 138, 255))

            bbox = text_draw.textbbox((tx, ty), "cloud", font=font)
            reveal_w = int((bbox[2] - bbox[0]) * handwriting)
            mask = Image.new("L", (width, height), 0)
            mask_draw = ImageDraw.Draw(mask)
            mask_draw.rectangle((bbox[0], bbox[1] - 8, bbox[0] + reveal_w, bbox[3] + 8), fill=255)
            frame = Image.composite(text_layer, frame, mask)

        frames.append(frame.convert("P", palette=Image.Palette.ADAPTIVE))

    frames[0].save(
        out_path,
        save_all=True,
        append_images=frames[1:],
        duration=int(1000 / fps),
        loop=0,
        optimize=False,
    )
    print(f"GIF generata: {out_path}")


if __name__ == "__main__":
    main()
