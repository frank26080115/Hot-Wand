"""Locate and rectify the OLED fiducial in a full-resolution camera still.

The reusable 3x3 matrix maps coordinates in the 256x1024 template to pixels
in the EXIF-oriented full camera image. Image matching may blur the template
and search image to bridge OLED pixel gaps; the saved crop and scores always
come from the original, unblurred still.
"""

from __future__ import annotations

from dataclasses import dataclass
from itertools import permutations
import json
from pathlib import Path

try:
    import cv2
    import numpy as np
    from PIL import Image, ImageOps
except ImportError as exc:
    raise RuntimeError("Fiducial mode requires Pillow, numpy and opencv-python") from exc


TEMPLATE_PATH = Path(__file__).resolve().parents[2] / "art" / "fiducial_x8.png"
MATCH_MINIMUM = 0.50
SEARCH_MAX_DIMENSION = 1000


@dataclass(frozen=True)
class Alignment:
    output_to_source: np.ndarray
    source_size: tuple[int, int]
    template_size: tuple[int, int]
    match_score: float
    search_zone: tuple[int, int, int, int]


def load_calibration(path: Path) -> dict:
    """Read the versioned JSON recipe for use by another capture script."""
    recipe = json.loads(path.expanduser().read_text(encoding="utf-8"))
    if recipe.get("schema") != "hot-wand-rx0-fiducial-v1":
        raise RuntimeError("Unsupported fiducial calibration JSON schema")
    return recipe


def alignment_from_calibration(recipe: dict) -> Alignment:
    """Rebuild a crop/deskew transform without running template search again."""
    matrix = np.asarray(recipe["output_to_source"], dtype=np.float64)
    source_size = tuple(int(value) for value in recipe["source_size"])
    output_size = tuple(int(value) for value in recipe["output_size"])
    if matrix.shape != (3, 3) or not np.all(np.isfinite(matrix)) or abs(np.linalg.det(matrix)) < 1e-10:
        raise RuntimeError("Calibration contains an invalid perspective transform")
    if len(source_size) != 2 or len(output_size) != 2 or min(*source_size, *output_size) <= 0:
        raise RuntimeError("Calibration contains invalid image dimensions")
    return Alignment(matrix, source_size, output_size, float(recipe["match_score"]), tuple(recipe["search_zone"]))


def load_oriented_bgr(path: Path) -> np.ndarray:
    """Read the full JPEG in the orientation used by the saved transform."""
    with Image.open(path) as image:
        rgb = np.asarray(ImageOps.exif_transpose(image).convert("RGB"))
    return cv2.cvtColor(rgb, cv2.COLOR_RGB2BGR)


def load_template(path: Path = TEMPLATE_PATH) -> np.ndarray:
    with Image.open(path) as image:
        # Alpha is composited over black, like the OLED background.
        rgba = ImageOps.exif_transpose(image).convert("RGBA")
        black = Image.new("RGBA", rgba.size, (0, 0, 0, 255))
        return cv2.cvtColor(np.asarray(Image.alpha_composite(black, rgba).convert("RGB")), cv2.COLOR_RGB2GRAY)


def _search_zone(image: np.ndarray, crop_to: int | None) -> tuple[np.ndarray, tuple[int, int, int, int]]:
    height, width = image.shape[:2]
    if crop_to is None:
        return image, (0, 0, width, height)
    if crop_to > min(width, height):
        raise RuntimeError(f"--crop-to {crop_to} exceeds the oriented {width} x {height} still")
    left = (width - crop_to) // 2
    top = (height - crop_to) // 2
    return image[top : top + crop_to, left : left + crop_to], (left, top, crop_to, crop_to)


def _rotated_template(template: np.ndarray, height: int, angle: float) -> tuple[np.ndarray, np.ndarray]:
    source_height, source_width = template.shape
    width = max(8, round(source_width * height / source_height))
    resized = cv2.resize(template, (width, height), interpolation=cv2.INTER_AREA)
    rotation = cv2.getRotationMatrix2D((width / 2, height / 2), angle, 1)
    cosine, sine = abs(rotation[0, 0]), abs(rotation[0, 1])
    bound_width = int(np.ceil(width * cosine + height * sine))
    bound_height = int(np.ceil(width * sine + height * cosine))
    rotation[0, 2] += (bound_width - width) / 2
    rotation[1, 2] += (bound_height - height) / 2
    rotated = cv2.warpAffine(resized, rotation, (bound_width, bound_height), flags=cv2.INTER_LINEAR)
    scaled = np.diag([width / source_width, height / source_height, 1.0])
    affine = np.vstack((rotation, [0.0, 0.0, 1.0])) @ scaled
    return rotated, affine


def _candidate_matches(search: np.ndarray, template: np.ndarray, heights: list[int], angles: list[float]):
    matches = []
    for height in heights:
        for angle in angles:
            rotated, affine = _rotated_template(template, height, angle)
            if rotated.shape[0] > search.shape[0] or rotated.shape[1] > search.shape[1]:
                continue
            # A modest search-only blur merges the dark gaps between OLED pixels.
            blurred = cv2.GaussianBlur(rotated, (0, 0), 1.2)
            result = cv2.matchTemplate(search, blurred, cv2.TM_CCOEFF_NORMED)
            _, score, _, location = cv2.minMaxLoc(result)
            if np.isfinite(score):
                matches.append((float(score), location, affine, height, angle))
    return sorted(matches, key=lambda match: match[0], reverse=True)


def _corner_block_alignment(full_bgr: np.ndarray, initial: np.ndarray) -> list[np.ndarray]:
    """Use the four bright 12x12-pixel corner blocks to fix perspective.

    The initial template match only needs to land near the display. Each
    corner block is segmented in its own predicted neighborhood, which avoids
    treating unrelated bright objects elsewhere in the photograph as markers.
    """
    gray = cv2.cvtColor(full_bgr, cv2.COLOR_BGR2GRAY)
    inverse = np.linalg.inv(initial)
    landmarks = [
        (0, 0, 0, 0),
        (160, 0, 255, 0),
        (160, 928, 255, 1023),
        (0, 928, 0, 1023),
    ]
    extreme_corners = []
    polygon_corners = []
    marker_template_points = []
    marker_photo_points = []
    for left, top, outer_x, outer_y in landmarks:
        template_box = np.float32([[[left, top], [left + 96, top], [left + 96, top + 96], [left, top + 96]]])
        predicted_box = cv2.perspectiveTransform(template_box, initial.astype(np.float32))[0]
        center = predicted_box.mean(axis=0)
        # The coarse match can be short by several OLED rows under projective
        # tilt, especially at the far end. Leave enough room for that error.
        radius = max(24, int(np.max(np.ptp(predicted_box, axis=0)) * 3.5))
        x0 = max(0, int(center[0] - radius))
        y0 = max(0, int(center[1] - radius))
        x1 = min(gray.shape[1], int(center[0] + radius + 1))
        y1 = min(gray.shape[0], int(center[1] + radius + 1))
        if x1 - x0 < 12 or y1 - y0 < 12:
            return []
        roi = cv2.GaussianBlur(gray[y0:y1, x0:x1], (0, 0), 1)
        _, binary = cv2.threshold(roi, 0, 255, cv2.THRESH_BINARY | cv2.THRESH_OTSU)
        # Each 12x12-pixel marker is a checker pattern, and the photographed
        # OLED has additional dark gaps. Bridge those gaps before choosing the
        # outer marker contour; a tiny 3x3 close can select one tile instead.
        binary = cv2.morphologyEx(binary, cv2.MORPH_CLOSE, np.ones((11, 11), np.uint8))
        count, labels, stats, centroids = cv2.connectedComponentsWithStats(binary)
        expected_area = cv2.contourArea(predicted_box)
        if expected_area < 10:
            return []
        choices = []
        for index in range(1, count):
            area = stats[index, cv2.CC_STAT_AREA]
            distance = np.linalg.norm(centroids[index] + (x0, y0) - center)
            if area >= max(20, expected_area * 0.2) and distance < radius * 0.9:
                choices.append((distance / radius - min(area / expected_area, 2), index))
        if not choices:
            return []
        selected = min(choices)[1]
        component = np.uint8(labels == selected) * 255
        contours, _ = cv2.findContours(component, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        if not contours:
            return []
        contour = max(contours, key=cv2.contourArea)
        outline = contour.reshape(-1, 2).astype(np.float32) + np.float32([x0, y0])
        outline_in_template = cv2.perspectiveTransform(outline.reshape(1, -1, 2), inverse.astype(np.float32))[0]
        direction_x = -1 if outer_x == 0 else 1
        direction_y = -1 if outer_y == 0 else 1
        extreme = np.argmax(direction_x * outline_in_template[:, 0] + direction_y * outline_in_template[:, 1])
        extreme_corners.append(outline[extreme])
        hull = cv2.convexHull(contour)
        vertices = cv2.approxPolyDP(hull, 0.02 * cv2.arcLength(hull, True), True).reshape(-1, 2)
        if len(vertices) != 4:
            vertices = cv2.boxPoints(cv2.minAreaRect(hull))
        vertices = vertices.astype(np.float32) + np.float32([x0, y0])
        approximate = cv2.perspectiveTransform(vertices.reshape(1, -1, 2), inverse.astype(np.float32))[0]
        # Choose from four actual block corners. Choosing the nearest pixel of
        # the entire edge would bias a marker inward when coarse scale is low.
        nearest = np.argmin(np.sum((approximate - [outer_x, outer_y]) ** 2, axis=1))
        polygon_corners.append(vertices[nearest])
        expected = np.float32(
            [[left, top], [left + 95, top], [left + 95, top + 95], [left, top + 95]]
        )
        order = min(
            permutations(range(4)),
            key=lambda indexes: np.sum((approximate[list(indexes)] - expected) ** 2),
        )
        marker_template_points.extend(expected)
        marker_photo_points.extend(vertices[list(order)])
    source_corners = np.float32([[0, 0], [255, 0], [255, 1023], [0, 1023]])
    candidates = [cv2.getPerspectiveTransform(source_corners, np.float32(points)) for points in (extreme_corners, polygon_corners)]
    all_markers, _ = cv2.findHomography(
        np.float32(marker_template_points), np.float32(marker_photo_points), cv2.RANSAC, 3
    )
    if all_markers is not None:
        candidates.append(all_markers)
    return candidates


def find_fiducial(full_bgr: np.ndarray, template: np.ndarray, crop_to: int | None = None) -> Alignment:
    """Template-search a limited scene and refine the four-corner homography."""
    zone, (left, top, zone_width, zone_height) = _search_zone(full_bgr, crop_to)
    gray = cv2.cvtColor(zone, cv2.COLOR_BGR2GRAY)
    reduction = min(1.0, SEARCH_MAX_DIMENSION / max(gray.shape))
    working = cv2.resize(gray, None, fx=reduction, fy=reduction, interpolation=cv2.INTER_AREA)
    working = cv2.GaussianBlur(working, (0, 0), 1.2)

    minimum_height = max(80, round(working.shape[0] * 0.12))
    maximum_height = round(working.shape[0] * 0.96)
    if minimum_height >= maximum_height:
        raise RuntimeError("Search zone is too small to contain the fiducial")
    heights = sorted({round(value) for value in np.geomspace(minimum_height, maximum_height, 17)})
    coarse = _candidate_matches(working, template, heights, list(range(-90, 91, 10)))
    if not coarse:
        raise RuntimeError("No fiducial template fits inside the search zone")
    enlargement = np.diag([1 / reduction, 1 / reduction, 1.0])
    origin = np.array([[1, 0, left], [0, 1, top], [0, 0, 1]], dtype=np.float64)
    output_size = (template.shape[1], template.shape[0])
    reference_blurred = cv2.GaussianBlur(template, (0, 0), 1.5)

    # A cross can make a wrong orientation look plausible in a coarse match.
    # Refine several distinct orientations, then choose by full-frame match.
    seeds = []
    for candidate in coarse:
        if all(abs(candidate[4] - seed[4]) >= 15 for seed in seeds):
            seeds.append(candidate)
        if len(seeds) == 12:
            break
    best = None
    for seed in seeds:
        height, angle = seed[3], seed[4]
        # Perspective can make the coarse full-template correlation prefer a
        # size well short of the actual long edge. Include a wider bracket.
        fine_heights = sorted({round(height * factor) for factor in (0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.4)})
        fine = _candidate_matches(working, template, fine_heights, [angle + offset for offset in (-5, -2, 0, 2, 5)])
        if not fine:
            continue
        score, location, affine, _, _ = fine[0]
        placement = np.array([[1, 0, location[0]], [0, 1, location[1]], [0, 0, 1]], dtype=np.float64)
        initial = origin @ enlargement @ placement @ affine
        for block_matrix in _corner_block_alignment(full_bgr, initial):
            try:
                block_crop = cv2.warpPerspective(full_bgr, np.linalg.inv(block_matrix), output_size)
            except np.linalg.LinAlgError:
                continue
            block_gray = cv2.GaussianBlur(cv2.cvtColor(block_crop, cv2.COLOR_BGR2GRAY), (0, 0), 1.5)
            block_score = float(cv2.matchTemplate(block_gray, reference_blurred, cv2.TM_CCOEFF_NORMED)[0, 0])
            if np.isfinite(block_score) and block_score > score:
                initial, score = block_matrix, block_score
        if best is None or score > best[0]:
            best = (score, initial)
    if best is None or best[0] < 0.35:
        raise RuntimeError("Initial fiducial match is too weak; check framing, crop, display and exposure")
    score, initial = best

    # ECC sees a softly blurred working copy; final output stays unblurred.
    approximate = cv2.warpPerspective(full_bgr, np.linalg.inv(initial), output_size)
    approximate_gray = cv2.cvtColor(approximate, cv2.COLOR_BGR2GRAY)
    reference = reference_blurred.astype(np.float32) / 255
    moving = cv2.GaussianBlur(approximate_gray, (0, 0), 1.5).astype(np.float32) / 255
    correction = np.eye(3, dtype=np.float32)
    try:
        cv2.findTransformECC(
            reference,
            moving,
            correction,
            cv2.MOTION_HOMOGRAPHY,
            (cv2.TERM_CRITERIA_EPS | cv2.TERM_CRITERIA_COUNT, 80, 1e-5),
            None,
            5,
        )
        matrix = initial @ correction
        refined = cv2.warpPerspective(full_bgr, np.linalg.inv(matrix), output_size)
        refined_gray = cv2.GaussianBlur(cv2.cvtColor(refined, cv2.COLOR_BGR2GRAY), (0, 0), 1.5)
        refined_score = float(cv2.matchTemplate(refined_gray, cv2.GaussianBlur(template, (0, 0), 1.5), cv2.TM_CCOEFF_NORMED)[0, 0])
        if np.isfinite(refined_score) and refined_score >= score - 0.03:
            score = refined_score
        else:
            matrix = initial
    except cv2.error:
        matrix = initial

    if score < MATCH_MINIMUM:
        raise RuntimeError(
            f"Fiducial match {score:.3f} is below {MATCH_MINIMUM:.2f} after alignment; check framing and exposure"
        )

    corners = cv2.perspectiveTransform(
        np.array([[[0, 0], [output_size[0], 0], [output_size[0], output_size[1]], [0, output_size[1]]]], dtype=np.float32),
        matrix.astype(np.float32),
    )[0]
    if (
        not np.all(np.isfinite(corners))
        or cv2.contourArea(corners) < 1000
        or np.any(corners[:, 0] < 0)
        or np.any(corners[:, 1] < 0)
        or np.any(corners[:, 0] >= full_bgr.shape[1])
        or np.any(corners[:, 1] >= full_bgr.shape[0])
    ):
        raise RuntimeError("Fiducial corners fall outside the full still; widen the search crop or reframe the camera")
    return Alignment(matrix / matrix[2, 2], (full_bgr.shape[1], full_bgr.shape[0]), output_size, score, (left, top, zone_width, zone_height))


def apply_alignment(full_bgr: np.ndarray, alignment: Alignment) -> np.ndarray:
    if (full_bgr.shape[1], full_bgr.shape[0]) != alignment.source_size:
        raise RuntimeError(
            f"Still size {full_bgr.shape[1]} x {full_bgr.shape[0]} differs from calibration {alignment.source_size}"
        )
    return cv2.warpPerspective(
        full_bgr,
        np.linalg.inv(alignment.output_to_source),
        alignment.template_size,
        flags=cv2.INTER_LINEAR,
    )


def exposure_quality(rectified_bgr: np.ndarray, template: np.ndarray) -> dict[str, float]:
    """Score white/black histogram separation, rejecting clipped OLED whites."""
    gray = cv2.cvtColor(rectified_bgr, cv2.COLOR_BGR2GRAY)
    white = gray[template >= 245]
    black = gray[template <= 10]
    if white.size < 100 or black.size < 100:
        raise RuntimeError("Fiducial template lacks enough white or black pixels for exposure scoring")
    # Clipping in any color channel can erase the shape of a white OLED pixel
    # even when the grayscale conversion remains below 250.
    clipping = float(np.mean(np.max(rectified_bgr, axis=2)[template >= 245] >= 250))
    contrast = float(np.percentile(white, 50) - np.percentile(black, 50))
    return {"clipped_fraction": clipping, "contrast": contrast, "white_median": float(np.median(white))}


def focus_quality(rectified_bgr: np.ndarray, template: np.ndarray) -> float:
    """Measure unblurred edge sharpness at the expected fiducial boundaries."""
    gray = cv2.cvtColor(rectified_bgr, cv2.COLOR_BGR2GRAY)
    template_edges = cv2.Canny(template, 40, 120)
    mask = cv2.dilate(template_edges, np.ones((9, 9), np.uint8)) != 0
    laplacian = cv2.Laplacian(gray, cv2.CV_32F)
    return float(np.mean(np.square(laplacian[mask])))
