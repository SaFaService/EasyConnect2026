from __future__ import annotations

from pathlib import Path

import numpy as np
from moviepy.editor import CompositeVideoClip, ImageClip, TextClip, VideoClip
from PIL import Image, ImageDraw, ImageFont

W, H = 1920, 1080
DURATION = 5.8
FPS = 30


def project_paths() -> tuple[Path, Path, Path]:
    root = Path(__file__).resolve().parents[1]
    logo = root.parent / "WebSiteComunicazione" / "assets" / "img" / "AntraluxLogo.png"
    cloud = root.parent / "WebSiteComunicazione" / "assets" / "img" / "AntraluxCloud.png"
    out = root / "assets" / "antralux_cloud_intro.webm"
    out.parent.mkdir(parents=True, exist_ok=True)
    return logo, cloud, out


def cloud_glow_clip() -> VideoClip:
    def make_cloud(t: float):
        img = Image.new("RGBA", (W, H), (0, 0, 0, 0))
        draw = ImageDraw.Draw(img)
        glow = int(70 + 45 * np.sin(t * 2.2))
        cx, cy = int(W * 0.60), int(H * 0.39)
        for r in range(0, 120, 6):
            alpha = max(0, glow - r)
            draw.ellipse(
                (cx - 220 + r, cy - 110 + r, cx + 220 - r, cy + 110 - r),
                outline=(120, 180, 230, alpha),
            )
        return np.array(img)

    return VideoClip(make_cloud, duration=DURATION)


def handwriting_mask_clip() -> VideoClip:
    def make_text_mask(t: float):
        progress = min(1.0, max(0.0, (t - 2.25) / 1.25))
        img = Image.new("L", (W, H), 0)
        draw = ImageDraw.Draw(img)
        font = None
        for name in ("segoesc.ttf", "arial.ttf"):
            p = Path("C:/Windows/Fonts") / name
            if p.exists():
                font = ImageFont.truetype(str(p), 122)
                break
        if font is None:
            font = ImageFont.load_default()
        x, y = int(W * 0.50), int(H * 0.49)
        draw.text((x, y), "Cloud", fill=255, font=font)

        width = int(progress * 620)
        crop = img.crop((x, y, x + width, y + 190))
        base = Image.new("L", (W, H), 0)
        base.paste(crop, (x, y))
        return np.array(base)

    return VideoClip(make_text_mask, ismask=True, duration=DURATION)


def main() -> None:
    logo_path, cloud_path, output = project_paths()

    logo = (
        ImageClip(str(logo_path))
        .set_duration(DURATION)
        .resize(height=250)
        .set_position(("center", int(H * 0.34)))
        .crossfadein(0.9)
        .set_end(3.25)
    )

    cloud_logo = (
        ImageClip(str(cloud_path))
        .set_duration(DURATION)
        .resize(height=250)
        .set_position(("center", int(H * 0.34)))
        .set_start(2.65)
        .crossfadein(0.45)
    )

    text = (
        TextClip("Cloud", fontsize=122, color="white", font="Arial-Italic")
        .set_position((int(W * 0.50), int(H * 0.49)))
        .set_duration(DURATION)
        .set_mask(handwriting_mask_clip())
    )

    tagline = (
        TextClip("Remote Air Management System", fontsize=52, color="white", font="Arial")
        .set_position(("center", int(H * 0.73)))
        .set_start(4.0)
        .crossfadein(0.75)
    )

    final = CompositeVideoClip(
        [cloud_glow_clip(), logo, cloud_logo, text, tagline],
        size=(W, H),
    )

    final.write_videofile(
        str(output),
        codec="libvpx-vp9",
        fps=FPS,
        preset="medium",
        threads=4,
        ffmpeg_params=["-pix_fmt", "yuva420p"],
    )
    print(f"Creato: {output}")


if __name__ == "__main__":
    main()
