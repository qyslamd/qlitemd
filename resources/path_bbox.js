
globalThis.__pathBBox = (function () {
  const CMD_CHARS = "MmLlHhVvCcSsQqTtAaZz";
  const ARGC = {M:2,L:2,T:2,H:1,V:1,C:6,S:4,Q:4,A:7,Z:0};
  const NUM_RE = /-?\d*\.\d+(?:[eE][-+]?\d+)?|-?\d+(?:[eE][-+]?\d+)?/y;

  function arcExtrema(x1, y1, rx, ry, rotDeg, largeArc, sweep, x2, y2) {
    if (rx === 0 || ry === 0) return [[x1, y1], [x2, y2]];
    const phi = ((rotDeg % 360) * Math.PI) / 180;
    const cosP = Math.cos(phi), sinP = Math.sin(phi);
    const dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
    const x1p = cosP * dx + sinP * dy;
    const y1p = -sinP * dx + cosP * dy;
    const rxsq = rx * rx, rysq = ry * ry;
    const num = rxsq * rysq - rxsq * y1p * y1p - rysq * x1p * x1p;
    const denom = rxsq * y1p * y1p + rysq * x1p * x1p;
    if (denom === 0) return [[x1, y1], [x2, y2]];
    let co = Math.sqrt(Math.max(0, num / denom));
    if (largeArc === sweep) co = -co;
    const cxp = (co * rx * y1p) / ry;
    const cyp = (-co * ry * x1p) / rx;
    const cx = cosP * cxp - sinP * cyp + (x1 + x2) / 2;
    const cy = sinP * cxp + cosP * cyp + (y1 + y2) / 2;

    function angle(ux, uy, vx, vy) {
      const d = Math.hypot(ux, uy) * Math.hypot(vx, vy);
      if (d === 0) return 0;
      let c = (ux * vx + uy * vy) / d;
      c = Math.max(-1, Math.min(1, c));
      const a = Math.acos(c);
      return ux * vy - uy * vx < 0 ? -a : a;
    }

    let theta1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
    let dtheta = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
    if (!sweep && dtheta > 0) dtheta -= 2 * Math.PI;
    else if (sweep && dtheta < 0) dtheta += 2 * Math.PI;
    const theta2 = theta1 + dtheta;

    const pts = [[x1, y1], [x2, y2]];
    const lo = Math.min(theta1, theta2), hi = Math.max(theta1, theta2);
    for (let k = 0; k < 4; k++) {
      let a = (k * Math.PI) / 2;
      while (a < lo) a += 2 * Math.PI;
      if (a <= hi) {
        pts.push([
          cx + rx * Math.cos(a) * cosP - ry * Math.sin(a) * sinP,
          cy + rx * Math.cos(a) * sinP + ry * Math.sin(a) * cosP,
        ]);
      }
    }
    return pts;
  }

  return function pathBBox(d) {
    if (!d) return { x: 0, y: 0, width: 0, height: 0 };

    const xs = [], ys = [];
    let cx = 0, cy = 0, startX = 0, startY = 0;
    let cmd = null;
    let i = 0;
    const n = d.length;
    let firstPairOfCmd = true;

    while (i < n) {
      const ch = d[i];
      if (CMD_CHARS.indexOf(ch) !== -1) {
        cmd = ch;
        firstPairOfCmd = true;
        i += 1;
        continue;
      }
      if (/\s/.test(ch) || ch === ",") { i += 1; continue; }
      if (cmd === null) { i += 1; continue; }
      if (cmd.toUpperCase() === "Z") {
        cx = startX; cy = startY;
        xs.push(cx); ys.push(cy);
        cmd = null;
        continue;
      }

      const argc = ARGC[cmd.toUpperCase()];
      const isRel = cmd === cmd.toLowerCase();
      const group = [];
      while (group.length < argc) {
        NUM_RE.lastIndex = i;
        const m = NUM_RE.exec(d);
        if (!m || m.index !== i) break;
        group.push(parseFloat(m[0]));
        i = NUM_RE.lastIndex;
        while (i < n && (/\s/.test(d[i]) || d[i] === ",")) i += 1;
      }
      if (group.length < argc) break; // malformed tail -- stop rather than misparse

      let effectiveCmd = cmd.toUpperCase();
      if (effectiveCmd === "M" && !firstPairOfCmd) effectiveCmd = "L";

      let nx, ny;
      if (effectiveCmd === "H") {
        nx = isRel ? cx + group[0] : group[0];
        ny = cy;
        xs.push(nx); ys.push(ny);
      } else if (effectiveCmd === "V") {
        nx = cx;
        ny = isRel ? cy + group[0] : group[0];
        xs.push(nx); ys.push(ny);
      } else if (effectiveCmd === "A") {
        let [rx, ry, rot, laf, sf, ex, ey] = group;
        nx = isRel ? cx + ex : ex;
        ny = isRel ? cy + ey : ey;
        rx = Math.abs(rx); ry = Math.abs(ry);
        const pts = arcExtrema(cx, cy, rx, ry, rot, laf, sf, nx, ny);
        for (const p of pts) { xs.push(p[0]); ys.push(p[1]); }
      } else {
        for (let k = 0; k < group.length; k += 2) {
          let px = group[k], py = group[k + 1];
          if (isRel) { px += cx; py += cy; }
          xs.push(px); ys.push(py);
        }
        nx = xs[xs.length - 1]; ny = ys[ys.length - 1];
      }

      cx = nx; cy = ny;
      if (effectiveCmd === "M") { startX = cx; startY = cy; }
      firstPairOfCmd = false;
    }

    if (xs.length === 0) return { x: 0, y: 0, width: 0, height: 0 };
    const minX = Math.min(...xs), maxX = Math.max(...xs);
    const minY = Math.min(...ys), maxY = Math.max(...ys);
    return { x: minX, y: minY, width: maxX - minX, height: maxY - minY };
  };
})();
