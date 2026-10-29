.pragma library

// Vector icon set authored on a 16x16 grid. `s` is stroked (round caps and
// joins), `f` is filled. Shapes are deliberately distinct so status never
// depends on colour alone.

const circle = "M2 8 A6 6 0 1 0 14 8 A6 6 0 1 0 2 8 Z";

function dot(cx, cy, r) {
    return "M" + (cx - r) + " " + cy + " A" + r + " " + r + " 0 1 0 " + (cx + r) + " " + cy
         + " A" + r + " " + r + " 0 1 0 " + (cx - r) + " " + cy + " Z";
}

const paths = {
    "check": { s: "M3.5 8.5 L6.5 11.5 L12.5 4.5" },
    "check-circle": { s: circle + " M5.4 8.2 L7.2 10 L10.7 6.1" },
    "x": { s: "M4 4 L12 12 M12 4 L4 12" },
    "circle-x": { s: circle + " M5.9 5.9 L10.1 10.1 M10.1 5.9 L5.9 10.1" },
    "circle-alert": { s: circle + " M8 4.9 L8 8.6", f: dot(8, 11, 0.95) },
    "circle-help": {
        s: circle + " M6.1 6.4 A1.95 1.95 0 1 1 8.9 8.15 C8.35 8.45 8 8.8 8 9.4 L8 9.5",
        f: dot(8, 11.6, 0.9)
    },
    "circle-minus": { s: circle + " M5.3 8 L10.7 8" },
    "triangle-alert": { s: "M8 2.2 L14.4 13.4 L1.6 13.4 Z M8 6.2 L8 9.3", f: dot(8, 11.4, 0.9) },
    "info": { s: circle + " M8 7.4 L8 11.1", f: dot(8, 5.1, 0.9) },
    "clock": { s: circle + " M8 4.8 L8 8 L10.3 9.4" },
    "ban": { s: circle + " M3.8 3.8 L12.2 12.2" },
    "download": { s: "M8 2.5 L8 10 M4.8 6.9 L8 10.1 L11.2 6.9 M3 13.5 L13 13.5" },
    "import": { s: "M8 2 L8 9.5 M5 6.6 L8 9.6 L11 6.6 M2.5 10.5 L2.5 13.5 L13.5 13.5 L13.5 10.5" },
    "export": { s: "M8 10 L8 2.5 M5 5.4 L8 2.4 L11 5.4 M2.5 10.5 L2.5 13.5 L13.5 13.5 L13.5 10.5" },
    "grip": {
        f: dot(6, 3.8, 1.15) + " " + dot(10, 3.8, 1.15) + " " + dot(6, 8, 1.15) + " "
         + dot(10, 8, 1.15) + " " + dot(6, 12.2, 1.15) + " " + dot(10, 12.2, 1.15)
    },
    "more": { f: dot(3.5, 8, 1.2) + " " + dot(8, 8, 1.2) + " " + dot(12.5, 8, 1.2) },
    "chevron-down": { s: "M4 6 L8 10 L12 6" },
    "chevron-up": { s: "M4 10 L8 6 L12 10" },
    "chevron-right": { s: "M6 4 L10 8 L6 12" },
    "arrow-up": { s: "M8 13 L8 3.2 M4 7.2 L8 3.2 L12 7.2" },
    "arrow-down": { s: "M8 3 L8 12.8 M4 8.8 L8 12.8 L12 8.8" },
    "plus": { s: "M8 3 L8 13 M3 8 L13 8" },
    "minus": { s: "M3 8 L13 8" },
    "search": { s: "M2.5 7 A4.5 4.5 0 1 0 11.5 7 A4.5 4.5 0 1 0 2.5 7 Z M10.3 10.3 L13.6 13.6" },
    "settings": {
        s: "M2.5 4.5 L4.5 4.5 M8 4.5 L13.5 4.5 M4.5 4.5 A1.75 1.75 0 1 0 8 4.5 A1.75 1.75 0 1 0 4.5 4.5 Z"
         + " M2.5 11.5 L8 11.5 M11.5 11.5 L13.5 11.5 M8 11.5 A1.75 1.75 0 1 0 11.5 11.5 A1.75 1.75 0 1 0 8 11.5 Z"
    },
    "refresh": { s: "M13 8 A5 5 0 1 1 11.6 4.5 M12.4 2.2 L12.4 5.2 L9.4 5.2" },
    "history": { s: "M3 8 A5 5 0 1 0 4.5 4.5 M2.6 2.4 L2.6 5.4 L5.6 5.4 M8 5.3 L8 8.2 L10 9.4" },
    "folder": {
        s: "M2 4.5 A1 1 0 0 1 3 3.5 L6.2 3.5 L7.7 5 L13 5 A1 1 0 0 1 14 6 L14 12 A1 1 0 0 1 13 13 L3 13 A1 1 0 0 1 2 12 Z"
    },
    "external": {
        s: "M9.5 2.5 L13.5 2.5 L13.5 6.5 M13.5 2.5 L7.5 8.5 M11.5 9.5 L11.5 13 L3 13 L3 4.5 L6.5 4.5"
    },
    "play": { f: "M5 2.8 L13.2 8 L5 13.2 Z" },
    "undo": { s: "M5.5 3.5 L2.5 6.5 L5.5 9.5 M2.5 6.5 L9.75 6.5 A3.5 3.5 0 0 1 9.75 13.5 L7 13.5" },
    "redo": { s: "M10.5 3.5 L13.5 6.5 L10.5 9.5 M13.5 6.5 L6.25 6.5 A3.5 3.5 0 0 0 6.25 13.5 L9 13.5" },
    "copy": {
        s: "M5.5 6.5 A1 1 0 0 1 6.5 5.5 L12.5 5.5 A1 1 0 0 1 13.5 6.5 L13.5 12.5 A1 1 0 0 1 12.5 13.5"
         + " L6.5 13.5 A1 1 0 0 1 5.5 12.5 Z M3.5 10.5 L3.5 3.5 A1 1 0 0 1 4.5 2.5 L10.5 2.5"
    },
    "trash": { s: "M2.5 4.5 L13.5 4.5 M6 4.5 L6 2.5 L10 2.5 L10 4.5 M4 4.5 L4.8 13.5 L11.2 13.5 L12 4.5 M6.8 7 L6.8 11 M9.2 7 L9.2 11" },
    "edit": { s: "M10.5 2.5 L13.5 5.5 L6 13 L3 13 L3 10 Z M9 4 L12 7" },
    "locate": { s: circle + " M8 0.8 L8 4.2 M8 11.8 L8 15.2 M0.8 8 L4.2 8 M11.8 8 L15.2 8" },
    "save": { s: "M3 2.5 L11 2.5 L13.5 5 L13.5 13.5 L2.5 13.5 L2.5 3 Z M5 2.5 L5 6 L10 6 L10 2.5 M5 13.5 L5 9.5 L11 9.5 L11 13.5" },
    "clipboard": { s: "M5.5 3 L3.5 3 L3.5 14 L12.5 14 L12.5 3 L10.5 3 M5.5 2 L10.5 2 L10.5 4.5 L5.5 4.5 Z" },
    "file": { s: "M4 1.8 L9.5 1.8 L12.5 4.8 L12.5 14.2 L4 14.2 Z M9.5 1.8 L9.5 4.8 L12.5 4.8" },
    "wrench": { s: "M10.5 2.3 A3.2 3.2 0 0 0 9.4 7.4 L2.8 14 L2 13.2 L8.6 6.6 A3.2 3.2 0 0 0 13.7 5.5 L11.6 6 L10 4.4 Z" },
    "layers": { s: "M8 2 L14 5.2 L8 8.4 L2 5.2 Z M2 8.2 L8 11.4 L14 8.2 M2 11 L8 14.2 L14 11" }
};

function strokePath(name) {
    const p = paths[name];
    return p && p.s ? p.s : "";
}

function fillPath(name) {
    const p = paths[name];
    return p && p.f ? p.f : "";
}
