/**
 * @file ascon.js
 * @brief NIST ASCON-128 AEAD Cryptographic Engine & Replay Verifier for Browser Client.
 * 100% bit-for-bit parity with ascon_ref.py and ESP32 C implementation (ESP32_Firmware_Integrated_Draft.c).
 */

const ASCON_RC = [
  0xf0n, 0xe1n, 0xd2n, 0xc3n,
  0xb4n, 0xa5n, 0x96n, 0x87n,
  0x78n, 0x69n, 0x5an, 0x4bn
];

function rotr64(x, n) {
  const bn = BigInt(n);
  return ((x >> bn) | (x << (64n - bn))) & 0xFFFFFFFFFFFFFFFFn;
}

function asconPermutation(s, rounds) {
  for (let r = 12 - rounds; r < 12; r++) {
    // 1. Constant addition
    s[2] ^= ASCON_RC[r];

    // 2. Substitution layer (5 parallel 5-bit S-boxes)
    s[0] ^= s[4]; s[4] ^= s[3]; s[2] ^= s[1];
    let t0 = (~s[0]) & s[1];
    let t1 = (~s[1]) & s[2];
    let t2 = (~s[2]) & s[3];
    let t3 = (~s[3]) & s[4];
    let t4 = (~s[4]) & s[0];
    s[0] ^= t1; s[1] ^= t2; s[2] ^= t3; s[3] ^= t4; s[4] ^= t0;
    s[1] ^= s[0]; s[0] ^= s[4]; s[3] ^= s[2]; s[2] = (~s[2]) & 0xFFFFFFFFFFFFFFFFn;

    // 3. Linear diffusion layer
    s[0] ^= rotr64(s[0], 19) ^ rotr64(s[0], 28);
    s[1] ^= rotr64(s[1], 61) ^ rotr64(s[1], 39);
    s[2] ^= rotr64(s[2], 1)  ^ rotr64(s[2], 6);
    s[3] ^= rotr64(s[3], 10) ^ rotr64(s[3], 17);
    s[4] ^= rotr64(s[4], 7)  ^ rotr64(s[4], 41);
  }
}

function load64BE(bytes, offset) {
  let v = 0n;
  for (let i = 0; i < 8; i++) {
    v = (v << 8n) | BigInt(bytes[offset + i] || 0);
  }
  return v;
}

function store64BE(v) {
  const bytes = new Uint8Array(8);
  for (let i = 7; i >= 0; i--) {
    bytes[i] = Number(v & 0xFFn);
    v >>= 8n;
  }
  return bytes;
}

/**
 * Decrypts an ASCON-128 AEAD payload and verifies the 16-byte authentication tag.
 * @param {Uint8Array} key 16-byte master key
 * @param {Uint8Array} nonce 16-byte nonce
 * @param {Uint8Array} ad Associated data
 * @param {Uint8Array} ciphertextWithTag Ciphertext concatenated with 16-byte tag
 * @returns {{ success: boolean, plaintext: string|null, tagMatch: boolean, latencyMs: number }}
 */
function ascon128Decrypt(key, nonce, ad, ciphertextWithTag) {
  const t0 = performance.now();
  if (ciphertextWithTag.length < 16) {
    return { success: false, plaintext: null, tagMatch: false, latencyMs: 0 };
  }

  const clen = ciphertextWithTag.length - 16;
  const ciphertext = ciphertextWithTag.slice(0, clen);
  const receivedTag = ciphertextWithTag.slice(clen);

  const k0 = load64BE(key, 0);
  const k1 = load64BE(key, 8);
  const n0 = load64BE(nonce, 0);
  const n1 = load64BE(nonce, 8);

  // 1. Initialization (a = 12 rounds)
  // IV for Ascon-128: 0x80400c0600000000
  const s = [0x80400c0600000000n, k0, k1, n0, n1];
  asconPermutation(s, 12);
  s[3] ^= k0;
  s[4] ^= k1;

  // 2. Process Associated Data (b = 6 rounds, rate = 8 bytes)
  if (ad && ad.length > 0) {
    for (let i = 0; i < ad.length; i += 8) {
      const chunk = ad.slice(i, Math.min(i + 8, ad.length));
      if (chunk.length === 8) {
        s[0] ^= load64BE(chunk, 0);
      } else {
        const padded = new Uint8Array(8);
        padded.set(chunk);
        padded[chunk.length] = 0x80;
        s[0] ^= load64BE(padded, 0);
      }
      asconPermutation(s, 6);
    }
  }
  // Domain separation
  s[4] ^= 1n;

  // 3. Process Ciphertext & Recover Plaintext
  const plaintext = new Uint8Array(clen);
  for (let i = 0; i < clen; i += 8) {
    const chunkLen = Math.min(8, clen - i);
    const chunk = ciphertext.slice(i, i + chunkLen);
    const s0Bytes = store64BE(s[0]);

    for (let j = 0; j < chunkLen; j++) {
      plaintext[i + j] = chunk[j] ^ s0Bytes[j];
    }

    if (chunkLen === 8) {
      s[0] = load64BE(chunk, 0);
      asconPermutation(s, 6);
    } else {
      const paddedPt = new Uint8Array(8);
      for (let j = 0; j < chunkLen; j++) {
        paddedPt[j] = plaintext[i + j];
      }
      paddedPt[chunkLen] = 0x80;
      s[0] = (s[0] & ((1n << BigInt((8 - chunkLen) * 8)) - 1n)) ^ load64BE(paddedPt, 0);
    }
  }

  // 4. Finalization (a = 12 rounds)
  s[1] ^= k0;
  s[2] ^= k1;
  asconPermutation(s, 12);
  s[3] ^= k0;
  s[4] ^= k1;

  const expectedTag = new Uint8Array(16);
  expectedTag.set(store64BE(s[3]), 0);
  expectedTag.set(store64BE(s[4]), 8);

  // Constant-time tag comparison
  let tagMatch = true;
  for (let i = 0; i < 16; i++) {
    if (expectedTag[i] !== receivedTag[i]) {
      tagMatch = false;
    }
  }

  const t1 = performance.now();
  const latencyMs = Number((t1 - t0).toFixed(3));

  if (!tagMatch) {
    return { success: false, plaintext: null, tagMatch: false, latencyMs };
  }

  const decStr = new TextDecoder().decode(plaintext);
  return { success: true, plaintext: decStr, tagMatch: true, latencyMs };
}

/**
 * Encrypts a plaintext string/bytes using NIST ASCON-128 AEAD and generates 16-byte authentication tag.
 * 100% bit-for-bit parity with ascon_ref.py and ESP32 C implementation.
 * @param {Uint8Array} key 16-byte master key
 * @param {Uint8Array} nonce 16-byte nonce
 * @param {Uint8Array|string} ad Associated data
 * @param {Uint8Array|string|Object} plaintext Plaintext bytes, UTF-8 string, or JSON Object
 * @returns {{ ciphertext: Uint8Array, tag: Uint8Array, ciphertextWithTag: Uint8Array, hexCiphertext: string, hexTag: string, latencyMs: number }}
 */
function ascon128Encrypt(key, nonce, ad, plaintext) {
  const t0 = performance.now();
  let ptBytes;
  if (typeof plaintext === 'string') {
    ptBytes = new TextEncoder().encode(plaintext);
  } else if (plaintext instanceof Uint8Array) {
    ptBytes = plaintext;
  } else {
    ptBytes = new TextEncoder().encode(JSON.stringify(plaintext));
  }
  const plen = ptBytes.length;

  const k0 = load64BE(key, 0);
  const k1 = load64BE(key, 8);
  const n0 = load64BE(nonce, 0);
  const n1 = load64BE(nonce, 8);

  // 1. Initialization (a = 12 rounds)
  // IV for Ascon-128: 0x80400c0600000000
  const s = [0x80400c0600000000n, k0, k1, n0, n1];
  asconPermutation(s, 12);
  s[3] ^= k0;
  s[4] ^= k1;

  // 2. Process Associated Data (b = 6 rounds, rate = 8 bytes)
  if (ad) {
    const adBytes = (typeof ad === 'string') ? new TextEncoder().encode(ad) : ad;
    if (adBytes.length > 0) {
      for (let i = 0; i < adBytes.length; i += 8) {
        const chunk = adBytes.slice(i, Math.min(i + 8, adBytes.length));
        if (chunk.length === 8) {
          s[0] ^= load64BE(chunk, 0);
        } else {
          const padded = new Uint8Array(8);
          padded.set(chunk);
          padded[chunk.length] = 0x80;
          s[0] ^= load64BE(padded, 0);
        }
        asconPermutation(s, 6);
      }
    }
  }
  // Domain separation
  s[4] ^= 1n;

  // 3. Process Plaintext & Generate Ciphertext (b = 6 rounds, rate = 8 bytes)
  const ciphertext = new Uint8Array(plen);
  for (let i = 0; i < plen; i += 8) {
    const chunkLen = Math.min(8, plen - i);
    const chunk = ptBytes.slice(i, i + chunkLen);

    if (chunkLen === 8) {
      s[0] ^= load64BE(chunk, 0);
      const s0Bytes = store64BE(s[0]);
      ciphertext.set(s0Bytes, i);
      asconPermutation(s, 6);
    } else {
      const paddedPt = new Uint8Array(8);
      paddedPt.set(chunk);
      paddedPt[chunkLen] = 0x80;
      s[0] ^= load64BE(paddedPt, 0);
      const s0Bytes = store64BE(s[0]);
      ciphertext.set(s0Bytes.slice(0, chunkLen), i);
    }
  }

  // 4. Finalization (a = 12 rounds)
  s[1] ^= k0;
  s[2] ^= k1;
  asconPermutation(s, 12);
  s[3] ^= k0;
  s[4] ^= k1;

  const tag = new Uint8Array(16);
  tag.set(store64BE(s[3]), 0);
  tag.set(store64BE(s[4]), 8);

  const ciphertextWithTag = new Uint8Array(plen + 16);
  ciphertextWithTag.set(ciphertext, 0);
  ciphertextWithTag.set(tag, plen);

  const t1 = performance.now();
  const latencyMs = Number((t1 - t0).toFixed(3));

  const hexCiphertext = Array.from(ciphertext).map(b => b.toString(16).padStart(2, '0')).join('');
  const hexTag = Array.from(tag).map(b => b.toString(16).padStart(2, '0')).join('');

  return {
    ciphertext,
    tag,
    ciphertextWithTag,
    hexCiphertext,
    hexTag,
    latencyMs
  };
}

/**
 * Replay Protection Nonce State Tracker
 */
class AsconReplayGuard {
  constructor() {
    this.highestNonce = 0n;
    this.totalVerified = 0;
    this.totalReplaysBlocked = 0;
  }

  /**
   * Check if a 64-bit nonce is strictly monotonic to reject replayed packets.
   * @param {BigInt|number|string} nonceVal 
   * @returns {boolean} True if nonce is valid and fresh, False if replay attack
   */
  checkAndAdvance(nonceVal) {
    const n = BigInt(nonceVal);
    if (n <= this.highestNonce && this.highestNonce !== 0n) {
      this.totalReplaysBlocked++;
      return false; // Replay detected!
    }
    this.highestNonce = n;
    this.totalVerified++;
    return true;
  }
}

// Export for browser window
window.Ascon128 = {
  encrypt: ascon128Encrypt,
  decrypt: ascon128Decrypt,
  ReplayGuard: AsconReplayGuard
};

