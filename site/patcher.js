/* The browser patcher's applier (docs/SPEC.md: "Patcher (distribution)"). Plain JavaScript,
 * no dependencies; it produces the same bytes as `python3 -m tools apply`. index.html uses
 * it as window.Patcher; the acceptance test loads it under Node through module.exports. */
(function (root) {
  'use strict';

  /* ---- SHA-256 (FIPS 180-4) over a Uint8Array, as lowercase hex ------------------------- */
  var K = new Uint32Array([
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
  ]);

  function rotr(x, n) { return (x >>> n) | (x << (32 - n)); }

  function sha256Hex(bytes) {
    var len = bytes.length, bits = len * 8;
    var padded = new Uint8Array(((len + 9 + 63) >> 6) << 6);
    padded.set(bytes);
    padded[len] = 0x80;
    var dv = new DataView(padded.buffer);
    dv.setUint32(padded.length - 8, Math.floor(bits / 0x100000000));
    dv.setUint32(padded.length - 4, bits >>> 0);
    var H = new Uint32Array([0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                             0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]);
    var W = new Uint32Array(64), t, off;
    for (off = 0; off < padded.length; off += 64) {
      for (t = 0; t < 16; t++) W[t] = dv.getUint32(off + t * 4);
      for (t = 16; t < 64; t++) {
        var s0 = rotr(W[t - 15], 7) ^ rotr(W[t - 15], 18) ^ (W[t - 15] >>> 3);
        var s1 = rotr(W[t - 2], 17) ^ rotr(W[t - 2], 19) ^ (W[t - 2] >>> 10);
        W[t] = (W[t - 16] + s0 + W[t - 7] + s1) >>> 0;
      }
      var a = H[0], b = H[1], c = H[2], d = H[3], e = H[4], f = H[5], g = H[6], h = H[7];
      for (t = 0; t < 64; t++) {
        var S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        var ch = (e & f) ^ (~e & g);
        var t1 = (h + S1 + ch + K[t] + W[t]) >>> 0;
        var S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        var maj = (a & b) ^ (a & c) ^ (b & c);
        var t2 = (S0 + maj) >>> 0;
        h = g; g = f; f = e; e = (d + t1) >>> 0; d = c; c = b; b = a; a = (t1 + t2) >>> 0;
      }
      H[0] = (H[0] + a) >>> 0; H[1] = (H[1] + b) >>> 0; H[2] = (H[2] + c) >>> 0; H[3] = (H[3] + d) >>> 0;
      H[4] = (H[4] + e) >>> 0; H[5] = (H[5] + f) >>> 0; H[6] = (H[6] + g) >>> 0; H[7] = (H[7] + h) >>> 0;
    }
    var hex = '';
    for (t = 0; t < 8; t++) hex += ('00000000' + H[t].toString(16)).slice(-8);
    return hex;
  }

  /* ---- the SysEx container (docs/SPEC.md: "SysEx container") ---------------------------- */
  function unpack7(packed) {
    var out = [], i, k, n, ms;
    for (i = 0; i < packed.length; i += 8) {
      n = Math.min(8, packed.length - i);
      if (n === 1) {
        if (packed[i] !== 0) throw new Error('empty tail group has a non-zero MS byte');
        break;
      }
      ms = packed[i];
      for (k = 1; k < n; k++) out.push(packed[i + k] | (((ms >> (k - 1)) & 1) << 7));
    }
    return new Uint8Array(out);
  }

  function pack7(data) {
    var groups = Math.floor(data.length / 7), tail = data.length % 7;
    var out = new Uint8Array(groups * 8 + (tail ? tail + 1 : 0)), o = 0, i, k, n, ms;
    for (i = 0; i < data.length; i += 7) {
      n = Math.min(7, data.length - i);
      ms = 0;
      for (k = 0; k < n; k++) if (data[i + k] & 0x80) ms |= 1 << k;
      out[o++] = ms;
      for (k = 0; k < n; k++) out[o++] = data[i + k] & 0x7f;
    }
    return out;
  }

  function trailerFor(payload) {
    var even = payload.length & ~1, total = 0, i;
    for (i = 0; i < even; i += 2) total = (total + (payload[i] | (payload[i + 1] << 8))) >>> 0;
    return [total & 0x7f, (total >> 8) & 0x7f];
  }

  function hex2(b) { return ('0' + b.toString(16)).slice(-2).toUpperCase(); }

  function decode(raw) {
    if (raw.length < 14 || raw[0] !== 0xF0 || raw[1] !== 0x01 || raw[2] !== 0x32 || raw[raw.length - 1] !== 0xF7)
      throw new Error('not a Sequential OS update SysEx file (expected F0 01 32 ... F7)');
    if (raw[3] !== 0x7C) throw new Error('unsupported SysEx command 0x' + hex2(raw[3]) + ' (expected 7C = OS update)');
    var pos = 4, target = 'main';
    if (raw[pos] === 0x7D) { target = 'panel'; pos++; }
    if (raw[pos] !== 0x7A) throw new Error('missing 7A header byte');
    pos++;
    var header = unpack7(raw.subarray(pos, pos + 6));
    if (header.length !== 5) throw new Error('truncated header group');
    var groups = header[0] * 0x1000000 + (header[1] << 16) + (header[2] << 8) + header[3], tail = header[4];
    if (tail > 6) throw new Error('header tail count out of range');
    pos += 6;
    var body = raw.subarray(pos, raw.length - 1);
    if (body.length < 2) throw new Error('missing trailer');
    var packed = body.subarray(0, body.length - 2), trailer = body.subarray(body.length - 2);
    if (packed.length !== groups * 8 + tail + 1) throw new Error('payload length inconsistent with the header');
    for (var i = 0; i < body.length; i++) if (body[i] & 0x80) throw new Error('non-7-bit byte inside the message');
    var payload = unpack7(packed), t = trailerFor(payload);
    if (t[0] !== trailer[0] || t[1] !== trailer[1]) throw new Error('trailer mismatch: the file is corrupt');
    return { target: target, payload: payload };
  }

  function encode(payload) {                         /* a Main OS file */
    var groups = Math.floor(payload.length / 7), tail = payload.length % 7;
    var packed = pack7(payload);
    var header = pack7(new Uint8Array([(groups >>> 24) & 0xff, (groups >>> 16) & 0xff, (groups >>> 8) & 0xff,
                                       groups & 0xff, tail]));
    var t = trailerFor(payload);
    var out = new Uint8Array(5 + 6 + packed.length + (tail === 0 ? 1 : 0) + 3), o = 0;
    out[o++] = 0xF0; out[o++] = 0x01; out[o++] = 0x32; out[o++] = 0x7C; out[o++] = 0x7A;
    out.set(header, o); o += 6;
    out.set(packed, o); o += packed.length;
    if (tail === 0) out[o++] = 0;                    /* the loader reads a tail MS byte regardless */
    out[o++] = t[0]; out[o++] = t[1]; out[o++] = 0xF7;
    return out;
  }

  /* ---- base64 (no atob: identical in the browser and under Node) ------------------------- */
  var B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  function base64ToBytes(s) {
    s = s.replace(/[^A-Za-z0-9+\/]/g, '');
    var out = new Uint8Array(Math.floor(s.length * 3 / 4)), o = 0, buf = 0, bits = 0, i;
    for (i = 0; i < s.length; i++) {
      buf = ((buf << 6) | B64.indexOf(s.charAt(i))) & 0xffffff;
      bits += 6;
      if (bits >= 8) { bits -= 8; out[o++] = (buf >> bits) & 0xff; }
    }
    return out.subarray(0, o);
  }

  /* ---- apply a manifest to the stock file ----------------------------------------------- */
  function apply(patch, raw) {
    var got = sha256Hex(raw);
    if (got !== patch.base.sha256)
      return { ok: false, stage: 'base', error: 'This is not Sequential\'s Main OS 2.1.0 file (' + patch.base.name +
               '): its SHA-256 is ' + got + ', expected ' + patch.base.sha256 + '.' };
    var payload;
    try { payload = decode(raw).payload; }
    catch (e) { return { ok: false, stage: 'base', error: 'The file could not be read: ' + e.message }; }
    var chunks = [], total = 0, cur = 0, i, s, off, data, nxt;   /* the base up to each span, then the span's bytes */
    for (i = 0; i < patch.spans.length; i++) {
      s = patch.spans[i];
      off = s.offset; data = base64ToBytes(s.data);
      nxt = s.insert ? off : off + data.length;          /* base position after the span */
      if (off < cur || nxt > payload.length) return { ok: false, stage: 'manifest', error: 'The patch does not fit this file.' };
      chunks.push(payload.subarray(cur, off)); chunks.push(data);
      total += off - cur + data.length;
      cur = nxt;
    }
    chunks.push(payload.subarray(cur)); total += payload.length - cur;
    var out = new Uint8Array(total), o = 0;
    for (i = 0; i < chunks.length; i++) { out.set(chunks[i], o); o += chunks[i].length; }
    var file = encode(out), h = sha256Hex(file);
    if (h !== patch.result.sha256)
      return { ok: false, stage: 'result', error: 'The result does not match the release (SHA-256 ' + h +
               ', expected ' + patch.result.sha256 + '). Do not install it.' };
    return { ok: true, bytes: file, sha256: h };
  }

  var api = { sha256Hex: sha256Hex, decode: decode, encode: encode, base64ToBytes: base64ToBytes, apply: apply };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.Patcher = api;
})(typeof self !== 'undefined' ? self : this);
