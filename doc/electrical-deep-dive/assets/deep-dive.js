/* Shared behavior for both generated schematic guides. No build step required. */
"use strict";
(() => {
  const data = JSON.parse(document.getElementById("deep-dive-data").textContent);
  const workspace = document.getElementById("workspace");
  const surface = document.getElementById("schematic");
  const inspector = document.getElementById("inspector");
  const frame = document.getElementById("information");
  const picker = document.getElementById("target-picker");
  const status = document.getElementById("status");
  const help = document.getElementById("help");
  const reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
  const NS = "http://www.w3.org/2000/svg";
  const escape = text => String(text).replace(/[&<>"']/g, char => ({"&":"&amp;", "<":"&lt;", ">":"&gt;", '"':"&quot;", "'":"&#39;"})[char]);
  let selected = null;
  let hovered = null;
  let ready = false;
  let origin = null;
  let resizeFrame = null;
  let focusAfterResize = false;
  let restoringFocus = false;

  function setLayout() {
    workspace.dataset.layout = window.innerWidth < 1024 || window.innerHeight < 600
      ? (window.innerHeight >= window.innerWidth ? "portrait" : "landscape") : "desktop";
  }
  setLayout();

  if (!window.OpenSeadragon) {
    status.classList.remove("sr-only");
    status.textContent = "The schematic viewer could not load. Check that the assets directory was uploaded with this page.";
    return;
  }
  const viewer = OpenSeadragon({
    element: surface,
    tileSources: data.tileSource,
    showNavigationControl: false,
    showNavigator: false,
    drawer: "canvas",
    autoResize: false,
    minZoomImageRatio: 1,
    maxZoomPixelRatio: 2,
    visibilityRatio: 1,
    constrainDuringPan: true,
    homeFillsViewer: false,
    animationTime: reducedMotion.matches ? 0 : 0.25,
    gestureSettingsMouse: {clickToZoom: false, dblClickToZoom: false, flickEnabled: false},
    gestureSettingsTouch: {clickToZoom: false, dblClickToZoom: false, flickEnabled: false},
    gestureSettingsPen: {clickToZoom: false, dblClickToZoom: false, flickEnabled: false},
  });
  // Disable the library's overlapping keyboard bindings; the surface has our own.
  viewer.addHandler("canvas-key", event => { event.preventDefaultAction = true; });
  viewer.addHandler("canvas-key-press", event => { event.preventDefaultAction = true; });

  const svg = document.createElementNS(NS, "svg");
  svg.classList.add("hotspots");
  svg.setAttribute("aria-label", "Schematic components and section titles");
  const group = document.createElementNS(NS, "g");
  svg.append(group);
  surface.append(svg);
  const rectangles = data.zones.map(zone => {
    const rect = document.createElementNS(NS, "rect");
    const target = data.targets[zone.target];
    const [x, y, width, height] = zone.bounds;
    Object.entries({x, y, width, height, tabindex: 0, role: "button", "aria-label": target.label,
      "aria-pressed": "false", "data-target": target.id, "vector-effect": "non-scaling-stroke"})
      .forEach(([key, value]) => rect.setAttribute(key, value));
    rect.classList.add("hotspot");
    rect.addEventListener("keydown", event => {
      if (event.key === "Enter" || event.key === " ") {
        event.preventDefault();
        event.stopPropagation();
        activate(target.id, rect, zone.bounds);
        document.getElementById("inspector-close").focus();
      }
    });
    rect.addEventListener("focus", () => {
      if (ready && !restoringFocus) focusBounds(zone.bounds, false);
    });
    group.append(rect);
    return rect;
  });

  data.sections.forEach(sid => {
    const section = data.targets[sid];
    const optgroup = document.createElement("optgroup");
    optgroup.label = section.label;
    const sectionOption = new Option("About " + section.label, sid);
    optgroup.append(sectionOption);
    Object.values(data.targets).filter(target => target.parent === sid).forEach(target => {
      optgroup.append(new Option(target.label + (target.value ? " · " + target.value : ""), target.id));
    });
    picker.append(optgroup);
  });
  picker.addEventListener("change", () => { if (picker.value) activate(picker.value, picker); });

  function image() { return viewer.world.getItemAt(0); }
  function paint() {
    if (!ready) return;
    const topLeft = viewer.viewport.pixelFromPoint(image().imageToViewportCoordinates(0, 0), true);
    const bottomRight = viewer.viewport.pixelFromPoint(image().imageToViewportCoordinates(data.width, data.height), true);
    group.setAttribute("transform", `matrix(${(bottomRight.x - topLeft.x) / data.width} 0 0 ${(bottomRight.y - topLeft.y) / data.height} ${topLeft.x} ${topLeft.y})`);
  }
  function updateSelection() {
    rectangles.forEach(rect => {
      const active = rect.dataset.target === selected;
      rect.classList.toggle("selected", active);
      rect.classList.toggle("hover", rect.dataset.target === hovered);
      rect.setAttribute("aria-pressed", String(active));
    });
  }

  // Library constraints prevent losing the image; this stricter clamp also removes
  // edge overscroll. Letterboxed axes remain centered, with no possible panning.
  function clamp() {
    if (!ready) return;
    const viewport = viewer.viewport;
    const zoom = Math.max(viewport.getHomeZoom(), Math.min(viewport.getMaxZoom(), viewport.getZoom(true)));
    if (Math.abs(zoom - viewport.getZoom(true)) > 1e-8) viewport.zoomTo(zoom, null, true);
    const view = viewport.getBounds(true);
    const world = image().getBounds();
    const center = viewport.getCenter(true);
    const x = view.width >= world.width ? world.x + world.width / 2
      : Math.max(world.x + view.width / 2, Math.min(world.x + world.width - view.width / 2, center.x));
    const y = view.height >= world.height ? world.y + world.height / 2
      : Math.max(world.y + view.height / 2, Math.min(world.y + world.height - view.height / 2, center.y));
    if (Math.abs(x - center.x) > 1e-8 || Math.abs(y - center.y) > 1e-8) {
      viewport.panTo(new OpenSeadragon.Point(x, y), true);
    }
  }

  function focusBounds(bounds, context = true, section = false) {
    if (!ready) return;
    const [x, y, width, height] = bounds;
    const viewport = viewer.viewport;
    if (context && (workspace.dataset.layout !== "desktop" || section)) {
      const span = section ? Math.max(width, height) : Math.max(80 * data.pixelsPerMm, 5 * Math.max(width, height));
      const aspect = surface.clientWidth / surface.clientHeight;
      const w = section ? width : aspect >= 1 ? span : span * aspect;
      const h = section ? height : aspect >= 1 ? span / aspect : span;
      // Move immediately when selecting or changing layout. A pending camera
      // animation otherwise competes with resize/clamping and drifts off target.
      viewport.fitBounds(image().imageToViewportRectangle(x + width / 2 - w / 2, y + height / 2 - h / 2, w, h), true);
    } else {
      viewport.panTo(image().imageToViewportCoordinates(x + width / 2, y + height / 2), true);
    }
    viewport.applyConstraints();
  }

  function fillInformation(target) {
    const template = document.getElementById(target.entry);
    let content = template.innerHTML;
    if (target.parent) {
      const parent = data.targets[target.parent];
      content += `<footer class="parent-link"><a href="#${escape(parent.id)}" data-target="${escape(parent.id)}">About ${escape(parent.label)}</a></footer>`;
    }
    frame.title = target.label + " — explanation";
    // srcdoc keeps all content baked into the page, including when hosted in a subdirectory.
    frame.srcdoc = `<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><base href="${escape(document.baseURI)}"><link rel="stylesheet" href="${escape(new URL("assets/deep-dive.css", document.baseURI).href)}"></head><body class="explanation" data-target="${escape(target.id)}">${content}</body></html>`;
  }
  frame.addEventListener("load", () => {
    const doc = frame.contentDocument;
    if (!doc) return;
    doc.addEventListener("click", event => {
      const link = event.target.closest("a[data-target]");
      if (link && data.targets[link.dataset.target]) {
        event.preventDefault();
        activate(link.dataset.target, document.getElementById("inspector-close"));
      }
    });
    doc.addEventListener("keydown", event => {
      if (event.key === "Escape") { event.preventDefault(); closeInspector(); }
    });
  });

  function activate(id, source = null, clickedBounds = null) {
    const target = data.targets[id];
    if (!target || !ready) return;
    origin = source || origin;
    selected = id;
    picker.value = id;
    inspector.hidden = false;
    workspace.classList.add("has-inspector");
    document.getElementById("inspector-title").textContent = target.label;
    document.getElementById("inspector-group").textContent = target.group && target.group !== target.label ? target.group : "";
    fillInformation(target);
    updateSelection();
    status.textContent = "Showing " + target.label;
    // Resize synchronously before focusing: the chosen item must use the space
    // beside the dialog, not the old full-width viewport.
    resizeViewer(false);
    focusBounds(clickedBounds || target.bounds, true, target.kind === "section");
  }
  function closeInspector() {
    if (inspector.hidden) return;
    selected = null;
    picker.value = "";
    inspector.hidden = true;
    workspace.classList.remove("has-inspector");
    updateSelection();
    resizeViewer(false);
    status.textContent = "Explanation closed";
    restoringFocus = true;
    (origin && origin.isConnected ? origin : surface).focus({preventScroll: true});
    restoringFocus = false;
  }
  document.getElementById("inspector-close").addEventListener("click", closeInspector);
  document.getElementById("help-open").addEventListener("click", () => help.showModal());
  document.addEventListener("keydown", event => {
    if (event.key === "Escape" && !help.open) closeInspector();
  });

  function hitAt(position) {
    const point = image().viewportToImageCoordinates(viewer.viewport.pointFromPixel(position, true));
    return data.zones.filter(zone => {
      const [x, y, w, h] = zone.bounds;
      return point.x >= x && point.x <= x + w && point.y >= y && point.y <= y + h;
    }).sort((a, b) => {
      const distance = z => Math.hypot(point.x - z.bounds[0] - z.bounds[2] / 2, point.y - z.bounds[1] - z.bounds[3] / 2);
      return distance(a) - distance(b) || a.bounds[2] * a.bounds[3] - b.bounds[2] * b.bounds[3];
    })[0];
  }
  viewer.addHandler("canvas-click", event => {
    event.preventDefaultAction = true;
    if (!ready || !event.quick) return;
    const hit = hitAt(event.position);
    if (hit) activate(hit.target, surface, hit.bounds);
  });
  surface.addEventListener("pointermove", event => {
    if (!ready || event.buttons) return;
    const box = surface.getBoundingClientRect();
    const hit = hitAt(new OpenSeadragon.Point(event.clientX - box.left, event.clientY - box.top));
    const id = hit ? hit.target : null;
    if (hovered !== id) { hovered = id; updateSelection(); }
    surface.classList.toggle("over-hotspot", Boolean(hit));
  });
  surface.addEventListener("pointerleave", () => { hovered = null; updateSelection(); });

  function perform(action) {
    if (!ready) return;
    const vp = viewer.viewport;
    if (action === "fit") vp.goHome(reducedMotion.matches);
    else if (action === "zoom-in" || action === "zoom-out") vp.zoomBy(action === "zoom-in" ? 1.4 : 1 / 1.4);
    else {
      const bounds = vp.getBounds();
      const dx = action === "left" ? -0.18 : action === "right" ? 0.18 : 0;
      const dy = action === "up" ? -0.18 : action === "down" ? 0.18 : 0;
      vp.panBy(new OpenSeadragon.Point(dx * bounds.width, dy * bounds.height));
    }
    vp.applyConstraints();
  }
  document.querySelectorAll("[data-action]").forEach(button => button.addEventListener("click", () => perform(button.dataset.action)));
  surface.addEventListener("keydown", event => {
    const action = {ArrowLeft: "left", ArrowRight: "right", ArrowUp: "up", ArrowDown: "down", "+": "zoom-in", "=": "zoom-in", "-": "zoom-out", Home: "fit"}[event.key];
    if (action) { event.preventDefault(); perform(action); }
  });

  function resizeViewer(refocus) {
    if (!ready || !surface.clientWidth || !surface.clientHeight) return;
    viewer.viewport.resize(new OpenSeadragon.Point(surface.clientWidth, surface.clientHeight), true);
    viewer.forceRedraw();
    if (refocus && selected) focusBounds(data.targets[selected].bounds, true, data.targets[selected].kind === "section");
    clamp();
    paint();
  }
  new ResizeObserver(() => {
    cancelAnimationFrame(resizeFrame);
    resizeFrame = requestAnimationFrame(() => {
      resizeViewer(focusAfterResize);
      focusAfterResize = false;
    });
  }).observe(surface);
  window.addEventListener("resize", () => {
    setLayout();
    focusAfterResize = true;
  });
  viewer.addHandler("animation", () => { clamp(); paint(); });
  viewer.addHandler("update-viewport", paint);
  viewer.addHandler("open", () => {
    ready = true;
    resizeViewer(false);
    viewer.viewport.goHome(true);
    paint();
    status.textContent = "Schematic ready. Select a component or section to explore.";
    surface.dataset.ready = "true";
  });
  viewer.addHandler("open-failed", () => {
    status.classList.remove("sr-only");
    status.textContent = "The schematic could not load. Upload the complete assets and tiles directories alongside this page.";
  });
})();
