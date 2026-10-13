#!/usr/bin/env python3

"""
Implementation of Ascon v1.2, an authenticated cipher and hash function
http://ascon.iaik.tugraz.at/
"""

debug = False
debugpermutation = False

# === Ascon hash/xof ===

def ascon_hash(message, variant="Ascon-Hash", hashlength=32):
    """
    Ascon hash function and extendable-output function.
    message: a bytes object of arbitrary length
    variant: "Ascon-Hash", "Ascon-Hasha" (both with 256-bit output for 128-bit security), "Ascon-Xof", or "Ascon-Xofa" (both with arbitrary output length, security=min(128, bitlen/2))
    hashlength: the requested output bytelength (must be 32 for variant "Ascon-Hash"; can be arbitrary for Ascon-Xof, but should be >= 32 for 128-bit security)
    returns a bytes object containing the hash tag
    """
    assert variant in ["Ascon-Hash", "Ascon-Hasha", "Ascon-Xof", "Ascon-Xofa"]
    if variant in ["Ascon-Hash", "Ascon-Hasha"]: assert(hashlength == 32)
    a = 12   # rounds
    b = 8 if variant in ["Ascon-Hasha", "Ascon-Xofa"] else 12
    rate = 8 # bytes

    # Initialization
    tagspec = int_to_bytes(256 if variant in ["Ascon-Hash", "Ascon-Hasha"] else 0, 4)
    S = bytes_to_state(to_bytes([0, rate * 8, a, a-b]) + tagspec + zero_bytes(32))
    if debug: printstate(S, "initial value:")

    ascon_permutation(S, a)
    if debug: printstate(S, "initialization:")

    # Message Processing (Absorbing)
    m_padding = to_bytes([0x80]) + zero_bytes(rate - (len(message) % rate) - 1)
    m_padded = message + m_padding

    # first s-1 blocks
    for block in range(0, len(m_padded) - rate, rate): #in slices of 8 byte and XORs with first element of state s[0]-->absorption phase
        S[0] ^= bytes_to_int(m_padded[block:block+8])  # rate=8
        ascon_permutation(S, b) #-->permutation operation for b rounds
    # last block #--> this is same as absorption and processing as earlier but happens outside for loop
    block = len(m_padded) - rate
    S[0] ^= bytes_to_int(m_padded[block:block+8])  # rate=8
    if debug: printstate(S, "process message:")

    # Finalization (Squeezing)
    H = b"" #-->initialised to store hash value
    ascon_permutation(S, a) #permutation for a rounds
    while len(H) < hashlength: #bytes are extracted from and appended to'H' until it reaches desired hash length
        H += int_to_bytes(S[0], 8)  # rate=8
        ascon_permutation(S, b)
    if debug: printstate(S, "finalization:")
    return H[:hashlength] #H=Final Hash Value


# === Ascon MAC/PRF ===

def ascon_mac(key, message, variant="Ascon-Mac", taglength=16):
    """
    Ascon message authentication code (MAC) and pseudorandom function (PRF).
    key: a bytes object of size 16
    message: a bytes object of arbitrary length (<= 16 for "Ascon-PrfShort")
    variant: "Ascon-Mac", "Ascon-Maca" (both 128-bit output, arbitrarily long input), "Ascon-Prf", "Ascon-Prfa" (both arbitrarily long input and output), or "Ascon-PrfShort" (t-bit output for t<=128, m-bit input for m<=128)
    taglength: the requested output bytelength l/8 (must be <=16 for variants "Ascon-Mac", "Ascon-Maca", and "Ascon-PrfShort", arbitrary for "Ascon-Prf", "Ascon-Prfa"; should be >= 16 for 128-bit security)
    returns a bytes object containing the authentication tag
    """
    assert variant in ["Ascon-Mac", "Ascon-Prf", "Ascon-Maca", "Ascon-Prfa", "Ascon-PrfShort"] #depending on the variant it validates the length of key and taglength such that they meet the requirements of the variant
    if variant in ["Ascon-Mac", "Ascon-Maca"]: assert(len(key) == 16 and taglength <= 16)
    if variant in ["Ascon-Prf", "Ascon-Prfa"]: assert(len(key) == 16)
    if variant == "Ascon-PrfShort": assert(len(key) == 16 and taglength <= 16 and len(message) <= 16)
    a = 12  # rounds
    b = 8 if variant in ["Ascon-Prfa", "Ascon-Maca"] else 12  # rounds
    msgblocksize = 40 if variant in ["Ascon-Prfa", "Ascon-Maca"] else 32 # bytes (input rate for Mac, Prf)
    rate = 16 # bytes (output rate)

  #Depending on the variant, it initializes the state S with specific values.
  #For the "Ascon-PrfShort" variant, it calculates an Initialization Vector (IV) and prepares the state.
  #For other variants, it sets up the state based on the key and other parameters.
    if variant == "Ascon-PrfShort":
        # Initialization + Message Processing (Absorbing)
        IV = to_bytes([len(key) * 8, len(message)*8, a + 64, taglength * 8]) + zero_bytes(4)
        S = bytes_to_state(IV + key + message + zero_bytes(16 - len(message)))
        if debug: printstate(S, "initial value:")

        ascon_permutation(S, a)
        if debug: printstate(S, "process message:")

        # Finalization (Squeezing)
        T = int_to_bytes(S[3] ^ bytes_to_int(key[0:8]), 8) + int_to_bytes(S[4] ^ bytes_to_int(key[8:16]), 8)
        return T[:taglength]

    else: # Ascon-Prf, Ascon-Prfa, Ascon-Mac, Ascon-Maca
        # Initialization
        if variant in ["Ascon-Mac", "Ascon-Maca"]: tagspec = int_to_bytes(16*8, 4)
        if variant in ["Ascon-Prf", "Ascon-Prfa"]: tagspec = int_to_bytes(0*8, 4)
        S = bytes_to_state(to_bytes([len(key) * 8, rate * 8, a + 128, a-b]) + tagspec + key + zero_bytes(16))
        if debug: printstate(S, "initial value:")

        ascon_permutation(S, a)
        if debug: printstate(S, "initialization:")

        # Message Processing (Absorbing)
        m_padding = to_bytes([0x80]) + zero_bytes(msgblocksize - (len(message) % msgblocksize) - 1)
        m_padded = message + m_padding

        # first s-1 blocks
        for block in range(0, len(m_padded) - msgblocksize, msgblocksize):
            S[0] ^= bytes_to_int(m_padded[block:block+8])     # msgblocksize=32 bytes
            S[1] ^= bytes_to_int(m_padded[block+8:block+16])
            S[2] ^= bytes_to_int(m_padded[block+16:block+24])
            S[3] ^= bytes_to_int(m_padded[block+24:block+32])
            if variant in ["Ascon-Prfa", "Ascon-Maca"]:
                S[4] ^= bytes_to_int(m_padded[block+32:block+40])
            ascon_permutation(S, b)
        # last block
        block = len(m_padded) - msgblocksize
        S[0] ^= bytes_to_int(m_padded[block:block+8])     # msgblocksize=32 bytes
        S[1] ^= bytes_to_int(m_padded[block+8:block+16])
        S[2] ^= bytes_to_int(m_padded[block+16:block+24])
        S[3] ^= bytes_to_int(m_padded[block+24:block+32])
        if variant in ["Ascon-Prfa", "Ascon-Maca"]:
            S[4] ^= bytes_to_int(m_padded[block+32:block+40])
        S[4] ^= 1
        if debug: printstate(S, "process message:")

        # Finalization (Squeezing)
        T = b""
        ascon_permutation(S, a)
        while len(T) < taglength:
            T += int_to_bytes(S[0], 8)  # rate=16
            T += int_to_bytes(S[1], 8)
            ascon_permutation(S, b)
        if debug: printstate(S, "finalization:")
        return T[:taglength]


# === Ascon AEAD encryption and decryption ===

def ascon_encrypt(key, nonce, associateddata, plaintext, variant="Ascon-128"):
    """
    Ascon encryption.
    key: a bytes object of size 16 (for Ascon-128, Ascon-128a; 128-bit security) or 20 (for Ascon-80pq; 128-bit security)
    nonce: a bytes object of size 16 (must not repeat for the same key!)
    associateddata: a bytes object of arbitrary length
    plaintext: a bytes object of arbitrary length
    variant: "Ascon-128", "Ascon-128a", or "Ascon-80pq" (specifies key size, rate and number of rounds)
    returns a bytes object of length len(plaintext)+16 containing the ciphertext and tag
    """
    assert variant in ["Ascon-128", "Ascon-128a", "Ascon-80pq"]
    if variant in ["Ascon-128", "Ascon-128a"]: assert(len(key) == 16 and len(nonce) == 16)
    if variant == "Ascon-80pq": assert(len(key) == 20 and len(nonce) == 16)
    S = [0, 0, 0, 0, 0]
    k = len(key) * 8   # bits
    a = 12   # rounds
    b = 8 if variant == "Ascon-128a" else 6   # rounds
    rate = 16 if variant == "Ascon-128a" else 8   # bytes

    ascon_initialize(S, k, rate, a, b, key, nonce)
    ascon_process_associated_data(S, b, rate, associateddata)
    ciphertext = ascon_process_plaintext(S, b, rate, plaintext)
    tag = ascon_finalize(S, rate, a, key)
    return ciphertext + tag


def ascon_decrypt(key, nonce, associateddata, ciphertext, variant="Ascon-128"):
    """
    Ascon decryption.
    key: a bytes object of size 16 (for Ascon-128, Ascon-128a; 128-bit security) or 20 (for Ascon-80pq; 128-bit security)
    nonce: a bytes object of size 16 (must not repeat for the same key!)
    associateddata: a bytes object of arbitrary length
    ciphertext: a bytes object of arbitrary length (also contains tag)
    variant: "Ascon-128", "Ascon-128a", or "Ascon-80pq" (specifies key size, rate and number of rounds)
    returns a bytes object containing the plaintext or None if verification fails
    """
    assert variant in ["Ascon-128", "Ascon-128a", "Ascon-80pq"]
    if variant in ["Ascon-128", "Ascon-128a"]: assert(len(key) == 16 and len(nonce) == 16 and len(ciphertext) >= 16)
    if variant == "Ascon-80pq": assert(len(key) == 20 and len(nonce) == 16 and len(ciphertext) >= 16)
    S = [0, 0, 0, 0, 0]
    k = len(key) * 8 # bits
    a = 12 # rounds
    b = 8 if variant == "Ascon-128a" else 6   # rounds
    rate = 16 if variant == "Ascon-128a" else 8   # bytes

    ascon_initialize(S, k, rate, a, b, key, nonce)
    ascon_process_associated_data(S, b, rate, associateddata)
    plaintext = ascon_process_ciphertext(S, b, rate, ciphertext[:-16])
    tag = ascon_finalize(S, rate, a, key)
    if tag == ciphertext[-16:]:
        return plaintext
    else:
        return None


# === Ascon AEAD building blocks ===

def ascon_initialize(S, k, rate, a, b, key, nonce):

  #Prepares the Ascon state for encryption by setting initial values, applying the Ascon permutation, and XOR-ing with a zero-padded key.
  #It helps ensure that the key is used securely and that the state is properly initialized for encryption
    """
    Ascon initialization phase - internal helper function.
    S: Ascon state, a list of 5 64-bit integers
    k: key size in bits
    rate: block size in bytes (8 for Ascon-128, Ascon-80pq; 16 for Ascon-128a)
    a: number of initialization/finalization rounds for permutation
    b: number of intermediate rounds for permutation
    key: a bytes object of size 16 (for Ascon-128, Ascon-128a; 128-bit security) or 20 (for Ascon-80pq; 128-bit security)
    nonce: a bytes object of size 16
    returns nothing, updates S
    """
    iv_zero_key_nonce = to_bytes([k, rate * 8, a, b] + (20-len(key))*[0]) + key + nonce #initialisation vector(IV) by concatenation of terms
    S[0], S[1], S[2], S[3], S[4] = bytes_to_state(iv_zero_key_nonce) #conversion of IV to Ascon state
    if debug: printstate(S, "initial value:")

    ascon_permutation(S, a)

    zero_key = bytes_to_state(zero_bytes(40-len(key)) + key) #zero-padding the original key to 20 bytes and converting it into the state representation
    S[0] ^= zero_key[0]
    S[1] ^= zero_key[1]
    S[2] ^= zero_key[2]
    S[3] ^= zero_key[3]
    S[4] ^= zero_key[4]
    #S is updated by XOR-ing its five 64-bit integers with the corresponding values in zero_key
     #This step ensures that the key is used only once and helps to prevent potential attacks or vulnerabilities
    if debug: printstate(S, "initialization:")


def ascon_process_associated_data(S, b, rate, associateddata):
  # function is responsible for processing the associated data in blocks, applying the Ascon permutation, and ensuring that the state is updated accordingly.
  #This is an important step in authenticated encryption where both the plaintext and associated data are processed before encryption.
    """
    Ascon associated data processing phase - internal helper function.
    S: Ascon state, a list of 5 64-bit integers
    b: number of intermediate rounds for permutation
    rate: block size in bytes (8 for Ascon-128, 16 for Ascon-128a)
    associateddata: a bytes object of arbitrary length
    returns nothing, updates S
    """

    #if condition is used to Calculates the number of zero bytes needed to pad the associated data to a multiple of the block size (rate).
    if len(associateddata) > 0:
        a_zeros = rate - (len(associateddata) % rate) - 1
        a_padding = to_bytes([0x80] + [0 for i in range(a_zeros)])
        a_padded = associateddata + a_padding
    #block processing carried out below
    #XORs the block's bytes with the corresponding bytes in the state S[0] (and S[1] if rate is 16)
    #Ascon permutation function is applied to the state S for b rounds to update the state.
        for block in range(0, len(a_padded), rate):
            S[0] ^= bytes_to_int(a_padded[block:block+8])
            if rate == 16:
                S[1] ^= bytes_to_int(a_padded[block+8:block+16])

            ascon_permutation(S, b)

    #final block processing-->distinguish associated data processing from plaintext processing
    S[4] ^= 1
    if debug: printstate(S, "process associated data:")


def ascon_process_plaintext(S, b, rate, plaintext):

# #function plays a crucial role in the encryption process by processing the plaintext in blocks, applying the Ascon permutation, and generating the ciphertext.
    """
    Ascon plaintext processing phase (during encryption) - internal helper function.
    S: Ascon state, a list of 5 64-bit integers
    b: number of intermediate rounds for permutation
    rate: block size in bytes (8 for Ascon-128, Ascon-80pq; 16 for Ascon-128a)
    plaintext: a bytes object of arbitrary length
    returns the ciphertext (without tag), updates S
    """

    #to create plaintext which is multiple of block rate by padding
    p_lastlen = len(plaintext) % rate
    p_padding = to_bytes([0x80] + (rate-p_lastlen-1)*[0x00])
    p_padded = plaintext + p_padding

    # first t-1 blocks
    ciphertext = to_bytes([])
    #XORing with s[0]
    for block in range(0, len(p_padded) - rate, rate):
      #If rate is 8, the resulting 64-bit integer is added to the ciphertext.
      #If rate is 16, two 64-bit integers are added to the ciphertext.
        if rate == 8:
            S[0] ^= bytes_to_int(p_padded[block:block+8])
            ciphertext += int_to_bytes(S[0], 8)
        elif rate == 16:
            S[0] ^= bytes_to_int(p_padded[block:block+8])
            S[1] ^= bytes_to_int(p_padded[block+8:block+16])
            ciphertext += (int_to_bytes(S[0], 8) + int_to_bytes(S[1], 8))

        ascon_permutation(S, b)

    # last block t
    #function handles the last block, which may not be a full block.
    #here same block processing
    #Depending on the block size (rate), it appends a portion of the resulting 64-bit integers to the ciphertext to match the length of the plaintext.
    block = len(p_padded) - rate
    if rate == 8:
        S[0] ^= bytes_to_int(p_padded[block:block+8])
        ciphertext += int_to_bytes(S[0], 8)[:p_lastlen]
    elif rate == 16:
        S[0] ^= bytes_to_int(p_padded[block:block+8])
        S[1] ^= bytes_to_int(p_padded[block+8:block+16])
        ciphertext += (int_to_bytes(S[0], 8)[:min(8,p_lastlen)] + int_to_bytes(S[1], 8)[:max(0,p_lastlen-8)])
    if debug: printstate(S, "process plaintext:")
    return ciphertext


def ascon_process_ciphertext(S, b, rate, ciphertext):

  #plays a crucial role in the decryption process by processing the ciphertext in blocks, applying the Ascon permutation, and generating the plaintext.

    """
    Ascon ciphertext processing phase (during decryption) - internal helper function.
    S: Ascon state, a list of 5 64-bit integers
    b: number of intermediate rounds for permutation
    rate: block size in bytes (8 for Ascon-128, Ascon-80pq; 16 for Ascon-128a)
    ciphertext: a bytes object of arbitrary length
    returns the plaintext, updates S
    """

    #similar to processing plaintext but happens in decryption instead

    c_lastlen = len(ciphertext) % rate
    c_padded = ciphertext + zero_bytes(rate - c_lastlen)

    # first t-1 blocks
    plaintext = to_bytes([])
    for block in range(0, len(c_padded) - rate, rate):
        if rate == 8:
            Ci = bytes_to_int(c_padded[block:block+8])
            plaintext += int_to_bytes(S[0] ^ Ci, 8)
            S[0] = Ci
        elif rate == 16:
            Ci = (bytes_to_int(c_padded[block:block+8]), bytes_to_int(c_padded[block+8:block+16]))
            plaintext += (int_to_bytes(S[0] ^ Ci[0], 8) + int_to_bytes(S[1] ^ Ci[1], 8))
            S[0] = Ci[0]
            S[1] = Ci[1]

        ascon_permutation(S, b)

    # last block t
    block = len(c_padded) - rate
    if rate == 8:
        c_padding1 = (0x80 << (rate-c_lastlen-1)*8)
        c_mask = (0xFFFFFFFFFFFFFFFF >> (c_lastlen*8))
        Ci = bytes_to_int(c_padded[block:block+8])
        plaintext += int_to_bytes(Ci ^ S[0], 8)[:c_lastlen]
        S[0] = Ci ^ (S[0] & c_mask) ^ c_padding1
    elif rate == 16:
        c_lastlen_word = c_lastlen % 8
        c_padding1 = (0x80 << (8-c_lastlen_word-1)*8)
        c_mask = (0xFFFFFFFFFFFFFFFF >> (c_lastlen_word*8))
        Ci = (bytes_to_int(c_padded[block:block+8]), bytes_to_int(c_padded[block+8:block+16]))
        plaintext += (int_to_bytes(S[0] ^ Ci[0], 8) + int_to_bytes(S[1] ^ Ci[1], 8))[:c_lastlen]
        if c_lastlen < 8:
            S[0] = Ci[0] ^ (S[0] & c_mask) ^ c_padding1
        else:
            S[0] = Ci[0]
            S[1] = Ci[1] ^ (S[1] & c_mask) ^ c_padding1
    if debug: printstate(S, "process ciphertext:")
    return plaintext


def ascon_finalize(S, rate, a, key):

  #function is crucial for generating the authentication tag at the end of encryption or decryption in Ascon
    """
    Ascon finalization phase - internal helper function.
    S: Ascon state, a list of 5 64-bit integers
    rate: block size in bytes (8 for Ascon-128, Ascon-80pq; 16 for Ascon-128a)
    a: number of initialization/finalization rounds for permutation
    key: a bytes object of size 16 (for Ascon-128, Ascon-128a; 128-bit security) or 20 (for Ascon-80pq; 128-bit security)
    returns the tag, updates S
    """
    #key mixing
    #Tt XORs the key bytes with specific words in the state S.
    #The key bytes are extracted and XORed with the appropriate state words to mix the key into the state
    assert(len(key) in [16,20])
    S[rate//8+0] ^= bytes_to_int(key[0:8])
    S[rate//8+1] ^= bytes_to_int(key[8:16])
    S[rate//8+2] ^= bytes_to_int(key[16:] + zero_bytes(24-len(key)))

    #to further mix the state
    ascon_permutation(S, a)

    #tag generation
    #After the permutation, the function XORs specific state words with parts of the key to generate the authentication tag
    S[3] ^= bytes_to_int(key[-16:-8])
    S[4] ^= bytes_to_int(key[-8:])
    tag = int_to_bytes(S[3], 8) + int_to_bytes(S[4], 8)
    if debug: printstate(S, "finalization:")
    return tag


# === Ascon permutation ===

def ascon_permutation(S, rounds=1):
  #used for the core permutation step in the Ascon sponge construction
    """
    Ascon core permutation for the sponge construction - internal helper function.
    S: Ascon state, a list of 5 64-bit integers
    rounds: number of rounds to perform
    returns nothing, updates S
    """
    #loops over specified number of round


    assert(rounds <= 12)
    if debugpermutation: printwords(S, "permutation input:")
    for r in range(12-rounds, 12):
        # --- add round constants ---##The round constants are calculated based on the current round r and XORed with specific words in the state S[2].
        S[2] ^= (0xf0 - r*0x10 + r*0x1)
        if debugpermutation: printwords(S, "round constant addition:")
        # --- substitution layer ---
        #The substitution layer involves bitwise XOR operations and word rotations.
        #Various XOR operations are performed between words in the state to achieve diffusion.
        #Word T is computed based on the current state values and XORed back into the state.
        S[0] ^= S[4]
        S[4] ^= S[3]
        S[2] ^= S[1]
        T = [(S[i] ^ 0xFFFFFFFFFFFFFFFF) & S[(i+1)%5] for i in range(5)]
        for i in range(5):
            S[i] ^= T[(i+1)%5]
        S[1] ^= S[0]
        S[0] ^= S[4]
        S[3] ^= S[2]
        S[2] ^= 0XFFFFFFFFFFFFFFFF
        if debugpermutation: printwords(S, "substitution layer:")
        # --- linear diffusion layer ---
        #the linear diffusion layer involves bit rotations (rotl and rotr).
        #Each word in the state is rotated by a fixed number of bits in both left (rotl) and right (rotr) directions.
        #These rotations are performed to achieve linear diffusion and mix the bit
        S[0] ^= rotr(S[0], 19) ^ rotr(S[0], 28)
        S[1] ^= rotr(S[1], 61) ^ rotr(S[1], 39)
        S[2] ^= rotr(S[2],  1) ^ rotr(S[2],  6)
        S[3] ^= rotr(S[3], 10) ^ rotr(S[3], 17)
        S[4] ^= rotr(S[4],  7) ^ rotr(S[4], 41)
        if debugpermutation: printwords(S, "linear diffusion layer:")


# === helper functions ===

def get_random_bytes(num):
    import os
    return to_bytes(os.urandom(num))

def zero_bytes(n):
    return n * b"\x00"

def to_bytes(l): # where l is a list or bytearray or bytes
    return bytes(bytearray(l))

def bytes_to_int(bytes):
    return sum([bi << ((len(bytes) - 1 - i)*8) for i, bi in enumerate(to_bytes(bytes))])

def bytes_to_state(bytes):
    return [bytes_to_int(bytes[8*w:8*(w+1)]) for w in range(5)]

def int_to_bytes(integer, nbytes):
    return to_bytes([(integer >> ((nbytes - 1 - i) * 8)) % 256 for i in range(nbytes)])

def rotr(val, r):
    return (val >> r) | ((val & (1<<r)-1) << (64-r))

def bytes_to_hex(b):
    return b.hex()
    #return "".join(x.encode('hex') for x in b)

def printstate(S, description=""):
    print(" " + description)
    print(" ".join(["{s:016x}".format(s=s) for s in S]))

def printwords(S, description=""):
    print(" " + description)
    print("\n".join(["  x{i}={s:016x}".format(**locals()) for i, s in enumerate(S)]))


# === some demo if called directly ===

def demo_print(data):
  #print data with aligned formatting. It takes a list of tuples, where each tuple contains a text label and a corresponding value to be printed.
    maxlen = max([len(text) for (text, val) in data])
    for text, val in data:
        print("{text}:{align} 0x{val} ({length} bytes)".format(text=text, align=((maxlen - len(text)) * " "), val=bytes_to_hex(val), length=len(val)))

def demo_aead(variant):
  #demonstrates authenticated encryption with associated data (AEAD) using the Ascon cipher.
  #It takes the Ascon variant ("Ascon-128", "Ascon-128a", or "Ascon-80pq") as an argument.




    assert variant in ["Ascon-128", "Ascon-128a", "Ascon-80pq"]
    keysize = 20 if variant == "Ascon-80pq" else 16
    print("=== demo encryption using {variant} ===".format(variant=variant))

    #Generates a random key and nonce.
    # choose a cryptographically strong random key and a nonce that never repeats for the same key:
    key   = get_random_bytes(keysize) # zero_bytes(keysize) -->input
    nonce = get_random_bytes(16)      # zero_bytes(16) -k->input

    associateddata = b"ASCON"  #-->input
    plaintext      = b"ascon"  #-->input

    #Encrypts and authenticates a plaintext message along with associated data using the specified variant.
    #Decrypts and verifies the received ciphertext and associated data.
    ciphertext        = ascon_encrypt(key, nonce, associateddata, plaintext,  variant) #-->output from encryption
    receivedplaintext = ascon_decrypt(key, nonce, associateddata, ciphertext, variant) #-->output from decryption

    if receivedplaintext == None: print("verification failed!")

   #Prints various pieces of information, including the key, nonce, plaintext, associated data, ciphertext, tag, and received plaintext.
    demo_print([("key", key),
                ("nonce", nonce),
                ("plaintext", plaintext),
                ("ass.data", associateddata),
                ("ciphertext", ciphertext[:-16]),
                ("tag", ciphertext[-16:]),
                ("received", receivedplaintext),
               ])

def demo_hash(variant="Ascon-Hash", hashlength=32):
    assert variant in ["Ascon-Xof", "Ascon-Hash", "Ascon-Xofa", "Ascon-Hasha"]
    print("=== demo hash using {variant} ===".format(variant=variant))

    message = b"ascon"
    tag = ascon_hash(message, variant, hashlength)

    demo_print([("message", message), ("tag", tag)])

def demo_mac(variant="Ascon-Mac", taglength=16):
    assert variant in ["Ascon-Mac", "Ascon-Prf", "Ascon-Maca", "Ascon-Prfa", "Ascon-PrfShort"]
    keysize = 16
    print("=== demo MAC using {variant} ===".format(variant=variant))

    key = get_random_bytes(keysize)
    message = b"ascon"
    tag = ascon_mac(key, message, variant)

    demo_print([("key", key), ("message", message), ("tag", tag)])


if __name__ == "__main__":
    demo_aead("Ascon-128")
    demo_hash("Ascon-Hash")
    demo_mac("Ascon-Mac")