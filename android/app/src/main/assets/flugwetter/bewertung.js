/* Flugtauglichkeit: Einstufung je Stunde und je Tag.
   Die Wetterseite zeigt sie als Zeile mit Matrix, die Karte färbt damit den Rand der Startplätze
   (die App rechnet dafür diese Datei unsichtbar mit denselben Daten). */

const RATE = { NONE: 0, RED: 1, YELLOW: 2, LIGHT: 3, GREEN: 4 };
const RATE_COLORS = ["#d9d9d9", "#d40000", "#ffe100", "#a5e37a", "#2fa82f"];
const RATE_NAMES = ["nicht bewertet", "rot – Störungswetter", "gelb – muss man schauen", "hellgrün – nur Abgleiter",
  "grün – fliegbar"];

const RATING = {
  windYellow: 20, windRed: 30,        // km/h Mittelwind am Start
  gustYellow: 30, gustRed: 40,        // km/h Böen am Start
  gustSpread: 20,                     // km/h Böe − Mittel, darüber rot
  calm: 4,                            // km/h, darunter zählt eine leicht falsche Richtung nicht
  wrongDeg: 90,                       // Grad neben der Startrichtung, ab dem es bei Wind rot ist
  upperYellow: 30, upperRed: 45,      // km/h in 700 hPa (etwa 3000 m)
  leeRed: 25,                         // km/h in 700 hPa von hinten
  rainRed: 0.2,                       // mm/h
  capeYellow: 500, capeRed: 1000,     // J/kg
  foehnYellow: 3, foehnRed: 6,        // hPa Druckunterschied über den Alpenhauptkamm
  foehnWind: 40,                      // km/h in 700 hPa von der anderen Seite des Kamms
  cloudAtStart: 70,                   // % Bedeckung auf Starthöhe: Start in den Wolken
  fogAbove: 300,                      // m: so weit über dem Modellgrund liegt der Start über dem Talnebel
  thermikMin: 300,                    // m Thermik über dem Start, darunter nur Abgleiter
  soarMin: 15, soarMax: 25, soarGust: 10, // km/h Soaringwind aus der Startrichtung, Böen höchstens so viel stärker
  xcThermik: 1500, xcUpper: 20, xcCape: 800, // Stern für Strecke
  window: 4, lightWindow: 2, starWindow: 3,   // Stunden am Stück für grün, hellgrün und den Stern am Symbol
};

const SNOW_CODES = [71, 73, 75, 77, 85, 86];

/* ---------- Startrichtungen ---------- */
// Sektoren (16) aus "SW, S, SO". Bei 8 Richtungen gilt je Richtung ein 45° breiter Sektor.
function siteSectors() {
  const idx = (SITE.dir || "").split(/[,;\s]+/).map(n => DIRS.indexOf(n.trim().toUpperCase().replace("E", "O")))
    .filter(i => i >= 0);
  const set = new Set(idx);
  if (idx.length && idx.every(i => i % 2 === 0)) idx.forEach(i => { set.add((i + 1) % 16); set.add((i + 15) % 16); });
  return set;
}
// Wie viele Sektoren neben einer Startrichtung, und Winkel zur nächsten Startrichtung (Sektormitte)
function dirOffset(sectors, wd) {
  const k = Math.round((((wd % 360) + 360) % 360) / 22.5) % 16;
  let n = 16, deg = 360;
  sectors.forEach(s => {
    const d = Math.min((k - s + 16) % 16, (s - k + 16) % 16);
    n = Math.min(n, d);
    let a = Math.abs((((wd % 360) + 360) % 360) - s * 22.5); if (a > 180) a = 360 - a;
    deg = Math.min(deg, a);
  });
  return { n, deg };
}

/* ---------- Föhn ---------- */
// Grobe Linie des Alpenhauptkamms (Länge, Breite) von West nach Ost
const RIDGE = [[5.8, 44.6], [6.9, 45.85], [7.9, 46.0], [8.6, 46.55], [9.4, 46.5], [10.4, 46.85], [11.0, 46.95],
  [12.3, 47.05], [13.5, 47.1], [14.5, 47.25], [15.9, 47.55]];
// Bezugspunkte: Westalpen Lugano/Zürich, Ostalpen Bozen/Innsbruck
const FOEHN_POINTS = { west: { south: [46.00, 8.95], north: [47.38, 8.54] }, east: { south: [46.50, 11.35], north: [47.27, 11.39] } };
function alpsSide(lat, lon) {
  if (lon < RIDGE[0][0] || lon > RIDGE[RIDGE.length - 1][0] || lat < 43.8 || lat > 48.3) return null;
  for (let i = 0; i < RIDGE.length - 1; i++) {
    const [x0, y0] = RIDGE[i], [x1, y1] = RIDGE[i + 1];
    if (lon >= x0 && lon <= x1) return lat >= y0 + (y1 - y0) * (lon - x0) / (x1 - x0) ? "north" : "south";
  }
  return null;
}
function foehnUrl() {
  const p = [FOEHN_POINTS.west.south, FOEHN_POINTS.west.north, FOEHN_POINTS.east.south, FOEHN_POINTS.east.north];
  return "https://api.open-meteo.com/v1/forecast?latitude=" + p.map(x => x[0]).join(",") + "&longitude=" +
    p.map(x => x[1]).join(",") + "&hourly=pressure_msl&timezone=auto&forecast_days=" + CONFIG.days;
}
// Druck je Stunde: { "2026-11-27T12:00": { west: [süd, nord], east: [süd, nord] } }
async function fetchFoehn() {
  const j = await (await fetch(foehnUrl())).json();
  if (!Array.isArray(j) || j.length !== 4) return null;
  const map = {};
  j[0].hourly.time.forEach((t, i) => {
    const v = k => j[k].hourly.pressure_msl[i];
    map[t] = { west: [v(0), v(1)], east: [v(2), v(3)] };
  });
  return map;
}

/* ---------- Stunde ---------- */
// Jedes Kriterium für sich, das schlechteste gilt. Ergebnis mit allen Werten für die Matrix.
function rateHour(r, foehn) {
  const s = r.start;
  if (!s || s.ws == null) return null;
  const c = [];
  const add = (label, value, level) => c.push({ label, value, level });
  const kmh = v => Math.round(v) + " km/h";

  add("Wind am Start", kmh(s.ws), s.ws > RATING.windRed ? RATE.RED : s.ws > RATING.windYellow ? RATE.YELLOW : RATE.GREEN);
  const spread = s.gust - s.ws;
  add("Böen", `${kmh(s.gust)} (+${Math.round(spread)})`,
    s.gust > RATING.gustRed || spread > RATING.gustSpread ? RATE.RED : s.gust > RATING.gustYellow ? RATE.YELLOW : RATE.GREEN);

  const sectors = siteSectors();
  let off = null;
  if (sectors.size) {
    off = dirOffset(sectors, s.wd);
    let lv;
    if (off.n === 0) lv = RATE.GREEN;
    else if (s.ws < RATING.calm) lv = off.n === 1 ? RATE.GREEN : RATE.YELLOW;
    else lv = off.deg > RATING.wrongDeg ? RATE.RED : RATE.YELLOW;
    add("Richtung", dirName(s.wd) + (off.n ? ` (${Math.round(off.deg)}° daneben)` : ""), lv);
  } else add("Richtung", dirName(s.wd) + " (Startrichtung unbekannt)", RATE.NONE);

  const up = r.levels.find(l => l.p === 700 && !l.surface && !l.hagl);
  if (up) {
    const lee = sectors.size && dirOffset(sectors, up.wd).deg > 90;
    const lv = up.ws > RATING.upperRed || (lee && up.ws > RATING.leeRed) ? RATE.RED : up.ws > RATING.upperYellow ? RATE.YELLOW : RATE.GREEN;
    add("Höhenwind 700 hPa", `${kmh(up.ws)} aus ${dirName(up.wd)}${lee ? " (von hinten, Lee)" : ""}`, lv);
  }

  const snow = SNOW_CODES.includes(r.code), thunder = r.code >= 95;
  add("Niederschlag", thunder ? "Gewitter" : snow ? "Schnee" : r.precip ? r.precip.toFixed(1) + " mm" : "keiner",
    thunder || snow || r.precip >= RATING.rainRed ? RATE.RED : r.precip > 0 ? RATE.YELLOW : RATE.GREEN);

  if (r.code === 45 || r.code === 48) {
    const aboveFog = SITE.alt > r.ground + RATING.fogAbove;
    add("Wolken", aboveFog ? "Nebel im Tal" : "Nebel am Start", aboveFog ? RATE.YELLOW : RATE.RED);
  } else {
    const p = prof(r, Math.max(SITE.alt, r.ground + 10));
    const inCloud = p && p.cc >= RATING.cloudAtStart;
    add("Wolken", inCloud ? `Start in den Wolken (${octas(p.cc)}/8)` : r.base ? `Basis ${r.base.lcl} m` : "–",
      inCloud ? RATE.YELLOW : RATE.GREEN);
  }

  const side = alpsSide(SITE.lat, SITE.lon), fp = foehn && foehn[r.time];
  if (side && fp) {
    const pair = SITE.lon < 10 ? fp.west : fp.east;
    if (pair[0] != null && pair[1] != null) {
      // Nordseite: Südföhn, wenn der Druck im Süden höher ist; Südseite umgekehrt.
      const dp = side === "north" ? pair[0] - pair[1] : pair[1] - pair[0];
      const across = up && up.ws > RATING.foehnWind &&
        (side === "north" ? up.wd >= 135 && up.wd <= 225 : up.wd >= 315 || up.wd <= 45);
      const lv = dp > RATING.foehnRed || across ? RATE.RED : dp > RATING.foehnYellow ? RATE.YELLOW : RATE.GREEN;
      add("Föhn", `Δp ${dp.toFixed(1)} hPa ${side === "north" ? "Süd–Nord" : "Nord–Süd"}${across ? ", starker Wind über den Kamm" : ""}`, lv);
    }
  }

  if (r.cape != null)
    add("Gewitterneigung", `CAPE ${Math.round(r.cape)} J/kg`, r.cape > RATING.capeRed ? RATE.RED : r.cape > RATING.capeYellow ? RATE.YELLOW : RATE.GREEN);

  let level = Math.min(...c.filter(x => x.level).map(x => x.level));
  const thermik = r.base ? r.base.thermik - SITE.alt : 0;
  const soaring = off && off.n === 0 && s.ws >= RATING.soarMin && s.ws <= RATING.soarMax && spread <= RATING.soarGust;
  const xc = thermik >= RATING.xcThermik && up && up.ws < RATING.xcUpper && (r.cape == null || r.cape <= RATING.xcCape);
  const notes = [];
  if (level === RATE.GREEN && thermik < RATING.thermikMin && !soaring) {
    level = RATE.LIGHT;
    notes.push(`Thermik nur ${Math.max(0, Math.round(thermik))} m über dem Start und kein Soaringwind: nur Abgleiter.`);
  }
  const star = level === RATE.GREEN ? (xc ? "Strecke" : soaring ? "Soaring" : null) : null;
  if (star === "Strecke") notes.push(`★ Strecke: Thermik ${Math.round(thermik)} m über dem Start, wenig Höhenwind.`);
  if (star === "Soaring") notes.push(`★ Soaring: gleichmäßiger Wind ${Math.round(s.ws)} km/h aus der Startrichtung.`);
  return { level, star, criteria: c, notes, thermik: Math.round(thermik) };
}

/* ---------- Tag ---------- */
// Stunden zwischen den bürgerlichen Dämmerungen eines Tages
function daylightHours(recs, day) {
  const [y, m, d] = day.split("-").map(Number);
  const tw = civilTwilight(y, m, d, SITE.lat, SITE.lon);
  const off = (recs.utcOffset || 0) / 60;
  const h0 = Math.floor((tw.dawn + off) / 60), h1 = Math.floor((tw.dusk + off) / 60);
  return recs.filter(r => r.date === day && r.hour >= h0 && r.hour <= h1);
}

// Bewertet alle Stunden eines Tages. Eine Stunde direkt neben einer roten wird höchstens gelb.
function rateDayHours(recs, day, foehn) {
  const hrs = daylightHours(recs, day);
  const rated = hrs.map(r => ({ rec: r, ...(rateHour(r, foehn) || { level: RATE.NONE, star: null, criteria: [], notes: [] }) }));
  const raw = rated.map(x => x.level);
  rated.forEach((x, i) => {
    if (x.level > RATE.YELLOW && (raw[i - 1] === RATE.RED || raw[i + 1] === RATE.RED)) {
      x.level = RATE.YELLOW; x.star = null; x.notes = [...x.notes, "Direkt neben einer roten Stunde."];
    }
  });
  return rated;
}

// Tagesfarbe ab |fromHour|: grün bei 4 h grün am Stück, hellgrün bei 2 h mindestens hellgrün,
// gelb bei 4 h mindestens gelb, sonst rot. Stern bei 3 Sternstunden am Stück.
function summarizeDay(rated, fromHour = 0) {
  const hrs = rated.filter(x => x.rec.hour >= fromHour);
  if (!hrs.length) return null;
  const run = test => {
    let best = 0, cur = 0, end = -1;
    hrs.forEach((x, i) => { cur = test(x) ? cur + 1 : 0; if (cur > best) { best = cur; end = i; } });
    return { len: best, from: end >= 0 ? hrs[end - best + 1].rec.hour : null, to: end >= 0 ? hrs[end].rec.hour + 1 : null };
  };
  const green = run(x => x.level >= RATE.GREEN), light = run(x => x.level >= RATE.LIGHT), yellow = run(x => x.level >= RATE.YELLOW);
  const stars = run(x => !!x.star);
  let level = RATE.RED, win = null;
  if (green.len >= RATING.window) { level = RATE.GREEN; win = green; }
  else if (light.len >= RATING.lightWindow) { level = RATE.LIGHT; win = light; }
  else if (yellow.len >= RATING.window) { level = RATE.YELLOW; win = yellow; }
  if (hrs.every(x => x.level === RATE.NONE)) level = RATE.NONE;
  return { level, star: stars.len >= RATING.starWindow, from: win && win.from, to: win && win.to };
}

// Für die Karte: der Rest von heute, nach Sonnenuntergang morgen
function rateForMap(recs, foehn) {
  const now = new Date(Date.now() + (recs.utcOffset || 0) * 1000);
  const today = now.toISOString().slice(0, 10), hour = now.getUTCHours();
  const t = summarizeDay(rateDayHours(recs, today, foehn), hour);
  if (t) return t;
  const tomorrow = new Date(now.getTime() + 86400000).toISOString().slice(0, 10);
  return summarizeDay(rateDayHours(recs, tomorrow, foehn)) || { level: RATE.NONE, star: false };
}
