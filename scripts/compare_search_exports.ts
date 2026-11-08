#!/usr/bin/env bun
/**
 * compare_search_exports.ts - compare a FindHelper CSV export against an Everything CSV export.
 *
 * WHY THIS EXISTS
 * ---------------
 * FindHelper and Everything describe the same NTFS volume with two different
 * schemas, so the two exports never match line for line:
 *
 *   Everything  : "Nom","Chemin","Taille","Date de modification"
 *                 -> name and PARENT FOLDER are separate columns, the full path
 *                    has to be re-assembled; size is raw bytes; date has seconds.
 *   FindHelper  : Filename,Size,Last Modified,Full Path,Extension,# Files
 *                 -> full path is a single column; size is human readable and
 *                    rounded ("1.07 GB"); date is truncated to the minute;
 *                    the file name is split into stem + extension; folders
 *                    carry a recursive size and a recursive file count.
 *
 * This script normalises both schemas to the same record shape and reports
 * every category of divergence, so the remaining differences can be attributed
 * to a real cause instead of a formatting artefact.
 *
 * USAGE
 * -----
 *   bun scripts/compare_search_exports.ts <findhelper.csv> <everything.csv> [options]
 *
 *   The two files may be given in any order: the format is detected from the
 *   header row.
 *
 * OPTIONS
 * -------
 *   --tz <IANA>      IANA time zone used for the DST cross-tab
 *                    (default: the time zone of the machine running the script;
 *                     pass the time zone of the *exported* machine, e.g. Europe/Paris)
 *   --json <path>    Also write the complete report as JSON.
 *   --max-list <n>   Maximum rows printed per section (default 40, 0 = unlimited).
 *   --fail-on-diff   Exit with code 1 when any difference is detected.
 *   --quiet          Print the summary only.
 *
 * EXIT CODES
 * ----------
 *   0  no difference (or differences only of a benign kind)
 *   1  differences found (with --fail-on-diff)
 *   2  usage / IO error
 */

const DEFAULT_MAX_LIST = 40;

// ---------------------------------------------------------------------------
// CLI
// ---------------------------------------------------------------------------

interface Options {
  files: string[];
  tz: string;
  jsonPath: string | null;
  maxList: number;
  failOnDiff: boolean;
  quiet: boolean;
}

function parseArgs(argv: string[]): Options {
  const opts: Options = {
    files: [],
    tz: Intl.DateTimeFormat().resolvedOptions().timeZone,
    jsonPath: null,
    maxList: DEFAULT_MAX_LIST,
    failOnDiff: false,
    quiet: false,
  };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i] as string;
    switch (arg) {
      case "--tz":
        opts.tz = argv[++i] ?? opts.tz;
        break;
      case "--json":
        opts.jsonPath = argv[++i] ?? null;
        break;
      case "--max-list": {
        const raw = argv[++i];
        if (raw !== undefined) opts.maxList = Number.parseInt(raw, 10) || 0;
        break;
      }
      case "--fail-on-diff":
        opts.failOnDiff = true;
        break;
      case "--quiet":
        opts.quiet = true;
        break;
      case "--help":
      case "-h":
        console.log(
          [
            "Usage: bun scripts/compare_search_exports.ts <findhelper.csv> <everything.csv> [options]",
            "",
            "  --tz <IANA>      time zone of the exported machine (default: system)",
            "  --json <path>    write the full report as JSON",
            "  --max-list <n>   rows printed per section (default 40, 0 = all)",
            "  --fail-on-diff   exit 1 when differences are found",
            "  --quiet          summary only",
          ].join("\n"),
        );
        process.exit(0);
        break;
      default:
        if (arg.startsWith("--")) {
          throw new Error(`Unknown option: ${arg}`);
        }
        opts.files.push(arg);
    }
  }
  if (opts.files.length !== 2) {
    throw new Error("Exactly two CSV files are required (FindHelper export + Everything export).");
  }
  return opts;
}

// ---------------------------------------------------------------------------
// CSV parsing (RFC 4180-ish: quoted fields, escaped quotes, CRLF)
// ---------------------------------------------------------------------------

/**
 * Split CSV text into records.
 *
 * The scanner jumps between the structural characters with indexOf/charCodeAt
 * instead of walking the text one character at a time: a 200k row export is
 * parsed ~6x faster that way, which matters when a sample grows to millions
 * of rows.
 */
function parseCsvText(text: string): string[][] {
  const records: string[][] = [];
  const n = text.length;
  let i = 0;
  let record: string[] = [];
  let field = "";
  let hasField = false;

  while (i < n) {
    const ch = text.charCodeAt(i);

    if (ch === 34 /* " */) {
      i++;
      const start = i;
      let end = text.indexOf('"', i);
      while (end !== -1 && text.charCodeAt(end + 1) === 34) {
        i = end + 2; // escaped quote ("" inside a quoted field)
        end = text.indexOf('"', i);
      }
      if (end === -1) {
        field += text.slice(start);
        i = n;
      } else {
        field += text.slice(start, end).replace(/""/g, '"');
        i = end + 1;
      }
      hasField = true;
      continue;
    }

    if (ch === 44 /* , */) {
      record.push(field);
      field = "";
      hasField = false;
      i++;
      continue;
    }

    if (ch === 10 /* \n */) {
      record.push(field);
      field = "";
      hasField = false;
      records.push(record);
      record = [];
      i++;
      continue;
    }

    if (ch === 13 /* \r */) {
      i++;
      continue;
    }

    // plain run up to the next structural character
    let end = i;
    while (end < n) {
      const c = text.charCodeAt(end);
      if (c === 34 || c === 44 || c === 10 || c === 13) break;
      end++;
    }
    field += text.slice(i, end);
    i = end;
  }

  if (field !== "" || hasField || record.length > 0) {
    record.push(field);
    records.push(record);
  }
  if (records.length > 0 && records[0]?.length) {
    records[0][0] = (records[0][0] ?? "").replace(/^\uFEFF/, "");
  }
  return records;
}

// ---------------------------------------------------------------------------
// Normalised record
// ---------------------------------------------------------------------------

interface SizeRange {
  lo: number;
  hi: number;
  unit: string;
  decimals: number;
}

interface Row {
  rawPath: string;
  key: string;
  name: string; // base name of the path
  parent: string;
  ext: string; // extension as exported (FindHelper column) or derived
  nameColumn: string; // file name as exported (FindHelper "Filename" / Everything "Nom")
  sizeBytes: number | null; // exact byte count when the export provides one
  sizeDisplay: string;
  sizeRange: SizeRange | null; // byte interval implied by a rounded display
  mtimeText: string;
  mtime: Date | null; // naive wall clock, interpreted as UTC for delta maths
  mtimeHasSeconds: boolean;
  isDir: boolean;
  isDirKnown: boolean; // false when the export has no type information
  fileCount: number | null;
}

const SIZE_UNITS: Record<string, number> = { B: 1, KB: 1024, MB: 1024 ** 2, GB: 1024 ** 3, TB: 1024 ** 4 };

function parseSizeDisplay(text: string): { value: number; unit: string; decimals: number } | null {
  const m = /^\s*([0-9]+(?:\.[0-9]+)?)\s*(B|KB|MB|GB|TB)\s*$/i.exec(text);
  if (!m) return null;
  const decimals = (m[1] as string).includes(".") ? ((m[1] as string).split(".")[1] as string).length : 0;
  return { value: Number.parseFloat(m[1] as string), unit: (m[2] as string).toUpperCase(), decimals };
}

function sizeRangeFromDisplay(text: string): SizeRange | null {
  const parsed = parseSizeDisplay(text);
  if (!parsed) return null;
  const mult = SIZE_UNITS[parsed.unit] ?? 1;
  // Half unit of the last printed digit: the export rounds, so the raw byte
  // count may legitimately sit anywhere inside the rounding interval.
  const half = parsed.decimals > 0 ? 0.5 / 10 ** parsed.decimals : 0;
  return {
    lo: Math.max(0, (parsed.value - half) * mult),
    hi: (parsed.value + half) * mult,
    unit: parsed.unit,
    decimals: parsed.decimals,
  };
}

function parseTimestamp(text: string): { date: Date | null; hasSeconds: boolean } {
  const m = /^\s*(\d{4})-(\d{2})-(\d{2})[ T](\d{2}):(\d{2})(?::(\d{2}))?\s*$/.exec(text);
  if (!m) return { date: null, hasSeconds: false };
  const [, y, mo, d, h, mi, s] = m;
  const date = new Date(Date.UTC(+y!, +mo! - 1, +d!, +h!, +mi!, s ? +s : 0));
  return { date: Number.isNaN(date.getTime()) ? null : date, hasSeconds: s !== undefined };
}

/** Split a file name into stem + extension the way FindHelper does (no leading-dot extension). */
export function splitName(name: string): { stem: string; ext: string } {
  const dot = name.lastIndexOf(".");
  if (dot <= 0) return { stem: name, ext: "" };
  return { stem: name.slice(0, dot), ext: name.slice(dot + 1) };
}

function normalizePath(path: string): string {
  let p = path.trim().replace(/\//g, "\\");
  p = p.replace(/\\+/g, "\\");
  if (p.length > 3) p = p.replace(/\\+$/, "");
  return p;
}

function pathKey(path: string): string {
  return normalizePath(path).toLowerCase();
}

function basename(path: string): string {
  const p = normalizePath(path);
  const idx = p.lastIndexOf("\\");
  return idx >= 0 ? p.slice(idx + 1) : p;
}

function parentPath(path: string): string {
  const p = normalizePath(path);
  const idx = p.lastIndexOf("\\");
  if (idx < 0) return "";
  if (idx === 2 && /^[A-Za-z]:/.test(p)) return p.slice(0, 2);
  return p.slice(0, idx);
}

/** Re-assemble the full path from Everything's "Nom" + "Chemin" columns. */
function joinEverythingPath(parent: string, name: string): string {
  const p = parent.trim().replace(/\//g, "\\");
  if (p === "") return name;
  const isDriveRoot = /^[A-Za-z]:$/.test(p) || /^[A-Za-z]:\\$/.test(p);
  const base = p.replace(/\\+$/, "");
  return `${base}\\${name}`;
}

// ---------------------------------------------------------------------------
// Format detection + parsing
// ---------------------------------------------------------------------------

export type ExportFormat = "findhelper" | "everything";

interface ParsedExport {
  format: ExportFormat;
  path: string;
  header: string[];
  rows: Row[];
}

const FINDHELPER_HEADER_HINTS = ["full path", "extension", "# files", "filename"];
const EVERYTHING_HEADER_HINTS = ["chemin", "nom", "dossier", "folder path"];

function normalizeHeader(h: string): string {
  return h
    .trim()
    .toLowerCase()
    .normalize("NFD")
    .replace(/[\u0300-\u036f]/g, "");
}

function pickColumn(header: string[], candidates: string[], fallbackIndex: number): number {
  const norm = header.map(normalizeHeader);
  for (const cand of candidates) {
    const idx = norm.findIndex((h) => h === cand);
    if (idx >= 0) return idx;
  }
  for (const cand of candidates) {
    const idx = norm.findIndex((h) => h.includes(cand));
    if (idx >= 0) return idx;
  }
  return fallbackIndex < header.length ? fallbackIndex : -1;
}

function detectFormat(header: string[]): ExportFormat {
  const norm = header.map(normalizeHeader);
  const findhelperScore = FINDHELPER_HEADER_HINTS.filter((h) => norm.some((c) => c === h || c.includes(h))).length;
  const everythingScore = EVERYTHING_HEADER_HINTS.filter((h) => norm.some((c) => c === h || c.includes(h))).length;
  if (findhelperScore > everythingScore) return "findhelper";
  if (everythingScore > findhelperScore) return "everything";
  // Last resort: a "Full Path" column means FindHelper.
  return norm.some((h) => h.includes("full path")) ? "findhelper" : "everything";
}

async function parseExport(path: string): Promise<ParsedExport> {
  const file = Bun.file(path);
  if (!(await file.exists())) {
    throw new Error(`File not found: ${path}`);
  }
  const records = parseCsvText(await file.text());
  const first = records.shift();
  if (!first) throw new Error(`Empty CSV: ${path}`);
  const header = first.map((h) => h.trim());
  const format = detectFormat(header);

  const rows: Row[] = [];
  for (const rec of records) {
    if (rec.length === 1 && (rec[0] as string).trim() === "") continue;
    const cell = (idx: number): string => (idx >= 0 && idx < rec.length ? (rec[idx] as string).trim() : "");
    let rawPath = "";
    let nameColumn = "";
    let sizeText = "";
    let mtimeText = "";
    let ext = "";
    let fileCountText = "";

    if (format === "findhelper") {
      const iPath = pickColumn(header, ["full path"], 3);
      const iName = pickColumn(header, ["filename"], 0);
      const iSize = pickColumn(header, ["size"], 1);
      const iMtime = pickColumn(header, ["last modified", "modified", "date"], 2);
      const iExt = pickColumn(header, ["extension"], 4);
      const iCount = pickColumn(header, ["# files", "files", "file count"], 5);
      rawPath = cell(iPath);
      nameColumn = cell(iName);
      sizeText = cell(iSize);
      mtimeText = cell(iMtime);
      ext = cell(iExt);
      fileCountText = cell(iCount);
    } else {
      const iName = pickColumn(header, ["nom", "name"], 0);
      const iParent = pickColumn(header, ["chemin", "folder path", "dossier", "parent", "path"], 1);
      const iSize = pickColumn(header, ["taille", "size"], 2);
      const iMtime = pickColumn(header, ["date de modification", "date modified", "modified", "date"], 3);
      nameColumn = cell(iName);
      rawPath = joinEverythingPath(cell(iParent), nameColumn);
      sizeText = cell(iSize);
      mtimeText = cell(iMtime);
    }

    if (rawPath === "" && nameColumn !== "") rawPath = nameColumn;

    const baseName = basename(rawPath) || nameColumn;
    const resolvedExt = ext !== "" ? ext : splitName(baseName).ext;

    // Directory detection:
    //  - FindHelper exposes it through the "# Files" column ("-" for files).
    //  - Everything has no type column: fall back to "has an extension => file",
    //    refined later by the parent/child cross-check.
    let isDir: boolean;
    let isDirKnown = true;
    if (format === "findhelper" && fileCountText !== "") {
      isDir = /^\d+$/.test(fileCountText.replace(/[,\s]/g, ""));
    } else {
      isDir = resolvedExt === "";
      isDirKnown = false;
    }

    const numericFileCount = /^\d+$/.test(fileCountText.replace(/[,\s]/g, ""))
      ? Number.parseInt(fileCountText.replace(/[,\s]/g, ""), 10)
      : null;

    const exactSize = /^\d+$/.test(sizeText) ? Number.parseInt(sizeText, 10) : null;
    const range = exactSize === null ? sizeRangeFromDisplay(sizeText) : null;
    const { date, hasSeconds } = parseTimestamp(mtimeText);

    rows.push({
      rawPath,
      key: pathKey(rawPath),
      name: baseName,
      parent: parentPath(rawPath),
      ext: resolvedExt,
      nameColumn,
      sizeBytes: exactSize,
      sizeDisplay: sizeText,
      sizeRange: range,
      mtimeText,
      mtime: date,
      mtimeHasSeconds: hasSeconds,
      isDir,
      isDirKnown,
      fileCount: numericFileCount,
    });
  }

  refineDirectoryFlags(rows);
  return { format, path, header, rows };
}

/**
 * Everything's export has no type column, so folder/file is inferred. Refine the
 * inference with the structure of the data itself: a path that is the parent of
 * another row is a folder, and a dot-file without extension is a file unless it
 * has children.
 */
function refineDirectoryFlags(rows: Row[]): void {
  const parents = new Set<string>();
  for (const r of rows) {
    if (r.parent) parents.add(pathKey(r.parent));
  }
  for (const r of rows) {
    if (r.isDirKnown) continue;
    if (parents.has(r.key)) {
      r.isDir = true;
      continue;
    }
    // ".git" style: a leading dot with no other dot is a dot-file, not a folder.
    if (r.name.startsWith(".") && !r.name.slice(1).includes(".")) {
      r.isDir = false;
      continue;
    }
    r.isDir = r.ext === "";
  }
}

// ---------------------------------------------------------------------------
// Time zone helpers
// ---------------------------------------------------------------------------

/** UTC offset in minutes of `timeZone` at the given instant. */
const offsetFormatterCache = new Map<string, Intl.DateTimeFormat>();

function offsetFormatter(timeZone: string): Intl.DateTimeFormat {
  let fmt = offsetFormatterCache.get(timeZone);
  if (!fmt) {
    fmt = new Intl.DateTimeFormat("en-US", {
      timeZone,
      hour12: false,
      year: "numeric",
      month: "2-digit",
      day: "2-digit",
      hour: "2-digit",
      minute: "2-digit",
      second: "2-digit",
    });
    offsetFormatterCache.set(timeZone, fmt);
  }
  return fmt;
}

function timeZoneOffsetMinutes(instant: Date, timeZone: string): number {
  const parts = offsetFormatter(timeZone).formatToParts(instant);
  const get = (t: string): number => Number(parts.find((p) => p.type === t)?.value ?? "0");
  const asUtc = Date.UTC(get("year"), get("month") - 1, get("day"), get("hour") % 24, get("minute"), get("second"));
  return Math.round((asUtc - instant.getTime()) / 60000);
}

function formatOffset(minutes: number): string {
  const sign = minutes < 0 ? "-" : "+";
  const abs = Math.abs(minutes);
  return `UTC${sign}${String(Math.floor(abs / 60)).padStart(2, "0")}:${String(abs % 60).padStart(2, "0")}`;
}

function formatDelta(minutes: number): string {
  if (minutes === 0) return "0";
  const sign = minutes > 0 ? "+" : "-";
  const abs = Math.abs(minutes);
  if (abs % 60 === 0) return `${sign}${abs / 60}h`;
  return `${sign}${abs}m`;
}

function floorToMinute(date: Date): Date {
  return new Date(Math.floor(date.getTime() / 60000) * 60000);
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

interface SizeMismatch {
  path: string;
  isDir: boolean;
  exactBytes: number;
  exactDisplay: string;
  roundedDisplay: string;
  roundedRange: SizeRange;
  roundedBytesApprox: number;
  relativeDelta: number;
}

interface DuplicateInfo {
  path: string;
  occurrences: number;
}

interface TimeMismatch {
  path: string;
  referenceText: string;
  otherText: string;
  deltaMinutes: number;
  localOffsetMinutes: number;
  impliedOffsetMinutes: number;
}

interface ExtraRowInfo {
  path: string;
  isDir: boolean;
  sizeDisplay: string;
  mtimeText: string;
  fileCount: number | null;
  parent: string;
  parentSeenInReference: boolean;
  nameSeenInReference: boolean;
}

interface Report {
  generatedAt: string;
  timeZone: string;
  currentOffsetMinutes: number;
  files: {
    findhelper: { path: string; header: string[]; rows: number; folders: number; files: number };
    everything: { path: string; header: string[]; rows: number; folders: number; files: number };
  };
  counts: {
    onlyInFindHelper: number;
    onlyInEverything: number;
    common: number;
    sizeCompared: number;
    sizeMatched: number;
    sizeMismatched: number;
    timeCompared: number;
    timeIdentical: number;
    timeDifferent: number;
  };
  onlyInFindHelper: ExtraRowInfo[];
  onlyInEverything: ExtraRowInfo[];
  sizeMismatches: SizeMismatch[];
  timeMismatches: TimeMismatch[];
  deltaHistogram: Record<string, number>;
  impliedOffsetHistogram: Record<string, number>;
  offsetByLocalOffset: Array<{ localOffsetMinutes: number; localOffset: string; deltas: Record<string, number> }>;
  nameMismatches: Array<{ path: string; findhelper: string; everything: string }>;
  duplicates: { findhelper: DuplicateInfo[]; everything: DuplicateInfo[] };
  secondsDropped: number;
  schemaNotes: string[];
}

function compare(findhelper: ParsedExport, everything: ParsedExport, tz: string): Report {
  const fhByKey = new Map<string, Row>();
  for (const r of findhelper.rows) if (!fhByKey.has(r.key)) fhByKey.set(r.key, r);
  const evByKey = new Map<string, Row>();
  for (const r of everything.rows) if (!evByKey.has(r.key)) evByKey.set(r.key, r);

  const onlyInFindHelper: ExtraRowInfo[] = [];
  const onlyInEverything: ExtraRowInfo[] = [];
  const sizeMismatches: SizeMismatch[] = [];
  const timeMismatches: TimeMismatch[] = [];
  const nameMismatches: Report["nameMismatches"] = [];
  const deltaHistogram: Record<string, number> = {};
  const impliedOffsetHistogram: Record<string, number> = {};
  const offsetByLocalOffset = new Map<number, Record<string, number>>();

  let sizeCompared = 0;
  let sizeMatched = 0;
  let timeCompared = 0;
  let timeIdentical = 0;
  let secondsDropped = 0;

  // Context used to annotate the extra rows.
  const evParents = new Set<string>();
  const evNames = new Set<string>();
  for (const r of everything.rows) {
    if (r.parent) evParents.add(pathKey(r.parent));
    evNames.add(r.name.toLowerCase());
  }

  const describeExtra = (r: Row): ExtraRowInfo => ({
    path: r.rawPath,
    isDir: r.isDir,
    sizeDisplay: r.sizeDisplay,
    mtimeText: r.mtimeText,
    fileCount: r.fileCount,
    parent: r.parent,
    parentSeenInReference: r.parent !== "" && evParents.has(pathKey(r.parent)),
    nameSeenInReference: evNames.has(r.name.toLowerCase()),
  });

  for (const [key, fh] of fhByKey) {
    const ev = evByKey.get(key);
    if (!ev) {
      onlyInFindHelper.push(describeExtra(fh));
      continue;
    }

    // --- size: one side prints a rounded size, the other raw bytes.
    // Whichever side carries the exact byte count is checked against the
    // rounding interval of the other side.
    const fhExact = fh.sizeBytes;
    const evExact = ev.sizeBytes;
    const roundedSide = fh.sizeRange !== null && evExact !== null ? "findhelper" : ev.sizeRange !== null && fhExact !== null ? "everything" : null;
    if (roundedSide === "findhelper" && evExact !== null && fh.sizeRange) {
      sizeCompared++;
      const inRange = evExact >= fh.sizeRange.lo && evExact <= fh.sizeRange.hi;
      if (inRange) {
        sizeMatched++;
      } else {
        const approx = (fh.sizeRange.lo + fh.sizeRange.hi) / 2;
        sizeMismatches.push({
          path: fh.rawPath,
          isDir: fh.isDir,
          exactBytes: evExact,
          exactDisplay: ev.sizeDisplay,
          roundedDisplay: fh.sizeDisplay,
          roundedRange: fh.sizeRange,
          roundedBytesApprox: Math.round(approx),
          relativeDelta: approx > 0 ? (evExact - approx) / approx : 0,
        });
      }
    } else if (roundedSide === "everything" && fhExact !== null && ev.sizeRange) {
      sizeCompared++;
      const inRange = fhExact >= ev.sizeRange.lo && fhExact <= ev.sizeRange.hi;
      if (inRange) {
        sizeMatched++;
      } else {
        const approx = (ev.sizeRange.lo + ev.sizeRange.hi) / 2;
        sizeMismatches.push({
          path: fh.rawPath,
          isDir: fh.isDir,
          exactBytes: fhExact,
          exactDisplay: fh.sizeDisplay,
          roundedDisplay: ev.sizeDisplay,
          roundedRange: ev.sizeRange,
          roundedBytesApprox: Math.round(approx),
          relativeDelta: approx > 0 ? (fhExact - approx) / approx : 0,
        });
      }
    } else if (fhExact !== null && evExact !== null) {
      sizeCompared++;
      if (fhExact === evExact) {
        sizeMatched++;
      } else {
        const approx = Math.max(fhExact, evExact);
        sizeMismatches.push({
          path: fh.rawPath,
          isDir: fh.isDir,
          exactBytes: evExact,
          exactDisplay: ev.sizeDisplay,
          roundedDisplay: fh.sizeDisplay,
          roundedRange: { lo: Math.min(fhExact, evExact), hi: Math.max(fhExact, evExact), unit: "B", decimals: 0 },
          roundedBytesApprox: fhExact,
          relativeDelta: approx > 0 ? (evExact - fhExact) / approx : 0,
        });
      }
    }

    // --- timestamp -------------------------------------------------------
    if (fh.mtime && ev.mtime) {
      timeCompared++;
      // Both columns are minute-resolution for FindHelper; compare floored values
      // so that the second-level truncation does not leak into the delta.
      const fhMin = floorToMinute(fh.mtime);
      const evMin = floorToMinute(ev.mtime);
      const deltaMinutes = Math.round((fhMin.getTime() - evMin.getTime()) / 60000);
      const localOffset = timeZoneOffsetMinutes(evMin, tz);
      const implied = localOffset + deltaMinutes;
      deltaHistogram[formatDelta(deltaMinutes)] = (deltaHistogram[formatDelta(deltaMinutes)] ?? 0) + 1;
      impliedOffsetHistogram[formatOffset(implied)] = (impliedOffsetHistogram[formatOffset(implied)] ?? 0) + 1;
      const bucket = offsetByLocalOffset.get(localOffset) ?? {};
      bucket[formatDelta(deltaMinutes)] = (bucket[formatDelta(deltaMinutes)] ?? 0) + 1;
      offsetByLocalOffset.set(localOffset, bucket);
      if (deltaMinutes === 0) {
        timeIdentical++;
      } else {
        timeMismatches.push({
          path: fh.rawPath,
          referenceText: ev.mtimeText,
          otherText: fh.mtimeText,
          deltaMinutes,
          localOffsetMinutes: localOffset,
          impliedOffsetMinutes: implied,
        });
      }
      if (ev.mtimeHasSeconds && !fh.mtimeHasSeconds) secondsDropped++;
    }

    // --- name decomposition: FindHelper "Filename" + "Extension" vs Everything "Nom".
    if (fh.nameColumn && ev.name) {
      const fhFullName = fh.ext ? `${fh.nameColumn}.${fh.ext}` : fh.nameColumn;
      if (fhFullName.toLowerCase() !== ev.name.toLowerCase()) {
        nameMismatches.push({ path: fh.rawPath, findhelper: fhFullName, everything: ev.name });
      }
    }
  }
  for (const [key, ev] of evByKey) {
    if (!fhByKey.has(key)) onlyInEverything.push(describeExtra(ev));
  }

  const count = (rows: Row[], dir: boolean): number => rows.filter((r) => r.isDir === dir).length;
  const currentOffset = timeZoneOffsetMinutes(new Date(), tz);

  const findDuplicates = (rows: Row[]): DuplicateInfo[] => {
    const seen = new Map<string, number>();
    for (const r of rows) seen.set(r.key, (seen.get(r.key) ?? 0) + 1);
    return [...seen.entries()]
      .filter(([, n]) => n > 1)
      .map(([key, n]) => ({ path: rows.find((r) => r.key === key)?.rawPath ?? key, occurrences: n }));
  };

  const schemaNotes: string[] = [
    "Everything splits a row into NAME + PARENT FOLDER; FindHelper stores the full path in one column.",
    "Everything reports the raw byte count; FindHelper reports a rounded human-readable size (B/KB/MB/GB, binary units).",
    "Everything keeps seconds in the date; FindHelper truncates to the minute.",
    "FindHelper splits the file name into stem + extension and adds a recursive '# Files' count for folders; Everything has no equivalent column.",
    "FindHelper folder sizes are recursive aggregates; Everything folder sizes are recursive too but computed by a different engine, so they can disagree.",
  ];

  return {
    generatedAt: new Date().toISOString(),
    timeZone: tz,
    currentOffsetMinutes: currentOffset,
    files: {
      findhelper: {
        path: findhelper.path,
        header: findhelper.header,
        rows: findhelper.rows.length,
        folders: count(findhelper.rows, true),
        files: count(findhelper.rows, false),
      },
      everything: {
        path: everything.path,
        header: everything.header,
        rows: everything.rows.length,
        folders: count(everything.rows, true),
        files: count(everything.rows, false),
      },
    },
    counts: {
      onlyInFindHelper: onlyInFindHelper.length,
      onlyInEverything: onlyInEverything.length,
      common: findhelper.rows.length - onlyInFindHelper.length,
      sizeCompared,
      sizeMatched,
      sizeMismatched: sizeMismatches.length,
      timeCompared,
      timeIdentical,
      timeDifferent: timeMismatches.length,
    },
    onlyInFindHelper,
    onlyInEverything,
    sizeMismatches,
    timeMismatches,
    deltaHistogram,
    impliedOffsetHistogram,
    offsetByLocalOffset: [...offsetByLocalOffset.entries()]
      .sort((a, b) => a[0] - b[0])
      .map(([localOffsetMinutes, deltas]) => ({
        localOffsetMinutes,
        localOffset: formatOffset(localOffsetMinutes),
        deltas,
      })),
    nameMismatches,
    duplicates: {
      findhelper: findDuplicates(findhelper.rows),
      everything: findDuplicates(everything.rows),
    },
    secondsDropped,
    schemaNotes,
  };
}

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------

function heading(text: string): string {
  return `\n${text}\n${"-".repeat(text.length)}`;
}

function printExtraRows(label: string, rows: ExtraRowInfo[], maxList: number): void {
  if (rows.length === 0) {
    console.log(`  ${label}: none`);
    return;
  }
  console.log(`  ${label}: ${rows.length}`);
  const shown = maxList === 0 ? rows : rows.slice(0, maxList);
  for (const r of shown) {
    const count = r.fileCount !== null ? `, ${r.fileCount} file(s)` : "";
    console.log(`    - ${r.path}`);
    console.log(
      `        ${r.isDir ? "folder" : "file"}, ${r.sizeDisplay || "?"}${count}, ${r.mtimeText || "?"}` +
        `  | parent "${r.parent}" ${r.parentSeenInReference ? "also present in Everything" : "NOT present in Everything"}` +
        `  | file name ${r.nameSeenInReference ? "also present in Everything" : "NOT present in Everything"}`,
    );
  }
  if (shown.length < rows.length) {
    console.log(`    ... and ${rows.length - shown.length} more (raise --max-list to see them)`);
  }
}

function printReport(report: Report, opts: Options): void {
  const { files, counts } = report;
  console.log("=".repeat(78));
  console.log("FindHelper vs Everything - CSV export comparison");
  console.log("=".repeat(78));
  console.log(`FindHelper : ${files.findhelper.path}`);
  console.log(`             ${files.findhelper.rows} rows (${files.findhelper.folders} folders, ${files.findhelper.files} files)`);
  console.log(`             header: ${files.findhelper.header.join(" | ")}`);
  console.log(`Everything : ${files.everything.path}`);
  console.log(`             ${files.everything.rows} rows (${files.everything.folders} folders, ${files.everything.files} files)`);
  console.log(`             header: ${files.everything.header.join(" | ")}`);
  console.log(`Time zone  : ${report.timeZone} (current offset ${formatOffset(report.currentOffsetMinutes)})`);

  console.log(heading("1. Row count"));
  const diff = files.findhelper.rows - files.everything.rows;
  console.log(
    `  FindHelper ${files.findhelper.rows} rows, Everything ${files.everything.rows} rows, difference ${diff >= 0 ? "+" : ""}${diff}`,
  );
  console.log(`  common paths: ${counts.common}`);
  console.log(`  only in FindHelper: ${counts.onlyInFindHelper}`);
  console.log(`  only in Everything: ${counts.onlyInEverything}`);

  console.log(heading("2. Paths only in FindHelper"));
  printExtraRows("rows", report.onlyInFindHelper, opts.maxList);

  console.log(heading("3. Paths only in Everything"));
  printExtraRows("rows", report.onlyInEverything, opts.maxList);

  console.log(heading("4. Size comparison (common paths)"));
  console.log(
    `  compared ${counts.sizeCompared}, inside the FindHelper rounding interval ${counts.sizeMatched}, outside it ${counts.sizeMismatched}`,
  );
  if (report.sizeMismatches.length > 0) {
    const shown = opts.maxList === 0 ? report.sizeMismatches : report.sizeMismatches.slice(0, opts.maxList);
    for (const m of shown) {
      const pctValue = m.relativeDelta * 100;
      const pct = `${pctValue >= 0 ? "+" : ""}${pctValue.toFixed(1)}`;
      console.log(
        `    - ${m.path}\n        Everything ${m.exactDisplay} (${m.exactBytes} B) vs FindHelper ${m.roundedDisplay}` +
          ` ~${m.roundedBytesApprox} B  (${pct}%)${m.isDir ? "  [folder aggregate]" : ""}`,
      );
    }
    if (shown.length < report.sizeMismatches.length) {
      console.log(`    ... and ${report.sizeMismatches.length - shown.length} more`);
    }
  }

  console.log(heading("5. Timestamp comparison (common paths)"));
  console.log(`  compared ${counts.timeCompared}, identical ${counts.timeIdentical}, different ${counts.timeDifferent}`);
  console.log(`  delta histogram (FindHelper - Everything): ${JSON.stringify(report.deltaHistogram)}`);
  console.log(`  implied UTC offset per column: ${JSON.stringify(report.impliedOffsetHistogram)}`);
  console.log("  local offset at the timestamp -> observed delta:");
  for (const bucket of report.offsetByLocalOffset) {
    console.log(`    ${bucket.localOffset} -> ${JSON.stringify(bucket.deltas)}`);
  }
  if (report.secondsDropped > 0) {
    console.log(`  note: ${report.secondsDropped} row(s) lose their seconds in the FindHelper column (minute truncation).`);
  }

  console.log(heading("6. Name decomposition cross-check"));
  console.log(`  rows whose file name differs between the two exports: ${report.nameMismatches.length}`);
  for (const m of report.nameMismatches.slice(opts.maxList === 0 ? undefined : opts.maxList)) {
    console.log(`    - ${m.path}`);
    console.log(`        FindHelper "${m.findhelper}" vs Everything "${m.everything}"`);
  }

  console.log(heading("7. Duplicate paths inside a single export"));
  console.log(`  FindHelper: ${report.duplicates.findhelper.length} path(s) repeated`);
  for (const d of report.duplicates.findhelper.slice(opts.maxList === 0 ? undefined : opts.maxList)) {
    console.log(`    - ${d.path} (${d.occurrences} rows)`);
  }
  console.log(`  Everything: ${report.duplicates.everything.length} path(s) repeated`);
  for (const d of report.duplicates.everything.slice(opts.maxList === 0 ? undefined : opts.maxList)) {
    console.log(`    - ${d.path} (${d.occurrences} rows)`);
  }

  if (!opts.quiet) {
    console.log(heading("8. Schema differences (not data differences)"));
    for (const note of report.schemaNotes) console.log(`  - ${note}`);
  }
}

// ---------------------------------------------------------------------------
// Verdict
// ---------------------------------------------------------------------------

function buildVerdict(report: Report): string[] {
  const out: string[] = [];
  const c = report.counts;

  if (c.onlyInFindHelper > 0 || c.onlyInEverything > 0) {
    out.push(
      `${c.onlyInFindHelper} path(s) exist only in the FindHelper export and ${c.onlyInEverything} only in the Everything export: ` +
        `the line counts differ because the two tools returned different result sets, not because of a formatting problem.`,
    );
    const unseenParents = report.onlyInFindHelper.filter((r) => !r.parentSeenInReference).length;
    if (unseenParents > 0 && report.onlyInFindHelper.length === unseenParents) {
      out.push(
        `  Every extra row sits under a parent folder that does not appear anywhere in the Everything export, while the file names themselves ` +
          `do appear there: the reference tool is not missing the name, it is not descending into (or is hiding) those parent folders - ` +
          `typically a hidden/system attribute or an index exclusion difference.`,
      );
    }
  } else {
    out.push("Both exports describe exactly the same set of paths.");
  }

  const implied = Object.entries(report.impliedOffsetHistogram).sort((a, b) => b[1] - a[1]);
  const dominant = implied[0];
  if (dominant && report.timeMismatches.length > 0) {
    const pct = Math.round((dominant[1] / Math.max(1, c.timeCompared)) * 100);
    out.push(
      `Timestamps: ${c.timeDifferent}/${c.timeCompared} rows differ, and ${pct}% of all rows imply the same UTC offset ` +
        `(${dominant[0]}) for the FindHelper column. A single constant offset means the FindHelper column applies a fixed offset to every ` +
        `timestamp instead of the DST-aware local offset, while the Everything column follows the local time zone (see the offset -> delta table).`,
    );
    if (dominant[0] === formatOffset(report.currentOffsetMinutes)) {
      out.push(
        `  The constant offset equals the CURRENT UTC offset (${formatOffset(report.currentOffsetMinutes)}): the current offset was applied ` +
          `to every timestamp regardless of the date (classic DST-unaware conversion).`,
      );
    }
  } else if (report.timeMismatches.length === 0) {
    out.push("Timestamps agree on every common path (ignoring the second-level truncation).");
  }

  if (c.sizeMismatched > 0) {
    const dirMismatches = report.sizeMismatches.filter((m) => m.isDir).length;
    out.push(
      `Sizes: ${c.sizeMismatched}/${c.sizeCompared} common rows fall outside the rounding interval of the FindHelper display` +
        (dirMismatches > 0
          ? ` (${dirMismatches} of them are folders, i.e. recursive aggregates that the two tools computed differently)`
          : "") +
        ".",
    );
  } else {
    out.push("Sizes: every common row is consistent with the FindHelper rounding interval.");
  }

  return out;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

async function main(): Promise<number> {
  const opts = parseArgs(process.argv.slice(2));
  const [fileA, fileB] = opts.files as [string, string];

  const a = await parseExport(fileA);
  const b = await parseExport(fileB);
  const findhelper = a.format === "findhelper" ? a : b;
  const everything = a.format === "everything" ? a : b;
  if (findhelper.format !== "findhelper" || everything.format !== "everything") {
    console.error("Could not classify both files (one must be a FindHelper export, the other an Everything export).");
    return 2;
  }

  const report = compare(findhelper, everything, opts.tz);
  printReport(report, opts);

  console.log(heading("Verdict"));
  for (const line of buildVerdict(report)) console.log(`  - ${line}`);

  if (opts.jsonPath) {
    await Bun.write(opts.jsonPath, JSON.stringify(report, null, 2));
    console.log(`\nJSON report written to ${opts.jsonPath}`);
  }

  const hasDiff =
    report.counts.onlyInFindHelper > 0 ||
    report.counts.onlyInEverything > 0 ||
    report.counts.sizeMismatched > 0 ||
    report.counts.timeDifferent > 0;
  return opts.failOnDiff && hasDiff ? 1 : 0;
}

try {
  process.exit(await main());
} catch (err) {
  console.error(`error: ${err instanceof Error ? err.message : String(err)}`);
  process.exit(2);
}
